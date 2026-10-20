// ui.h
// OLED screens for PulseLab (128x64, U8g2).
//
// The draw functions are templates, so they work with the real U8G2 display
// on the ESP32 and with a simulated display on the PC (tests/render_screens.cpp),
// which is how the screenshots in the docs were made.
//
// Screens (short button press = next):
//   Live     heart rate, SpO2, pulse wave
//   HRV      heart rate variability (RMSSD), stress level, beat-to-beat interval graph
//   Details  heart rate, SpO2, perfusion index, R, signal score
//   Wave     big pulse wave
//   Summary  shown after a session is stopped (long press)

#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "quality.h"
#include "session.h"
#include "hrv.h"
#include "pacer.h"
#include "breathing.h"
#include "guided.h"

namespace pulselab {

enum class Screen : uint8_t { Live, Hrv, Breathe, Guided, Details, Wave, Summary };

// ---------------- Pulse wave history ----------------

constexpr uint8_t WAVE_POINTS = 128;   // one point per 40 ms = last ~5 s
constexpr float   WAVE_SHRINK = 0.15f; // how fast the scale shrinks per frame (grows at once)

class WaveBuffer {
public:
  // good = false for points recorded while settling, moving or clipping.
  // They are still drawn, but don't change the scale (they're clipped to the edges),
  // so one movement spike doesn't squash the real pulse flat for 5 s.
  void push(float v, bool good) {
    buf_[head_] = v;
    good_[head_] = good;
    head_ = (head_ + 1) % WAVE_POINTS;
    if (count_ < WAVE_POINTS) count_++;
  }
  void clear() { head_ = count_ = 0; scaleSet_ = false; }
  uint8_t count() const { return count_; }
  float at(uint8_t i) const { return buf_[idx(i)]; }   // i = 0 is the oldest point
  bool goodAt(uint8_t i) const { return good_[idx(i)]; }

