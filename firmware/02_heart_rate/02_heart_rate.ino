/*
 * PulseLab - Phase 2: filtering and heart rate
 *
 * Reads raw IR from the MAX30100 at 100 Hz and passes every sample to
 * HeartRateDetector (ppg_dsp.h), which filters the signal, finds beats
 * and works out BPM. No library heart-rate code is used.
 *
 * Two output modes (set PLOTTER_MODE below):
 *   1 = Serial Plotter: filtered pulse wave, beat markers and BPM
 *   0 = Serial Monitor: one line per beat with the interval and BPM,
 *       plus the raw IR level every 2 s (useful for testing)
 *
 * Board:   ESP32C3 Dev Module, "USB CDC On Boot" = Enabled
 * Library: MAX30100lib by OXullo Intersecans
 * Wiring:  same as Phase 1 (docs/HARDWARE.md)
 */

#include <Wire.h>
#include "MAX30100_PulseOximeter.h"  // makes the IDE pick OXullo's library
#include "MAX30100.h"
#include "ppg_dsp.h"

using namespace pulselab;

// ---------- Pins ----------
constexpr int PIN_SDA = 6;
constexpr int PIN_SCL = 7;
constexpr uint32_t I2C_FREQ_HZ = 400000;

// ---------- Output ----------
#define PLOTTER_MODE 1

// Plotter shows only 50 points, so print every 4th sample (~2 s on screen).
constexpr uint8_t PLOT_EVERY_N = 4;
// Hold the beat marker for a few samples so it isn't skipped by PLOT_EVERY_N.
constexpr uint8_t BEAT_MARK_SAMPLES = 8;

MAX30100 sensor;
HeartRateDetector hr;

uint8_t  plotCounter = 0;
uint8_t  beatMark = 0;
float    plotScale = 100.0f;   // follows the signal size so the beat marker fits
uint16_t lastIr = 0;
bool     lastFinger = false;
bool     lastSettling = false;
uint32_t lastStatusMs = 0;

// If the ESP32 resets in the middle of an I2C transfer (for example during
// an upload), the sensor can be left holding SDA low, waiting for clock pulses
// that never come. Then it isn't found until the power is removed.
// Sending up to 9 clock pulses and a STOP lets it finish and release the bus.
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

  // STOP condition: SDA goes low -> high while SCL is high
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

  i2cBusRecover();
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(I2C_FREQ_HZ);

  // Keep trying instead of stopping, so a loose wire can be fixed without a reset.
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
  sensor.setLedsCurrent(MAX30100_LED_CURR_50MA, MAX30100_LED_CURR_27_1MA);
  sensor.setHighresModeEnabled(true);

  hr.reset();

#if !PLOTTER_MODE
  Serial.println(F("PulseLab Phase 2 - heart rate"));
  Serial.println(F("Place your finger gently on the sensor and keep still."));
#endif
}

void printForPlotter(const HrResult& r) {
  if (r.beat) beatMark = BEAT_MARK_SAMPLES;

  // Only show the signal once the filter has settled. The jump from placing
  // a finger would otherwise stretch the graph for a while.
  bool show = r.finger && !r.settling;

  // Beat marker height follows the recent pulse size (half-life ~1.4 s).
  if (show) {
    float mag = fabsf(r.signal);
    plotScale = (mag > plotScale) ? mag : plotScale * 0.995f;
    if (plotScale < 20.0f) plotScale = 20.0f;
  } else {
    plotScale = 20.0f;
  }

  if (++plotCounter >= PLOT_EVERY_N) {
    plotCounter = 0;
    float marker = beatMark ? plotScale : 0.0f;
    Serial.printf("PPG:%.1f,Beat:%.1f,BPM:%.1f\n", show ? r.signal : 0.0f, marker, r.bpm);
  }
  if (beatMark) beatMark--;
}

void printForMonitor(const HrResult& r) {
  if (r.finger != lastFinger) {
    Serial.println(r.finger ? F("\nFinger detected. Hold still...") : F("\nNo finger."));
    lastFinger = r.finger;
  }
  if (lastSettling && !r.settling && r.finger) {
    Serial.println(F("Measuring."));
  }
  lastSettling = r.settling;

  if (r.beat) {
    if (r.ibiMs == 0) {
      Serial.println(F("Beat (first one, no interval yet)"));
    } else {
      Serial.printf("Beat  interval %4u ms  %-8s  BPM %s\n",
                    r.ibiMs,
                    r.ibiValid ? "ok" : "rejected",
                    r.bpm > 0 ? String(r.bpm, 0).c_str() : "--");
    }
  }
}

void loop() {
  uint16_t ir, red;

  sensor.update();

  while (sensor.getRawValues(&ir, &red)) {
    lastIr = ir;
    HrResult r = hr.process((float)ir);

#if PLOTTER_MODE
    printForPlotter(r);
#else
    printForMonitor(r);
#endif
  }

#if !PLOTTER_MODE
  // Raw IR every 2 s. Use this to set FINGER_IR_MIN in ppg_dsp.h:
  // note the value with and without a finger and pick something in between.
  if (millis() - lastStatusMs >= 2000) {
    lastStatusMs = millis();
    Serial.printf("  (raw IR = %u)\n", lastIr);
  }
#endif
}
