// sim_finger.h
// Simulated finger on a MAX30100, used by the PC tests.
//
//   raw = DC * (1 + breathing) * (1 - pulseSize * pulse(t)) + noise
//
// Red uses pulseSize * R, so the "true" R (and SpO2) is known.
// Not a perfect model of a real finger, but it has the parts that matter:
// DC level, pulse shape with a second bump, beat-to-beat variation,
// breathing, noise, finger placement, pressure bumps and ADC clipping.

#pragma once
#include <cmath>
#include <random>
#include <vector>

struct Sim {
  float seconds   = 40;
  float bpm       = 75;
  float rStart    = 0.5f;   // true R at the start
  float rEnd      = -1;     // if set, R changes linearly to this by the end
  float dcIr      = 46000;
  float dcRed     = 30000;
  float piIr      = 0.02f;  // IR pulse size as a fraction of DC (2%)
  float noiseIr   = 5;
  float noiseRed  = 8;
  float breathing = 0.004f; // breathing changes the DC by 0.4%
  float ampJitter = 0;      // beat-to-beat change in pulse height (0.2 = +-20%)
  float beatJitter = 0.03f; // random beat-to-beat change in interval (SD as a fraction, 0.03 = 3%)
  float rsa = 0;            // breathing makes the heart rate go up and down (0.05 = +-5%)
  float breathRate = 0.25f; // breaths per second (0.25 = 15/min normal, 0.1 = 6/min slow breathing)
  float bpmEnd = -1;        // if set, heart rate changes linearly to this by the end
  float breathAmp = 0;      // breathing changes each pulse's height (0.1 = +-10%)
  float bumpAt    = -1;     // short finger-pressure bump (10%, 150 ms)
  float fingerOffFrom = -1, fingerOffTo = -1;
  float dicrotic  = 0.35f;  // size of the second bump of each pulse (real fingers: anything from 0.1 to ~0.6)
  float dicroticAt = 0.38f; // when the second bump peaks (s after the pulse starts; the first peaks at 0.12)
  float shakeEvery = -1;    // if set, a random finger movement on average this often (s)
};

class SimFinger {
public:
  SimFinger(const Sim& s, unsigned seed)
    : s_(s), rng_(seed), nIr_(0, s.noiseIr), nRed_(0, s.noiseRed), jitter_(0, s.beatJitter), amp_(0, 1) {}

  // Next sample at 100 Hz. Returns false when the simulation is over.
  bool next(float& rawIr, float& rawRed) {
    float t = i_ / 100.0f;
    if (t >= s_.seconds) return false;
    i_++;
    t_ = t;

    fingerOff_ = (t >= s_.fingerOffFrom && t < s_.fingerOffTo);
    rTrue_ = (s_.rEnd < 0) ? s_.rStart : s_.rStart + (s_.rEnd - s_.rStart) * t / s_.seconds;

    if (fingerOff_) {
      rawIr = 150 + nIr_(rng_);
      rawRed = 120 + nRed_(rng_);
      wasOff_ = true;
      return true;
    }
    if (wasOff_) { onSince_ = t; wasOff_ = false; }

    while (nextBeat_ <= t) {
      beats_.push_back(nextBeat_);
      // Only draw a random height when asked for, so older tests keep the same random sequence.
      float a = (s_.ampJitter > 0) ? 1 + s_.ampJitter * amp_(rng_) : 1.0f;
      a *= 1 + s_.breathAmp * sinf(2 * 3.14159f * s_.breathRate * nextBeat_);
      heights_.push_back(a < 0.2f ? 0.2f : a);
      float breath = s_.rsa * sinf(2 * 3.14159f * s_.breathRate * nextBeat_);
      float bpmNow = (s_.bpmEnd < 0) ? s_.bpm : s_.bpm + (s_.bpmEnd - s_.bpm) * nextBeat_ / s_.seconds;
      nextBeat_ += 60.0f / bpmNow * (1 + jitter_(rng_) - breath);
    }
    float v = 0;
    for (size_t k = beats_.size() >= 3 ? beats_.size() - 3 : 0; k < beats_.size(); k++) v += heights_[k] * pulse(t - beats_[k]);

    float breath = 1 + s_.breathing * sinf(2 * 3.14159f * s_.breathRate * t);
    float on = t - onSince_;
    float ramp = on < 0.3f ? on / 0.3f : 1.0f;  // finger being placed
    float bump = (s_.bumpAt >= 0 && t > s_.bumpAt && t < s_.bumpAt + 0.15f)
                   ? 1 + 0.1f * sinf(3.14159f * (t - s_.bumpAt) / 0.15f) : 1;
    if (s_.shakeEvery > 0) {
      // random movements: 100-400 ms, 1-6% of DC, either direction
      if (t >= shakeStart_ + shakeLen_ && unif_(rng_) < 0.01f / s_.shakeEvery) {
        shakeStart_ = t; shakeLen_ = 0.1f + 0.3f * unif_(rng_); shakeSize_ = (unif_(rng_) < 0.5f ? -1 : 1) * (0.01f + 0.05f * unif_(rng_));
      }
      if (t >= shakeStart_ && t < shakeStart_ + shakeLen_) bump *= 1 + shakeSize_ * sinf(3.14159f * (t - shakeStart_) / shakeLen_);
    }
    rawIr  = 150 + ramp * (s_.dcIr  * breath * bump * (1 - s_.piIr * v) - 150) + nIr_(rng_);
    rawRed = 120 + ramp * (s_.dcRed * breath * bump * (1 - s_.piIr * rTrue_ * v) - 120) + nRed_(rng_);
    if (rawRed > 65535) rawRed = 65535;  // 16-bit ADC limit
    if (rawIr > 65535) rawIr = 65535;
    return true;
  }

  float t() const { return t_; }
  Sim& params() { return s_; }   // change settings while running (e.g. breathing slower from 60 s on)
  const std::vector<float>& beatTimes() const { return beats_; }  // true beat start times (s)
  float trueR() const { return rTrue_; }
  bool fingerOff() const { return fingerOff_; }

private:
  float pulse(float t) const {
    float d = s_.dicrotic;
    auto g = [](float x, float mu, float s) { return expf(-0.5f * (x - mu) * (x - mu) / (s * s)); };
    return g(t, 0.12f, 0.05f) + d * g(t, s_.dicroticAt, 0.06f);
  }

  Sim s_;
  std::mt19937 rng_;
  std::normal_distribution<float> nIr_, nRed_, jitter_, amp_;
  std::uniform_real_distribution<float> unif_{0.0f, 1.0f};
  float shakeStart_ = -10, shakeLen_ = 0, shakeSize_ = 0;
  std::vector<float> beats_, heights_;
  float nextBeat_ = 0, onSince_ = 0, t_ = 0, rTrue_ = 0;
  bool wasOff_ = false, fingerOff_ = false;
  int i_ = 0;
};
