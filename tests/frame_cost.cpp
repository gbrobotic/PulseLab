// Estimates how much of the OLED changes per frame, to pick the frame rate.
//
// The SH1106 screen is 8 "pages" (strips of 8 rows). Sending all 8 took
// 33 ms on my board (Phase 4 status line), so ~4.1 ms per page. If only
// the pages that changed are sent, the cost per frame is much lower.
//
//   g++ -std=c++17 -O2 -I U8G2/csrc -I firmware/05_hrv tests/frame_cost.cpp *.o -o frame_cost

#include <cstdio>
#include <cstring>
#include "u8g2.h"
#include "ui.h"
#include "sim_finger.h"

using namespace pulselab;

class SimDisplay {
public:
  SimDisplay() { u8g2_Setup_ssd1306_128x64_noname_f(&u8g2_, U8G2_R0, u8x8_byte_empty, u8x8_dummy_cb); }
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
  uint8_t* buffer() { return u8g2_GetBufferPtr(&u8g2_); }
private:
  u8g2_t u8g2_;
};

int main() {
  const float MS_PER_PAGE = 33.0f / 8;
  for (Screen scr : {Screen::Live, Screen::Hrv, Screen::Details, Screen::Wave}) {
    Sim s; s.seconds = 70; s.ampJitter = 0.15f; s.rsa = 0.04f;
    SimFinger f(s, 7);
    HeartRateDetector hr; hr.reset();
    Spo2Estimator sp; sp.reset();
    QualityMonitor qm; qm.reset();
    WaveBuffer wave;
    HrvAnalyzer hv; hv.reset(); HrvResult hvr{};
    SimDisplay d;
    uint8_t last[1024]; bool lastValid = false;
    HrResult h{}; Spo2Result r{}; QualityResult q{};
    float ir, red; int n = 0, frames = 0, pages = 0, maxPages = 0, beatFlash = 0;
    while (f.next(ir, red)) {
      h = hr.process(ir); r = sp.process(ir, red, h); q = qm.process(h, r, ir, red); hvr = hv.process(h, q);
      if (h.beat) beatFlash = 15;
      if (beatFlash) beatFlash--;
      if (++n % 4 == 0) {  // new wave point -> new frame (25 fps)
        bool show = h.finger && !h.settling;
        wave.push(show ? h.signal : 0.0f, show && q.state != SignalState::Moving);
        if (f.t() < 10) continue;  // only measure once it's running normally
        wave.updateScale();
        UiData u{};
        u.screen = scr; u.q = q; u.bpm = h.bpm; u.spo2 = r.spo2; u.pi = r.pi; u.r = r.r;
        u.beatFlash = beatFlash > 0; u.fingerSec = 20; u.wave = &wave; u.hrv = &hvr; u.hrvHistory = &hv;
        drawScreen(d, u);
        int changed = 0;
        for (int p = 0; p < 8; p++) {
          if (!lastValid || memcmp(d.buffer() + p * 128, last + p * 128, 128) != 0) changed++;
        }
        memcpy(last, d.buffer(), 1024); lastValid = true;
        frames++; pages += changed; if (changed > maxPages) maxPages = changed;
      }
    }
    float avg = (float)pages / frames;
    const char* name = scr == Screen::Live ? "Live" : scr == Screen::Hrv ? "HRV" : scr == Screen::Details ? "Details" : "Wave";
    printf("%-8s 25 fps: %.1f of 8 pages change per frame (max %d) -> ~%.0f ms per frame, I2C busy ~%.0f%%\n",
           name, avg, maxPages, avg * MS_PER_PAGE, avg * MS_PER_PAGE * 25 / 10);
  }
}
