// pacer.h
// Breathing pacer: tells you when to breathe in and out at a set rate.
//
// In Phase 6 it's a test tool. If I breathe along with it while recording,
// I know the true breathing rate, so the breathing rate code can be checked
// against it. Phase 7 (guided breathing) will build on it.
//
// Time is counted in sensor samples (100 per second), not millis(), so the
// pacer lines up exactly with the recorded data.

#pragma once
#include <stdint.h>
#include <math.h>

namespace pulselab {

constexpr uint8_t PACE_OPTIONS[]  = {0, 6, 10, 15, 20};  // breaths per minute, 0 = off
constexpr uint8_t PACE_COUNT      = sizeof(PACE_OPTIONS);
constexpr float   PACE_IN_PART    = 0.4f;   // breathing in takes 40% of each breath, out 60%
                                            // (a longer out-breath is the usual advice for slow breathing)

struct PacerState {
  uint8_t perMin;      // 0 = off
  bool    inhale;      // breathe in now
  float   level;       // 0 = empty lungs .. 1 = full, smooth (for the circle on the screen)
  float   secondsLeft; // until the switch between in and out
  uint32_t breaths;    // full breaths since the pace was set
};

class BreathPacer {
public:
  // Next rate in the list (off -> 6 -> 10 -> 15 -> 20 -> off). Starts a new breath.
  uint8_t next() {
    option_ = (option_ + 1) % PACE_COUNT;
    samples_ = 0;
    return PACE_OPTIONS[option_];
  }
  uint8_t perMin() const { return PACE_OPTIONS[option_]; }

  // Jump straight to a rate from the list (0 = off). Starts a new breath.
  void set(uint8_t perMin) {
    option_ = 0;
    for (uint8_t i = 0; i < PACE_COUNT; i++) if (PACE_OPTIONS[i] == perMin) option_ = i;
    samples_ = 0;
  }

  // Call once per sensor sample.
  void tick() { if (perMin()) samples_++; }

  PacerState state() const {
    PacerState s{};
    s.perMin = perMin();
    if (!s.perMin) return s;
    float period = 60.0f * 100.0f / s.perMin;        // samples per breath
    float inLen = PACE_IN_PART * period;
    s.breaths = (uint32_t)(samples_ / period);
    float t = fmodf((float)samples_, period);
    if (t < inLen) {
      s.inhale = true;
      s.level = 0.5f - 0.5f * cosf(3.14159265f * t / inLen);           // 0 -> 1
      s.secondsLeft = (inLen - t) / 100.0f;
    } else {
      s.inhale = false;
      s.level = 0.5f + 0.5f * cosf(3.14159265f * (t - inLen) / (period - inLen));  // 1 -> 0
      s.secondsLeft = (period - t) / 100.0f;
    }
    return s;
  }

private:
  uint8_t  option_ = 0;
  uint32_t samples_ = 0;
};

}  // namespace pulselab
