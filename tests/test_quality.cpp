// Tests the Phase 4 signal quality logic on simulated fingers.
//
//   g++ -std=c++17 -O2 -I firmware/04_display tests/test_quality.cpp -o test_quality
//   ./test_quality

#include <cstdio>
#include <cmath>
#include <map>
#include "quality.h"
#include "session.h"
#include "sim_finger.h"

using namespace pulselab;

struct Timeline {
  std::map<SignalState, int> count;      // samples in each state (after the first 8 s)
  int samples = 0;
  int showBpm = 0, showSpo2 = 0;
  float firstMovingAfterBump = -1;       // seconds after the bump
  float goodAgainAfterBump = -1;
  int minScore = 100, maxScore = 0;
  bool spo2ShownWhileOff = false;
};

static Timeline run(const Sim& s, unsigned seed) {
  SimFinger finger(s, seed);
  HeartRateDetector hr; hr.reset();
  Spo2Estimator sp; sp.reset();
  QualityMonitor qm; qm.reset();
  Timeline tl;
  float ir, red;
  while (finger.next(ir, red)) {
    float t = finger.t();
    HrResult h = hr.process(ir);
    Spo2Result r = sp.process(ir, red, h);
    QualityResult q = qm.process(h, r, ir, red);

    if (finger.fingerOff() && q.showSpo2) tl.spo2ShownWhileOff = true;

    if (s.bumpAt >= 0 && t >= s.bumpAt) {
      if (tl.firstMovingAfterBump < 0 && q.state == SignalState::Moving) tl.firstMovingAfterBump = t - s.bumpAt;
      if (tl.firstMovingAfterBump >= 0 && tl.goodAgainAfterBump < 0 && q.state == SignalState::Good && q.showSpo2)
        tl.goodAgainAfterBump = t - s.bumpAt;
    }

    bool inBump = s.bumpAt >= 0 && t >= s.bumpAt && t < s.bumpAt + 8;
    if (t >= 8 && !inBump && !finger.fingerOff()) {
      tl.samples++;
      tl.count[q.state]++;
      if (q.showBpm) tl.showBpm++;
      if (q.showSpo2) tl.showSpo2++;
      if (q.score < tl.minScore) tl.minScore = q.score;
      if (q.score > tl.maxScore) tl.maxScore = q.score;
    }
  }
  return tl;
}

static float pct(int a, int b) { return b ? 100.0f * a / b : 0; }

