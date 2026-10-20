// Renders the PulseLab OLED screens to image files on a PC, using the real
// U8g2 graphics code. This is how I check the layout without uploading,
// and how the screenshots in docs/images were made.
//
// Needs the U8g2 C sources (https://github.com/olikraus/u8g2, folder csrc):
//   gcc -O2 -c U8G2/csrc/*.c
//   g++ -std=c++17 -O2 -I U8G2/csrc -I firmware/05_hrv tests/render_screens.cpp *.o -o render
//   mkdir -p out/movement && ./render out    (writes one .pgm per screen)

#include <cstdio>
#include <string>
#include "u8g2.h"
#include "ui.h"
#include "sim_finger.h"

using namespace pulselab;

// Same function names as the Arduino U8G2 class, so ui.h works unchanged.
class SimDisplay {
public:
  SimDisplay() {
    u8g2_Setup_ssd1306_128x64_noname_f(&u8g2_, U8G2_R0, u8x8_byte_empty, u8x8_dummy_cb);
    u8g2_SetFontPosBaseline(&u8g2_);
  }
  void clearBuffer() { u8g2_ClearBuffer(&u8g2_); }
  void setFont(const uint8_t* f) { u8g2_SetFont(&u8g2_, f); }
  int drawStr(int x, int y, const char* s) { return u8g2_DrawStr(&u8g2_, x, y, s); }
  int getStrWidth(const char* s) { return u8g2_GetStrWidth(&u8g2_, s); }
  void setDrawColor(uint8_t c) { u8g2_SetDrawColor(&u8g2_, c); }
  void drawBox(int x, int y, int w, int h) { u8g2_DrawBox(&u8g2_, x, y, w, h); }
  void drawFrame(int x, int y, int w, int h) { u8g2_DrawFrame(&u8g2_, x, y, w, h); }
  void drawHLine(int x, int y, int w) { u8g2_DrawHLine(&u8g2_, x, y, w); }
  void drawVLine(int x, int y, int h) { u8g2_DrawVLine(&u8g2_, x, y, h); }
  void drawLine(int x0, int y0, int x1, int y1) { u8g2_DrawLine(&u8g2_, x0, y0, x1, y1); }
  void drawPixel(int x, int y) { u8g2_DrawPixel(&u8g2_, x, y); }
  void drawDisc(int x, int y, int r, uint8_t opt = U8G2_DRAW_ALL) { u8g2_DrawDisc(&u8g2_, x, y, r, opt); }
  void drawCircle(int x, int y, int r, uint8_t opt = U8G2_DRAW_ALL) { u8g2_DrawCircle(&u8g2_, x, y, r, opt); }
  void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2) { u8g2_DrawTriangle(&u8g2_, x0, y0, x1, y1, x2, y2); }

  bool pixel(int x, int y) {
    uint8_t* buf = u8g2_GetBufferPtr(&u8g2_);
    return (buf[(y / 8) * 128 + x] >> (y % 8)) & 1;
  }
  void savePgm(const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    fprintf(f, "P5\n128 64\n255\n");
    for (int y = 0; y < 64; y++)
      for (int x = 0; x < 128; x++) fputc(pixel(x, y) ? 255 : 0, f);
    fclose(f);
  }

private:
  u8g2_t u8g2_;
};

