// Tests the Phase 5 HRV code.
//
//   g++ -std=c++17 -O2 -I firmware/05_hrv tests/test_hrv.cpp -o test_hrv
//   ./test_hrv

#include <cstdio>
#include <cmath>
#include <random>
#include <vector>
#include "hrv.h"
#include "spo2.h"
#include "sim_finger.h"

using namespace pulselab;

static int failures = 0;
static void check(const char* name, bool ok, const char* detail) {
  printf("  [%s] %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
  if (!ok) failures++;
}

// Feed a list of intervals as if they came one after another.
static HrvAnalyzer feed(const std::vector<uint16_t>& ibis, uint32_t& t, HrvAnalyzer* use = nullptr) {
  static HrvAnalyzer a;
  HrvAnalyzer& h = use ? *use : a;
  if (!use) h.reset();
  for (uint16_t v : ibis) { t += v / 10; h.addInterval(v, t); }
  return h;
}

// Reference RMSSD / SDNN, written the plain way.
static void reference(const std::vector<uint16_t>& v, double& rmssd, double& sdnn) {
  double s = 0; int n = 0;
  for (size_t i = 1; i < v.size(); i++) { double d = (double)v[i] - v[i - 1]; s += d * d; n++; }
  rmssd = sqrt(s / n);
  double m = 0; for (auto x : v) m += x; m /= v.size();
  double q = 0; for (auto x : v) q += (x - m) * (x - m);
  sdnn = sqrt(q / v.size());
}

int main() {
  char buf[256];
  uint32_t t;

  printf("1. Perfectly steady 800 ms\n");
  { t = 0; std::vector<uint16_t> v(60, 800); auto h = feed(v, t); auto r = h.result();
    snprintf(buf, sizeof buf, "RMSSD %.1f SDNN %.1f HR %.1f", r.rmssd, r.sdnn, r.meanHr);
    check("RMSSD 0, SDNN 0, HR 75", r.ready && r.rmssd < 0.01f && r.sdnn < 0.01f && fabsf(r.meanHr - 75) < 0.1f, buf); }

  printf("2. Alternating 780 / 820 ms\n");
  { t = 0; std::vector<uint16_t> v; for (int i = 0; i < 60; i++) v.push_back(i % 2 ? 820 : 780);
    auto h = feed(v, t); auto r = h.result();
    snprintf(buf, sizeof buf, "RMSSD %.2f SDNN %.2f pNN50 %.0f%%", r.rmssd, r.sdnn, r.pnn50);
    check("RMSSD exactly 40, SDNN 20, pNN50 0", fabsf(r.rmssd - 40) < 0.01f && fabsf(r.sdnn - 20) < 0.01f && r.pnn50 == 0, buf); }

  printf("3. Random intervals compared with a plain reference calculation\n");
  { std::mt19937 rng(1); std::normal_distribution<float> n(0, 30);
    std::vector<uint16_t> v; for (int i = 0; i < 70; i++) v.push_back((uint16_t)(820 + n(rng)));
    t = 0; auto h = feed(v, t); auto r = h.result();
    // what's inside the 60 s window: the last intervals, minus the 2 still pending
    std::vector<uint16_t> used(v.begin(), v.end() - HRV_PENDING);
    double sum = 0; size_t k = used.size(); while (k > 0 && sum + used[k - 1] <= 60000 + used[k - 1]) { sum += used[k - 1]; k--; }
    std::vector<uint16_t> win(used.begin() + k, used.end());
    double rr, sd; reference(win, rr, sd);
    snprintf(buf, sizeof buf, "code: RMSSD %.2f SDNN %.2f | reference: %.2f %.2f (%zu intervals)", r.rmssd, r.sdnn, rr, sd, win.size());
    check("same as reference within 1%", fabsf(r.rmssd - rr) < 0.01 * rr && fabsf(r.sdnn - sd) < 0.01 * sd, buf); }

  printf("4. One early beat (560 ms then 1040 ms) in a breathing rhythm\n");
  { auto make = [](bool ectopic) { std::vector<uint16_t> v;
      for (int i = 0; i < 70; i++) v.push_back((uint16_t)(800 + 40 * sinf(i * 2 * 3.14159f / 4.5f)));
      if (ectopic) { v[40] = 560; v[41] = 1040; } return v; };
    HrvAnalyzer a, b; t = 0; feed(make(false), t, &a); t = 0; feed(make(true), t, &b);
    auto ra = a.result(), rb = b.result();
    // without the artifact check, what would RMSSD be?
    double rawR, rawS; auto vv = make(true); std::vector<uint16_t> last(vv.end() - 2 - ra.intervals, vv.end() - 2); reference(last, rawR, rawS);
    snprintf(buf, sizeof buf, "clean %.1f ms, with early beat %.1f ms (without cleaning it would be %.1f), artifacts %u%%",
             ra.rmssd, rb.rmssd, rawR, rb.artifactPct);
    check("early beat removed, RMSSD within 5%", fabsf(rb.rmssd - ra.rmssd) < 0.05f * ra.rmssd && rb.artifactPct > 0, buf); }

  printf("5. Gap: no difference taken across it\n");
  { HrvAnalyzer a; a.reset(); t = 0;
    for (int i = 0; i < 30; i++) { t += 80; a.addInterval(800, t); }
    a.markGap();
    for (int i = 0; i < 30; i++) { t += 90; a.addInterval(900, t); }
    auto r = a.result();
    snprintf(buf, sizeof buf, "RMSSD %.1f (a jump of 100 ms across the gap would give >0)", r.rmssd);
    check("RMSSD 0", r.rmssd < 0.01f, buf); }

  printf("6. Only the last 60 s count\n");
  { HrvAnalyzer a; a.reset(); t = 0;
    for (int i = 0; i < 90; i++) { uint16_t v = i % 2 ? 860 : 740; t += v / 10; a.addInterval(v, t); }
    float before = a.result().rmssd;
    for (int i = 0; i < 90; i++) { t += 80; a.addInterval(800, t); }
    auto r = a.result();
    snprintf(buf, sizeof buf, "RMSSD %.1f during the variable part, %.1f after 72 s of steady beats", before, r.rmssd);
    check("old part forgotten", before > 100 && r.rmssd < 0.01f, buf); }

  printf("7. Not shown before 30 s of clean beats\n");
  { HrvAnalyzer a; a.reset(); t = 0; bool readyEarly = false; float readyAt = -1; float secs = 0;
    for (int i = 0; i < 60; i++) { t += 80; a.addInterval(800, t); secs += 0.8f;
      if (a.result().ready && readyAt < 0) readyAt = secs;
      if (secs < 30 && a.result().ready) readyEarly = true; }
    snprintf(buf, sizeof buf, "ready after %.1f s of beats (2 still pending)", readyAt);
    check("ready at ~31.6 s, not before", !readyEarly && readyAt > 31 && readyAt < 33.5f, buf); }

  printf("8. A fake interval just before the signal goes bad is never used\n");
  for (int waitRule = 1; waitRule >= 0; waitRule--) {
    // waitRule = 0 shows what would happen without the "wait 2 beats" rule:
    // the 700 ms interval goes in straight away.
    HrvAnalyzer a; a.reset();
    QualityResult good{SignalState::Good, 100, true, true};
    QualityResult moving{SignalState::Moving, 30, false, false};
    HrResult h{}; h.finger = true;
    auto beat = [&](uint16_t ms) {
      for (int k = 0; k < ms / 10 - 1; k++) { h.beat = false; a.process(h, good); }
      h.beat = true; h.ibiMs = ms; h.ibiValid = true; a.process(h, good); h.beat = false; };
    for (int i = 0; i < 50; i++) beat(800);
    beat(700);                                     // finger starting to move: not a real heartbeat
    if (waitRule) a.process(h, moving);            // ...and the quality code notices a moment later
    else { beat(800); beat(800); a.process(h, moving); }  // without the rule it would already be counted
    for (int i = 0; i < 40; i++) beat(800);
    auto r = a.result();
    snprintf(buf, sizeof buf, "%s: RMSSD %.1f", waitRule ? "with the rule" : "if it had been used", r.rmssd);
    if (waitRule) check("RMSSD stays 0", r.rmssd < 0.01f, buf);
    else printf("         (for comparison) %s\n", buf);
  }

  printf("9. Stress levels\n");
  { bool ok = stressFromRmssd(15) == StressLevel::High && stressFromRmssd(25) == StressLevel::Some &&
              stressFromRmssd(45) == StressLevel::Calm && stressFromRmssd(70) == StressLevel::Relaxed &&
              stressFromRmssd(0) == StressLevel::Unknown;
    check("15 High, 25 Some, 45 Calm, 70 Relaxed", ok, ""); }

  printf("10. Whole chain on the simulated finger vs the true beats\n");
  for (float j : {0.01f, 0.03f, 0.06f}) {
    Sim s; s.seconds = 100; s.bpm = 70; s.beatJitter = j; s.ampJitter = 0.1f;
    SimFinger f(s, 11);
    HeartRateDetector hr; hr.reset(); Spo2Estimator sp; sp.reset(); QualityMonitor qm; qm.reset(); HrvAnalyzer hv; hv.reset();
    float ir, red;
    while (f.next(ir, red)) { auto h = hr.process(ir); auto r = sp.process(ir, red, h); auto q = qm.process(h, r, ir, red); hv.process(h, q); }
    // true RMSSD over (roughly) the same last 60 s
    const auto& bt = f.beatTimes(); std::vector<uint16_t> tru;
    for (size_t i = 1; i < bt.size(); i++) if (bt[i] > s.seconds - 62 && bt[i] < s.seconds - 1.5f) tru.push_back((uint16_t)((bt[i] - bt[i - 1]) * 1000 + 0.5f));
    double rr, sd; reference(tru, rr, sd);
    auto r = hv.result();
    snprintf(buf, sizeof buf, "jitter %.0f%%: measured RMSSD %.1f, true %.1f (%+.1f%%), artifacts %u%%",
             j * 100, r.rmssd, rr, 100 * (r.rmssd - rr) / rr, r.artifactPct);
    check("within 8% of the truth", r.ready && fabsf(r.rmssd - rr) < 0.08 * rr, buf);
  }

  // Runs the whole chain on a simulated finger. Returns the HRV result and counts.
  struct Run { HrvResult v; int beats = 0, rejected = 0, resets = 0; float readyAt = -1, trueRmssd = 0; };
  auto runChain = [](const Sim& s, unsigned seed) {
    Run out;
    SimFinger f(s, seed);
    HeartRateDetector hr; hr.reset(); Spo2Estimator sp; sp.reset(); QualityMonitor qm; qm.reset(); HrvAnalyzer hv; hv.reset();
    float ir, red; bool shown = false;
    while (f.next(ir, red)) {
      auto h = hr.process(ir); auto r = sp.process(ir, red, h); auto q = qm.process(h, r, ir, red); auto v = hv.process(h, q);
      if (h.beat && h.ibiMs) { out.beats++; if (!h.ibiValid) out.rejected++; }
      if (shown && h.bpm == 0) out.resets++;
      shown = h.bpm > 0;
      if (v.ready && out.readyAt < 0) out.readyAt = f.t();
    }
    out.v = hv.result();
    const auto& bt = f.beatTimes(); std::vector<uint16_t> tru;
    for (size_t i = 1; i < bt.size(); i++) if (bt[i] > s.seconds - 62 && bt[i] < s.seconds - 1.5f) tru.push_back((uint16_t)((bt[i] - bt[i - 1]) * 1000 + 0.5f));
    double rr, sd; reference(tru, rr, sd); out.trueRmssd = (float)rr;
    return out;
  };

  printf("11. Slow deep breathing (6 per minute), heart rate swinging +-15%%, +-25%%, +-35%%\n");
  printf("    (my first real test: long out-breath beats were rejected, BPM reset 4 times, HRV came out too low)\n");
  for (float rsa : {0.15f, 0.25f, 0.35f}) {
    Sim s; s.seconds = 120; s.bpm = 75; s.beatJitter = 0.01f; s.rsa = rsa; s.breathRate = 0.1f;
    Run o = runChain(s, 5);
    snprintf(buf, sizeof buf, "+-%.0f%%: rejected %d/%d, resets %d, RMSSD %.1f (true %.1f), artifacts %u%%, HRV after %.0f s",
             rsa * 100, o.rejected, o.beats, o.resets, o.v.rmssd, o.trueRmssd, o.v.artifactPct, o.readyAt);
    check("no resets, RMSSD within 5%, shown within 45 s",
          o.resets == 0 && o.rejected <= 0.08f * o.beats && fabsf(o.v.rmssd - o.trueRmssd) < 0.05f * o.trueRmssd &&
          o.readyAt > 0 && o.readyAt < 45 && o.v.steady, buf);
  }

  printf("12. Heart rate rising from 65 to 100 BPM (e.g. after fast breathing or exercise)\n");
  { Sim s; s.seconds = 90; s.bpm = 65; s.bpmEnd = 100; s.beatJitter = 0.01f;
    Run o = runChain(s, 6);
    snprintf(buf, sizeof buf, "RMSSD %.1f, steady %s, level %s", o.v.rmssd, o.v.steady ? "yes" : "no", stressText(o.v.level));
    check("no stress level given while HR is changing", o.v.ready && !o.v.steady && o.v.level == StressLevel::Unknown, buf); }

  printf("13. Wrong intervals right after placing the finger are not used\n");
  { HrvAnalyzer a; a.reset();
    QualityResult searching{SignalState::Searching, 85, false, false};
    QualityResult good{SignalState::Good, 100, true, true};
    HrResult h{}; h.finger = true;
    auto beat = [&](uint16_t ms, const QualityResult& q) {
      for (int k = 0; k < ms / 10 - 1; k++) { h.beat = false; a.process(h, q); }
      h.beat = true; h.ibiMs = ms; h.ibiValid = true; a.process(h, q); h.beat = false; };
    beat(327, searching); beat(311, searching); beat(963, searching);   // like my first real test
    float t = 0, readyAt = -1;
    for (int i = 0; i < 60; i++) {
      uint16_t v = (uint16_t)(900 + 40 * sinf(i * 2 * 3.14159f / 4.5f));
      beat(v, good); t += v / 1000.0f;
      if (a.result().ready && readyAt < 0) readyAt = t;
    }
    snprintf(buf, sizeof buf, "HRV shown %.1f s after the good signal started (was ~60 s in my test), artifacts %u%%", readyAt, a.result().artifactPct);
    check("shown after ~32 s, nothing marked as artifact", readyAt > 0 && readyAt < 36 && a.result().artifactPct == 0, buf); }

  printf("\n%s (%d failed)\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", failures);
  return failures ? 1 : 0;
}
