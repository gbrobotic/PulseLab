// breathing.h
// Breathing rate from the pulse signal, no extra sensor.
//
// Breathing changes the PPG in 3 ways (Karlen et al. 2013, docs/REFERENCES.md ref 9):
//   timing    beats come faster while breathing in, slower while breathing out
//   height    each pulse gets a bit bigger / smaller
//   baseline  the average IR level goes up and down
// Each beat gives one value of each. This file:
//   1. BeatFeatureTracker: measures the 3 values for every good beat
//   2. BreathSeries: keeps one of them for the last 64 s, evenly resampled at 4 Hz
//   3. BreathRateEstimator: a breathing rate from each series, and one final
//      rate only if they agree ("smart fusion")

#pragma once
#include <stdint.h>
#include <math.h>
#include "ppg_dsp.h"
#include "quality.h"

namespace pulselab {

// ---------------- 1. Per-beat values ----------------

struct BeatFeature {
  float t;      // time of the beat (s since the finger was placed, sub-sample)
  float ibi;    // interval since the previous beat (ms)          -> timing
  float amp;    // pulse height: max - min of the filtered signal -> height
  float base;   // average raw IR over the beat                   -> baseline
  bool  good;   // from a good signal and a valid interval
};

class BeatFeatureTracker {
public:
  void reset() {
    n_ = 0; sum_ = 0; hi_ = -1e9f; lo_ = 1e9f; samples_ = 0; have_ = false;
  }

  // Call once per sample. Returns true when a beat finished, with its values in `out`.
  bool process(float rawIr, const HrResult& h, const QualityResult& q, BeatFeature& out) {
    if (!h.finger) { reset(); return false; }
    samples_++;
    if (h.settling) return false;

    sum_ += rawIr; n_++;
    if (h.signal > hi_) hi_ = h.signal;
    if (h.signal < lo_) lo_ = h.signal;

    if (!h.beat) return false;
    bool ok = have_ && h.ibiMs > 0;
    if (ok) {
      out.t = samples_ / FS_HZ;
      out.ibi = (float)h.ibiMs;
      out.amp = hi_ - lo_;
      out.base = (float)(sum_ / n_);
      out.good = h.ibiValid && (q.state == SignalState::Good || q.state == SignalState::WeakPulse);
    }
    have_ = true;
    n_ = 0; sum_ = 0; hi_ = -1e9f; lo_ = 1e9f;
    return ok;
  }

private:
  double   sum_ = 0;
  uint32_t n_ = 0, samples_ = 0;
  float    hi_ = -1e9f, lo_ = 1e9f;
  bool     have_ = false;
};

// ---------------- 2 + 3. Breathing rate ----------------
//
// The 3 per-beat values come at irregular times (one per beat), so they are
// first turned into evenly spaced series at 4 Hz by drawing straight lines
// between good beats. Then, every 5 s, over the last 60 s:
//   - remove the straight-line trend, apply a Hann window
//   - power at 141 frequencies from 5 to 40 breaths/min (0.25 steps), with the
//     Goertzel algorithm (cheap: the ESP32-C3 has no floating point unit, so
//     a full FFT plus filtering would be slower)
//   - weight the power by a 0.08-0.8 Hz band-pass response and by frequency^1.5
//     (slow drift and the ~0.1 Hz blood pressure waves have most power at low
//     frequencies, and would otherwise win over real breathing)
//   - each series' spectrum is scaled to sum 1, the 3 are added, and the top
//     peak is the breathing rate. It's only shown if
//       * the timing series and at least one other have their own peak within
//         3 breaths/min of it, or all 3 do. (Height and baseline alone aren't
//         enough: a deeper pulse also lowers that beat's average level, so
//         those two partly measure the same thing and can agree on noise.)
//       * at least 30% of the combined power is within +-2 breaths/min of the
//         peak (one clear peak, not a flat noisy spectrum)
//     Otherwise "--" is shown. A missing number is better than a wrong one.
// The settings were chosen on my 5 recordings (docs/phase6_breathing.md).

constexpr float    RESP_FS          = 4.0f;    // series sample rate (Hz)
constexpr uint16_t RESP_WINDOW      = 240;     // 60 s at 4 Hz
constexpr uint16_t RESP_STEP        = 20;      // new estimate every 5 s
constexpr float    RESP_MIN         = 5.0f;    // breaths/min searched
constexpr float    RESP_MAX         = 40.0f;   // was 35; I measured myself at ~32 by counting
constexpr float    RESP_BIN         = 0.25f;
constexpr uint8_t  RESP_BINS        = 141;     // (40 - 5) / 0.25 + 1
constexpr float    RESP_LOW_HZ      = 0.08f;   // band-pass used as a weight on the spectrum
constexpr float    RESP_HIGH_HZ     = 0.8f;    // 48 /min, so 40 /min isn't turned down (was 0.6)
constexpr float    RESP_MAX_GAP_S   = 5.0f;    // two good beats further apart than this = gap
constexpr float    RESP_MIN_COVER   = 0.8f;    // a series needs 80% of the window without gaps
constexpr float    RESP_AGREE       = 3.0f;    // breaths/min
constexpr float    RESP_TILT        = 1.5f;    // weight power by (f / 10)^RESP_TILT (was 1.0)
constexpr float    RESP_MIN_SHARE   = 0.30f;   // power near the peak / all power
constexpr uint8_t  RESP_CHUNKS      = 3;       // each spectrum is done in 3 steps (one per loop)

enum RespSeries : uint8_t { RESP_TIMING = 0, RESP_HEIGHT = 1, RESP_BASELINE = 2, RESP_SERIES = 3 };

struct BreathResult {
  bool     valid;               // a rate the 3 series (at least 2) agree on
  float    rate;                // breaths per minute
  uint8_t  agree;               // how many series agree (2 or 3)
  float    peakShare;           // share of the combined power within +-2 breaths/min of the peak (0..1)
  bool     seriesOk[RESP_SERIES];   // enough data in this series
  float    seriesRate[RESP_SERIES]; // each series' own peak
  float    seriesSnr[RESP_SERIES];  // each series' peak / median power: how much the peak stands out
  float    progress;            // 0..1 until the first 60 s are collected
  bool     updated;             // a new estimate was made on this call
  uint32_t estimates;           // how many estimates so far
};

class BreathRateEstimator {
public:
  BreathRateEstimator() { buildTables(); reset(); }

