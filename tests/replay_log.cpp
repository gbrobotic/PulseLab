// Replays a recorded Serial log (from validation/capture_serial.py) through
// the HRV code, so real recordings can be checked on the PC.
//
//   g++ -std=c++17 -O2 -I firmware/05_hrv tests/replay_log.cpp -o replay_log
//   ./replay_log validation/logs/phase3.txt          rejected intervals ("rej") count as gaps
//   ./replay_log validation/logs/hrv_slow.txt all     use every interval, let the HRV check decide
//
// Uses the "Beat 812 ms ok" lines. Note: logs recorded before Phase 5 have
// intervals rounded to 10 ms. "all" is useful for logs recorded with older
// code that wrongly rejected real intervals (see docs/phase5_hrv.md).

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "hrv.h"

using namespace pulselab;

int main(int argc, char** argv) {
  if (argc < 2) { printf("usage: replay_log file.txt [all]\n"); return 1; }
  bool useAll = (argc > 2 && strcmp(argv[2], "all") == 0);
  FILE* f = fopen(argv[1], "r");
  if (!f) { printf("can't open %s\n", argv[1]); return 1; }

  HrvAnalyzer hv; hv.reset();
  char line[512];
  uint32_t t = 0;
  int beats = 0;
  float nextPrint = 20;
  printf("%-8s %-6s %-6s %-6s %-6s %-6s %s\n", "time", "RMSSD", "SDNN", "pNN50", "HR", "artif", "level");
  while (fgets(line, sizeof line, f)) {
    const char* p = strstr(line, "Beat");
    if (!p) continue;
    int ms = 0; char status[8] = {0};
    if (sscanf(p, "Beat %d ms %7s", &ms, status) != 2) continue;
    beats++;
    t += (uint32_t)(ms / 10);
    if (strcmp(status, "ok") == 0 || useAll) hv.addInterval((uint16_t)ms, t); else hv.markGap();
    float sec = t / 100.0f;
    const HrvResult& r = hv.result();
    if (sec >= nextPrint) {
      nextPrint += 20;
      if (r.ready) printf("%5.0f s   %5.1f  %5.1f  %4.0f%%  %5.1f  %3u%%   %s\n", sec, r.rmssd, r.sdnn, r.pnn50, r.meanHr, r.artifactPct, r.steady ? stressText(r.level) : "HR changing");
      else printf("%5.0f s   not ready (%.0f%%)\n", sec, r.progress * 100);
    }
  }
  fclose(f);
  const HrvResult& r = hv.result();
  printf("\nEnd of log (%d beats): RMSSD %.1f ms, SDNN %.1f ms, pNN50 %.0f%%, HR %.1f, %u clean intervals, %u%% artifacts -> %s\n",
         beats, r.rmssd, r.sdnn, r.pnn50, r.meanHr, r.intervals, r.artifactPct, r.steady ? stressText(r.level) : "HR changing");
  return 0;
}
