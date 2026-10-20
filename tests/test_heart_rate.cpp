// Tests the Phase 2 beat detector on a simulated PPG signal, on a PC.
//
// Build and run (needs g++):
//   g++ -std=c++17 -O2 -I firmware/04_display tests/test_heart_rate.cpp -o test_hr
//   ./test_hr
//
// The fake signal has: a DC level, breathing drift, systolic + dicrotic pulse
// shape, beat-to-beat variation and random noise. Not perfect, but close
// enough to check that the filter and peak detection logic work.

#include <cstdio>
#include <cmath>
#include <random>
#include <vector>
#include "ppg_dsp.h"

using namespace pulselab;

struct Segment {
  float seconds;
  float bpm;        // 0 = finger removed
  float amplitude;  // pulse size in raw units
  float breathing;  // breathing drift size in raw units
  float bumpAt = -1.0f;   // optional: a short bump (finger pressed/moved) at this time in the segment
  float bumpSize = 0.0f;
};

struct Outcome {
  int trueBeats = 0;
  int detected = 0;
  std::vector<float> bpmAtEnd;   // BPM reported at the end of each segment
  std::vector<float> trueBpmEnd;  // real average BPM of the last 8 simulated beats
  float firstBpmSec = -1.0f;       // seconds after the finger went on until BPM was shown
  float maxAfterSettle = 0.0f;     // biggest |filtered signal| after settling
};

// Placing a finger isn't instant: IR rises from ~150 to the full level over a few hundred ms.
constexpr float RAMP_SEC = 0.3f;

static float pulseShape(float t) {
  // t = seconds since start of this beat
  auto g = [](float x, float mu, float s) { return expf(-0.5f * (x - mu) * (x - mu) / (s * s)); };
  return g(t, 0.12f, 0.05f) + 0.35f * g(t, 0.38f, 0.06f);
}

static Outcome run(const std::vector<Segment>& segs, float noiseStd, unsigned seed) {
  std::mt19937 rng(seed);
  std::normal_distribution<float> noise(0.0f, noiseStd);
  std::normal_distribution<float> jitter(0.0f, 0.03f);  // 3% beat-to-beat variation

  HeartRateDetector hr;
  hr.reset();
  Outcome out;

  float t = 0.0f;
  for (const auto& s : segs) {
    float segStart = t;
    float segEnd = t + s.seconds;
    float nextBeat = t;
    std::vector<float> beatStarts;
    float lastBpm = 0.0f;

    while (t < segEnd) {
      float raw;
      if (s.bpm <= 0.0f) {
        raw = 800.0f + noise(rng);  // no finger: low reading
      } else {
        while (nextBeat <= t) {
          beatStarts.push_back(nextBeat);
          nextBeat += (60.0f / s.bpm) * (1.0f + jitter(rng));
        }
        float v = 0.0f;
        for (size_t i = beatStarts.size() >= 3 ? beatStarts.size() - 3 : 0; i < beatStarts.size(); i++)
          v += pulseShape(t - beatStarts[i]);
        float breathing = s.breathing * sinf(2.0f * 3.14159f * 0.25f * t);
        float drift = 150.0f * sinf(2.0f * 3.14159f * 0.03f * t);
        float on = t - segStart;
        float level = on < RAMP_SEC ? 150.0f + (45000.0f - 150.0f) * on / RAMP_SEC : 45000.0f;
        float bump = 0.0f;
        if (s.bumpAt >= 0.0f && on > s.bumpAt && on < s.bumpAt + 0.15f)
          bump = s.bumpSize * sinf(3.14159f * (on - s.bumpAt) / 0.15f);
        raw = level + breathing + drift - s.amplitude * v + bump + noise(rng);
      }

      HrResult r = hr.process(raw);
      if (r.beat) out.detected++;
      if (r.bpm > 0 && out.firstBpmSec < 0) out.firstBpmSec = t - segStart;
      if (r.finger && !r.settling && fabsf(r.signal) > out.maxAfterSettle) out.maxAfterSettle = fabsf(r.signal);
      lastBpm = r.bpm;
      t += 1.0f / FS_HZ;
    }
    if (s.bpm > 0.0f) out.trueBeats += (int)beatStarts.size();
    out.bpmAtEnd.push_back(lastBpm);
    float trueBpm = 0.0f;
    if (beatStarts.size() > 9) {
      size_t n = beatStarts.size();
      trueBpm = 60.0f * 8.0f / (beatStarts[n - 1] - beatStarts[n - 9]);
    }
    out.trueBpmEnd.push_back(trueBpm);
  }
  return out;
}

static int failures = 0;

static void check(const char* name, bool ok, const char* detail) {
  printf("  [%s] %s  %s\n", ok ? "PASS" : "FAIL", name, detail);
  if (!ok) failures++;
}

