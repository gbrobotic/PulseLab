/*
 * PulseLab - Phase 3: SpO2 and perfusion index
 *
 * Same as Phase 2, plus the red channel:
 *   HeartRateDetector (ppg_dsp.h) finds the beats from IR,
 *   Spo2Estimator (spo2.h) uses those beats to measure red and IR
 *   per heartbeat and works out R, SpO2 and perfusion index.
 *
 * Output modes (set PLOTTER_MODE below):
 *   1 = Serial Plotter: IR and red pulse waves, BPM and SpO2
 *   0 = Serial Monitor: one line per beat with BPM, R, SpO2 and PI,
 *       plus raw IR/red levels every 2 s
 *
 * The SpO2 formula (110 - 25*R) is a common starting point, not calibrated
 * for this sensor yet. Note the R values: they're what I'll calibrate with
 * in Phase 11.
 *
 * Board:   ESP32C3 Dev Module, "USB CDC On Boot" = Enabled
 * Library: MAX30100lib by OXullo Intersecans
 * Wiring:  same as Phase 1 (docs/HARDWARE.md)
 */

#include <Wire.h>
#include "MAX30100_PulseOximeter.h"  // makes the IDE pick OXullo's library
#include "MAX30100.h"
#include "ppg_dsp.h"
#include "spo2.h"

using namespace pulselab;

// ---------- Pins ----------
constexpr int PIN_SDA = 6;
constexpr int PIN_SCL = 7;
constexpr int PIN_LED = 8;      // onboard blue LED, on when the pin is LOW
constexpr uint32_t I2C_FREQ_HZ = 400000;

// ---------- Output ----------
#define PLOTTER_MODE 1

constexpr uint8_t PLOT_EVERY_N = 4;  // plotter shows 50 points, so ~2 s on screen

MAX30100 sensor;
HeartRateDetector hr;
Spo2Estimator spo2;

uint8_t  plotCounter = 0;
float    irScale = 20.0f, redScale = 20.0f;
uint16_t lastIr = 0, lastRed = 0;
bool     lastFinger = false;
bool     lastSettling = false;
bool     clipSeen = false;
uint32_t lastStatusMs = 0;
uint32_t ledOffAt = 0;

// See Phase 2 / debug log #6: frees the I2C bus if the sensor was left
// holding SDA low after a reset in the middle of a transfer.
void i2cBusRecover() {
  pinMode(PIN_SDA, INPUT_PULLUP);
  pinMode(PIN_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_SCL, HIGH);
  delayMicroseconds(5);
  for (int i = 0; i < 9 && digitalRead(PIN_SDA) == LOW; i++) {
    digitalWrite(PIN_SCL, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_SCL, HIGH);
    delayMicroseconds(5);
  }
  pinMode(PIN_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_SDA, LOW);
  delayMicroseconds(5);
  digitalWrite(PIN_SCL, HIGH);
  delayMicroseconds(5);
  digitalWrite(PIN_SDA, HIGH);
  delayMicroseconds(5);
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  // The LED flashes on every heartbeat. If it flashes but Serial shows
  // nothing, the program is fine and the problem is the Serial settings
  // (USB CDC On Boot, port, baud rate).
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);   // short flash at start-up
  delay(200);
  digitalWrite(PIN_LED, HIGH);

#if !PLOTTER_MODE
  // Only in Monitor mode: the Serial Plotter would draw the "3" in this
  // line as an extra "value 1" graph.
  Serial.println(F("PulseLab Phase 3 starting"));
