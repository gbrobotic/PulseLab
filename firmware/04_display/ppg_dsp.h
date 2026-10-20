// ppg_dsp.h
// Signal processing for PulseLab: filtering + beat detection + heart rate.
//
// Plain C++ with no Arduino code in it, so the same file runs on the ESP32
// and can also be tested on a PC (see tests/test_heart_rate.cpp).
//
// Signal chain (100 samples per second):
//
//   raw IR -> remove DC -> band-pass 0.5-4 Hz -> invert -> slope -> peak detection -> IBI -> BPM
//
// Beats are detected on the slope (how fast the signal rises), not on the
// signal itself. The rise at the start of each pulse is sharp, while breathing
// drift is slow, so the slope ignores breathing much better. The point of
// fastest rise is also a more stable timing mark than the top of the peak.
//

#pragma once
#include <stdint.h>
#include <math.h>

namespace pulselab {

// ---------------- Settings ----------------

constexpr float    FS_HZ            = 100.0f;  // sensor sample rate
constexpr uint32_t SAMPLE_MS        = 10;      // 1000 / FS_HZ

constexpr float    FINGER_IR_MIN    = 10000.0f; // raw IR below this = no finger
constexpr uint32_t SETTLE_SAMPLES   = 200;      // ignore first 2 s after finger is placed (filters settling)

constexpr float    DC_ALPHA         = 0.01f;    // DC tracker speed (about 1 s time constant)
constexpr float    DC_ALPHA_FAST    = 0.1f;     // faster while settling (~0.1 s), so the jump from placing a finger dies out quickly
constexpr float    ENV_DECAY        = 0.998f;   // envelope decay per sample (half-life ~3.5 s)
constexpr float    ENV_DECAY_FAST   = 0.98f;    // decay when beats stop coming (half-life ~0.35 s)
constexpr float    MISSING_BEAT_RATIO = 1.5f;   // "beats stopped" = no beat for 1.5x the normal interval
constexpr uint32_t MISSING_BEAT_SAMPLES = 150;  // ...or 1.5 s if the normal interval isn't known yet
constexpr float    THRESH_RATIO     = 0.5f;     // beat threshold = 50% of recent max slope
constexpr float    MIN_THRESHOLD    = 3.0f;     // never go below this (stops noise counting as beats)
constexpr uint8_t  SLOPE_SAMPLES    = 4;        // slope = rise over the last 4 samples (40 ms)
constexpr uint32_t REFRACTORY_SAMPLES = 30;     // 300 ms: no second beat within this (max 200 BPM)
constexpr float    REFRACTORY_RATIO = 0.6f;     // once HR is known, also block 60% of the normal beat interval

constexpr uint16_t IBI_MIN_MS       = 300;      // 200 BPM
constexpr uint16_t IBI_MAX_MS       = 2000;     // 30 BPM
constexpr float    IBI_MAX_JUMP     = 0.30f;    // reject an interval >30% away from the median
constexpr float    IBI_START_JUMP   = 0.15f;    // stricter for the first BPM: first intervals must agree within 15%
constexpr uint8_t  IBI_BUFFER_SIZE  = 8;        // average BPM over last 8 beats
constexpr uint8_t  IBI_MIN_FOR_BPM  = 3;        // need 3 good intervals before showing BPM
constexpr uint8_t  MAX_REJECTS      = 3;        // after 3 rejects in a row, assume HR really changed

// ---------------- Biquad filter ----------------
// One 2nd-order IIR section, transposed direct form II.
// Coefficients come from filter_design.py (scipy butter, output='sos').

class Biquad {
public:
  Biquad(float b0, float b1, float b2, float a1, float a2)
    : b0_(b0), b1_(b1), b2_(b2), a1_(a1), a2_(a2) {}

  float process(float x) {
    float y = b0_ * x + z1_;
    z1_ = b1_ * x - a1_ * y + z2_;
    z2_ = b2_ * x - a2_ * y;
    return y;
  }