  void reset() {
    filled_ = 0; head_ = 0; sinceEstimate_ = 0;
    haveGood_ = false; haveGrid_ = false;
    pending_ = 0;
    result_ = BreathResult{};
  }

  // Add one beat. Fills the 4 Hz series up to this beat's time.
  void addBeat(const BeatFeature& b) {
    if (!b.good) return;              // skipped; the line goes straight to the next good beat
    float v[RESP_SERIES] = {b.ibi, b.amp, b.base};
    if (!haveGrid_) {
      nextGrid_ = ceilf(b.t * RESP_FS) / RESP_FS;
      haveGrid_ = true;
    }
    bool join = haveGood_ && (b.t - lastT_) <= RESP_MAX_GAP_S;
    while (nextGrid_ <= b.t) {
      float x[RESP_SERIES];
      bool ok = join;
      for (uint8_t k = 0; k < RESP_SERIES; k++) {
        if (join) {
          float a = (nextGrid_ - lastT_) / (b.t - lastT_);
          x[k] = last_[k] + a * (v[k] - last_[k]);
        } else {
          x[k] = v[k];
        }
      }
      push(x, ok);
      nextGrid_ += 1.0f / RESP_FS;
    }
    lastT_ = b.t;
    for (uint8_t k = 0; k < RESP_SERIES; k++) last_[k] = v[k];
    haveGood_ = true;
  }

  // Call often (from loop() on the ESP32). When an estimate is due, the work
  // is split into small steps, one per call: 3 series x 3 parts of the
  // spectrum, then combining. Without an FPU the whole thing in one go could
  // take long enough to worry about the sensor FIFO (160 ms).
  // Returns true when a new estimate is ready.
  bool update() {
    result_.updated = false;
    result_.progress = fminf((float)filled_ / RESP_WINDOW, 1.0f);
    if (pending_ == 0) {
      if (filled_ < RESP_WINDOW || sinceEstimate_ < RESP_STEP) return false;
      sinceEstimate_ = 0;
      snapshot();
      pending_ = 1;
      return false;
    }
    uint8_t step = pending_ - 1;
    if (step < RESP_SERIES * RESP_CHUNKS) {
      spectrumPart(step / RESP_CHUNKS, step % RESP_CHUNKS);
      pending_++;
      return false;
    }
    combine();
    pending_ = 0;
    result_.updated = true;
    result_.estimates++;
    return true;
  }

