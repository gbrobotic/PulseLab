// ble_packets.h
// What PulseLab sends over Bluetooth Low Energy, byte by byte.
//
// Plain C++ (no BLE library in here), so the packing can be tested on the PC,
// and dashboard/pulselab.js decodes exactly these layouts.
// All numbers are little-endian. Every packet is 20 bytes or less, so it fits
// in one notification even before the phone asks for a bigger MTU.
//
// 1. Standard Heart Rate service (0x180D), characteristic 0x2A37
//    Bluetooth SIG format, so fitness apps understand it without knowing PulseLab:
//      [0]    flags: bit0 = 0 (heart rate is uint8), bits1-2 = sensor contact
//             (supported, and detected when a finger is on), bit4 = RR intervals follow
//      [1]    heart rate (BPM)
//      [2..3] RR interval of this beat, in 1/1024 s (only if bit4 is set)
//
// 2. PulseLab service (own 128-bit UUIDs, see the end of this file)
//    vitals   (notify, 2x per second, 20 bytes)
//      [0]  version (1)          [1]  signal state (SignalState)   [2]  quality 0-100
//      [3]  flags: bit0 BPM shown, bit1 SpO2 shown, bit2 HRV ready, bit3 HR steady,
//                  bit4 breathing rate valid, bit5 finger on
//      [4]  BPM x10 (u16)        [6]  SpO2 x10 (u16)               [8]  PI x100 (u16)
//      [10] RMSSD x10 (u16)      [12] breathing /min x10 (u16)
//      [14] HRV progress %       [15] breathing progress %         [16] stress level
//      [17] guided phase         [18] guided seconds left (u16)
//    wave     (notify, 8 points = every 320 ms, 18 bytes)
//      [0] sequence number (counts up, to spot lost packets)  [1] number of points (8)
//      [2..17] 8 x int16: the filtered pulse signal, 25 points per second
//    guided   (notify, 4x per second during a session, 8 bytes)
//      [0] phase  [1] flags: bit0 paused, bit1 breathing in  [2] circle size 0-255
//      [3] seconds left in this part (u16)  [5] last swing x10 (u16)  [7] pace /min
//    result   (read + notify at the end of a session, 17 bytes)
//      [0..5]  HR before / slow / after x10 (u16)   [6..11] RMSSD before / slow / after x10 (u16)
//      [12] swing x10 (u16)  [14] swing breaths  [15] breathing rate in the slow part x10 (u16)
//    control  (write, 1 byte): 1 = start a guided session, 2 = cancel / close the result

#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "ppg_dsp.h"
#include "spo2.h"
#include "quality.h"
#include "hrv.h"
#include "breathing.h"
#include "guided.h"