  void reset() { z1_ = z2_ = 0.0f; }

private:
  float b0_, b1_, b2_, a1_, a2_;
  float z1_ = 0.0f, z2_ = 0.0f;
};

// 4th-order Butterworth band-pass, 0.5-4 Hz at 100 Hz (two biquads).
// 0.5 Hz = 30 BPM, 4 Hz = 240 BPM. Removes baseline drift and high-frequency noise.
class BandPass {
public:
  float process(float x) { return s2_.process(s1_.process(x)); }
  void reset() { s1_.reset(); s2_.reset(); }

private:
  Biquad s1_{0.0104324134f,  0.0208648267f, 0.0104324134f, -1.7178445866f, 0.7634885626f};
  Biquad s2_{1.0000000000f, -2.0000000000f, 1.0000000000f, -1.9585309427f, 0.9597079331f};
};

// ---------------- Heart rate detector ----------------

struct HrResult {
  bool     finger;     // finger on the sensor
  bool     settling;   // filters still settling, ignore output
  float    signal;     // filtered PPG (positive peak = heartbeat)
  float    slope;      // rise of the signal over the last 40 ms
  float    threshold;  // current beat threshold (in slope units)
  bool     beat;       // a beat was detected on this sample
  uint16_t ibiMs;      // interval since previous beat (0 if none / rejected)
  bool     ibiValid;   // this interval passed the checks
  float    bpm;        // averaged heart rate (0 until enough beats)
  bool     beatsMissing; // no beat for longer than expected (finger moved, or signal lost)
};

class HeartRateDetector {
public:
  void reset() {
    filter_.reset();
    sampleIdx_ = 0;
    fingerStart_ = 0;
    finger_ = false;
    dc_ = 0.0f;
    for (uint8_t i = 0; i < SLOPE_SAMPLES; i++) yHist_[i] = 0.0f;
    yHead_ = 0;
    s1_ = s2_ = 0.0f;
    envelope_ = 0.0f;
    armed_ = false;
    lastBeat_ = 0;
    haveLastBeat_ = false;
    ibiCount_ = 0;
    ibiHead_ = 0;
    rejects_ = 0;
    bpm_ = 0.0f;
  }

  // Call once per sample, at exactly FS_HZ.
  // Time is counted in samples, not millis(), because the sensor FIFO
  // gives us samples in bursts. Sample count is the real time base.
  HrResult process(float rawIr) {
    HrResult r{};
    sampleIdx_++;

    // --- finger detection ---
    if (rawIr < FINGER_IR_MIN) {
      if (finger_) reset();
      return r;  // all zero, finger = false
    }
    if (!finger_) {
      finger_ = true;
      fingerStart_ = sampleIdx_;
      dc_ = rawIr;  // start DC at the current level so there is no big jump
    }
    r.finger = true;

    // --- DC removal + band-pass ---
    bool settling = (sampleIdx_ - fingerStart_ < SETTLE_SAMPLES);
    dc_ += (settling ? DC_ALPHA_FAST : DC_ALPHA) * (rawIr - dc_);
    // More blood = more light absorbed = lower IR reading, so the heartbeat
    // shows up as a dip. Invert it so each beat is a positive peak.
    float y0 = -filter_.process(rawIr - dc_);
    float s0 = y0 - yHist_[yHead_];  // rise over the last SLOPE_SAMPLES samples
    r.signal = y0;
    r.slope = s0;

    if (settling) {
      r.settling = true;
      // Measure the pulse size in the second half of settling, to start the
      // envelope at the right level instead of at zero.
      if (sampleIdx_ - fingerStart_ >= SETTLE_SAMPLES / 2 && s0 > envelope_) envelope_ = s0;
      shift(y0, s0);
      return r;
    }

    // --- adaptive threshold ---
    // The envelope follows the size of recent beats (their max slope).
    // It decays slowly, but much faster if beats stop being found, e.g. after
    // the finger moved and a big bump pushed the envelope up.
    uint32_t sinceBeat = haveLastBeat_ ? sampleIdx_ - lastBeat_ : sampleIdx_ - fingerStart_ - SETTLE_SAMPLES;
    uint32_t missingAfter = MISSING_BEAT_SAMPLES;
    if (ibiCount_ >= IBI_MIN_FOR_BPM) missingAfter = (uint32_t)(MISSING_BEAT_RATIO * medianIbi() / SAMPLE_MS);
    r.beatsMissing = (sinceBeat > missingAfter);
    envelope_ *= r.beatsMissing ? ENV_DECAY_FAST : ENV_DECAY;
    if (s0 > envelope_) envelope_ = s0;
    float threshold = THRESH_RATIO * envelope_;
    if (threshold < MIN_THRESHOLD) threshold = MIN_THRESHOLD;
    r.threshold = threshold;

    // Re-arm only after the signal starts falling (slope below zero).
    // Together with the refractory time, this stops the small second bump
    // after each beat (the dicrotic wave) counting as another beat.
    if (!armed_ && s0 < 0.0f) armed_ = true;

    // --- peak detection on the previous slope sample (s1 is a local max) ---
    uint32_t candidate = sampleIdx_ - 1;
    bool isPeak = (s1_ > s2_) && (s1_ >= s0) && (s1_ > threshold);
    uint32_t refractory = REFRACTORY_SAMPLES;
    if (ibiCount_ >= IBI_MIN_FOR_BPM) {
      // The second bump of each pulse comes ~250-350 ms after the beat.
      // Once we know the normal interval, block most of it.
      uint32_t adaptive = (uint32_t)(REFRACTORY_RATIO * medianIbi() / SAMPLE_MS);
      if (adaptive > refractory) refractory = adaptive;
    }
    bool refractoryOk = !haveLastBeat_ || (candidate - lastBeat_ >= refractory);

    if (armed_ && isPeak && refractoryOk) {
      r.beat = true;
      armed_ = false;

      if (haveLastBeat_) {
        uint32_t ibi = (candidate - lastBeat_) * SAMPLE_MS;
        r.ibiMs = (uint16_t)(ibi > 65535 ? 65535 : ibi);
        r.ibiValid = acceptIbi(r.ibiMs);
      }
      lastBeat_ = candidate;
      haveLastBeat_ = true;
    }

    r.bpm = bpm_;
    shift(y0, s0);
    return r;
  }