  // For the PC tests: run the whole estimate now if one is due.
  bool updateNow() {
    for (int i = 0; i < RESP_SERIES * RESP_CHUNKS + 3; i++) if (update()) return true;
    return false;
  }

  const BreathResult& result() const { return result_; }

private:
  void push(const float* x, bool ok) {
    for (uint8_t k = 0; k < RESP_SERIES; k++) buf_[k][head_] = x[k];
    ok_[head_] = ok;
    head_ = (head_ + 1) % RESP_WINDOW;
    if (filled_ < RESP_WINDOW) filled_++;
    sinceEstimate_++;
  }

  // Copy the window out in time order, so new beats can keep coming in
  // while the estimate is spread over several loop() calls.
  void snapshot() {
    uint16_t good = 0;
    for (uint16_t i = 0; i < RESP_WINDOW; i++) {
      uint16_t j = (head_ + i) % RESP_WINDOW;
      for (uint8_t k = 0; k < RESP_SERIES; k++) win_[k][i] = buf_[k][j];
      if (ok_[j]) good++;
    }
    cover_ = (float)good / RESP_WINDOW;
  }

  void spectrumPart(uint8_t k, uint8_t part) {
    float* x = win_[k];
    if (part == 0) {
      // remove the best straight line (least squares), then the Hann window
      const float n = RESP_WINDOW, mt = (n - 1) / 2;
      float mx = 0;
      for (uint16_t i = 0; i < RESP_WINDOW; i++) mx += x[i];
      mx /= n;
      float sxy = 0, sxx = 0;
      for (uint16_t i = 0; i < RESP_WINDOW; i++) { float d = i - mt; sxy += d * (x[i] - mx); sxx += d * d; }
      float slope = sxy / sxx;
      for (uint16_t i = 0; i < RESP_WINDOW; i++) x[i] = (x[i] - mx - slope * (i - mt)) * hann_[i];
    }

    // Goertzel at this part's frequencies
    uint8_t from = part * RESP_BINS / RESP_CHUNKS, to = (part + 1) * RESP_BINS / RESP_CHUNKS;
    for (uint8_t b = from; b < to; b++) {
      float c = coeff_[b], s1 = 0, s2 = 0;
      for (uint16_t i = 0; i < RESP_WINDOW; i++) {
        float s0 = x[i] + c * s1 - s2;
        s2 = s1; s1 = s0;
      }
      spec_[k][b] = (s1 * s1 + s2 * s2 - c * s1 * s2) * weight_[b];
    }
    if (part != RESP_CHUNKS - 1) return;

    float total = 0;
    for (uint8_t b = 0; b < RESP_BINS; b++) total += spec_[k][b];
    bool ok = cover_ >= RESP_MIN_COVER && total > 0;
    result_.seriesOk[k] = ok;
    uint8_t best = 0;
    for (uint8_t b = 0; b < RESP_BINS; b++) {
      spec_[k][b] = ok ? spec_[k][b] / total : 0;
      if (spec_[k][b] > spec_[k][best]) best = b;
    }
    result_.seriesRate[k] = ok ? RESP_MIN + best * RESP_BIN : 0;
    // median power (copy + partial sort) to see how far the peak stands out
    float tmp[RESP_BINS];
    for (uint8_t b = 0; b < RESP_BINS; b++) tmp[b] = spec_[k][b];
    uint8_t mid = RESP_BINS / 2;
    for (uint8_t i = 0; i <= mid; i++) {          // selection sort up to the middle
      uint8_t m = i;
      for (uint8_t j = i + 1; j < RESP_BINS; j++) if (tmp[j] < tmp[m]) m = j;
      float t = tmp[i]; tmp[i] = tmp[m]; tmp[m] = t;
    }
    result_.seriesSnr[k] = (ok && tmp[mid] > 0) ? spec_[k][best] / tmp[mid] : 0;
  }

