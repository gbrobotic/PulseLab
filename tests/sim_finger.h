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
  float bumpAt    = -1;     // short finger-pressure bump (10%, 150 ms)
  float fingerOffFrom = -1, fingerOffTo = -1;
};

class SimFinger {
public:
  SimFinger(const Sim& s, unsigned seed)
    : s_(s), rng_(seed), nIr_(0, s.noiseIr), nRed_(0, s.noiseRed), jitter_(0, 0.03f), amp_(0, 1) {}

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
      heights_.push_back(a < 0.2f ? 0.2f : a);
      nextBeat_ += 60.0f / s_.bpm * (1 + jitter_(rng_));
    }
    float v = 0;
    for (size_t k = beats_.size() >= 3 ? beats_.size() - 3 : 0; k < beats_.size(); k++) v += heights_[k] * pulse(t - beats_[k]);

    float breath = 1 + s_.breathing * sinf(2 * 3.14159f * 0.25f * t);
    float on = t - onSince_;
    float ramp = on < 0.3f ? on / 0.3f : 1.0f;  // finger being placed
    float bump = (s_.bumpAt >= 0 && t > s_.bumpAt && t < s_.bumpAt + 0.15f)
                   ? 1 + 0.1f * sinf(3.14159f * (t - s_.bumpAt) / 0.15f) : 1;
    rawIr  = 150 + ramp * (s_.dcIr  * breath * bump * (1 - s_.piIr * v) - 150) + nIr_(rng_);
    rawRed = 120 + ramp * (s_.dcRed * breath * bump * (1 - s_.piIr * rTrue_ * v) - 120) + nRed_(rng_);
    if (rawRed > 65535) rawRed = 65535;  // 16-bit ADC limit
    if (rawIr > 65535) rawIr = 65535;
    return true;
  }

  float t() const { return t_; }
  float trueR() const { return rTrue_; }
  bool fingerOff() const { return fingerOff_; }

private:
  static float pulse(float t) {
    auto g = [](float x, float mu, float s) { return expf(-0.5f * (x - mu) * (x - mu) / (s * s)); };
    return g(t, 0.12f, 0.05f) + 0.35f * g(t, 0.38f, 0.06f);
  }

  Sim s_;
  std::mt19937 rng_;
  std::normal_distribution<float> nIr_, nRed_, jitter_, amp_;
  std::vector<float> beats_, heights_;
  float nextBeat_ = 0, onSince_ = 0, t_ = 0, rTrue_ = 0;
  bool wasOff_ = false, fingerOff_ = false;
  int i_ = 0;
};
