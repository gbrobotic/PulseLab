// Tests the Phase 3 SpO2 / perfusion index code on simulated red + IR signals.
//
//   g++ -std=c++17 -O2 -I firmware/03_spo2 tests/test_spo2.cpp -o test_spo2
//   ./test_spo2
//
// The simulated finger has a known "true" R built in:
//   raw = DC * (1 + breathing) * (1 - pulseSize * pulse(t)) + noise
// with pulseSize_red / pulseSize_ir = R. So the code should find that R
// again, whatever the DC levels, heart rate or noise are.

#include <cstdio>
#include <cmath>
#include <random>
#include <vector>
#include "spo2.h"

using namespace pulselab;

struct Sim {
  float seconds   = 40;
  float bpm       = 75;
  float rStart    = 0.5f;   // true R at the start
  float rEnd      = -1;     // if set, R changes linearly to this by the end
  float dcIr      = 46000;
  float dcRed     = 30000;
  float piIr      = 0.02f;  // IR pulse size as a fraction of DC (2%)
  float noiseIr   = 5;
  float noiseRed  = 8;      // red is usually noisier on the MAX30100
  float breathing = 0.004f; // breathing changes the DC by 0.4%
  float bumpAt    = -1;     // short finger-pressure bump
  float fingerOffFrom = -1, fingerOffTo = -1;
};

struct Out {
  float spo2 = 0, r = 0, pi = 0;
  float firstSpo2Sec = -1;
  float trueREnd = 0;
  int validBeats = 0, beatsDone = 0;
  float spo2AtOffEnd = -1;  // SpO2 shown while the finger was off
};

static float pulseShape(float t) {
  auto g = [](float x, float mu, float s) { return expf(-0.5f * (x - mu) * (x - mu) / (s * s)); };
  return g(t, 0.12f, 0.05f) + 0.35f * g(t, 0.38f, 0.06f);
}

static Out run(const Sim& s, unsigned seed) {
  std::mt19937 rng(seed);
  std::normal_distribution<float> nIr(0, s.noiseIr), nRed(0, s.noiseRed), jitter(0, 0.03f);
  HeartRateDetector hr; hr.reset();
  Spo2Estimator sp; sp.reset();
  Out o;

  std::vector<float> beats;
  float nextBeat = 0, onSince = 0;
  bool wasOff = false;
  for (int i = 0; i < (int)(s.seconds * FS_HZ); i++) {
    float t = i / FS_HZ;
    bool off = (t >= s.fingerOffFrom && t < s.fingerOffTo);
    float rawIr, rawRed;
    float rTrue = (s.rEnd < 0) ? s.rStart : s.rStart + (s.rEnd - s.rStart) * t / s.seconds;
    o.trueREnd = rTrue;

    if (off) {
      rawIr = 150 + nIr(rng);
      rawRed = 120 + nRed(rng);
      wasOff = true;
    } else {
      if (wasOff) { onSince = t; wasOff = false; }
      while (nextBeat <= t) { beats.push_back(nextBeat); nextBeat += 60.0f / s.bpm * (1 + jitter(rng)); }
      float v = 0;
      for (size_t k = beats.size() >= 3 ? beats.size() - 3 : 0; k < beats.size(); k++) v += pulseShape(t - beats[k]);
      float breath = 1 + s.breathing * sinf(2 * 3.14159f * 0.25f * t);
      float on = t - onSince;
      float ramp = on < 0.3f ? on / 0.3f : 1.0f;  // placing the finger
      float bump = (s.bumpAt >= 0 && t > s.bumpAt && t < s.bumpAt + 0.15f) ? 1 + 0.1f * sinf(3.14159f * (t - s.bumpAt) / 0.15f) : 1;
      rawIr  = 150 + ramp * (s.dcIr  * breath * bump * (1 - s.piIr * v) - 150) + nIr(rng);
      rawRed = 120 + ramp * (s.dcRed * breath * bump * (1 - s.piIr * rTrue * v) - 120) + nRed(rng);
      if (rawRed > 65535) rawRed = 65535;  // ADC limit
      if (rawIr > 65535) rawIr = 65535;
    }

    HrResult h = hr.process(rawIr);
    Spo2Result r = sp.process(rawIr, rawRed, h);
    if (r.beatDone) o.beatsDone++;
    if (r.beatValid) o.validBeats++;
    if (r.spo2 > 0 && o.firstSpo2Sec < 0) o.firstSpo2Sec = t;
    if (off && t + 1.0f / FS_HZ >= s.fingerOffTo) o.spo2AtOffEnd = r.spo2;
    o.spo2 = r.spo2; o.r = r.r; o.pi = r.pi;
  }
  return o;
}

