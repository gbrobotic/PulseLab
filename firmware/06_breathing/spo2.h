// spo2.h
// SpO2 and perfusion index for PulseLab.
//
// Works together with HeartRateDetector (ppg_dsp.h): the heart rate code
// finds the beats, and this uses those beats to cut the red and IR signals
// into one window per heartbeat.
//
// For each beat window:
//   AC = peak-to-trough of the filtered signal   (the pulsing part)
//   DC = average of the raw signal               (the steady part)
//
//   R    = (AC_red / DC_red) / (AC_ir / DC_ir)    "ratio of ratios"
//   SpO2 = SPO2_A - SPO2_B * R                    (empirical, calibrated later in Phase 11)
//   PI   = AC_ir / DC_ir * 100 %                  (perfusion index)
//
// Dividing AC by DC cancels out things that are different for every finger
// and every sensor (skin thickness, LED brightness, how hard you press), so
// what's left mostly depends on how much of the blood is carrying oxygen.
//
// The final values are medians over the last few beats, so one bad beat
// (finger moved) doesn't change the reading.

#pragma once
#include <stdint.h>
#include <math.h>
#include "ppg_dsp.h"

namespace pulselab {

// ---------------- Settings ----------------

constexpr float   SPO2_A           = 110.0f;  // SpO2 = A - B * R  (common starting values,
constexpr float   SPO2_B           = 25.0f;   //  to be replaced by my own calibration)

constexpr float   R_MIN            = 0.2f;    // R outside this range = bad beat, ignore it
constexpr float   R_MAX            = 2.0f;    //  (R 0.4-0.6 is normal, 2.0 would be ~60% SpO2)
constexpr float   AC_MIN           = 5.0f;    // pulse smaller than this (filtered units) = too weak
constexpr float   ADC_CLIP         = 65000.0f;// 16-bit ADC, values this high mean the LED is too bright
constexpr uint16_t MIN_WINDOW      = 30;      // a beat window shorter than 300 ms is not a real beat

constexpr float   PI_MIN_RELIABLE  = 0.5f;    // below 0.5% perfusion, SpO2 is not trustworthy (see docs)

constexpr uint8_t R_BUFFER_SIZE    = 8;       // median over the last 8 good beats
constexpr uint8_t R_MIN_FOR_SPO2   = 4;       // need 4 good beats before showing SpO2

// ---------------- Result ----------------

struct Spo2Result {
  bool  beatDone;     // a beat window finished on this sample
  bool  beatValid;    // ...and it passed the checks
  float beatR;        // R of that one beat (0 if not valid)
  float beatPi;       // PI of that one beat
  float redSignal;    // filtered red PPG (positive peak = heartbeat), for plotting
  float r;            // median R over recent beats (0 until enough beats)
  float spo2;         // SpO2 in %, 0 until enough beats. Not clamped, can be >100 before calibration
  float pi;           // median perfusion index in %
  float dcIr;         // DC levels of the last beat, useful for checking LED brightness
  float dcRed;
  bool  clipping;     // raw value hit the top of the ADC range
  bool  lowPerfusion; // pulse too weak for a reliable SpO2 (PI < PI_MIN_RELIABLE)
};

// ---------------- Estimator ----------------

class Spo2Estimator {
public:
  void reset() {
    redFilter_.reset();
    redDc_ = 0.0f;
    haveRedDc_ = false;
    windowOpen_ = false;
    clearWindow();
    rCount_ = rHead_ = 0;
    r_ = spo2_ = pi_ = 0.0f;
    lastDcIr_ = lastDcRed_ = 0.0f;
  }

