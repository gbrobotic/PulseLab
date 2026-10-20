// hrv.h
// Heart rate variability (HRV) for PulseLab.
//
// HRV is how much the time between heartbeats changes from beat to beat.
// At rest the heart doesn't beat like a metronome: the gaps get a bit
// shorter when breathing in and longer when breathing out. That variation
// is mostly controlled by the parasympathetic ("rest") nervous system, so
// it's usually higher when relaxed and lower when stressed or tired.
//
// Measures, over the last 60 s of clean beats:
//   RMSSD  root mean square of the differences between neighbouring intervals (ms)
//          -> the standard short-term HRV measure, mostly "rest" nervous system
//   SDNN   standard deviation of the intervals (ms)
//   pNN50  % of neighbouring intervals that differ by more than 50 ms
//
// HRV is very sensitive to wrong intervals: one early beat or one beat
// timed on a finger movement can double RMSSD. So intervals are cleaned first:
//   - only intervals recorded while the signal is good are used (not while
//     settling / measuring / moving / too bright). Others leave a gap
//   - an interval more than 20% away from the median of its neighbours
//     (2 before, itself, 2 after) is marked as an artifact (early beat,
//     missed beat, ...). Looking at both sides follows the slow wave that
//     breathing makes in the intervals, so deep slow breathing isn't
//     mistaken for artifacts (that happened with a median of past beats only)
//   - differences are only taken between two normal intervals that follow
//     each other directly, never across a gap or an artifact
//   - every interval waits for 2 more beats before it's used (needed anyway
//     for the neighbours above). If the finger is lifted or moves in that
//     time, the waiting ones are thrown away. (In Phase 3 the last interval
//     before lifting the finger was fake.)
//   - RMSSD only means something when the heart rate is steady. If the
//     average heart rate in the first and last 20 s of the window differs by
//     more than 12% (e.g. after exercise, or fast breathing), no stress level
//     is given ("HR changing")

#pragma once
#include <stdint.h>
#include <math.h>
#include "ppg_dsp.h"
#include "quality.h"

namespace pulselab {

// ---------------- Settings ----------------

constexpr uint32_t HRV_WINDOW_SAMPLES = 6000;  // 60 s at 100 Hz
constexpr float    HRV_MIN_SECONDS    = 30.0f; // need 30 s of clean intervals before showing HRV
constexpr uint8_t  HRV_MIN_PAIRS      = 20;    // ...and at least 20 neighbouring pairs
constexpr float    HRV_ARTIFACT_JUMP  = 0.20f; // >20% from the median of its neighbours = artifact
constexpr uint8_t  HRV_PENDING        = 2;     // an interval is used once 2 more beats have come
constexpr float    HRV_STEADY_CHANGE  = 0.12f; // first vs last 20 s of the window: >12% = HR changing
constexpr uint32_t HRV_STEADY_PART    = 2000;  // 20 s at 100 Hz
constexpr uint8_t  HRV_MAX_ITEMS      = 160;   // 60 s at up to 160 BPM

// Stress indicator from RMSSD (ms). Rough ranges for resting adults, not a
// clinical measure: HRV also depends a lot on age, fitness, breathing,
// posture and time of day. Personal comparisons (same person, same
// conditions) mean much more than these fixed numbers.
constexpr float RMSSD_RELAXED = 60.0f;  // >= 60 ms        "Relaxed"
constexpr float RMSSD_CALM    = 35.0f;  // 35-60 ms        "Calm"
constexpr float RMSSD_SOME    = 20.0f;  // 20-35 ms        "Some stress", below 20 "High stress"

enum class StressLevel : uint8_t { Unknown, High, Some, Calm, Relaxed };

inline const char* stressText(StressLevel s) {
  switch (s) {
    case StressLevel::High:    return "High stress";
    case StressLevel::Some:    return "Some stress";
    case StressLevel::Calm:    return "Calm";
    case StressLevel::Relaxed: return "Relaxed";
    default:                   return "--";
  }
}

inline StressLevel stressFromRmssd(float rmssd) {
  if (rmssd <= 0.0f)          return StressLevel::Unknown;
  if (rmssd >= RMSSD_RELAXED) return StressLevel::Relaxed;
  if (rmssd >= RMSSD_CALM)    return StressLevel::Calm;
  if (rmssd >= RMSSD_SOME)    return StressLevel::Some;
  return StressLevel::High;
}

struct HrvResult {
  bool        ready;        // enough clean data to show
  bool        steady;       // heart rate steady enough for the stress level to mean something
  bool        updated;      // a new interval was added on this sample
  float       rmssd, sdnn, pnn50;   // ms, ms, %
  float       meanHr;       // from the clean intervals
  uint16_t    intervals;    // clean intervals in the window
  uint16_t    pairs;        // neighbouring pairs used for RMSSD
  uint8_t     artifactPct;  // % of intervals in the window marked as artifacts
  float       progress;     // 0..1 until ready
  StressLevel level;
};

class HrvAnalyzer {
public:
  void reset() {
    count_ = head_ = 0;
    pendingN_ = 0;
    gap_ = true;
    sampleIdx_ = 0;
    last_ = HrvResult{};
  }

