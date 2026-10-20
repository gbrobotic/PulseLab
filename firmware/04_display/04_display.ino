/*
 * PulseLab - Phase 4: OLED screens, button and signal quality
 *
 * Builds on Phase 3 (heart rate + SpO2) and adds:
 *   quality.h  signal quality: decides when the values can be shown and
 *              what to tell the user ("Place finger", "Hold still", ...)
 *   session.h  measurement session with a summary at the end
 *   ui.h       the OLED screens
 *
 * Button (GPIO4 to GND):
 *   short press  next screen (Live -> Details -> Wave)
 *   long press   start / stop a session (shows a summary at the end)
 *
 * Serial (set SERIAL_MODE):
 *   0 = off, 1 = Serial Plotter, 2 = Serial Monitor (one line per beat + status)
 *
 * Board:     ESP32C3 Dev Module, "USB CDC On Boot" = Enabled
 * Libraries: MAX30100lib (OXullo Intersecans), U8g2 (olikraus)
 * Wiring:    docs/HARDWARE.md. OLED and sensor share SDA=GPIO6, SCL=GPIO7.
 */

#include <Wire.h>
#include <string.h>
#include <U8g2lib.h>
#include "MAX30100_PulseOximeter.h"  // makes the IDE pick OXullo's library
#include "MAX30100.h"
#include "ppg_dsp.h"
#include "spo2.h"
#include "quality.h"
#include "session.h"
#include "ui.h"

using namespace pulselab;

// ---------- Pins ----------
constexpr int PIN_SDA    = 6;
constexpr int PIN_SCL    = 7;
constexpr int PIN_BUTTON = 4;   // to GND, internal pull-up
constexpr int PIN_LED    = 8;   // onboard blue LED, on when LOW
constexpr uint32_t I2C_FREQ_HZ = 400000;

// ---------- Settings ----------
#define SERIAL_MODE 2

constexpr uint32_t FRAME_MIN_MS    = 30;   // new frame when a new wave point arrives (every 40 ms = 25 fps)...
constexpr uint32_t FRAME_MAX_MS    = 100;  // ...and at least every 100 ms
constexpr uint32_t FULL_REFRESH_MS = 5000; // send the whole screen now and then, just in case
constexpr uint32_t STALL_MS        = 300;  // no samples for this long = sensor FIFO stuck (see docs)
constexpr uint32_t LONG_PRESS_MS   = 800;
constexpr uint32_t DEBOUNCE_MS     = 30;
constexpr uint32_t BEAT_FLASH_MS   = 150;

U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
MAX30100 sensor;

HeartRateDetector hr;
Spo2Estimator     spo2;
QualityMonitor    quality;
Session           session;
WaveBuffer        wave;

// Latest results, used to draw the screen
HrResult      lastH{};
Spo2Result    lastS{};
QualityResult lastQ{};
Screen        screen = Screen::Live;
uint32_t      fingerSamples = 0;
uint8_t       waveCounter = 0;
bool          newWavePoint = false;
float         shownScore = 0.0f;       // quality bar, smoothed so it slides instead of jumping

// Copy of what's on the OLED, to send only the parts that changed
uint8_t  sentBuffer[1024];
bool     sentValid = false;
uint32_t lastFullRefreshMs = 0;

// Timing / diagnostics
uint32_t lastSampleMs = 0, lastFrameMs = 0, lastStatusMs = 0;
uint32_t beatFlashUntil = 0;
uint32_t frameMs = 0, frameMsMax = 0;
uint32_t framesCount = 0, pagesCount = 0;  // for the status line: frames/s and pages per frame
uint32_t fifoResets = 0;
uint16_t lastIr = 0, lastRed = 0;
SignalState lastPrintedState = SignalState::NoFinger;

// ---------- Button: short and long press with debounce ----------
class Button {
public:
  void begin(int pin) { pin_ = pin; pinMode(pin, INPUT_PULLUP); }
  // 0 = nothing, 1 = short press (on release), 2 = long press (while held)
  uint8_t poll(uint32_t now) {
    bool down = digitalRead(pin_) == LOW;
    if (down != raw_) { raw_ = down; changedAt_ = now; }
    if (now - changedAt_ < DEBOUNCE_MS) return 0;
    if (down && !pressed_) { pressed_ = true; pressedAt_ = now; longDone_ = false; }
    else if (!down && pressed_) { pressed_ = false; if (!longDone_) return 1; }
    if (pressed_ && !longDone_ && now - pressedAt_ >= LONG_PRESS_MS) { longDone_ = true; return 2; }
    return 0;
  }
private:
  int pin_ = -1;
  bool raw_ = false, pressed_ = false, longDone_ = false;
  uint32_t changedAt_ = 0, pressedAt_ = 0;
};
Button button;

