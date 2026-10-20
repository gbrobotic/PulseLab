// Tests the Phase 3 SpO2 / perfusion index code on simulated red + IR signals.
//
//   g++ -std=c++17 -O2 -I firmware/05_hrv tests/test_spo2.cpp -o test_spo2
//   ./test_spo2
//
// The simulated finger has a known "true" R built in:
//   raw = DC * (1 + breathing) * (1 - pulseSize * pulse(t)) + noise
// with pulseSize_red / pulseSize_ir = R. So the code should find that R
// again, whatever the DC levels, heart rate or noise are.

#include <cstdio>
#include <cmath>
#include <vector>
#include "spo2.h"

using namespace pulselab;

#include "sim_finger.h"

struct Out {
  float spo2 = 0, r = 0, pi = 0;
  float firstSpo2Sec = -1;
  float trueREnd = 0;
  int validBeats = 0, beatsDone = 0;
  float spo2AtOffEnd = -1;  // SpO2 shown while the finger was off
};

static Out run(const Sim& s, unsigned seed) {
  SimFinger finger(s, seed);
  HeartRateDetector hr; hr.reset();
  Spo2Estimator sp; sp.reset();
  Out o;
  float rawIr, rawRed;
  while (finger.next(rawIr, rawRed)) {
    float t = finger.t();
    o.trueREnd = finger.trueR();
    HrResult h = hr.process(rawIr);
    Spo2Result r = sp.process(rawIr, rawRed, h);
    if (r.beatDone) o.beatsDone++;
    if (r.beatValid) o.validBeats++;
    if (r.spo2 > 0 && o.firstSpo2Sec < 0) o.firstSpo2Sec = t;
    if (finger.fingerOff() && t + 0.01f >= s.fingerOffTo) o.spo2AtOffEnd = r.spo2;
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