int main() {
  char buf[200];

  printf("1. Steady 72 BPM, normal signal\n");
  {
    auto o = run({{60, 72, 400, 250}}, 5, 1);
    snprintf(buf, sizeof buf, "BPM=%.1f (true %.1f), beats %d/%d", o.bpmAtEnd[0], o.trueBpmEnd[0], o.detected, o.trueBeats);
    check("BPM within 2 of true", fabsf(o.bpmAtEnd[0] - o.trueBpmEnd[0]) <= 2, buf);
    check("beat count within 3 (first 2 s are skipped)", abs(o.detected - (o.trueBeats - 2)) <= 3, buf);
  }

  printf("2. Low (45 BPM) and high (150 BPM) heart rate\n");
  for (float bpm : {45.0f, 150.0f}) {
    auto o = run({{60, bpm, 400, 250}}, 5, 2);
    snprintf(buf, sizeof buf, "BPM=%.1f (true %.1f), beats %d/%d", o.bpmAtEnd[0], o.trueBpmEnd[0], o.detected, o.trueBeats);
    check("BPM within 3% of true", fabsf(o.bpmAtEnd[0] - o.trueBpmEnd[0]) <= 0.03f * o.trueBpmEnd[0], buf);
  }

  printf("3. Heart rate changes 60 -> 100 BPM\n");
  {
    auto o = run({{30, 60, 400, 250}, {30, 100, 400, 250}}, 5, 3);
    snprintf(buf, sizeof buf, "part 1: %.1f (true %.1f), part 2: %.1f (true %.1f)",
             o.bpmAtEnd[0], o.trueBpmEnd[0], o.bpmAtEnd[1], o.trueBpmEnd[1]);
    check("follows the change", fabsf(o.bpmAtEnd[0] - o.trueBpmEnd[0]) <= 2 && fabsf(o.bpmAtEnd[1] - o.trueBpmEnd[1]) <= 3, buf);
  }

  printf("4. Finger removed for 5 s, then put back\n");
  {
    auto o = run({{20, 75, 400, 250}, {5, 0, 0, 0}, {20, 75, 400, 250}}, 5, 4);
    snprintf(buf, sizeof buf, "before: %.1f, finger off: %.1f, after: %.1f (true %.1f)",
             o.bpmAtEnd[0], o.bpmAtEnd[1], o.bpmAtEnd[2], o.trueBpmEnd[2]);
    check("shows 0 with no finger and recovers", o.bpmAtEnd[1] == 0 && fabsf(o.bpmAtEnd[2] - o.trueBpmEnd[2]) <= 2, buf);
  }

  printf("5. Weak signal (pulse 60, breathing 60, noise 5)\n");
  {
    auto o = run({{60, 80, 60, 60}}, 5, 5);
    snprintf(buf, sizeof buf, "BPM=%.1f (true %.1f), beats %d/%d", o.bpmAtEnd[0], o.trueBpmEnd[0], o.detected, o.trueBeats);
    check("BPM within 3 of true", fabsf(o.bpmAtEnd[0] - o.trueBpmEnd[0]) <= 3, buf);
  }

  printf("6. Breathing drift bigger than the pulse (pulse 150, breathing 400, noise 5)\n");
  {
    auto o = run({{60, 70, 150, 400}}, 5, 6);
    snprintf(buf, sizeof buf, "BPM=%.1f (true %.1f), beats %d/%d", o.bpmAtEnd[0], o.trueBpmEnd[0], o.detected, o.trueBeats);
    check("BPM within 3 of true", fabsf(o.bpmAtEnd[0] - o.trueBpmEnd[0]) <= 3, buf);
  }

  printf("7. Strong signal near clipping (pulse 2000, noise 10)\n");
  {
    auto o = run({{60, 90, 2000, 300}}, 10, 7);
    snprintf(buf, sizeof buf, "BPM=%.1f (true %.1f), beats %d/%d", o.bpmAtEnd[0], o.trueBpmEnd[0], o.detected, o.trueBeats);
    check("BPM within 2 of true", fabsf(o.bpmAtEnd[0] - o.trueBpmEnd[0]) <= 2, buf);
  }

  printf("8. Placing the finger (IR rises over 300 ms) should not upset the start\n");
  {
    auto o = run({{30, 90, 300, 150}}, 5, 8);
    snprintf(buf, sizeof buf, "BPM shown after %.1f s, biggest signal after settling %.0f, BPM=%.1f (true %.1f)",
             o.firstBpmSec, o.maxAfterSettle, o.bpmAtEnd[0], o.trueBpmEnd[0]);
    // a 300-unit pulse gives a filtered signal of roughly 150-250, so anything far above means a leftover jump
    check("BPM within 5 s and no big leftover jump", o.firstBpmSec > 0 && o.firstBpmSec < 5 && o.maxAfterSettle < 400, buf);
  }

  printf("9. Finger pressed harder for a moment, right after settling and again later\n");
  {
    auto a = run({{20, 105, 500, 200, 2.4f, -6000}}, 5, 9);
    auto b = run({{20, 105, 500, 200, 10.0f, 8000}}, 5, 10);
    snprintf(buf, sizeof buf, "early bump: first BPM after %.1f s, BPM=%.1f (true %.1f); later bump: BPM=%.1f (true %.1f)",
             a.firstBpmSec, a.bpmAtEnd[0], a.trueBpmEnd[0], b.bpmAtEnd[0], b.trueBpmEnd[0]);
    check("recovers quickly and BPM stays right", a.firstBpmSec > 0 && a.firstBpmSec < 8 &&
          fabsf(a.bpmAtEnd[0] - a.trueBpmEnd[0]) <= 2 && fabsf(b.bpmAtEnd[0] - b.trueBpmEnd[0]) <= 2, buf);
  }

  printf("\n%s (%d failed)\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", failures);
  return failures ? 1 : 0;
}