// ---------- I2C bus recovery (Phase 2, debug log #6) ----------
void i2cBusRecover() {
  pinMode(PIN_SDA, INPUT_PULLUP);
  pinMode(PIN_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_SCL, HIGH);
  delayMicroseconds(5);
  for (int i = 0; i < 9 && digitalRead(PIN_SDA) == LOW; i++) {
    digitalWrite(PIN_SCL, LOW);  delayMicroseconds(5);
    digitalWrite(PIN_SCL, HIGH); delayMicroseconds(5);
  }
  pinMode(PIN_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_SDA, LOW);  delayMicroseconds(5);
  digitalWrite(PIN_SCL, HIGH); delayMicroseconds(5);
  digitalWrite(PIN_SDA, HIGH); delayMicroseconds(5);
}

void showMessage(const char* line1, const char* line2) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_helvB12_tr);
  drawCentered(oled, 64, 22, "PulseLab");
  oled.setFont(u8g2_font_6x10_tf);
  drawCentered(oled, 64, 42, line1);
  drawCentered(oled, 64, 56, line2);
  oled.sendBuffer();
}

void resetMeasurement() {
  hr.reset();
  spo2.reset();
  quality.reset();
  wave.clear();
  fingerSamples = 0;
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  // Never wait for the PC. If USB is plugged in but no Serial Monitor is open,
  // printing could otherwise block long enough to overflow the sensor FIFO.
  Serial.setTxTimeoutMs(0);
#endif

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
  button.begin(PIN_BUTTON);

  i2cBusRecover();
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(I2C_FREQ_HZ);

  oled.setBusClock(I2C_FREQ_HZ);
  oled.begin();
  showMessage("vitals analyzer", "starting...");

  uint8_t attempt = 0;
  while (!sensor.begin()) {
    attempt++;
    if (attempt == 1 || attempt % 5 == 0) {
      Serial.printf("MAX30100 not found (try %u). Check wiring and pull-ups (docs/DEBUG_LOG.md).\n", attempt);
      showMessage("Sensor not found", "check wiring");
    }
    delay(400);
  }
  Wire.setClock(I2C_FREQ_HZ);

  sensor.setMode(MAX30100_MODE_SPO2_HR);
  sensor.setSamplingRate(MAX30100_SAMPRATE_100HZ);
  sensor.setLedsPulseWidth(MAX30100_SPC_PW_1600US_16BITS);
  sensor.setLedsCurrent(MAX30100_LED_CURR_50MA, MAX30100_LED_CURR_27_1MA);
  sensor.setHighresModeEnabled(true);
  sensor.resetFifo();

  resetMeasurement();
  lastSampleMs = millis();

#if SERIAL_MODE == 2
  Serial.println(F("PulseLab Phase 4 starting"));
#endif
}

// ---------- Per sample ----------
void processSample(uint16_t ir, uint16_t red) {
  lastIr = ir; lastRed = red;
  HrResult h = hr.process((float)ir);
  Spo2Result s = spo2.process((float)ir, (float)red, h);
  QualityResult q = quality.process(h, s, (float)ir, (float)red);
  session.process(h, s, q);

  fingerSamples = h.finger ? fingerSamples + 1 : 0;
  if (++waveCounter >= 4) {  // one wave point per 40 ms
    waveCounter = 0;
    bool show = h.finger && !h.settling;
    bool good = show && q.state != SignalState::Moving && q.state != SignalState::Clipping;
    wave.push(show ? h.signal : 0.0f, good);
    newWavePoint = true;
  }

  if (h.beat) {
    beatFlashUntil = millis() + BEAT_FLASH_MS;
    digitalWrite(PIN_LED, LOW);
  }

#if SERIAL_MODE == 1
  static uint8_t plotCounter = 0;
  if (++plotCounter >= 4) {
    plotCounter = 0;
    Serial.printf("IR:%.1f,BPM:%.1f,SpO2:%.1f,Quality:%u\n",
                  (h.finger && !h.settling) ? h.signal : 0.0f,
                  q.showBpm ? h.bpm : 0.0f, q.showSpo2 ? s.spo2 : 0.0f, q.score);
  }
#elif SERIAL_MODE == 2
  if (q.state != lastPrintedState) {
    Serial.printf("[%s]\n", stateText(q.state));
    lastPrintedState = q.state;
  }
  if (h.beat && h.ibiMs > 0) {
    Serial.printf("Beat %4u ms %-3s BPM %5.1f | SpO2 %5.1f%% PI %.2f%% R %.3f | quality %3u%s%s\n",
                  h.ibiMs, h.ibiValid ? "ok" : "rej", h.bpm, s.spo2, s.pi, s.r, q.score,
                  q.showBpm ? "" : "  (BPM hidden)", q.showSpo2 ? "" : "  (SpO2 hidden)");
  }
#endif

  lastH = h; lastS = s; lastQ = q;
}

void printSessionSummary(const SessionSummary& s) {
#if SERIAL_MODE == 2
  Serial.printf("\nSession: %lu s, HR avg %.1f (%.1f-%.1f, %u beats), SpO2 avg %.1f min %.1f (%u beats), good signal %u%%\n",
                (unsigned long)s.seconds, s.bpmAvg, s.bpmMin, s.bpmMax, s.bpmCount,
                s.spo2Avg, s.spo2Min, s.spo2Count, s.goodPct);
  // One line that can be pasted straight into a spreadsheet (Phase 11)
  Serial.printf("SESSION_CSV,%lu,%.1f,%.1f,%.1f,%.1f,%.1f,%u\n\n",
                (unsigned long)s.seconds, s.bpmAvg, s.bpmMin, s.bpmMax, s.spo2Avg, s.spo2Min, s.goodPct);
#else
  (void)s;
#endif
}

