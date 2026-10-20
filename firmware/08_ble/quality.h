// quality.h
// Signal quality for PulseLab.
//
// Decides whether the numbers on screen can be trusted, and if not,
// what the user should do about it ("Place finger", "Hold still", ...).
//
// It looks at:
//   - finger on / filter settling           (from the heart rate code)
//   - ADC clipping                          (raw value near 65535 = LED too bright)
//   - movement                              (signal suddenly much bigger than a normal pulse,
//                                            or about 2 beats in a row not found)
//   - how many recent beats were accepted   (rejected intervals = unreliable)
//   - perfusion index                       (weak pulse = SpO2 unreliable)
//
// and turns that into a state + a 0-100 score.

#pragma once
#include <stdint.h>
#include <math.h>
#include "ppg_dsp.h"
#include "spo2.h"

namespace pulselab {

enum class SignalState : uint8_t {
  NoFinger,    // "Place finger"
  Settling,    // "Measuring..." (first 2 s)
  Searching,   // "Measuring..." (waiting for enough beats)
  Moving,      // "Hold still"
  Clipping,    // "Too bright"  (LED saturates the sensor)
  WeakPulse,   // values shown, but SpO2 hidden ("Weak pulse")
  Good
};

struct QualityResult {
  SignalState state;
  uint8_t     score;      // 0-100
  bool        showBpm;    // BPM is trustworthy right now
  bool        showSpo2;   // SpO2 is trustworthy right now
};

// ---------------- Settings ----------------

constexpr float    MOTION_RATIO       = 1.5f;  // |signal| > 1.5x a normal pulse height = movement
constexpr uint32_t MOTION_HOLD        = 150;   // stay in "Moving" for 1.5 s after the last sign of it
constexpr uint32_t CLIP_HOLD          = 100;   // stay in "Clipping" for 1 s
constexpr float    LOST_BEATS_RATIO   = 2.5f;  // no beat for 2.5x the normal interval = signal lost
constexpr uint32_t LOST_BEATS_DEFAULT = 300;   //  (3 s if the heart rate isn't known yet)
constexpr float    PI_FULL_SCORE      = 1.0f;  // PI of 1% or more counts as a strong pulse
constexpr uint8_t  SCORE_SHOW_BPM     = 50;    // minimum score to show BPM
constexpr uint8_t  SCORE_SHOW_SPO2    = 60;    // minimum score to show SpO2
constexpr uint8_t  BEAT_HISTORY       = 8;     // how many recent beats count towards the score

class QualityMonitor {
public:
  void reset() {
    sampleIdx_ = 0;
    movingUntil_ = clipUntil_ = 0;
    lastBeat_ = 0;
    winMax_ = -1e9f; winMin_ = 1e9f;
    typAmp_ = 0.0f;
    beatBits_ = 0; beatCount_ = 0;
    wasFinger_ = false;
  }

  QualityResult process(const HrResult& h, const Spo2Result& s, float rawIr, float rawRed) {
    QualityResult q{};
    sampleIdx_++;

    if (!h.finger) {
      if (wasFinger_) reset();
      q.state = SignalState::NoFinger;
      return q;
    }
    wasFinger_ = true;

    if (rawIr >= ADC_CLIP || rawRed >= ADC_CLIP) clipUntil_ = sampleIdx_ + CLIP_HOLD;

    if (h.settling) {
      q.state = (sampleIdx_ < clipUntil_) ? SignalState::Clipping : SignalState::Settling;
      return q;
    }

    // Track the normal pulse height: peak-to-trough between accepted beats.
    if (h.signal > winMax_) winMax_ = h.signal;
    if (h.signal < winMin_) winMin_ = h.signal;
    if (h.beat) {
      lastBeat_ = sampleIdx_;
      if (h.ibiMs > 0) {
        if (h.ibiValid) {
          float amp = winMax_ - winMin_;
          typAmp_ = (typAmp_ == 0.0f) ? amp : 0.8f * typAmp_ + 0.2f * amp;
        }
        beatBits_ = (uint8_t)((beatBits_ << 1) | (h.ibiValid ? 1 : 0));
        if (beatCount_ < BEAT_HISTORY) beatCount_++;
      }
      winMax_ = -1e9f; winMin_ = 1e9f;
    }

    // Movement: a swing far bigger than a normal pulse, or about 2 beats in a
    // row not found. A single missed beat is normal (one smaller pulse can fall
    // under the threshold) and only lowers the score a little, so it doesn't
    // count as movement. In testing that rule alone gave 7% false "Hold still".
    uint32_t lostAfter = LOST_BEATS_DEFAULT;
    if (h.bpm > 0.0f) lostAfter = (uint32_t)(LOST_BEATS_RATIO * 6000.0f / h.bpm);
    if (lastBeat_ == 0) lastBeat_ = sampleIdx_;  // count from the end of settling
    bool beatsLost = (sampleIdx_ - lastBeat_) > lostAfter;
    if ((typAmp_ > 0.0f && fabsf(h.signal) > MOTION_RATIO * typAmp_) || beatsLost) {
      movingUntil_ = sampleIdx_ + MOTION_HOLD;
    }

    // Score: share of recent beats that were accepted, weighted by pulse strength.
    float acc = 1.0f;
    if (beatCount_ > 0) acc = (float)popcount(beatBits_, beatCount_) / beatCount_;
    float piFactor = 0.7f;  // until PI is known
    if (s.pi > 0.0f) piFactor = fminf(s.pi / PI_FULL_SCORE, 1.0f);
    float score = 100.0f * acc * (0.5f + 0.5f * piFactor);

    if (sampleIdx_ < clipUntil_) {
      q.state = SignalState::Clipping;
      score = fminf(score, 20.0f);
    } else if (sampleIdx_ < movingUntil_) {
      q.state = SignalState::Moving;
      score = fminf(score, 30.0f);
    } else if (h.bpm <= 0.0f) {
      q.state = SignalState::Searching;
    } else if (s.lowPerfusion) {
      q.state = SignalState::WeakPulse;
    } else {
      q.state = SignalState::Good;
    }

    q.score = (uint8_t)(score + 0.5f);
    bool okState = (q.state == SignalState::Good || q.state == SignalState::WeakPulse);
    q.showBpm  = okState && h.bpm > 0.0f && q.score >= SCORE_SHOW_BPM;
    q.showSpo2 = (q.state == SignalState::Good) && s.spo2 > 0.0f && q.score >= SCORE_SHOW_SPO2;
    return q;
  }

private:
  static uint8_t popcount(uint8_t bits, uint8_t n) {
    uint8_t c = 0;
    for (uint8_t i = 0; i < n; i++) c += (bits >> i) & 1;
    return c;
  }

  uint32_t sampleIdx_ = 0;
  uint32_t movingUntil_ = 0, clipUntil_ = 0;
  uint32_t lastBeat_ = 0;
  float    winMax_ = -1e9f, winMin_ = 1e9f;
  float    typAmp_ = 0.0f;
  uint8_t  beatBits_ = 0, beatCount_ = 0;
  bool     wasFinger_ = false;
};

// Short text for each state, for the screen and Serial.
inline const char* stateText(SignalState s) {
  switch (s) {
    case SignalState::NoFinger:  return "Place finger";
    case SignalState::Settling:  return "Measuring...";
    case SignalState::Searching: return "Measuring...";
    case SignalState::Moving:    return "Hold still";
    case SignalState::Clipping:  return "Too bright";
    case SignalState::WeakPulse: return "Weak pulse";
    case SignalState::Good:      return "Good signal";
  }
  return "";
}

}  // namespace pulselab