  // Call once per frame before drawing. The scale follows the good points:
  // it grows straight away (so the wave never goes off the screen) but
  // shrinks smoothly, so the wave doesn't jump in height when a tall peak
  // scrolls out of view.
  void updateScale() {
    float lo = 1e9f, hi = -1e9f;
    int n = 0;
    for (uint8_t i = 0; i < count_; i++) {
      if (!goodAt(i)) continue;
      float v = at(i);
      if (v < lo) lo = v;
      if (v > hi) hi = v;
      n++;
    }
    if (n < 10 || hi - lo < 1.0f) return;  // not enough good signal, keep the old scale
    float pad = 0.1f * (hi - lo);
    lo -= pad; hi += pad;
    if (!scaleSet_) { lo_ = lo; hi_ = hi; scaleSet_ = true; return; }
    lo_ = (lo < lo_) ? lo : lo_ + WAVE_SHRINK * (lo - lo_);
    hi_ = (hi > hi_) ? hi : hi_ + WAVE_SHRINK * (hi - hi_);
  }
  bool hasScale() const { return scaleSet_; }
  float lo() const { return lo_; }
  float hi() const { return hi_; }

private:
  uint8_t idx(uint8_t i) const { return (head_ + WAVE_POINTS - count_ + i) % WAVE_POINTS; }
  float   buf_[WAVE_POINTS] = {0};
  bool    good_[WAVE_POINTS] = {false};
  uint8_t head_ = 0, count_ = 0;
  bool    scaleSet_ = false;
  float   lo_ = 0.0f, hi_ = 1.0f;
};

// ---------------- Everything a screen needs ----------------

struct UiData {
  Screen        screen;
  QualityResult q;
  float         bpm, spo2, pi, r;
  bool          beatFlash;      // heart icon filled right after a beat
  float         fingerSec;      // seconds since the finger was placed
  const WaveBuffer*     wave;
  bool          sessionActive;
  uint32_t      sessionSec;
  const SessionSummary* summary;
  const HrvResult*      hrv;        // may be null
  const HrvAnalyzer*    hrvHistory; // for the interval graph, may be null
  PacerState    pacer;
  bool          recording;      // raw data is being sent over Serial (SERIAL_MODE 3)
  const BreathResult*   breath;     // may be null
  const GuidedResult*   guided;     // may be null
  PacerState    guidedPacer;    // the session's own pacer
  float         guidedFollow;   // breathing rate measured at the end of the slow part (0 = none)
};

// Measured breathing rate as short text: "12.3", "45%" while collecting, "--" if unsure.
inline void breathText(const UiData& u, char* buf, int size, bool withPercent = true) {
  const BreathResult* b = u.breath;
  bool finger = u.q.state != SignalState::NoFinger;
  if (!finger || !b) snprintf(buf, size, "--");
  else if (b->valid) snprintf(buf, size, "%.1f", b->rate);
  else if (b->progress < 1.0f && withPercent) snprintf(buf, size, "%d%%", (int)(b->progress * 100));
  else snprintf(buf, size, "--");
}

constexpr float MEASURE_SECONDS = 7.0f;  // roughly how long until SpO2 appears (for the progress bar)

// ---------------- Small helpers ----------------

inline int roundi(float v) { return (int)(v + (v >= 0 ? 0.5f : -0.5f)); }

template <class D>
void drawCentered(D& d, int xCenter, int y, const char* s) {
  d.drawStr(xCenter - d.getStrWidth(s) / 2, y, s);
}

template <class D>
void drawRight(D& d, int xRight, int y, const char* s) {
  d.drawStr(xRight - d.getStrWidth(s), y, s);
}

// 9x8 heart, filled for a moment on every beat.
template <class D>
void drawHeart(D& d, int x, int y, bool filled) {
  if (filled) {
    d.drawDisc(x + 2, y + 2, 2);
    d.drawDisc(x + 6, y + 2, 2);
    d.drawTriangle(x, y + 3, x + 9, y + 3, x + 4, y + 8);
  } else {
    d.drawCircle(x + 2, y + 2, 2);
    d.drawCircle(x + 6, y + 2, 2);
    d.drawLine(x, y + 3, x + 4, y + 7);
    d.drawLine(x + 8, y + 3, x + 4, y + 7);
  }
}

template <class D>
void drawWave(D& d, const WaveBuffer& w, int x0, int y0, int width, int height) {
  int n = w.count();
  if (n > width) n = width;
  if (n < 2 || !w.hasScale()) {
    d.drawHLine(x0, y0 + height / 2, width);
    return;
  }
  int first = w.count() - n;
  float lo = w.lo(), range = w.hi() - w.lo();
  int xStart = x0 + width - n;  // newest point always at the right edge
  int prevY = 0;
  for (int i = 0; i < n; i++) {
    float f = (w.at(first + i) - lo) / range;
    if (f < 0.0f) f = 0.0f;   // movement spikes are clipped to the edges
    if (f > 1.0f) f = 1.0f;
    int y = y0 + (height - 1) - roundi(f * (height - 1));
    if (i > 0) d.drawLine(xStart + i - 1, prevY, xStart + i, y);
    prevY = y;
  }
}

// Top row: heart, status text (or session timer), signal quality bar.
template <class D>
void drawTopBar(D& d, const UiData& u) {
  d.setFont(u8g2_font_5x8_tf);
  drawHeart(d, 0, 0, u.beatFlash);

  char text[24];
  bool warning = (u.q.state == SignalState::Moving || u.q.state == SignalState::Clipping ||
                  u.q.state == SignalState::WeakPulse);
  bool good = (u.q.state == SignalState::Good);
  if (u.sessionActive && good) {
    snprintf(text, sizeof text, "REC %02lu:%02lu", (unsigned long)(u.sessionSec / 60), (unsigned long)(u.sessionSec % 60));
  } else {
    snprintf(text, sizeof text, "%s", stateText(u.q.state));
  }

  if (warning) {  // warnings in a white box so they stand out
    int w = d.getStrWidth(text) + 4;
    d.drawBox(12, 0, w, 9);
    d.setDrawColor(0);
    d.drawStr(14, 8, text);
    d.setDrawColor(1);
  } else {
    d.drawStr(13, 8, text);
  }

  // quality bar
  d.drawFrame(103, 1, 25, 7);
  int fill = u.q.score * 23 / 100;
  if (fill > 0) d.drawBox(104, 2, fill, 5);

  d.drawHLine(0, 10, 128);
}

// ---------------- Screens ----------------

template <class D>
void drawLive(D& d, const UiData& u) {
  drawTopBar(d, u);

  if (u.q.state == SignalState::NoFinger) {
    d.setFont(u8g2_font_6x12_tf);
    drawCentered(d, 64, 32, "Place your finger");
    drawCentered(d, 64, 45, "on the sensor");
    d.setFont(u8g2_font_5x8_tf);
    drawCentered(d, 64, 62, "short press: next screen");
    return;
  }

  if (u.q.state == SignalState::Settling || u.q.state == SignalState::Searching) {
    d.setFont(u8g2_font_6x12_tf);
    drawCentered(d, 64, 26, "Measuring...");
    float p = u.fingerSec / MEASURE_SECONDS;
    if (p > 0.95f) p = 0.95f;
    d.drawFrame(14, 31, 100, 7);
    d.drawBox(15, 32, roundi(p * 98), 5);
    drawWave(d, *u.wave, 0, 44, 128, 20);
    return;
  }

  char buf[8];
  d.setFont(u8g2_font_logisoso20_tn);
  if (u.q.showBpm) snprintf(buf, sizeof buf, "%d", roundi(u.bpm));
  else snprintf(buf, sizeof buf, "--");
  drawCentered(d, 32, 34, buf);
  if (u.q.showSpo2) snprintf(buf, sizeof buf, "%d", roundi(u.spo2 > 100.0f ? 100.0f : u.spo2));
  else snprintf(buf, sizeof buf, "--");
  drawCentered(d, 96, 34, buf);

  d.setFont(u8g2_font_5x7_tf);
  drawCentered(d, 32, 43, "BPM");
  drawCentered(d, 96, 43, "SpO2 %");
  d.drawVLine(64, 14, 30);

  drawWave(d, *u.wave, 0, 46, 128, 18);
}

template <class D>
void drawDetails(D& d, const UiData& u) {
  drawTopBar(d, u);
  d.setFont(u8g2_font_6x10_tf);
  char buf[16];
  bool finger = u.q.state != SignalState::NoFinger;

  d.drawStr(0, 21, "Heart rate");
  if (u.q.showBpm) snprintf(buf, sizeof buf, "%d bpm", roundi(u.bpm)); else snprintf(buf, sizeof buf, "-- bpm");
  drawRight(d, 127, 21, buf);

  d.drawStr(0, 31, "SpO2");
  if (u.q.showSpo2) snprintf(buf, sizeof buf, "%.1f %%", u.spo2); else snprintf(buf, sizeof buf, "-- %%");
  drawRight(d, 127, 31, buf);

  d.drawStr(0, 41, "Perfusion");
  if (finger && u.pi > 0) snprintf(buf, sizeof buf, "%.2f %%", u.pi); else snprintf(buf, sizeof buf, "-- %%");
  drawRight(d, 127, 41, buf);

  d.drawStr(0, 51, "R ratio");
  if (finger && u.r > 0) snprintf(buf, sizeof buf, "%.3f", u.r); else snprintf(buf, sizeof buf, "--");
  drawRight(d, 127, 51, buf);

  d.drawStr(0, 61, "Breathing");
  char rate[10];
  breathText(u, rate, sizeof rate);
  if (strchr(rate, '%')) snprintf(buf, sizeof buf, "wait %s", rate); else snprintf(buf, sizeof buf, "%s /min", rate);
  drawRight(d, 127, 61, buf);
}

template <class D>
void drawWaveScreen(D& d, const UiData& u) {
  drawTopBar(d, u);
  drawWave(d, *u.wave, 0, 13, 128, 51);
  if (u.q.showBpm) {
    char buf[12];
    snprintf(buf, sizeof buf, "%d bpm", roundi(u.bpm));
    d.setFont(u8g2_font_5x8_tf);
    int w = d.getStrWidth(buf) + 3;
    d.setDrawColor(0);
    d.drawBox(127 - w, 12, w + 1, 9);  // clear a spot so the text is readable over the wave
    d.setDrawColor(1);
    drawRight(d, 127, 20, buf);
  }
}

template <class D>
void drawSummary(D& d, const UiData& u) {
  const SessionSummary& s = *u.summary;
  d.setFont(u8g2_font_5x8_tf);
  d.drawBox(0, 0, 128, 10);
  d.setDrawColor(0);
  drawCentered(d, 64, 8, "Session summary");
  d.setDrawColor(1);

  d.setFont(u8g2_font_6x10_tf);
  char buf[20];
  d.drawStr(0, 19, "Duration");
  snprintf(buf, sizeof buf, "%02lu:%02lu", (unsigned long)(s.seconds / 60), (unsigned long)(s.seconds % 60));
  drawRight(d, 127, 19, buf);

  if (s.bpmCount == 0) {
    drawCentered(d, 64, 38, "Not enough good");
    drawCentered(d, 64, 49, "signal recorded");
    return;
  }

  d.drawStr(0, 28, "HR avg");
  snprintf(buf, sizeof buf, "%d bpm", roundi(s.bpmAvg));
  drawRight(d, 127, 28, buf);

  d.drawStr(0, 37, "HR range");
  snprintf(buf, sizeof buf, "%d-%d", roundi(s.bpmMin), roundi(s.bpmMax));
  drawRight(d, 127, 37, buf);

  d.drawStr(0, 46, "SpO2 avg/min");
  if (s.spo2Count) snprintf(buf, sizeof buf, "%d/%d %%", roundi(s.spo2Avg > 100 ? 100 : s.spo2Avg), roundi(s.spo2Min > 100 ? 100 : s.spo2Min));
  else snprintf(buf, sizeof buf, "--");
  drawRight(d, 127, 46, buf);

  d.drawStr(0, 55, "Good signal");
  snprintf(buf, sizeof buf, "%d %%", s.goodPct);
  drawRight(d, 127, 55, buf);

  d.drawStr(0, 64, "HRV (RMSSD)");
  if (s.rmssd > 0) snprintf(buf, sizeof buf, "%d ms", roundi(s.rmssd));
  else snprintf(buf, sizeof buf, "too short");
  drawRight(d, 127, 64, buf);
}

// Beat-to-beat intervals, one point per beat, newest at the right.
// When relaxed, this usually shows slow waves: the heart speeds up when
// breathing in and slows down when breathing out.
template <class D>
void drawTachogram(D& d, const HrvAnalyzer& h, int x0, int y0, int width, int height, int step) {
  int n = h.historyCount();
  int maxPts = width / step + 1;
  int first = n > maxPts ? n - maxPts : 0;
  int lo = 65535, hi = 0;
  for (int i = first; i < n; i++) {
    if (!h.historyNormal(i)) continue;
    int v = h.historyIbi(i);
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  if (hi <= lo) {  // nothing yet, or all the same
    d.drawHLine(x0, y0 + height / 2, width);
    return;
  }
  int pad = (hi - lo) / 10 + 5;
  lo -= pad; hi += pad;
  int count = n - first;
  int xStart = x0 + width - 1 - (count - 1) * step;
  bool havePrev = false;
  int px = 0, py = 0;
  for (int i = first; i < n; i++) {
    int x = xStart + (i - first) * step;
    if (!h.historyNormal(i)) { havePrev = false; continue; }  // artifacts are left out
    int y = y0 + (height - 1) - (h.historyIbi(i) - lo) * (height - 1) / (hi - lo);
    if (havePrev) d.drawLine(px, py, x, y);
    d.drawPixel(x, y - 1); d.drawPixel(x, y + 1);  // small dot on each beat
    px = x; py = y; havePrev = true;
  }
}

template <class D>
void drawHrv(D& d, const UiData& u) {
  drawTopBar(d, u);

  if (u.q.state == SignalState::NoFinger || !u.hrv) {
    d.setFont(u8g2_font_6x12_tf);
    drawCentered(d, 64, 32, "Place your finger");
    drawCentered(d, 64, 45, "and keep still");
    return;
  }

  const HrvResult& h = *u.hrv;
  if (!h.ready) {
    d.setFont(u8g2_font_6x10_tf);
    drawCentered(d, 64, 23, "HRV: keep still");
    char buf[24];
    int left = roundi((1.0f - h.progress) * HRV_MIN_SECONDS);
    snprintf(buf, sizeof buf, "about %d s left", left < 1 ? 1 : left);
    d.setFont(u8g2_font_5x8_tf);
    drawCentered(d, 64, 33, buf);
    d.drawFrame(14, 36, 100, 6);
    d.drawBox(15, 37, roundi(h.progress * 98), 4);
    if (u.hrvHistory) drawTachogram(d, *u.hrvHistory, 0, 45, 128, 19, 3);
    return;
  }

  char buf[12];
  // left: RMSSD
  d.setFont(u8g2_font_logisoso20_tn);
  snprintf(buf, sizeof buf, "%d", roundi(h.rmssd));
  drawCentered(d, 30, 34, buf);
  d.setFont(u8g2_font_5x7_tf);
  drawCentered(d, 30, 43, "HRV ms");
  d.drawVLine(60, 14, 30);

  // right: stress level text + 4-step gauge
  // (no level while the heart rate is rising or falling: RMSSD then
  //  measures the change in heart rate, not relaxation)
  const char* levelText = h.steady ? stressText(h.level) : "HR changing";
  d.setFont(u8g2_font_6x10_tf);
  if (d.getStrWidth(levelText) > 62) d.setFont(u8g2_font_5x7_tf);  // "High stress" etc. don't fit in 6x10
  drawCentered(d, 95, 23, levelText);
  int steps = h.steady ? (int)h.level : 0;  // High=1 .. Relaxed=4
  for (int i = 0; i < 4; i++) {
    int x = 66 + i * 15;
    if (i < steps) d.drawBox(x, 27, 13, 6);
    else d.drawFrame(x, 27, 13, 6);
  }
  d.setFont(u8g2_font_5x7_tf);
  snprintf(buf, sizeof buf, "SDNN %d", roundi(h.sdnn));
  drawCentered(d, 95, 43, buf);

  if (u.hrvHistory) drawTachogram(d, *u.hrvHistory, 0, 46, 128, 18, 3);
}

// Breathing pacer: a circle that grows while breathing in and shrinks while
// breathing out, with "In" / "Out" and a countdown next to it.
// Breathing circle: grows while breathing in, shrinks while breathing out.
template <class D>
void drawPacerCircle(D& d, float level) {
  int r = 4 + roundi(level * 21.0f);
  d.drawCircle(32, 38, 25);              // the "full" size, as a guide
  d.drawDisc(32, 38, r);
}

template <class D>
void drawBreathe(D& d, const UiData& u) {
  drawTopBar(d, u);
  const PacerState& p = u.pacer;
  char buf[24], rate[10];
  breathText(u, rate, sizeof rate);
  bool collecting = strchr(rate, '%') != nullptr;

  if (!p.perMin) {
    // No pacer: show the measured breathing rate big.
    d.setFont(u8g2_font_6x10_tf);
    drawCentered(d, 64, 22, "Breathing rate");
    if (collecting) {
      d.setFont(u8g2_font_6x10_tf);
      drawCentered(d, 64, 38, "keep still...");
      int w = roundi(u.breath->progress * 98);
      d.drawFrame(14, 43, 100, 6);
      if (w > 0) d.drawBox(15, 44, w, 4);
    } else {
      d.setFont(u8g2_font_logisoso20_tn);
      if (rate[0] == '-') {
        drawCentered(d, 64, 49, "--");
      } else {
        int w = d.getStrWidth(rate);
        d.drawStr(58 - w / 2, 49, rate);
        d.setFont(u8g2_font_6x10_tf);
        d.drawStr(62 + w / 2, 49, "/min");
      }
    }
    d.setFont(u8g2_font_5x8_tf);
    bool unclear = !collecting && rate[0] == '-' && u.q.state != SignalState::NoFinger;
    drawCentered(d, 64, 63, unclear ? "not clear yet, keep still" : "long press: pacer");
    return;
  }

  drawPacerCircle(d, p.level);

  d.setFont(u8g2_font_helvB12_tr);
  drawCentered(d, 96, 30, p.inhale ? "In" : "Out");
  d.setFont(u8g2_font_6x10_tf);
  snprintf(buf, sizeof buf, "%d s", (int)ceilf(p.secondsLeft));
  drawCentered(d, 96, 43, buf);
  d.setFont(u8g2_font_5x8_tf);
  snprintf(buf, sizeof buf, "%u /min%s", p.perMin, u.recording ? " REC" : "");
  drawCentered(d, 96, 53, buf);
  snprintf(buf, sizeof buf, "you: %s", rate);   // what PulseLab measures
  drawCentered(d, 96, 62, buf);
}

// Guided breathing session: intro, before / slow / after, results.
template <class D>
void drawGuided(D& d, const UiData& u) {
  drawTopBar(d, u);
  char buf[32];
  const GuidedResult* g = u.guided;
  GuidedPhase ph = g ? g->phase : GuidedPhase::Off;

  if (ph == GuidedPhase::Off) {
    d.setFont(u8g2_font_6x10_tf);
    drawCentered(d, 64, 22, "Guided breathing");
    d.setFont(u8g2_font_5x8_tf);
    drawCentered(d, 64, 33, "1 min normal breathing,");
    drawCentered(d, 64, 42, "3 min slow (6 /min),");
    drawCentered(d, 64, 51, "1 min normal again");
    drawCentered(d, 64, 63, u.q.state == SignalState::NoFinger ? "place your finger first" : "long press: start");
    return;
  }

  if (ph == GuidedPhase::Done) {
    d.setFont(u8g2_font_5x8_tf);
    drawRight(d, 72, 19, "before");
    drawRight(d, 100, 19, "slow");
    drawRight(d, 127, 19, "after");
    const PhaseStats* ps[3] = {&g->before, &g->during, &g->after};
    const int xs[3] = {72, 100, 127};
    d.drawStr(0, 28, "HR");
    d.drawStr(0, 37, "HRV ms");
    for (int i = 0; i < 3; i++) {
      if (ps[i]->bpm > 0) snprintf(buf, sizeof buf, "%d", roundi(ps[i]->bpm)); else snprintf(buf, sizeof buf, "--");
      drawRight(d, xs[i], 28, buf);
      if (ps[i]->rmssd > 0) snprintf(buf, sizeof buf, "%d", roundi(ps[i]->rmssd)); else snprintf(buf, sizeof buf, "--");
      drawRight(d, xs[i], 37, buf);
    }
    d.drawHLine(0, 40, 128);
    if (g->swingBreaths) snprintf(buf, sizeof buf, "HR swing/breath %d bpm", roundi(g->swing));
    else snprintf(buf, sizeof buf, "HR swing/breath --");
    d.drawStr(0, 49, buf);
    if (u.guidedFollow > 0) snprintf(buf, sizeof buf, "you breathed %.1f /min", u.guidedFollow);
    else snprintf(buf, sizeof buf, "you breathed -- /min");
    d.drawStr(0, 57, buf);
    drawRight(d, 127, 63, "long press: close");
    return;
  }

  uint32_t leftS = (g->phaseLength - g->phaseSamples + 99) / 100;

  if (ph == GuidedPhase::Breathe) {
    const PacerState& p = u.guidedPacer;
    drawPacerCircle(d, p.level);
    d.setFont(u8g2_font_helvB12_tr);
    drawCentered(d, 96, 30, p.inhale ? "In" : "Out");
    d.setFont(u8g2_font_6x10_tf);
    snprintf(buf, sizeof buf, "%d s", (int)ceilf(p.secondsLeft));
    drawCentered(d, 96, 41, buf);
    d.setFont(u8g2_font_5x8_tf);
    snprintf(buf, sizeof buf, "2/3 %lu:%02lu", (unsigned long)(leftS / 60), (unsigned long)(leftS % 60));
    drawCentered(d, 96, 52, buf);
    if (g->paused) snprintf(buf, sizeof buf, "finger?");
    else if (g->lastSwing > 0) snprintf(buf, sizeof buf, "swing %d", roundi(g->lastSwing));
    else snprintf(buf, sizeof buf, "swing --");
    drawCentered(d, 96, 62, buf);
    return;
  }

  // Before / After: breathe normally, countdown
  d.setFont(u8g2_font_6x10_tf);
  drawCentered(d, 64, 22, ph == GuidedPhase::Before ? "1/3 breathe normally" : "3/3 breathe normally");
  d.setFont(u8g2_font_helvB12_tr);
  snprintf(buf, sizeof buf, "%lu:%02lu", (unsigned long)(leftS / 60), (unsigned long)(leftS % 60));
  drawCentered(d, 64, 40, buf);
  int w = roundi(98.0f * g->phaseSamples / (g->phaseLength ? g->phaseLength : 1));
  d.drawFrame(14, 45, 100, 6);
  if (w > 0) d.drawBox(15, 46, w, 4);
  d.setFont(u8g2_font_5x8_tf);
  if (g->paused) snprintf(buf, sizeof buf, "paused: place your finger");
  else if (u.q.showBpm) snprintf(buf, sizeof buf, "keep still   %d BPM", roundi(u.bpm));
  else snprintf(buf, sizeof buf, "keep still");
  drawCentered(d, 64, 62, buf);
}

template <class D>
void drawScreen(D& d, const UiData& u) {
  d.clearBuffer();
  switch (u.screen) {
    case Screen::Live:    drawLive(d, u); break;
    case Screen::Hrv:     drawHrv(d, u); break;
    case Screen::Breathe: drawBreathe(d, u); break;
    case Screen::Guided:  drawGuided(d, u); break;
    case Screen::Details: drawDetails(d, u); break;
    case Screen::Wave:    drawWaveScreen(d, u); break;
    case Screen::Summary: drawSummary(d, u); break;
  }
}

}  // namespace pulselab