  float bpm() const { return bpm_; }

private:
  void shift(float y0, float s0) {
    yHist_[yHead_] = y0;
    yHead_ = (yHead_ + 1) % SLOPE_SAMPLES;
    s2_ = s1_; s1_ = s0;
  }

  bool acceptIbi(uint16_t ibi) {
    if (ibi < IBI_MIN_MS || ibi > IBI_MAX_MS) return false;

    if (ibiCount_ >= IBI_MIN_FOR_BPM) {
      float med = medianIbi();
      if (fabsf((float)ibi - med) > IBI_MAX_JUMP * med) {
        // Probably a missed or extra beat. But if it keeps happening,
        // the heart rate really changed, so start fresh.
        if (++rejects_ >= MAX_REJECTS) {
          ibiCount_ = 0;
          ibiHead_ = 0;
          rejects_ = 0;
          bpm_ = 0.0f;
        }
        return false;
      }
    }
    rejects_ = 0;

    ibiBuf_[ibiHead_] = ibi;
    ibiHead_ = (ibiHead_ + 1) % IBI_BUFFER_SIZE;
    if (ibiCount_ < IBI_BUFFER_SIZE) ibiCount_++;

    // At the start there is no median to compare against yet. Before showing
    // the first BPM, make sure the first few intervals agree with each other.
    // If not, drop the oldest one and wait for another beat.
    // (While ibiCount_ <= IBI_MIN_FOR_BPM the buffer hasn't wrapped, so the
    // intervals are stored in order at index 0..ibiCount_-1.)
    if (ibiCount_ == IBI_MIN_FOR_BPM && bpm_ == 0.0f) {
      float med = medianIbi();
      bool consistent = true;
      for (uint8_t i = 0; i < ibiCount_; i++) {
        if (fabsf((float)ibiBuf_[i] - med) > IBI_START_JUMP * med) consistent = false;
      }
      if (!consistent) {
        for (uint8_t i = 1; i < ibiCount_; i++) ibiBuf_[i - 1] = ibiBuf_[i];
        ibiCount_--;
        ibiHead_--;
        return true;
      }
    }

    if (ibiCount_ >= IBI_MIN_FOR_BPM) {
      uint32_t sum = 0;
      for (uint8_t i = 0; i < ibiCount_; i++) sum += ibiBuf_[i];
      bpm_ = 60000.0f * ibiCount_ / (float)sum;
    }
    return true;
  }

  float medianIbi() const {
    uint16_t tmp[IBI_BUFFER_SIZE];
    for (uint8_t i = 0; i < ibiCount_; i++) tmp[i] = ibiBuf_[i];
    // insertion sort, max 8 values
    for (uint8_t i = 1; i < ibiCount_; i++) {
      uint16_t v = tmp[i];
      int8_t j = i - 1;
      while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; }
      tmp[j + 1] = v;
    }
    if (ibiCount_ % 2) return tmp[ibiCount_ / 2];
    return 0.5f * (tmp[ibiCount_ / 2 - 1] + tmp[ibiCount_ / 2]);
  }

  BandPass filter_;
  uint32_t sampleIdx_ = 0;
  uint32_t fingerStart_ = 0;
  bool     finger_ = false;
  float    dc_ = 0.0f;
  float    yHist_[SLOPE_SAMPLES] = {0};  // last few filtered samples, for the slope
  uint8_t  yHead_ = 0;
  float    s1_ = 0.0f, s2_ = 0.0f;
  float    envelope_ = 0.0f;
  bool     armed_ = false;
  uint32_t lastBeat_ = 0;
  bool     haveLastBeat_ = false;

  uint16_t ibiBuf_[IBI_BUFFER_SIZE] = {0};
  uint8_t  ibiCount_ = 0;
  uint8_t  ibiHead_ = 0;
  uint8_t  rejects_ = 0;
  float    bpm_ = 0.0f;
};

}  // namespace pulselab
