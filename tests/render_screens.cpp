// Renders the PulseLab OLED screens to image files on a PC, using the real
// U8g2 graphics code. This is how I check the layout without uploading,
// and how the screenshots in docs/images were made.
//
// Needs the U8g2 C sources (https://github.com/olikraus/u8g2, folder csrc):
//   gcc -O2 -c U8G2/csrc/*.c
//   g++ -std=c++17 -O2 -I U8G2/csrc -I firmware/04_display tests/render_screens.cpp *.o -o render
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

  struct Case { const char* name; UiData u; };
  std::vector<Case> cases;

  { UiData u = base; cases.push_back({"1_live_good", u}); }
  { UiData u = base; u.q = {SignalState::NoFinger, 0, false, false}; u.wave = &flat; u.beatFlash = false; cases.push_back({"2_place_finger", u}); }
  { UiData u = base; u.q = {SignalState::Searching, 0, false, false}; u.fingerSec = 3.5f; u.wave = &startWave; cases.push_back({"3_measuring", u}); }
  { UiData u = base; u.q = {SignalState::Moving, 30, false, false}; u.beatFlash = false; cases.push_back({"4_hold_still", u}); }
  { UiData u = base; u.sessionActive = true; u.sessionSec = 83; cases.push_back({"5_live_recording", u}); }
  { UiData u = base; u.screen = Screen::Details; u.beatFlash = false; cases.push_back({"6_details", u}); }
  { UiData u = base; u.screen = Screen::Wave; cases.push_back({"7_wave", u}); }
  { UiData u = base; u.screen = Screen::Summary; cases.push_back({"8_summary", u}); }

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
