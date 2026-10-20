// guided.h
// Guided breathing session: before / slow breathing / after.
//
//   1. Before   (1 min)  breathe normally, measure heart rate and HRV
//   2. Breathe  (3 min)  breathe with the pacer at 6 per minute
//   3. After    (1 min)  breathe normally again
//   4. Done              before / during / after on the screen
//
// Why 6 per minute: at about 6 breaths a minute the breathing and the body's
// own blood pressure rhythm (~0.1 Hz) line up, and the heart rate swings much
// more with each breath (resonance breathing, Lehrer & Gevirtz 2014,
// docs/REFERENCES.md ref 8). In Phase 5 my HRV went from ~36 to ~60 ms with it.
//
// "Swing": for each breath during the slow part, the highest minus the lowest
// heart rate (from the beat intervals) within that breath. That's the usual
// way HRV breathing training shows how well it's working. Bigger = the heart
// is following the breathing more.
//
// If the finger is lifted, the session pauses (time stops) until it's back.

#pragma once
#include <stdint.h>
#include <math.h>
#include "ppg_dsp.h"
#include "quality.h"
#include "hrv.h"
#include "pacer.h"

namespace pulselab {

constexpr uint32_t GUIDED_BEFORE_SAMPLES = 6000;   // 60 s
constexpr uint32_t GUIDED_AFTER_SAMPLES  = 6000;   // 60 s
constexpr uint8_t  GUIDED_PACE           = 6;      // breaths per minute
constexpr uint8_t  GUIDED_SKIP_BREATHS   = 2;      // the first 2 breaths aren't counted for the swing (getting into the rhythm)
constexpr uint8_t  GUIDED_MIN_BEATS      = 3;      // a breath needs at least 3 good beats to measure its swing

enum class GuidedPhase : uint8_t { Off, Before, Breathe, After, Done };

inline const char* guidedPhaseText(GuidedPhase p) {
  switch (p) {
    case GuidedPhase::Before:  return "before";
    case GuidedPhase::Breathe: return "slow breathing";
    case GuidedPhase::After:   return "after";
    case GuidedPhase::Done:    return "done";
    default:                   return "off";
  }
}

struct PhaseStats {
  float    bpm;      // average heart rate from good intervals (0 = none)
  float    rmssd;    // HRV over the phase (0 = not enough clean data)
  uint16_t beats;    // good intervals used
  float    sdnn;     // SDNN over the phase (0 = not enough clean data)
  bool     steady;   // heart rate steady in this part (no rise or fall of more than 12%, see hrv.h)
};
// Why both RMSSD and SDNN: RMSSD looks at the difference between neighbouring
// beats (fast changes). At 6 breaths a minute one breath is ~10 s, ~13 beats,
// so the heart rate goes up and down slowly: a big swing, but small steps from
// beat to beat. SDNN (spread of all intervals) and the swing show that slow
// wave; RMSSD may not change much. Shaffer & Ginsberg 2017 (ref 6): "The RMSSD
// is less affected by respiration than is RSA". My second session showed it:
// swing 20 BPM per breath, but RMSSD 36 -> 30 ms.

struct GuidedResult {
  GuidedPhase phase;
  bool        paused;        // no finger: time is stopped
  bool        changed;       // the phase changed on this sample
  uint32_t    phaseSamples;  // time in this phase so far
  uint32_t    phaseLength;   // length of this phase
  PhaseStats  before, during, after;
  float       swing;         // average heart rate swing per breath in the slow part (BPM)
  float       lastSwing;     // swing of the last full breath (BPM, 0 if not measured)
  uint16_t    swingBreaths;  // breaths that went into the average
};

class GuidedSession {
public:
  // Start a session: seconds of slow breathing (and of before / after).
  void start(uint16_t breatheSeconds = 180, uint16_t beforeSeconds = 60, uint16_t afterSeconds = 60) {
    breatheSamples_ = (uint32_t)breatheSeconds * 100;
    beforeSamples_ = (uint32_t)beforeSeconds * 100;
    afterSamples_ = (uint32_t)afterSeconds * 100;
    r_ = GuidedResult{};
    enter(GuidedPhase::Before);
  }

  void stop() { r_ = GuidedResult{}; pacer_.set(0); }

  bool active() const { return r_.phase == GuidedPhase::Before || r_.phase == GuidedPhase::Breathe || r_.phase == GuidedPhase::After; }

