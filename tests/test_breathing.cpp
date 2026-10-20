// Tests for Phase 6 (breathing rate). Run from the repo root:
//   g++ -std=c++17 -O2 -I firmware/06_breathing tests/test_breathing.cpp -o test_breathing && ./test_breathing
//
// Tests 1-3: the breathing pacer (the true rate the estimate is checked against).
// Tests 4-7: the breathing rate on the simulated finger.
// Test 8: my real recordings (validation/logs/raw_*.txt), if they're there.

#include <cstdio>
#include <cmath>
#include <initializer_list>
#include "pacer.h"
#include "ppg_dsp.h"
#include "spo2.h"
#include "quality.h"
#include "breathing.h"
#include "sim_finger.h"
#include <vector>
#include <cstring>
#include <cstdlib>

using namespace pulselab;

static int failures = 0;
static BreathRateEstimator est;   // static: big buffers

struct RespRun { int estimates = 0, shown = 0, within = 0; double err = 0; };

// Feed samples through the same chain as the device and collect breathing estimates.
struct Chain {
  HeartRateDetector hr; Spo2Estimator sp; QualityMonitor qm; BeatFeatureTracker bt;
  Chain() { hr.reset(); sp.reset(); qm.reset(); bt.reset(); est.reset(); }
  void feed(float ir, float red, float truth, RespRun& r) {
    HrResult h = hr.process(ir); Spo2Result s = sp.process(ir, red, h); QualityResult q = qm.process(h, s, ir, red);
    BeatFeature b;
    if (bt.process(ir, h, q, b)) est.addBeat(b);
    if (!h.finger) est.reset();
    if (est.updateNow()) {
      const BreathResult& e = est.result();
      r.estimates++;
      if (e.valid) { r.shown++; r.err += fabs(e.rate - truth); if (fabs(e.rate - truth) <= 2) r.within++; }
    }
  }
};