  // Call once per sample with the heart rate and quality results.
  HrvResult process(const HrResult& h, const QualityResult& q) {
    sampleIdx_++;
    last_.updated = false;

    if (!h.finger) {
      if (count_ || pendingN_) reset();
      return last_;
    }

    // Only beats from a good signal. In particular not while "Measuring...":
    // the first intervals after placing the finger are often wrong, and in my
    // first real test they poisoned the reference and held HRV up for a minute.
    bool badSignal = h.settling || !(q.state == SignalState::Good || q.state == SignalState::WeakPulse);
    if (badSignal) {
      dropPending();
      return last_;
    }

    if (h.beat && h.ibiMs > 0) {
      if (!h.ibiValid) {
        dropPending();          // the beats around a rejected interval aren't trusted either
      } else {
        addPending(h.ibiMs);
      }
    }
    return last_;
  }

  // Also used directly by the tests: add one interval that ended at sample `now`.
  void addInterval(uint16_t ibiMs, uint32_t now) {
    sampleIdx_ = now;
    addPending(ibiMs);
  }
  void markGap() { dropPending(); }

  const HrvResult& result() const { return last_; }

  // For the beat-interval graph on the screen: i = 0 is the oldest.
  uint8_t  historyCount() const { return count_; }
  uint16_t historyIbi(uint8_t i) const { return items_[idx(i)].ibi; }
  bool     historyNormal(uint8_t i) const { return items_[idx(i)].normal; }

private:
  struct Item {
    uint16_t ibi;
    uint32_t endSample;
    bool     normal;     // passed the artifact check
    bool     follows;    // directly follows the previous item (no gap in between)
  };
  struct Pending { uint16_t ibi; uint32_t endSample; bool follows; };

  uint8_t idx(uint8_t i) const { return (head_ + HRV_MAX_ITEMS - count_ + i) % HRV_MAX_ITEMS; }

  void dropPending() {
    pendingN_ = 0;
    gap_ = true;
  }

  void addPending(uint16_t ibi) {
    pending_[pendingN_++] = {ibi, sampleIdx_, !gap_};
    gap_ = false;
    if (pendingN_ > HRV_PENDING) {
      commit(pending_[0]);
      for (uint8_t i = 1; i < pendingN_; i++) pending_[i - 1] = pending_[i];
      pendingN_--;
    }
  }