int main(int argc, char** argv) {
  std::string out = argc > 1 ? argv[1] : ".";

  // A realistic pulse wave: run the real signal chain on the finger simulator.
  WaveBuffer wave, flat;
  {
    Sim s; s.bpm = 76; s.seconds = 12; s.ampJitter = 0.1f;
    SimFinger f(s, 3);
    HeartRateDetector hr; hr.reset();
    float ir, red; int n = 0;
    while (f.next(ir, red)) {
      HrResult h = hr.process(ir);
      if (h.finger && !h.settling && (++n % 4 == 0)) wave.push(h.signal, true);
    }
  }
  WaveBuffer startWave;  // only ~1.5 s of signal, as while measuring
  for (int i = WAVE_POINTS - 38; i < WAVE_POINTS; i++) startWave.push(wave.at(i), true);

  // The finger moved 2.5 s ago: the movement spike is still in the last 5 s.
  // "old" scales to every point (Phase 4 first version), "new" ignores the
  // points recorded while the quality code said "Hold still".
  WaveBuffer moveOld, moveNew;
  {
    Sim s; s.bpm = 78; s.bumpAt = 9.0f; s.seconds = 9.0f + 4.5f;
    SimFinger f(s, 5);
    HeartRateDetector hr; hr.reset();
    Spo2Estimator sp; sp.reset();
    QualityMonitor qm; qm.reset();
    float ir, red; int n = 0;
    while (f.next(ir, red)) {
      HrResult h = hr.process(ir);
      Spo2Result r = sp.process(ir, red, h);
      QualityResult q = qm.process(h, r, ir, red);
      if (++n % 4 == 0 && h.finger && !h.settling) {
        bool good = q.state != SignalState::Moving && q.state != SignalState::Clipping;
        moveOld.push(h.signal, true);
        moveNew.push(h.signal, good);
      }
    }
  }
  for (WaveBuffer* w : {&wave, &startWave, &moveOld, &moveNew}) w->updateScale();

  // HRV: 75 s of a relaxed finger with breathing-linked heart rate changes
  HrvAnalyzer hrvFull, hrvHalf;
  HrvResult hrvFullR{}, hrvHalfR{};
  {
    Sim s; s.bpm = 72; s.seconds = 75; s.beatJitter = 0.012f; s.rsa = 0.05f;
    SimFinger f(s, 21);
    HeartRateDetector hr; hr.reset(); Spo2Estimator sp; sp.reset(); QualityMonitor qm; qm.reset();
    hrvFull.reset(); hrvHalf.reset();
    float ir, red;
    while (f.next(ir, red)) {
      HrResult h = hr.process(ir); Spo2Result r = sp.process(ir, red, h); QualityResult q = qm.process(h, r, ir, red);
      hrvFull.process(h, q);
      if (f.t() < 20) hrvHalf.process(h, q);
    }
    hrvFullR = hrvFull.result(); hrvHalfR = hrvHalf.result();
    printf("render HRV: RMSSD %.1f, level %s; collecting: %.0f%%\n", hrvFullR.rmssd, stressText(hrvFullR.level), hrvHalfR.progress * 100);
  }
  // slow deep breathing (6/min) and a heart rate that is rising
  auto runHrv = [](Sim s, unsigned seed, HrvAnalyzer& hv) {
    SimFinger f(s, seed);
    HeartRateDetector hr; hr.reset(); Spo2Estimator sp; sp.reset(); QualityMonitor qm; qm.reset(); hv.reset();
    float ir, red;
    while (f.next(ir, red)) { HrResult h = hr.process(ir); Spo2Result r = sp.process(ir, red, h); hv.process(h, qm.process(h, r, ir, red)); }
    return hv.result();
  };
  HrvAnalyzer hrvSlow, hrvRising;
  HrvResult hrvSlowR, hrvRisingR;
  { Sim s; s.bpm = 72; s.seconds = 80; s.beatJitter = 0.01f; s.rsa = 0.22f; s.breathRate = 0.1f; hrvSlowR = runHrv(s, 4, hrvSlow); }
  { Sim s; s.bpm = 65; s.bpmEnd = 100; s.seconds = 80; s.beatJitter = 0.015f; hrvRisingR = runHrv(s, 4, hrvRising); }
  printf("render HRV slow breathing: RMSSD %.1f %s; rising HR: RMSSD %.1f steady %d\n", hrvSlowR.rmssd, stressText(hrvSlowR.level), hrvRisingR.rmssd, hrvRisingR.steady);

  SessionSummary sum{};
  sum.valid = true; sum.seconds = 135; sum.bpmCount = 168;
  sum.bpmAvg = 76.3f; sum.bpmMin = 71.2f; sum.bpmMax = 82.6f;
  sum.spo2Count = 160; sum.spo2Avg = 97.8f; sum.spo2Min = 97.1f; sum.goodPct = 94;

  UiData base{};
  base.screen = Screen::Live;
  base.q = {SignalState::Good, 92, true, true};
  base.bpm = 76.4f; base.spo2 = 97.9f; base.pi = 1.72f; base.r = 0.481f;
  base.beatFlash = true; base.fingerSec = 20; base.wave = &wave;
  base.summary = &sum;
  base.hrv = &hrvFullR; base.hrvHistory = &hrvFull;
  sum.rmssd = 43.6f;

  struct Case { const char* name; UiData u; };
  std::vector<Case> cases;

  { UiData u = base; cases.push_back({"a_live_good", u}); }
  { UiData u = base; u.screen = Screen::Hrv; cases.push_back({"b_hrv", u}); }
  { UiData u = base; u.screen = Screen::Hrv; u.hrv = &hrvSlowR; u.hrvHistory = &hrvSlow; cases.push_back({"c_hrv_slow_breathing", u}); }
  { UiData u = base; u.screen = Screen::Hrv; u.hrv = &hrvRisingR; u.hrvHistory = &hrvRising; u.beatFlash = false; cases.push_back({"d_hrv_hr_changing", u}); }
  { UiData u = base; u.screen = Screen::Hrv; u.hrv = &hrvHalfR; u.hrvHistory = &hrvHalf; u.beatFlash = false; cases.push_back({"e_hrv_collecting", u}); }
  { UiData u = base; u.screen = Screen::Wave; cases.push_back({"g_wave", u}); }
  { UiData u = base; u.screen = Screen::Summary; cases.push_back({"h_summary", u}); }
  { UiData u = base; u.q = {SignalState::NoFinger, 0, false, false}; u.wave = &flat; u.beatFlash = false; cases.push_back({"i_place_finger", u}); }
  { UiData u = base; u.q = {SignalState::Searching, 0, false, false}; u.fingerSec = 3.5f; u.wave = &startWave; cases.push_back({"j_measuring", u}); }
  { UiData u = base; u.q = {SignalState::Moving, 30, false, false}; u.beatFlash = false; cases.push_back({"k_hold_still", u}); }
  { UiData u = base; u.sessionActive = true; u.sessionSec = 83; cases.push_back({"l_live_recording", u}); }
  // Phase 6: breathing rate and pacer (6/min: 4 s in, 6 s out)
  static BreathResult brOk{}, brWait{}, brUnsure{};
  brOk.valid = true; brOk.rate = 12.4f; brOk.progress = 1; brOk.agree = 3;
  brWait.progress = 0.45f;
  brUnsure.progress = 1;
  base.breath = &brOk;
  { UiData u = base; u.screen = Screen::Details; u.beatFlash = false; cases.push_back({"f_details", u}); }
  // Phase 7: guided breathing
  {
    static GuidedResult gOff{}, gBefore{}, gSlow{}, gAfter{}, gDone{};
    gBefore.phase = GuidedPhase::Before; gBefore.phaseLength = 6000; gBefore.phaseSamples = 1800;
    gSlow.phase = GuidedPhase::Breathe; gSlow.phaseLength = 18000; gSlow.phaseSamples = 4200; gSlow.lastSwing = 17.4f;
    gAfter.phase = GuidedPhase::After; gAfter.phaseLength = 6000; gAfter.phaseSamples = 5100;
    gDone.phase = GuidedPhase::Done;
    gDone.before = {80.3f, 35.6f, 81, 52.0f}; gDone.during = {77.2f, 30.3f, 227, 71.4f}; gDone.after = {79.0f, 27.4f, 79, 55.8f};
    gDone.swing = 21.6f; gDone.swingBreaths = 16;
    BreathPacer gp; gp.set(6); for (int i = 0; i < 250; i++) gp.tick();
    UiData u = base; u.screen = Screen::Guided; u.beatFlash = false;
    u.guided = &gOff;    cases.push_back({"p_guided_start", u});
    u.guided = &gBefore; cases.push_back({"q_guided_before", u});
    u.guided = &gSlow;   u.guidedPacer = gp.state(); cases.push_back({"r_guided_slow", u});
    u.guided = &gAfter;  cases.push_back({"s_guided_after", u});
    u.guided = &gDone;   u.guidedFollow = 6.1f; cases.push_back({"t_guided_result", u});
  }
  {
    BreathPacer p6; p6.next();                       // 6 per minute
    UiData u = base; u.screen = Screen::Breathe; u.beatFlash = false;
    cases.push_back({"m_breathe_off", u});
    u.breath = &brWait; cases.push_back({"m_breathe_collecting", u});
    u.breath = &brUnsure; cases.push_back({"m_breathe_unsure", u});
    static BreathResult br6{}; br6.valid = true; br6.rate = 6.2f; br6.progress = 1;
    u.breath = &br6;
    for (int i = 0; i < 150; i++) p6.tick();         // 1.5 s into breathing in
    u.pacer = p6.state(); u.recording = true; cases.push_back({"n_breathe_in", u});
    for (int i = 0; i < 400; i++) p6.tick();         // 5.5 s: breathing out
    u.pacer = p6.state(); cases.push_back({"o_breathe_out", u});
  }

  std::vector<Case> moveCases;
  { UiData u = base; u.screen = Screen::Wave; u.bpm = 78; u.wave = &moveOld; moveCases.push_back({"1_after_movement_before_fix", u}); }
  { UiData u = base; u.screen = Screen::Wave; u.bpm = 78; u.wave = &moveNew; moveCases.push_back({"2_after_movement_after_fix", u}); }

  SimDisplay d;
  for (auto& c : cases) {
    drawScreen(d, c.u);
    d.savePgm(out + "/" + c.name + ".pgm");
    printf("wrote %s/%s.pgm\n", out.c_str(), c.name);
  }
  // second set, in a sub-folder (must exist): out/movement
  for (auto& c : moveCases) {
    drawScreen(d, c.u);
    d.savePgm(out + "/movement/" + c.name + ".pgm");
    printf("wrote %s/movement/%s.pgm\n", out.c_str(), c.name);
  }
  return 0;
}
