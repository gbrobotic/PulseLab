// Tests for Phase 7 (guided breathing session). Run from the repo root:
//   g++ -std=c++17 -O2 -I firmware/07_guided -I tests tests/test_guided.cpp -o test_guided && ./test_guided

#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <initializer_list>
#include "ppg_dsp.h"
#include "spo2.h"
#include "quality.h"
#include "guided.h"
#include "sim_finger.h"

using namespace pulselab;

static int failures = 0;
static void check(const char* name, bool ok, const char* detail) {
  printf("  [%s] %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
  if (!ok) failures++;
}

struct Chain {
  HeartRateDetector hr; Spo2Estimator sp; QualityMonitor qm; GuidedSession g;
  Chain() { hr.reset(); sp.reset(); qm.reset(); }
  HrResult feed(float ir, float red) {
    HrResult h = hr.process(ir); Spo2Result s = sp.process(ir, red, h); QualityResult q = qm.process(h, s, ir, red);
    g.process(h, q);
    return h;
  }
};

// Simulated person: breathing normally (15/min, small swing), then slow at 6/min
// with a big swing while the pacer runs, then normal again.
struct Person { float normalRsa = 0.02f, slowRsa = 0.12f; };

static void runSession(Chain& c, SimFinger& f, const Person& p, std::vector<float>* truthSwing, float* trueRmssd) {
  float ir, red;
  // settle 10 s before starting, like putting the finger on and then pressing the button
  while (f.t() < 10 && f.next(ir, red)) c.feed(ir, red);
  c.g.start(180);
  double tStart = f.t(), tSlow = tStart + 60, tAfter = tSlow + 180;
  bool switched = false, back = false;
  while (c.g.active() && f.next(ir, red)) {
    if (!switched && f.t() >= tSlow) { f.params().breathRate = 0.1f; f.params().rsa = p.slowRsa; switched = true; }
    if (!back && f.t() >= tAfter) { f.params().breathRate = 0.25f; f.params().rsa = p.normalRsa; back = true; }
    c.feed(ir, red);
  }
  if (truthSwing) {
    // true swing from the simulator's beat times, per 10 s pacer breath, skipping the first 2
    const auto& bt = f.beatTimes();
    for (int k = GUIDED_SKIP_BREATHS; k < 18; k++) {
      double a = tSlow + 10.0 * k, b = a + 10.0; float hi = 0, lo = 1000;
      for (size_t i = 1; i < bt.size(); i++) if (bt[i] >= a && bt[i] < b) { float hr = 60.0f / (bt[i] - bt[i - 1]); hi = fmaxf(hi, hr); lo = fminf(lo, hr); }
      truthSwing->push_back(hi - lo);
    }
  }
  (void)trueRmssd;
}

int main() {
  char buf[256];

  printf("1. Phases: 60 s before, 180 s slow breathing, 60 s after, then done\n");
  {
    Sim s; s.seconds = 400; s.bpm = 70; s.rsa = 0.02f; Chain c; SimFinger f(s, 1);
    float ir, red; int changes = 0; double tBreathe = -1, tAfter = -1, tDone = -1;
    while (f.t() < 10 && f.next(ir, red)) c.feed(ir, red);
    c.g.start(180); double t0 = f.t();
    while (f.next(ir, red)) {
      c.feed(ir, red);
      const GuidedResult& r = c.g.result();
      if (r.changed) { changes++; if (r.phase == GuidedPhase::Breathe) tBreathe = f.t() - t0; if (r.phase == GuidedPhase::After) tAfter = f.t() - t0; if (r.phase == GuidedPhase::Done) tDone = f.t() - t0; }
    }
    snprintf(buf, sizeof buf, "slow from %.2f s, after from %.2f s, done at %.2f s", tBreathe, tAfter, tDone);
    check("timing exact", fabs(tBreathe - 60) < 0.02 && fabs(tAfter - 240) < 0.02 && fabs(tDone - 300) < 0.02 && changes == 3, buf);
  }

  printf("2. Finger lifted for 10 s during slow breathing: pauses, time doesn't run\n");
  {
    Sim s; s.seconds = 420; s.bpm = 70; s.rsa = 0.02f; s.fingerOffFrom = 120; s.fingerOffTo = 130;
    Chain c; SimFinger f(s, 2); float ir, red; double tDone = -1; int pausedSamples = 0;
    while (f.t() < 10 && f.next(ir, red)) c.feed(ir, red);
    c.g.start(180); double t0 = f.t();
    while (f.next(ir, red)) { c.feed(ir, red); if (c.g.result().paused) pausedSamples++; if (c.g.result().changed && c.g.result().phase == GuidedPhase::Done) tDone = f.t() - t0; }
    snprintf(buf, sizeof buf, "paused %.1f s, done at %.1f s (300 + pause)", pausedSamples / 100.0, tDone);
    check("paused for the time without a finger", fabs(pausedSamples / 100.0 - 10) < 0.1 && fabs(tDone - 310) < 0.2, buf);
  }

  printf("3. Swing per breath matches the simulator's true heart rate swing\n");
  for (float rsa : {0.06f, 0.12f}) {
    Sim s; s.seconds = 400; s.bpm = 70; s.rsa = 0.02f; s.beatJitter = 0.01f;
    Person p; p.slowRsa = rsa;
    Chain c; SimFinger f(s, 3); std::vector<float> truth;
    runSession(c, f, p, &truth, nullptr);
    double tm = 0; for (float v : truth) tm += v; tm /= truth.size();
    const GuidedResult& r = c.g.result();
    snprintf(buf, sizeof buf, "swing +-%.0f%%: measured %.1f BPM over %u breaths, true %.1f BPM", rsa * 100, r.swing, r.swingBreaths, tm);
    check("within 1.5 BPM", r.swingBreaths >= 14 && fabs(r.swing - tm) < 1.5, buf);
  }

  printf("4. HRV goes up while breathing slowly (simulated person)\n");
  {
    Sim s; s.seconds = 400; s.bpm = 70; s.rsa = 0.02f; s.beatJitter = 0.01f;
    Chain c; SimFinger f(s, 4); Person p;
    runSession(c, f, p, nullptr, nullptr);
    const GuidedResult& r = c.g.result();
    snprintf(buf, sizeof buf, "RMSSD before %.1f, slow %.1f, after %.1f ms | HR %.1f / %.1f / %.1f | swing %.1f", r.before.rmssd, r.during.rmssd, r.after.rmssd,
             r.before.bpm, r.during.bpm, r.after.bpm, r.swing);
    // RMSSD (beat-to-beat differences) rises less than the swing itself; my
    // real Phase 5 test went from ~36 to ~60 ms (1.7x)
    check("slow > 1.5x before, after back down", r.during.rmssd > 1.5f * r.before.rmssd && r.after.rmssd < 0.7f * r.during.rmssd && r.phase == GuidedPhase::Done, buf);
  }

  printf("5. My recordings: swing per breath with slow breathing (raw_pace6) vs breathing normally (raw_free)\n");
  {
    float sw[2] = {0, 0}; int n[2] = {0, 0}; const char* files[2] = {"validation/logs/raw_pace6.txt", "validation/logs/raw_free.txt"};
    bool found = true;
    for (int k = 0; k < 2; k++) {
      FILE* f = fopen(files[k], "r");
      if (!f) { found = false; break; }
      Chain c; char line[128]; bool started = false; long i0 = -1;
      while (fgets(line, sizeof line, f)) {
        unsigned long i; unsigned ir, red;
        if (sscanf(line, "%lu,%u,%u", &i, &ir, &red) != 3) continue;
        if (i0 < 0) i0 = (long)i;
        if (!started && (long)i - i0 >= 1000) { c.g.start(130, 5, 5); started = true; }   // 130 s of "slow" part
        c.feed(ir, red);
      }
      fclose(f);
      sw[k] = c.g.result().swing; n[k] = c.g.result().swingBreaths;
    }
    if (!found) printf("  (skipped, recordings not found)\n");
    else {
      snprintf(buf, sizeof buf, "6/min: %.1f BPM per breath (%d breaths), normal: %.1f BPM (%d breaths)", sw[0], n[0], sw[1], n[1]);
      check("slow breathing swing clearly bigger (> 1.5x)", sw[0] > 1.5f * sw[1] && n[0] >= 8, buf);
    }
  }

  printf("6. My first real session (validation/logs/raw_guided1.txt): the PC gets the same result as the device\n");
  {
    FILE* f = fopen("validation/logs/raw_guided1.txt", "r");
    if (!f) printf("  (skipped, recording not found)\n");
    else {
      Chain c; char line[160]; float dev[9] = {0}; bool haveDev = false;
      while (fgets(line, sizeof line, f)) {
        if (strstr(line, "fifo reset")) { c.hr.reset(); c.sp.reset(); c.qm.reset(); continue; }
        if (strstr(line, "# guided before")) { c.g.start(180); continue; }
        if (const char* p = strstr(line, "GUIDED_CSV,")) {
          int secs; haveDev = sscanf(p, "GUIDED_CSV,%d,%f,%f,%f,%f,%f,%f,%f,%f", &secs, &dev[0], &dev[1], &dev[2], &dev[3], &dev[4], &dev[5], &dev[6], &dev[7]) == 9;
          continue;
        }
        unsigned long i; unsigned ir, red;
        if (sscanf(line, "%lu,%u,%u", &i, &ir, &red) == 3) c.feed(ir, red);
      }
      fclose(f);
      const GuidedResult& r = c.g.result();
      float pc[8] = {r.before.bpm, r.during.bpm, r.after.bpm, r.before.rmssd, r.during.rmssd, r.after.rmssd, r.swing, (float)r.swingBreaths};
      float worst = 0;
      for (int k = 0; k < 8; k++) worst = fmaxf(worst, fabsf(pc[k] - dev[k]));
      snprintf(buf, sizeof buf, "device HR %.1f/%.1f/%.1f HRV %.1f/%.1f/%.1f swing %.1f; PC HR %.1f/%.1f/%.1f HRV %.1f/%.1f/%.1f swing %.1f",
               dev[0], dev[1], dev[2], dev[3], dev[4], dev[5], dev[6], pc[0], pc[1], pc[2], pc[3], pc[4], pc[5], pc[6]);
      check("same to 0.1", haveDev && worst <= 0.1f, buf);
      snprintf(buf, sizeof buf, "HRV %.1f -> %.1f -> %.1f ms", pc[3], pc[4], pc[5]);
      check("HRV during slow breathing > 1.5x before", pc[4] > 1.5f * pc[3], buf);
    }
  }

  printf("\n%s (%d failed)\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", failures);
  return failures ? 1 : 0;
}