#endif

  i2cBusRecover();
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(I2C_FREQ_HZ);

  uint8_t attempt = 0;
  while (!sensor.begin()) {
    attempt++;
    if (attempt == 1 || attempt % 5 == 0) {
      Serial.printf("MAX30100 not found (try %u). Check wiring and pull-ups (docs/DEBUG_LOG.md).\n", attempt);
    }
    delay(400);
  }
  Wire.setClock(I2C_FREQ_HZ);

  sensor.setMode(MAX30100_MODE_SPO2_HR);
  sensor.setSamplingRate(MAX30100_SAMPRATE_100HZ);
  sensor.setLedsPulseWidth(MAX30100_SPC_PW_1600US_16BITS);
  // IR, Red. If red is much darker than IR (raw red < ~15000) try a higher
  // red current, if it clips (>65000) try lower. Check with PLOTTER_MODE 0.
  sensor.setLedsCurrent(MAX30100_LED_CURR_50MA, MAX30100_LED_CURR_27_1MA);
  sensor.setHighresModeEnabled(true);

  hr.reset();
  spo2.reset();

#if !PLOTTER_MODE
  Serial.println(F("PulseLab Phase 3 - heart rate + SpO2"));
  Serial.println(F("Place your finger gently on the sensor and keep still."));
#endif
}

// Follows the size of a signal so both waves can be drawn at the same height.
static void trackScale(float& scale, float v) {
  float m = fabsf(v);
  scale = (m > scale) ? m : scale * 0.995f;
  if (scale < 20.0f) scale = 20.0f;
}

void printForPlotter(const HrResult& h, const Spo2Result& s) {
  bool show = h.finger && !h.settling;
  if (show) {
    trackScale(irScale, h.signal);
    trackScale(redScale, s.redSignal);
  } else {
    irScale = redScale = 20.0f;
  }

  if (++plotCounter >= PLOT_EVERY_N) {
    plotCounter = 0;
    // Red is drawn scaled to the same height as IR, so the shapes can be compared.
    float red = show ? s.redSignal * (irScale / redScale) : 0.0f;
    Serial.printf("IR:%.1f,Red_scaled:%.1f,BPM:%.1f,SpO2:%.1f\n",
                  show ? h.signal : 0.0f, red, h.bpm, s.spo2);
  }
}

void printForMonitor(const HrResult& h, const Spo2Result& s) {
  if (h.finger != lastFinger) {
    Serial.println(h.finger ? F("\nFinger detected. Hold still...") : F("\nNo finger."));
    lastFinger = h.finger;
  }
  if (lastSettling && !h.settling && h.finger) Serial.println(F("Measuring."));
  lastSettling = h.settling;
  if (s.clipping) clipSeen = true;

  if (h.beat && h.ibiMs > 0) {
    Serial.printf("Beat %4u ms %-3s BPM %5.1f | ", h.ibiMs, h.ibiValid ? "ok" : "rej", h.bpm);
    if (s.beatValid) {
      Serial.printf("this beat R %.3f PI %.2f%% | ", s.beatR, s.beatPi);
    } else {
      Serial.print(F("this beat skipped         | "));
    }
    if (s.spo2 > 0) {
      Serial.printf("R %.3f SpO2 %5.1f%% PI %.2f%%%s\n", s.r, s.spo2, s.pi,
                    s.lowPerfusion ? "  (weak pulse, SpO2 not reliable)" : "");
    } else {
      Serial.println(F("SpO2 --"));
    }
  }
}

void loop() {
  uint16_t ir, red;

  sensor.update();

  while (sensor.getRawValues(&ir, &red)) {
    lastIr = ir;
    lastRed = red;
    HrResult h = hr.process((float)ir);
    Spo2Result s = spo2.process((float)ir, (float)red, h);

    if (h.beat) {
      digitalWrite(PIN_LED, LOW);
      ledOffAt = millis() + 60;
    }

#if PLOTTER_MODE
    printForPlotter(h, s);
#else
    printForMonitor(h, s);
#endif
  }

  if (ledOffAt && millis() >= ledOffAt) {
    digitalWrite(PIN_LED, HIGH);
    ledOffAt = 0;
  }

#if !PLOTTER_MODE
  if (millis() - lastStatusMs >= 2000) {
    lastStatusMs = millis();
    Serial.printf("  (raw IR %u, raw red %u%s)\n", lastIr, lastRed,
                  clipSeen ? ", CLIPPING: LED too bright" : "");
    clipSeen = false;
  }
#endif
}