namespace pulselab {

constexpr uint8_t BLE_VERSION       = 1;
constexpr uint8_t BLE_WAVE_POINTS       = 8;
constexpr uint8_t VITALS_LEN        = 20;
constexpr uint8_t BLE_WAVE_LEN          = 2 + 2 * BLE_WAVE_POINTS;
constexpr uint8_t GUIDED_LEN        = 8;
constexpr uint8_t RESULT_LEN        = 17;
constexpr uint8_t CMD_START_GUIDED  = 1;
constexpr uint8_t CMD_CANCEL_GUIDED = 2;

inline void putU16(uint8_t* p, float v) {
  long x = lroundf(v);
  if (x < 0) x = 0;
  if (x > 65535) x = 65535;
  p[0] = (uint8_t)(x & 0xFF); p[1] = (uint8_t)(x >> 8);
}
inline void putI16(uint8_t* p, float v) {
  long x = lroundf(v);
  if (x < -32768) x = -32768;
  if (x > 32767) x = 32767;
  uint16_t u = (uint16_t)(int16_t)x;
  p[0] = (uint8_t)(u & 0xFF); p[1] = (uint8_t)(u >> 8);
}
inline uint8_t clampU8(float v) { return v <= 0 ? 0 : v >= 255 ? 255 : (uint8_t)lroundf(v); }

// Standard heart rate measurement. Returns the length (2 or 4).
inline uint8_t packHeartRate(uint8_t* out, float bpm, bool finger, uint16_t rrMs) {
  uint8_t flags = 0x02 | (finger ? 0x04 : 0x00);   // contact supported (+ detected)
  out[1] = clampU8(bpm);
  if (rrMs > 0) {
    flags |= 0x10;
    putU16(out + 2, rrMs * 1024.0f / 1000.0f);
    out[0] = flags;
    return 4;
  }
  out[0] = flags;
  return 2;
}

inline void packVitals(uint8_t* out, const HrResult& h, const Spo2Result& s, const QualityResult& q,
                       const HrvResult& v, const BreathResult& b, const GuidedResult& g) {
  memset(out, 0, VITALS_LEN);
  out[0] = BLE_VERSION;
  out[1] = (uint8_t)q.state;
  out[2] = q.score;
  out[3] = (q.showBpm ? 0x01 : 0) | (q.showSpo2 ? 0x02 : 0) | (v.ready ? 0x04 : 0) | (v.steady ? 0x08 : 0) |
           (b.valid ? 0x10 : 0) | (h.finger ? 0x20 : 0);
  putU16(out + 4, h.bpm * 10);
  putU16(out + 6, s.spo2 * 10);
  putU16(out + 8, s.pi * 100);
  putU16(out + 10, v.rmssd * 10);
  putU16(out + 12, b.valid ? b.rate * 10 : 0);
  out[14] = clampU8(v.progress * 100);
  out[15] = clampU8(b.progress * 100);
  out[16] = (uint8_t)v.level;
  out[17] = (uint8_t)g.phase;
  uint32_t left = g.phaseLength > g.phaseSamples ? (g.phaseLength - g.phaseSamples + 99) / 100 : 0;
  putU16(out + 18, (float)left);
}

inline void packWave(uint8_t* out, uint8_t seq, const float* points) {
  out[0] = seq;
  out[1] = BLE_WAVE_POINTS;
  for (uint8_t i = 0; i < BLE_WAVE_POINTS; i++) putI16(out + 2 + 2 * i, points[i]);
}

inline void packGuided(uint8_t* out, const GuidedResult& g, const PacerState& p) {
  memset(out, 0, GUIDED_LEN);
  out[0] = (uint8_t)g.phase;
  out[1] = (g.paused ? 0x01 : 0) | (p.inhale ? 0x02 : 0);
  out[2] = clampU8(p.level * 255);
  uint32_t left = g.phaseLength > g.phaseSamples ? (g.phaseLength - g.phaseSamples + 99) / 100 : 0;
  putU16(out + 3, (float)left);
  putU16(out + 5, g.lastSwing * 10);
  out[7] = p.perMin;
}

inline void packResult(uint8_t* out, const GuidedResult& g, float follow) {
  memset(out, 0, RESULT_LEN);
  putU16(out + 0, g.before.bpm * 10);  putU16(out + 2, g.during.bpm * 10);  putU16(out + 4, g.after.bpm * 10);
  putU16(out + 6, g.before.rmssd * 10); putU16(out + 8, g.during.rmssd * 10); putU16(out + 10, g.after.rmssd * 10);
  putU16(out + 12, g.swing * 10);
  out[14] = (uint8_t)(g.swingBreaths > 255 ? 255 : g.swingBreaths);
  putU16(out + 15, follow * 10);
}

// UUIDs. Standard ones from the Bluetooth SIG, PulseLab's own made up once
// (random 128-bit, only the 2nd group differs between characteristics).
#define BLE_HR_SERVICE_UUID       "180D"
#define BLE_HR_MEASUREMENT_UUID   "2A37"
#define BLE_BODY_LOCATION_UUID    "2A38"
#define BLE_PULSELAB_SERVICE_UUID "7e1a0001-5a4e-4c2f-9d3b-2b7c1e5a9f10"
#define BLE_VITALS_UUID           "7e1a0002-5a4e-4c2f-9d3b-2b7c1e5a9f10"
#define BLE_WAVE_UUID             "7e1a0003-5a4e-4c2f-9d3b-2b7c1e5a9f10"
#define BLE_GUIDED_UUID           "7e1a0004-5a4e-4c2f-9d3b-2b7c1e5a9f10"
#define BLE_RESULT_UUID           "7e1a0005-5a4e-4c2f-9d3b-2b7c1e5a9f10"
#define BLE_CONTROL_UUID          "7e1a0006-5a4e-4c2f-9d3b-2b7c1e5a9f10"

}  // namespace pulselab