static RespRun simRun(Sim s, unsigned seed) {
  RespRun r; Chain c; SimFinger f(s, seed); float ir, red;
  while (f.next(ir, red)) c.feed(ir, red, s.breathRate * 60, r);
  return r;
}
static void check(const char* name, bool ok, const char* detail) {
  printf("  [%s] %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
  if (!ok) failures++;
}

int main() {
  char buf[200];

  printf("1. Pacer is off at the start, long presses go 6 -> 10 -> 15 -> 20 -> off\n");
  {
    BreathPacer p;
    int seq[6]; seq[0] = p.perMin();
    for (int i = 1; i < 6; i++) seq[i] = p.next();
    snprintf(buf, sizeof buf, "%d %d %d %d %d %d", seq[0], seq[1], seq[2], seq[3], seq[4], seq[5]);
    check("order", seq[0] == 0 && seq[1] == 6 && seq[2] == 10 && seq[3] == 15 && seq[4] == 20 && seq[5] == 0, buf);
  }

  printf("2. Exact timing at every rate: in = 40%% of a breath, out = 60%%, N breaths per minute\n");
  for (int rate : {6, 10, 15, 20}) {
    BreathPacer p;
    while (p.perMin() != rate) p.next();
    int inSamples = 0, switches = 0; bool prevIn = true;
    float minLevel = 1, maxLevel = 0;
    for (int i = 0; i < 6000; i++) {             // 60 s
      PacerState s = p.state();
      if (s.inhale) inSamples++;
      if (s.inhale != prevIn) switches++;
      prevIn = s.inhale;
      minLevel = fminf(minLevel, s.level); maxLevel = fmaxf(maxLevel, s.level);
      p.tick();
    }
    PacerState end = p.state();
    snprintf(buf, sizeof buf, "%d/min: %u breaths in 60 s, breathing in %.1f%% of the time, %d in/out switches, circle %.2f-%.2f",
             rate, end.breaths, inSamples / 60.0, switches, minLevel, maxLevel);
    check("correct", end.breaths == (uint32_t)rate && fabs(inSamples / 6000.0 - 0.4) < 0.005 && switches == 2 * rate - 1 &&
          minLevel < 0.01f && maxLevel > 0.99f, buf);
  }

  printf("3. Changing the pace starts a new breath (breathing in, empty lungs)\n");
  {
    BreathPacer p; p.next();
    for (int i = 0; i < 777; i++) p.tick();
    p.next();
    PacerState s = p.state();
    snprintf(buf, sizeof buf, "rate %u, in %d, level %.2f, %.1f s left", s.perMin, s.inhale, s.level, s.secondsLeft);
    check("restarts", s.perMin == 10 && s.inhale && s.level < 0.01f && fabs(s.secondsLeft - 2.4f) < 0.01f, buf);
  }

  printf("4. Simulated finger, all 3 breathing effects, 6 to 36 breaths/min\n");
  for (float rate : {6.f, 10.f, 15.f, 20.f, 25.f, 30.f, 36.f}) {
    RespRun tot;
    for (unsigned seed = 1; seed <= 3; seed++) {
      Sim s; s.seconds = 150; s.bpm = rate > 25 ? 85 : 72; s.breathRate = rate / 60; s.rsa = 0.05f; s.breathAmp = 0.1f; s.breathing = 0.004f;
      s.beatJitter = 0.02f; s.ampJitter = 0.1f; s.noiseIr = 10;
      RespRun r = simRun(s, seed);
      tot.estimates += r.estimates; tot.shown += r.shown; tot.within += r.within; tot.err += r.err;
    }
    snprintf(buf, sizeof buf, "%2.0f /min: shown %3.0f%% of %d estimates, within 2: %3.0f%%, mean error %.2f", rate,
             100.0 * tot.shown / tot.estimates, tot.estimates, tot.shown ? 100.0 * tot.within / tot.shown : 0, tot.shown ? tot.err / tot.shown : 0);
    check("shown >= 80% and within 2 /min >= 95%", tot.shown >= 0.8 * tot.estimates && tot.within >= 0.95 * tot.shown, buf);
  }

  printf("5. Only the baseline effect (no timing or height change): if a rate is shown, it must be right\n");
  {
    // the baseline change also leaks a little into the other two in the
    // simulator, so a rate may be shown, but it must not be a wrong one
    Sim s; s.seconds = 150; s.breathRate = 0.25f; s.rsa = 0; s.breathAmp = 0; s.breathing = 0.004f; s.beatJitter = 0.02f;
    RespRun r = simRun(s, 4);
    snprintf(buf, sizeof buf, "shown %d of %d estimates, %d of them right", r.shown, r.estimates, r.within);
    check("no wrong rate shown", r.within == r.shown, buf);
  }

  printf("6. No breathing in the signal at all (only random beat-to-beat noise): few made-up numbers\n");
  {
    // Not zero: random noise sometimes looks like a breathing rhythm for a
    // minute. Before the timing rule + peak share check: shown 62% of the time.
    RespRun tot;
    for (unsigned seed = 1; seed <= 8; seed++) {
      Sim s; s.seconds = 150; s.breathRate = 0.25f; s.rsa = 0; s.breathAmp = 0; s.breathing = 0; s.beatJitter = 0.03f; s.ampJitter = 0.1f;
      RespRun r = simRun(s, seed);
      tot.estimates += r.estimates; tot.shown += r.shown;
    }
    snprintf(buf, sizeof buf, "shown %d of %d estimates (%.0f%%)", tot.shown, tot.estimates, 100.0 * tot.shown / tot.estimates);
    check("shown in under 30%", tot.shown < 0.3 * tot.estimates, buf);
  }

  printf("7. Finger lifted for 5 s: starts collecting again, no old data used\n");
  {
    Sim s; s.seconds = 100; s.breathRate = 0.25f; s.rsa = 0.05f; s.breathAmp = 0.1f; s.fingerOffFrom = 70; s.fingerOffTo = 75;
    RespRun r; Chain c; SimFinger f(s, 6); float ir, red; float progressAt80 = -1;
    while (f.next(ir, red)) { c.feed(ir, red, 15, r); if (progressAt80 < 0 && f.t() >= 80) progressAt80 = est.result().progress; }
    snprintf(buf, sizeof buf, "progress 5 s after putting the finger back: %.0f%%", progressAt80 * 100);
    check("collecting again from 0", progressAt80 < 0.15f, buf);
  }

  printf("8. My real recordings (validation/logs/raw_pace*.txt, from the repo root)\n");
  for (int pace : {6, 10, 15, 20}) {
    char path[64]; snprintf(path, sizeof path, "validation/logs/raw_pace%d.txt", pace);
    FILE* f = fopen(path, "r");
    if (!f) { printf("  (skipped, %s not found)\n", path); continue; }
    RespRun r; Chain c; char line[128];
    while (fgets(line, sizeof line, f)) {
      if (strstr(line, "fifo reset")) { c = Chain(); continue; }
      unsigned long i; unsigned ir, red;
      if (sscanf(line, "%lu,%u,%u", &i, &ir, &red) == 3) c.feed(ir, red, pace, r);
    }
    fclose(f);
    snprintf(buf, sizeof buf, "%2d /min: shown %3.0f%% of %d estimates, within 2: %3.0f%%", pace,
             100.0 * r.shown / r.estimates, r.estimates, r.shown ? 100.0 * r.within / r.shown : 0);
    // 15/min was recorded right after coming in from outside (HR ~115): fewer estimates are shown, that's expected
    check("within 2 /min >= 90% of the shown estimates", r.shown > 0 && r.within >= 0.9 * r.shown, buf);
  }
  printf("9. My free breathing recording: I counted about 80 breaths in 150 s = 32 /min\n");
  {
    FILE* f = fopen("validation/logs/raw_free2.txt", "r");
    if (!f) printf("  (skipped, validation/logs/raw_free2.txt not found)\n");
    else {
      RespRun r; Chain c; char line[128]; int within3 = 0;
      while (fgets(line, sizeof line, f)) {
        unsigned long i; unsigned ir, red;
        if (sscanf(line, "%lu,%u,%u", &i, &ir, &red) != 3) continue;
        int before = r.shown;
        c.feed(ir, red, 32, r);
        if (r.shown > before && fabs(est.result().rate - 32) <= 3) within3++;
      }
      fclose(f);
      // counting by hand is only roughly right, so +-3 here
      snprintf(buf, sizeof buf, "shown %d of %d estimates, within 2: %d, within 3: %d", r.shown, r.estimates, r.within, within3);
      check("shown at least half the time, all within 3 of my count", r.shown >= r.estimates / 2 && within3 == r.shown, buf);
    }
  }

  printf("\n%s (%d failed)\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", failures);
  return failures ? 1 : 0;
}
