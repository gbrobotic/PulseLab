// session.h
// A measurement session: started and stopped with a long button press.
// While it runs it collects the values that were actually shown on screen
// (only when the signal quality was good), and at the end gives a summary:
// duration, average / min / max heart rate, average / min SpO2 and how much
// of the time the signal was good.
//
// Also useful for Phase 11: one averaged session value is a much better
// number to compare with a reference oximeter than a single glance.

#pragma once
#include <stdint.h>
#include "ppg_dsp.h"
#include "spo2.h"
#include "quality.h"

namespace pulselab {

struct SessionSummary {
  bool     valid;        // a session has finished
  uint32_t seconds;
  uint16_t bpmCount;     // how many beats went into the averages
  float    bpmAvg, bpmMin, bpmMax;
  uint16_t spo2Count;
  float    spo2Avg, spo2Min;
  uint8_t  goodPct;      // % of the time values were shown
};

class Session {
public:
  void start() {
    active_ = true;
    samples_ = good_ = 0;
    bpmN_ = spo2N_ = 0;
    bpmSum_ = spo2Sum_ = 0.0f;
    bpmMin_ = 1e9f; bpmMax_ = 0.0f; spo2Min_ = 1e9f;
  }

  void stop() {
    if (!active_) return;
    active_ = false;
    SessionSummary& s = summary_;
    s.valid = true;
    s.seconds = samples_ / 100;
    s.bpmCount = bpmN_;
    s.bpmAvg = bpmN_ ? bpmSum_ / bpmN_ : 0.0f;
    s.bpmMin = bpmN_ ? bpmMin_ : 0.0f;
    s.bpmMax = bpmN_ ? bpmMax_ : 0.0f;
    s.spo2Count = spo2N_;
    s.spo2Avg = spo2N_ ? spo2Sum_ / spo2N_ : 0.0f;
    s.spo2Min = spo2N_ ? spo2Min_ : 0.0f;
    s.goodPct = samples_ ? (uint8_t)(100.0f * good_ / samples_ + 0.5f) : 0;
  }

  // Call once per sample.
  void process(const HrResult& h, const Spo2Result& s, const QualityResult& q) {
    if (!active_) return;
    samples_++;
    if (q.showBpm) good_++;
    // One value per beat, only when it was good enough to be shown.
    if (h.beat && h.ibiValid && q.showBpm) {
      bpmSum_ += h.bpm; bpmN_++;
      if (h.bpm < bpmMin_) bpmMin_ = h.bpm;
      if (h.bpm > bpmMax_) bpmMax_ = h.bpm;
    }
    if (s.beatValid && q.showSpo2) {
      spo2Sum_ += s.spo2; spo2N_++;
      if (s.spo2 < spo2Min_) spo2Min_ = s.spo2;
    }
  }

  bool active() const { return active_; }
  uint32_t seconds() const { return samples_ / 100; }
  const SessionSummary& summary() const { return summary_; }

private:
  bool     active_ = false;
  uint32_t samples_ = 0, good_ = 0;
  uint16_t bpmN_ = 0, spo2N_ = 0;
  float    bpmSum_ = 0, spo2Sum_ = 0;
  float    bpmMin_ = 1e9f, bpmMax_ = 0, spo2Min_ = 1e9f;
  SessionSummary summary_{};
};

}  // namespace pulselab