  void combine() {
    float sum[RESP_BINS];
    uint8_t used = 0;
    for (uint8_t k = 0; k < RESP_SERIES; k++) if (result_.seriesOk[k]) used++;
    result_.valid = false; result_.agree = 0;
    if (used < 2) return;
    uint8_t best = 0;
    for (uint8_t b = 0; b < RESP_BINS; b++) {
      sum[b] = spec_[0][b] + spec_[1][b] + spec_[2][b];
      if (sum[b] > sum[best]) best = b;
    }
    float rate = RESP_MIN + best * RESP_BIN;
    if (best > 0 && best < RESP_BINS - 1) {     // parabola through the top 3 bins
      float d = sum[best - 1] - 2 * sum[best] + sum[best + 1];
      if (d < 0) rate += RESP_BIN * 0.5f * (sum[best - 1] - sum[best + 1]) / d;
    }
    float near = 0, all = 0;
    for (uint8_t b = 0; b < RESP_BINS; b++) {
      all += sum[b];
      if (fabsf(RESP_MIN + b * RESP_BIN - rate) <= 2.0f) near += sum[b];
    }
    result_.peakShare = all > 0 ? near / all : 0;
    uint8_t agree = 0;
    for (uint8_t k = 0; k < RESP_SERIES; k++)
      if (result_.seriesOk[k] && fabsf(result_.seriesRate[k] - rate) <= RESP_AGREE) agree++;
    result_.agree = agree;
    bool timingAgrees = result_.seriesOk[RESP_TIMING] && fabsf(result_.seriesRate[RESP_TIMING] - rate) <= RESP_AGREE;
    bool agreeOk = agree == 3 || (agree == 2 && timingAgrees);
    if (agreeOk && result_.peakShare >= RESP_MIN_SHARE) { result_.valid = true; result_.rate = rate; }
  }

  void buildTables() {
    const float kPi = 3.14159265f;
    for (uint16_t i = 0; i < RESP_WINDOW; i++) hann_[i] = 0.5f - 0.5f * cosf(2 * kPi * i / (RESP_WINDOW - 1));
    // Band-pass weight: response of a Butterworth band-pass (order 2, like
    // scipy butter(2, [low, high], 'band')) at each frequency. Digital filter
    // frequencies map to analog ones through tan() (bilinear transform).
    const float wl = tanf(kPi * RESP_LOW_HZ / RESP_FS), wh = tanf(kPi * RESP_HIGH_HZ / RESP_FS);
    const float w0sq = wl * wh, bw = wh - wl;
    for (uint8_t b = 0; b < RESP_BINS; b++) {
      float f = (RESP_MIN + b * RESP_BIN) / 60.0f;          // Hz
      float w = 2 * kPi * f / RESP_FS;
      coeff_[b] = 2 * cosf(w);
      float wa = tanf(w / 2);                                 // analog frequency
      float x = (wa * wa - w0sq) / (wa * bw);
      float g = 1.0f / sqrtf(1.0f + x * x * x * x);           // |H|, order 2
      // g^4: the same as filtering forwards and backwards (zero phase), as power
      weight_[b] = g * g * g * g * powf(f * 60.0f / 10.0f, RESP_TILT);
    }
  }

  float    buf_[RESP_SERIES][RESP_WINDOW];
  bool     ok_[RESP_WINDOW];
  uint16_t filled_ = 0, head_ = 0, sinceEstimate_ = 0;
  float    win_[RESP_SERIES][RESP_WINDOW];
  float    cover_ = 0;
  float    spec_[RESP_SERIES][RESP_BINS];
  float    hann_[RESP_WINDOW], coeff_[RESP_BINS], weight_[RESP_BINS];
  uint8_t  pending_ = 0;
  bool     haveGood_ = false, haveGrid_ = false;
  float    nextGrid_ = 0, lastT_ = 0, last_[RESP_SERIES] = {0, 0, 0};
  BreathResult result_{};
};

}  // namespace pulselab
