// Replays a raw recording (SERIAL_MODE 3, "index,ir,red" per sample) through
// the same signal processing code that runs on the ESP32.
//
//   g++ -std=c++17 -O2 -I firmware/07_guided tests/replay_raw.cpp -o replay_raw
//   ./replay_raw validation/logs/raw_pace10.txt          summary per pacer section
//   ./replay_raw validation/logs/raw_pace10.txt beats    also one line per beat
//   ./replay_raw validation/logs/raw_pace10.txt resp     also every breathing rate estimate
//
// The "# pace N from INDEX" notes in the recording say which breathing rate
// I was following from that sample on, so each section is reported separately.
// Lost samples (gaps in the index) are filled in if the gap is short (<= 5
// samples, straight line between the neighbours), otherwise the measurement
// starts again, like when the finger is lifted. So does a "# fifo reset" note.
// If the file has no pace notes but its name contains "pace<N>" (raw_pace10.txt),
// the whole file is taken as pace N.
// If the recording has a "# guided before from N" note (Phase 7), the guided
// session is run again from there and its result printed, to compare with the
// device's "# GUIDED_CSV" line.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "ppg_dsp.h"
#include "spo2.h"
#include "quality.h"
#include "hrv.h"
#include "breathing.h"
#include "guided.h"
#include <cmath>
#include <algorithm>

using namespace pulselab;

struct Section {
  int pace;                 // breaths per minute the pacer showed, 0 = off
  uint32_t fromIndex;
  uint32_t samples = 0, goodSamples = 0;
  double bpmSum = 0; uint32_t bpmCount = 0;
  HrvResult hrvAtEnd{};
  std::vector<float> resp;  // breathing rate estimates (NAN = none shown)
};