  void commit(const Pending& p) {
    // Artifact check: compare with the median of the neighbours, 2 before
    // (if they follow directly) and the 2 waiting after it.
    uint16_t v[5];
    uint8_t n = 0;
    v[n++] = p.ibi;
    if (p.follows) {
      for (int k = 1; k <= 2 && k <= count_; k++) {
        const Item& it = items_[idx(count_ - k)];
        v[n++] = it.ibi;
        if (!it.follows) break;
      }
    }
    for (uint8_t k = 1; k < pendingN_ && k <= 2; k++) v[n++] = pending_[k].ibi;
    bool normal = true;
    if (n >= 3) {
      float med = median(v, n);
      normal = fabsf((float)p.ibi - med) <= HRV_ARTIFACT_JUMP * med;
    }

    items_[head_] = {p.ibi, p.endSample, normal, p.follows};
    head_ = (head_ + 1) % HRV_MAX_ITEMS;
    if (count_ < HRV_MAX_ITEMS) count_++;

    // Forget anything older than the window.
    while (count_ > 0 && p.endSample - items_[idx(0)].endSample > HRV_WINDOW_SAMPLES) count_--;

    calculate();
    last_.updated = true;
  }

  static float median(uint16_t* v, uint8_t n) {
    for (uint8_t i = 1; i < n; i++) {
      uint16_t x = v[i]; int j = i - 1;
      while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
      v[j + 1] = x;
    }
    return (n % 2) ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
  }

  void calculate() {
    double sum = 0, sumSq = 0, diffSq = 0;
    uint16_t n = 0, pairs = 0, over50 = 0, artifacts = 0;
    for (uint8_t i = 0; i < count_; i++) {
      const Item& it = items_[idx(i)];
      if (!it.normal) { artifacts++; continue; }
      sum += it.ibi; sumSq += (double)it.ibi * it.ibi; n++;
      if (i > 0 && it.follows) {
        const Item& prev = items_[idx(i - 1)];
        if (prev.normal) {
          double d = (double)it.ibi - prev.ibi;
          diffSq += d * d; pairs++;
          if (fabs(d) > 50.0) over50++;
        }
      }
    }

    HrvResult& r = last_;
    r.intervals = n;
    r.pairs = pairs;
    r.artifactPct = count_ ? (uint8_t)(100.0f * artifacts / count_ + 0.5f) : 0;
    r.rmssd = pairs ? (float)sqrt(diffSq / pairs) : 0.0f;
    double mean = n ? sum / n : 0;
    r.sdnn = n > 1 ? (float)sqrt(fmax(sumSq / n - mean * mean, 0.0)) : 0.0f;
    r.pnn50 = pairs ? 100.0f * over50 / pairs : 0.0f;
    r.meanHr = mean > 0 ? (float)(60000.0 / mean) : 0.0f;

    float seconds = (float)(sum / 1000.0);
    float p1 = seconds / HRV_MIN_SECONDS, p2 = (float)pairs / HRV_MIN_PAIRS;
    r.progress = fminf(fminf(p1, p2), 1.0f);
    r.ready = (seconds >= HRV_MIN_SECONDS && pairs >= HRV_MIN_PAIRS);

    // Steady heart rate? Compare the average interval in the first and the
    // last 20 s of what's in the window.
    r.steady = true;
    if (r.ready && count_ > 0) {
      uint32_t t0 = items_[idx(0)].endSample, t1 = items_[idx(count_ - 1)].endSample;
      double a = 0, b = 0; int na = 0, nb = 0;
      for (uint8_t i = 0; i < count_; i++) {
        const Item& it = items_[idx(i)];
        if (!it.normal) continue;
        if (it.endSample - t0 <= HRV_STEADY_PART) { a += it.ibi; na++; }
        if (t1 - it.endSample <= HRV_STEADY_PART) { b += it.ibi; nb++; }
      }
      if (na && nb && t1 - t0 > 2 * HRV_STEADY_PART) {
        a /= na; b /= nb;
        if (fabs(a - b) > HRV_STEADY_CHANGE * 0.5 * (a + b)) r.steady = false;
      }
    }
    r.level = (r.ready && r.steady) ? stressFromRmssd(r.rmssd) : StressLevel::Unknown;
  }

  Item     items_[HRV_MAX_ITEMS];
  uint8_t  count_ = 0, head_ = 0;
  Pending  pending_[HRV_PENDING + 1];
  uint8_t  pendingN_ = 0;
  bool     gap_ = true;
  uint32_t sampleIdx_ = 0;
  HrvResult last_{};
};

}  // namespace pulselab