void handleButton(uint32_t now) {
  uint8_t ev = button.poll(now);
  if (ev == 1) {  // short press: next screen
    switch (screen) {
      case Screen::Live:    screen = Screen::Details; break;
      case Screen::Details: screen = Screen::Wave; break;
      case Screen::Wave:    screen = Screen::Live; break;
      case Screen::Summary: screen = Screen::Live; break;
    }
  } else if (ev == 2) {  // long press: start / stop session
    if (session.active()) {
      session.stop();
      printSessionSummary(session.summary());
      screen = Screen::Summary;
    } else {
      session.start();
      screen = Screen::Live;
#if SERIAL_MODE == 2
      Serial.println(F("\nSession started"));
#endif
    }
  }
}

// The SH1106 is split into 8 "pages" (strips of 8 rows, 128 bytes each).
// Sending all 8 takes ~33 ms. Most frames only change a few pages (mainly
// the wave), so only those are sent. That's what makes 25 fps possible.
uint8_t sendChangedPages(uint32_t now) {
  uint8_t* buf = oled.getBufferPtr();
  bool full = !sentValid || (now - lastFullRefreshMs >= FULL_REFRESH_MS);
  uint8_t sent = 0;
  for (uint8_t page = 0; page < 8; page++) {
    uint8_t* cur = buf + page * 128;
    uint8_t* old = sentBuffer + page * 128;
    if (full || memcmp(cur, old, 128) != 0) {
      oled.updateDisplayArea(0, page, 16, 1);   // 16 tiles of 8 px = whole width, 1 page high
      memcpy(old, cur, 128);
      sent++;
    }
  }
  if (full) { sentValid = true; lastFullRefreshMs = now; }
  return sent;
}

void drawFrame(uint32_t now) {
  shownScore += 0.25f * ((float)lastQ.score - shownScore);

  UiData u{};
  u.screen = screen;
  u.q = lastQ;
  u.q.score = (uint8_t)(shownScore + 0.5f);
  u.bpm = lastH.bpm;
  u.spo2 = lastS.spo2;
  u.pi = lastS.pi;
  u.r = lastS.r;
  u.beatFlash = now < beatFlashUntil;
  u.fingerSec = fingerSamples / 100.0f;
  u.wave = &wave;
  u.sessionActive = session.active();
  u.sessionSec = session.seconds();
  u.summary = &session.summary();

  uint32_t t0 = millis();
  wave.updateScale();
  drawScreen(oled, u);
  pagesCount += sendChangedPages(now);
  framesCount++;
  frameMs = millis() - t0;
  if (frameMs > frameMsMax) frameMsMax = frameMs;
}

void loop() {
  uint32_t now = millis();

  // 1. Read everything the sensor has collected
  sensor.update();
  uint16_t ir, red;
  bool got = false;
  while (sensor.getRawValues(&ir, &red)) {
    processSample(ir, red);
    got = true;
  }
  if (got) lastSampleMs = now;

  // 2. Watchdog: the library can't read a completely full FIFO (16 samples),
  //    so if the loop was ever blocked for >160 ms the data would stop for good.
  //    If no samples arrive for 300 ms, reset the FIFO and start measuring again.
  if (now - lastSampleMs > STALL_MS) {
    sensor.resetFifo();
    resetMeasurement();
    fifoResets++;
    lastSampleMs = now;
#if SERIAL_MODE == 2
    Serial.println(F("Sensor FIFO stalled, reset"));
#endif
  }

  // 3. Button
  handleButton(now);

  // 4. Screen: one frame per new wave point, so the wave scrolls 1 pixel at a time
  uint32_t sinceFrame = now - lastFrameMs;
  if ((newWavePoint && sinceFrame >= FRAME_MIN_MS) || sinceFrame >= FRAME_MAX_MS) {
    lastFrameMs = now;
    newWavePoint = false;
    drawFrame(now);
  }

  // 5. LED off after the beat flash
  if (now >= beatFlashUntil) digitalWrite(PIN_LED, HIGH);

#if SERIAL_MODE == 2
  if (now - lastStatusMs >= 2000) {
    float fps = framesCount * 1000.0f / (now - lastStatusMs);
    float pages = framesCount ? (float)pagesCount / framesCount : 0.0f;
    lastStatusMs = now;
    framesCount = pagesCount = 0;
    Serial.printf("  (raw IR %u, red %u | %.1f fps, %.1f pages/frame, frame %lu ms, max %lu ms | FIFO resets %lu)\n",
                  lastIr, lastRed, fps, pages, (unsigned long)frameMs, (unsigned long)frameMsMax, (unsigned long)fifoResets);
  }
#endif
}