  // Call once per sample, right after HeartRateDetector::process(),
  // with the same raw IR, the raw red, and the heart rate result.
  Spo2Result process(float rawIr, float rawRed, const HrResult& hr) {
    Spo2Result out{};

    if (!hr.finger) {
      if (haveRedDc_) reset();
      return out;
    }

    // Red goes through the same steps as IR: DC removal, band-pass, invert.
    // Using the identical filter means both channels are delayed and scaled
    // the same way, so their ratio isn't affected by the filter.
    if (!haveRedDc_) { redDc_ = rawRed; haveRedDc_ = true; }
    redDc_ += (hr.settling ? DC_ALPHA_FAST : DC_ALPHA) * (rawRed - redDc_);
    float red = -redFilter_.process(rawRed - redDc_);
    float ir = hr.signal;  // IR is already filtered by the heart rate code
    out.redSignal = red;

    bool clip = rawIr >= ADC_CLIP || rawRed >= ADC_CLIP;
    out.clipping = clip;

    if (hr.settling) {
      out.r = r_; out.spo2 = spo2_; out.pi = pi_;
      return out;
    }

    // A beat closes the current window and opens the next one.
    if (hr.beat) {
      if (windowOpen_) {
        out.beatDone = true;
        // Only use windows that are exactly one normal heartbeat long:
        // the heart rate code says the interval was ok, nothing clipped.
        if (hr.ibiValid && !windowClipped_ && n_ >= MIN_WINDOW) {
          evaluateWindow(out);
        }
      }
      clearWindow();
      windowOpen_ = true;
    }

    if (windowOpen_) {
      if (ir > irMax_) irMax_ = ir;
      if (ir < irMin_) irMin_ = ir;
      if (red > redMax_) redMax_ = red;
      if (red < redMin_) redMin_ = red;
      irSum_ += rawIr;
      redSum_ += rawRed;
      n_++;
      if (clip) windowClipped_ = true;
    }

    out.r = r_;
    out.spo2 = spo2_;
    out.pi = pi_;
    out.lowPerfusion = (spo2_ > 0.0f && pi_ < PI_MIN_RELIABLE);
    out.dcIr = lastDcIr_;
    out.dcRed = lastDcRed_;
    return out;
  }

private:
  void clearWindow() {
    irMax_ = redMax_ = -1e9f;
    irMin_ = redMin_ = 1e9f;
    irSum_ = redSum_ = 0.0;
    n_ = 0;
    windowClipped_ = false;
  }

  void evaluateWindow(Spo2Result& out) {
    float acIr  = irMax_ - irMin_;
    float acRed = redMax_ - redMin_;
    float dcIr  = (float)(irSum_ / n_);
    float dcRed = (float)(redSum_ / n_);
    lastDcIr_ = dcIr;
    lastDcRed_ = dcRed;

    if (acIr < AC_MIN || acRed < AC_MIN || dcIr <= 0 || dcRed <= 0) return;

    float ratioIr  = acIr / dcIr;
    float ratioRed = acRed / dcRed;
    float r = ratioRed / ratioIr;
    if (r < R_MIN || r > R_MAX) return;

    float pi = ratioIr * 100.0f;

    out.beatValid = true;
    out.beatR = r;
    out.beatPi = pi;

    rBuf_[rHead_] = r;
    piBuf_[rHead_] = pi;
    rHead_ = (rHead_ + 1) % R_BUFFER_SIZE;
    if (rCount_ < R_BUFFER_SIZE) rCount_++;

    if (rCount_ >= R_MIN_FOR_SPO2) {
      r_ = median(rBuf_, rCount_);
      pi_ = median(piBuf_, rCount_);
      spo2_ = SPO2_A - SPO2_B * r_;
    }
  }

  static float median(const float* v, uint8_t n) {
    float tmp[R_BUFFER_SIZE];
    for (uint8_t i = 0; i < n; i++) tmp[i] = v[i];
    for (uint8_t i = 1; i < n; i++) {
      float x = tmp[i];
      int8_t j = i - 1;
      while (j >= 0 && tmp[j] > x) { tmp[j + 1] = tmp[j]; j--; }
      tmp[j + 1] = x;
    }
    return (n % 2) ? tmp[n / 2] : 0.5f * (tmp[n / 2 - 1] + tmp[n / 2]);
  }

  BandPass redFilter_;
  float    redDc_ = 0.0f;
  bool     haveRedDc_ = false;

  bool     windowOpen_ = false;
  bool     windowClipped_ = false;
  float    irMax_ = -1e9f, irMin_ = 1e9f, redMax_ = -1e9f, redMin_ = 1e9f;
  double   irSum_ = 0.0, redSum_ = 0.0;
  uint16_t n_ = 0;

  float    rBuf_[R_BUFFER_SIZE] = {0};
  float    piBuf_[R_BUFFER_SIZE] = {0};
  uint8_t  rCount_ = 0, rHead_ = 0;
  float    r_ = 0.0f, spo2_ = 0.0f, pi_ = 0.0f;
  float    lastDcIr_ = 0.0f, lastDcRed_ = 0.0f;
};

}  // namespace pulselab