static int failures = 0;
static void check(const char* name, bool ok, const char* detail) {
  printf("  [%s] %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
  if (!ok) failures++;
}

int main() {
  char buf[256];

  printf("1. Normal finger, holding still\n");
  {
    int falseMoving = 0, total = 0, shownBpm = 0, shownSpo2 = 0, minScore = 100;
    for (unsigned seed = 1; seed <= 5; seed++) {
      Sim s; auto tl = run(s, seed);
      falseMoving += tl.count[SignalState::Moving]; total += tl.samples;
      shownBpm += tl.showBpm; shownSpo2 += tl.showSpo2;
      if (tl.minScore < minScore) minScore = tl.minScore;
    }
    snprintf(buf, sizeof buf, "BPM shown %.1f%%, SpO2 shown %.1f%%, 'Hold still' %.1f%% of the time, lowest score %d (5 runs)",
             pct(shownBpm, total), pct(shownSpo2, total), pct(falseMoving, total), minScore);
    check("values shown >95% of the time, almost no false 'Hold still'",
          pct(shownBpm, total) > 95 && pct(shownSpo2, total) > 95 && pct(falseMoving, total) < 2, buf);
  }

  printf("2. Finger pressed harder for a moment at 20 s\n");
  {
    Sim s; s.bumpAt = 20; auto tl = run(s, 2);
    snprintf(buf, sizeof buf, "'Hold still' after %.2f s, good again after %.1f s", tl.firstMovingAfterBump, tl.goodAgainAfterBump);
    check("shows 'Hold still' straight away and recovers within 6 s",
          tl.firstMovingAfterBump >= 0 && tl.firstMovingAfterBump < 0.3f && tl.goodAgainAfterBump > 0 && tl.goodAgainAfterBump < 6, buf);
  }

  printf("3. Red LED too bright (clipping)\n");
  {
    Sim s; s.dcRed = 66000; auto tl = run(s, 3);
    snprintf(buf, sizeof buf, "'Too bright' %.0f%% of the time, SpO2 shown %.0f%%", pct(tl.count[SignalState::Clipping], tl.samples), pct(tl.showSpo2, tl.samples));
    check("says 'Too bright' and never shows SpO2", pct(tl.count[SignalState::Clipping], tl.samples) > 95 && tl.showSpo2 == 0, buf);
  }

  printf("4. Very weak pulse (PI 0.3%%)\n");
  {
    Sim s; s.piIr = 0.003f; auto tl = run(s, 4);
    snprintf(buf, sizeof buf, "'Weak pulse' %.0f%%, BPM shown %.0f%%, SpO2 shown %.0f%%",
             pct(tl.count[SignalState::WeakPulse], tl.samples), pct(tl.showBpm, tl.samples), pct(tl.showSpo2, tl.samples));
    check("BPM still shown, SpO2 hidden", pct(tl.showBpm, tl.samples) > 80 && pct(tl.showSpo2, tl.samples) < 5, buf);
  }

  printf("5. Finger removed for 5 s\n");
  {
    Sim s; s.seconds = 45; s.fingerOffFrom = 20; s.fingerOffTo = 25; auto tl = run(s, 5);
    snprintf(buf, sizeof buf, "SpO2 shown while off: %s, values shown after: BPM %.0f%%",
             tl.spo2ShownWhileOff ? "yes" : "no", pct(tl.showBpm, tl.samples));
    check("nothing shown while off, back after", !tl.spo2ShownWhileOff && pct(tl.showBpm, tl.samples) > 80, buf);
  }

  printf("6. Fast and slow heart rate (140 / 50 BPM)\n");
  for (float b : {140.0f, 50.0f}) {
    Sim s; s.bpm = b; auto tl = run(s, 6);
    snprintf(buf, sizeof buf, "BPM %.0f: shown %.0f%%, false 'Hold still' %.1f%%", b, pct(tl.showBpm, tl.samples), pct(tl.count[SignalState::Moving], tl.samples));
    check("values shown, no false movement", pct(tl.showBpm, tl.samples) > 95 && pct(tl.count[SignalState::Moving], tl.samples) < 2, buf);
  }

  printf("7. Uneven pulse heights (+-25%%) and strong breathing (1%%)\n");
  {
    int falseMoving = 0, total = 0, shownSpo2 = 0;
    for (unsigned seed = 1; seed <= 5; seed++) {
      Sim s; s.ampJitter = 0.25f; s.breathing = 0.01f; auto tl = run(s, seed);
      falseMoving += tl.count[SignalState::Moving]; total += tl.samples; shownSpo2 += tl.showSpo2;
    }
    snprintf(buf, sizeof buf, "false 'Hold still' %.1f%% of the time, SpO2 shown %.1f%% (5 runs)", pct(falseMoving, total), pct(shownSpo2, total));
    check("hardly any false 'Hold still'", pct(falseMoving, total) < 5, buf);
  }

  printf("8. Session summary (start at 10 s, stop at 40 s)\n");
  for (int withGap = 0; withGap <= 1; withGap++) {
    Sim s; s.seconds = 40;
    if (withGap) { s.fingerOffFrom = 20; s.fingerOffTo = 30; }  // finger lifted for 10 s
    SimFinger f(s, 8);
    HeartRateDetector hr; hr.reset();
    Spo2Estimator sp; sp.reset();
    QualityMonitor qm; qm.reset();
    Session sess;
    float ir, red;
    while (f.next(ir, red)) {
      if (!sess.active() && f.t() >= 10.0f) sess.start();
      HrResult h = hr.process(ir);
      Spo2Result r = sp.process(ir, red, h);
      QualityResult q = qm.process(h, r, ir, red);
      sess.process(h, r, q);
    }
    sess.stop();
    const SessionSummary& m = sess.summary();
    snprintf(buf, sizeof buf, "%s: %lu s, HR avg %.1f (%.1f-%.1f, %u beats), SpO2 avg %.1f min %.1f, good %u%%",
             withGap ? "finger off 10 s" : "steady", (unsigned long)m.seconds, m.bpmAvg, m.bpmMin, m.bpmMax,
             m.bpmCount, m.spo2Avg, m.spo2Min, m.goodPct);
    bool ok = m.valid && m.seconds == 30 && fabsf(m.bpmAvg - 75) <= 2.5f && fabsf(m.spo2Avg - 97.5f) <= 1.0f;
    if (withGap) ok = ok && m.goodPct >= 40 && m.goodPct <= 70;  // ~1/3 off + time to settle again
    else ok = ok && m.goodPct >= 95;
    check(withGap ? "good-signal % drops when the finger is off, averages still right" : "averages right, good signal ~100%", ok, buf);
  }

  printf("9. First BPM shown after the finger is put back (10 runs)\n");
  {
    float worst = 0, slowest = 0;
    for (unsigned seed = 1; seed <= 10; seed++) {
      Sim s; s.seconds = 40; s.fingerOffFrom = 20; s.fingerOffTo = 30;
      SimFinger f(s, seed);
      HeartRateDetector hr; hr.reset();
      Spo2Estimator sp; sp.reset();
      QualityMonitor qm; qm.reset();
      float ir, red; bool done = false;
      while (f.next(ir, red)) {
        HrResult h = hr.process(ir);
        Spo2Result r = sp.process(ir, red, h);
        QualityResult q = qm.process(h, r, ir, red);
        if (!done && f.t() >= 30 && q.showBpm) {
          done = true;
          if (fabsf(h.bpm - 75) > fabsf(worst)) worst = h.bpm - 75;
          if (f.t() - 30 > slowest) slowest = f.t() - 30;
        }
      }
      if (!done) slowest = 99;
    }
    snprintf(buf, sizeof buf, "worst first value %+.1f BPM off, slowest shown after %.1f s", worst, slowest);
    check("first value within 4 BPM, shown within 7 s", fabsf(worst) <= 4 && slowest < 7, buf);
  }

  printf("\n%s (%d failed)\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", failures);
  return failures ? 1 : 0;
}