int main(int argc, char** argv) {
  if (argc < 2) { printf("usage: replay_raw file.txt [beats]\n"); return 1; }
  bool printBeats = (argc > 2 && strcmp(argv[2], "beats") == 0);
  bool printResp = (argc > 2 && strcmp(argv[2], "resp") == 0);
  FILE* f = fopen(argv[1], "r");
  if (!f) { printf("can't open %s\n", argv[1]); return 1; }

  HeartRateDetector hr; hr.reset();
  Spo2Estimator sp; sp.reset();
  QualityMonitor qm; qm.reset();
  HrvAnalyzer hv; hv.reset();
  BeatFeatureTracker bt; bt.reset();
  static BreathRateEstimator br;   // static: ~6 KB of buffers
  br.reset();
  auto resetAll = [&]() { hr.reset(); sp.reset(); qm.reset(); hv.reset(); bt.reset(); br.reset(); };

  static GuidedSession gs;
  bool guidedSeen = false;
  int filePace = 0;
  if (const char* pp = strstr(argv[1], "pace")) filePace = atoi(pp + 4);
  std::vector<Section> sections;
  auto newSection = [&](int pace, uint32_t from) { Section sec; sec.pace = pace; sec.fromIndex = from; sections.push_back(sec); };
  newSection(filePace, 0);
  uint32_t total = 0, lost = 0, filled = 0, restarts = 0, badLines = 0, fifoResets = 0;
  long lastIndex = -1;
  float lastIr = 0, lastRed = 0;

  auto feed = [&](float ir, float red, uint32_t index) {
    HrResult h = hr.process(ir);
    Spo2Result s = sp.process(ir, red, h);
    QualityResult q = qm.process(h, s, ir, red);
    hv.process(h, q);
    gs.process(h, q);
    BeatFeature bf;
    if (bt.process(ir, h, q, bf)) br.addBeat(bf);
    if (!h.finger) br.reset();
    Section& sec = sections.back();
    if (br.updateNow()) {
      const BreathResult& b = br.result();
      sec.resp.push_back(b.valid ? b.rate : NAN);
      if (printResp)
        printf("%7.1f s  breathing %s%5.1f /min (agree %u) | timing %4.1f%s height %4.1f%s baseline %4.1f%s\n", index / 100.0,
               b.valid ? "" : "--", b.valid ? b.rate : 0.0f, b.agree,
               b.seriesRate[0], b.seriesOk[0] ? "" : "x", b.seriesRate[1], b.seriesOk[1] ? "" : "x", b.seriesRate[2], b.seriesOk[2] ? "" : "x");
    }
    sec.samples++;
    if (q.state == SignalState::Good) sec.goodSamples++;
    if (q.showBpm && h.bpm > 0) { sec.bpmSum += h.bpm; sec.bpmCount++; }
    sec.hrvAtEnd = hv.result();
    if (printBeats && h.beat && h.ibiMs > 0) {
      const HrvResult& v = hv.result();
      printf("%7.2f s  Beat %4u ms %-3s BPM %5.1f | %-12s | HRV %s%.1f\n", index / 100.0, h.ibiMs, h.ibiValid ? "ok" : "rej",
             h.bpm, stateText(q.state), v.ready ? "" : "(not ready) ", v.rmssd);
    }
    if (printBeats && h.secondBump) printf("%7.2f s  (skipped 2nd bump)\n", index / 100.0);
  };

  char line[256];
  while (fgets(line, sizeof line, f)) {
    if (line[0] == '#') {
      int pace; unsigned long from;
      if (sscanf(line, "# pace %d from %lu", &pace, &from) == 2) newSection(pace, (uint32_t)from);
      if (strstr(line, "fifo reset")) { fifoResets++; resetAll(); }
      if (strstr(line, "# guided before")) { gs.start(180); guidedSeen = true; }
      if (strstr(line, "# guided cancelled")) gs.stop();
      if (strstr(line, "GUIDED_CSV")) printf("device: %s", line + 2);
      continue;
    }
    unsigned long index; unsigned ir, red;
    if (sscanf(line, "%lu,%u,%u", &index, &ir, &red) != 3) { if (line[0] >= '0' && line[0] <= '9') badLines++; continue; }

    if (lastIndex >= 0 && (long)index != lastIndex + 1) {
      long gap = (long)index - lastIndex - 1;
      if (gap > 0) {
        lost += gap;
        if (gap <= 5) {  // short gap: fill with a straight line
          for (long k = 1; k <= gap; k++) {
            float t = (float)k / (gap + 1);
            feed(lastIr + t * (ir - lastIr), lastRed + t * (red - lastRed), lastIndex + k);
            filled++;
          }
        } else {
          resetAll();
          restarts++;
        }
      }
    }
    feed((float)ir, (float)red, index);
    total++;
    lastIndex = (long)index; lastIr = (float)ir; lastRed = (float)red;
  }
  fclose(f);

  if (guidedSeen) {
    const GuidedResult& g = gs.result();
    printf("replay: GUIDED_CSV,180,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%u  (phase at the end: %s)\n", g.before.bpm, g.during.bpm, g.after.bpm,
           g.before.rmssd, g.during.rmssd, g.after.rmssd, g.swing, g.swingBreaths, guidedPhaseText(g.phase));
  }
  printf("\n%s: %u samples (%.1f s), lost %u (%u filled in, %u restarts), broken lines %u, FIFO resets %u\n\n",
         argv[1], total, total / 100.0, lost, filled, restarts, badLines, fifoResets);
  printf("%-10s %8s %8s %8s %9s | %s\n", "pacer", "length", "good", "avg BPM", "RMSSD", "breathing rate (every 5 s after the first 60 s)");
  for (const Section& s : sections) {
    if (s.samples == 0) continue;
    char pace[16];
    if (s.pace) snprintf(pace, sizeof pace, "%d /min", s.pace); else snprintf(pace, sizeof pace, "off");
    printf("%-10s %6.0f s %7.0f%% %8.1f %6.1f ms | ", pace, s.samples / 100.0, 100.0 * s.goodSamples / s.samples,
           s.bpmCount ? s.bpmSum / s.bpmCount : 0.0, s.hrvAtEnd.rmssd);
    std::vector<float> shown;
    for (float r : s.resp) if (!std::isnan(r)) shown.push_back(r);
    if (s.resp.empty()) { printf("no estimate (needs 60 s of good signal)\n"); continue; }
    printf("%zu estimates, shown %3.0f%%", s.resp.size(), 100.0 * shown.size() / s.resp.size());
    if (!shown.empty()) {
      std::sort(shown.begin(), shown.end());
      printf(", median %.1f /min", shown[shown.size() / 2]);
      if (s.pace) {
        int within = 0; double err = 0;
        for (float r : shown) { within += fabsf(r - s.pace) <= 2.0f; err += fabsf(r - s.pace); }
        printf(", within 2 of %d: %3.0f%%, mean error %.1f", s.pace, 100.0 * within / shown.size(), err / shown.size());
      }
    }
    printf("\n");
  }
  return 0;
}
