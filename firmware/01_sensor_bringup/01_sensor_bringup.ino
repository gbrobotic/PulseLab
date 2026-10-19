/*
 * PulseLab — Phase 1: Sensor bring-up
 *
 * What this does:
 *   1. Scans the I2C bus and reports which devices respond
 *      (expect 0x57 = MAX30100, 0x3C = OLED).
 *   2. Configures the MAX30100 for raw SpO2-mode sampling at 100 Hz.
 *   3. Streams raw IR, raw Red, and a DC-removed IR signal to the
 *      Serial Plotter so you can see your pulse.
 *
 * Board:   ESP32C3 Dev Module, "USB CDC On Boot" = Enabled
 * Library: MAX30100lib by OXullo Intersecans (Library Manager)
 * Serial:  115200 baud — open Tools > Serial Plotter
 */

#include <Wire.h>
// This header exists only in OXullo's MAX30100lib. Including it first makes the
// Arduino IDE pick that library, even if another "MAX30100" library is installed.
#include "MAX30100_PulseOximeter.h"
#include "MAX30100.h"

// ---------- Pins ----------
constexpr int PIN_SDA = 6;
constexpr int PIN_SCL = 7;

// ---------- Settings ----------
constexpr uint32_t I2C_FREQ_HZ = 400000;   // fast mode
constexpr float    DC_ALPHA    = 0.95f;    // DC-removal filter strength (0.9–0.99)
constexpr uint16_t FINGER_IR_THRESHOLD = 10000; // raw IR below this = no finger (tune it)
// The Arduino IDE 2 Serial Plotter only shows the last 50 points. At 100 Hz that is
// half a second, less than one heartbeat. Printing every 4th sample shows ~2 s (2–3 beats).
// The filter still runs on every sample; only the printing is reduced.
constexpr uint8_t  PLOT_EVERY_N = 4;
uint8_t plotCounter = 0;

MAX30100 sensor;

// Simple DC-removal (high-pass) filter state
float dcState = 0.0f;

// Removes the slow DC level so only the pulsing (AC) part remains.
// y[n] = w[n] - w[n-1],  where w[n] = x[n] + alpha * w[n-1]
float removeDC(float x) {
  float w = x + DC_ALPHA * dcState;
  float y = w - dcState;
  dcState = w;
  return y;
}

void scanI2C() {
  Serial.println(F("\n--- I2C scan ---"));
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("Found device at 0x%02X", addr);
      if (addr == 0x57) Serial.print(F("  <- MAX30100"));
      if (addr == 0x3C || addr == 0x3D) Serial.print(F("  <- OLED"));
      Serial.println();
      found++;
    }
  }
  if (found == 0) {
    Serial.println(F("No I2C devices found. Check wiring and the MAX30100 pull-up issue (docs/HARDWARE.md)."));
  }
  Serial.println(F("----------------\n"));
}

void setup() {
  Serial.begin(115200);
  delay(1500);  // give USB CDC time to connect

  Serial.println(F("PulseLab Phase 1 — sensor bring-up"));

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(I2C_FREQ_HZ);

  scanI2C();

  // Note: the library calls Wire.begin() internally. On ESP32 the bus is
  // already running on our pins, so that call leaves our pin choice intact.
  if (!sensor.begin()) {
    Serial.println(F("MAX30100 not found. Stopping."));
    while (true) delay(1000);
  }
  Wire.setClock(I2C_FREQ_HZ);

  sensor.setMode(MAX30100_MODE_SPO2_HR);
  sensor.setSamplingRate(MAX30100_SAMPRATE_100HZ);
  sensor.setLedsPulseWidth(MAX30100_SPC_PW_1600US_16BITS);
  sensor.setLedsCurrent(MAX30100_LED_CURR_50MA,     // IR
                        MAX30100_LED_CURR_27_1MA);  // Red
  sensor.setHighresModeEnabled(true);

  Serial.println(F("Sensor configured. Place your finger gently on the sensor."));
  delay(500);
}

void loop() {
  uint16_t ir, red;

  // Pull new samples from the sensor FIFO
  sensor.update();

  while (sensor.getRawValues(&ir, &red)) {
    float irAC = removeDC((float)ir);
    bool finger = ir > FINGER_IR_THRESHOLD;

    if (++plotCounter < PLOT_EVERY_N) continue;
    plotCounter = 0;

    // Serial Plotter format: label:value pairs, comma-separated.
    // Finger is shown as 100 (finger on) or 0 (no finger) so it's visible next to IR_AC.
    // Tip: plot only IR_AC first — raw values are much larger and flatten the graph.
    Serial.printf("IR_AC:%.1f,Finger:%d\n", finger ? irAC : 0.0f, finger ? 100 : 0);

    // Uncomment to also see raw values:
    // Serial.printf("IR:%u,Red:%u\n", ir, red);
  }
}