  // Call once per sensor sample, after heart rate and quality.
  void process(const HrResult& h, const QualityResult& q) {
    r_.changed = false;
    if (!active()) return;

    r_.paused = !h.finger;
    if (r_.paused) {
      hrv_.markGap();          // don't feed it: it would reset itself without a finger
      breathOk_ = false;       // this breath is spoiled
      return;
    }

    bool good = (q.state == SignalState::Good || q.state == SignalState::WeakPulse) && !h.settling;
    hrv_.process(h, q);
    if (h.beat && h.ibiMs > 0 && h.ibiValid && good) {
      sumIbi_ += h.ibiMs; nIbi_++;
      float bpm = 60000.0f / h.ibiMs;
      if (r_.phase == GuidedPhase::Breathe) {
        if (bpm > hi_) hi_ = bpm;
        if (bpm < lo_) lo_ = bpm;
        beatsInBreath_++;
      }
    } else if (h.beat && !(h.ibiValid && good)) {
      breathOk_ = false;       // a bad beat in this breath: its swing isn't trusted
    }

    if (r_.phase == GuidedPhase::Breathe) {
      pacer_.tick();
      uint32_t b = pacer_.state().breaths;
      if (b != breathIndex_) {   // a breath just ended
        if (breathIndex_ >= GUIDED_SKIP_BREATHS && breathOk_ && beatsInBreath_ >= GUIDED_MIN_BEATS) {
          float sw = hi_ - lo_;
          r_.lastSwing = sw;
          swingSum_ += sw;
          r_.swingBreaths++;
          r_.swing = swingSum_ / r_.swingBreaths;
        }
        breathIndex_ = b;
        newBreath();
      }
    }

    r_.phaseSamples++;
    if (r_.phaseSamples >= r_.phaseLength) finishPhase();
  }

  const GuidedResult& result() const { return r_; }
  PacerState pacerState() const { return pacer_.state(); }

private:
  void enter(GuidedPhase p) {
    r_.phase = p;
    r_.phaseSamples = 0;
    r_.changed = true;
    r_.phaseLength = p == GuidedPhase::Before ? beforeSamples_
                   : p == GuidedPhase::Breathe ? breatheSamples_
                   : p == GuidedPhase::After ? afterSamples_ : 0;
    hrv_.reset();
    sumIbi_ = 0; nIbi_ = 0;
    pacer_.set(p == GuidedPhase::Breathe ? GUIDED_PACE : 0);
    breathIndex_ = 0;
    swingSum_ = 0;
    newBreath();
  }

  void newBreath() { hi_ = 0; lo_ = 1000; beatsInBreath_ = 0; breathOk_ = true; }

  void finishPhase() {
    PhaseStats s{};
    s.bpm = nIbi_ ? 60000.0f * nIbi_ / (float)sumIbi_ : 0.0f;
    s.beats = nIbi_;
    const HrvResult& v = hrv_.result();
    s.rmssd = v.ready ? v.rmssd : 0.0f;
    s.sdnn = v.ready ? v.sdnn : 0.0f;
    s.steady = v.ready && v.steady;
    GuidedPhase p = r_.phase;
    if (p == GuidedPhase::Before) { r_.before = s; enter(GuidedPhase::Breathe); }
    else if (p == GuidedPhase::Breathe) {
      r_.during = s;
      float swing = r_.swing; uint16_t n = r_.swingBreaths;  // keep them, enter() doesn't touch r_ results
      enter(GuidedPhase::After);
      r_.swing = swing; r_.swingBreaths = n;
    }
    else if (p == GuidedPhase::After) { r_.after = s; enter(GuidedPhase::Done); r_.phaseLength = 0; }
  }

  GuidedResult r_{};
  BreathPacer  pacer_;
  HrvAnalyzer  hrv_;          // reset at every phase, so each phase gets its own HRV
  uint32_t     breatheSamples_ = 18000, beforeSamples_ = GUIDED_BEFORE_SAMPLES, afterSamples_ = GUIDED_AFTER_SAMPLES;
  uint32_t     sumIbi_ = 0;
  uint16_t     nIbi_ = 0;
  uint32_t     breathIndex_ = 0;
  float        hi_ = 0, lo_ = 1000, swingSum_ = 0;
  uint8_t      beatsInBreath_ = 0;
  bool         breathOk_ = true;
};

}  // namespace pulselab