static int failures = 0;
static void check(const char* name, bool ok, const char* detail) {
  printf("  [%s] %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
  if (!ok) failures++;
}
static float spo2Of(float r) { return SPO2_A - SPO2_B * r; }

int main() {
  char buf[256];

  printf("1. Normal finger, R = 0.5 (SpO2 should be %.1f)\n", spo2Of(0.5f));
  {
    Sim s; auto o = run(s, 1);
    snprintf(buf, sizeof buf, "R=%.3f SpO2=%.1f PI=%.2f%% (true PI 2.00%%), first SpO2 after %.1f s, good beats %d/%d",
             o.r, o.spo2, o.pi, o.firstSpo2Sec, o.validBeats, o.beatsDone);
    check("SpO2 within 1", fabsf(o.spo2 - spo2Of(0.5f)) <= 1.0f, buf);
  }

  printf("2. Lower oxygen: R = 0.7 and R = 1.0\n");
  for (float rt : {0.7f, 1.0f}) {
    Sim s; s.rStart = rt; auto o = run(s, 2);
    snprintf(buf, sizeof buf, "true R %.2f -> R=%.3f, SpO2=%.1f (should be %.1f)", rt, o.r, o.spo2, spo2Of(rt));
    check("SpO2 within 1", fabsf(o.spo2 - spo2Of(rt)) <= 1.0f, buf);
  }

  printf("3. Different LED brightness / finger (DC levels change, R stays 0.5)\n");
  for (auto dc : std::vector<std::pair<float, float>>{{46000, 12000}, {20000, 55000}, {60000, 60000}}) {
    Sim s; s.dcIr = dc.first; s.dcRed = dc.second; auto o = run(s, 3);
    snprintf(buf, sizeof buf, "DC ir %.0f red %.0f -> SpO2=%.1f", dc.first, dc.second, o.spo2);
    check("SpO2 within 1", fabsf(o.spo2 - spo2Of(0.5f)) <= 1.0f, buf);
  }

  printf("4. Weak pulse (PI 0.5%%) and noisy red\n");
  {
    Sim s; s.piIr = 0.005f; s.noiseRed = 15; auto o = run(s, 4);
    snprintf(buf, sizeof buf, "SpO2=%.1f, good beats %d/%d", o.spo2, o.validBeats, o.beatsDone);
    check("SpO2 within 2", fabsf(o.spo2 - spo2Of(0.5f)) <= 2.0f, buf);
  }

  printf("5. Fast (140) and slow (50) heart rate\n");
  for (float b : {140.0f, 50.0f}) {
    Sim s; s.bpm = b; auto o = run(s, 5);
    snprintf(buf, sizeof buf, "BPM %.0f -> SpO2=%.1f", b, o.spo2);
    check("SpO2 within 1", fabsf(o.spo2 - spo2Of(0.5f)) <= 1.0f, buf);
  }

  printf("6. Red LED too bright (red clips at the ADC limit)\n");
  {
    Sim s; s.dcRed = 66000; auto o = run(s, 6);
    snprintf(buf, sizeof buf, "SpO2=%.1f, good beats %d", o.spo2, o.validBeats);
    check("no SpO2 shown from clipped data", o.spo2 == 0.0f, buf);
  }

  printf("7. Finger removed for 5 s, then put back\n");
  {
    Sim s; s.seconds = 45; s.fingerOffFrom = 20; s.fingerOffTo = 25; auto o = run(s, 7);
    snprintf(buf, sizeof buf, "while off: %.1f, at end: %.1f", o.spo2AtOffEnd, o.spo2);
    check("0 while off, correct after", o.spo2AtOffEnd == 0.0f && fabsf(o.spo2 - spo2Of(0.5f)) <= 1.0f, buf);
  }

  printf("8. Finger pressed harder for a moment (10%% bump)\n");
  {
    Sim s; s.bumpAt = 20; auto o = run(s, 8);
    snprintf(buf, sizeof buf, "SpO2 at end %.1f", o.spo2);
    check("SpO2 within 1 after the bump", fabsf(o.spo2 - spo2Of(0.5f)) <= 1.0f, buf);
  }

  printf("9. Oxygen slowly dropping (R 0.5 -> 0.8 over 60 s, SpO2 97.5 -> 90)\n");
  {
    Sim s; s.seconds = 60; s.rEnd = 0.8f; auto o = run(s, 9);
    snprintf(buf, sizeof buf, "SpO2 at end %.1f, true at end %.1f", o.spo2, spo2Of(o.trueREnd));
    check("follows within 1.5", fabsf(o.spo2 - spo2Of(o.trueREnd)) <= 1.5f, buf);
  }

  printf("\n%s (%d failed)\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", failures);
  return failures ? 1 : 0;
}
