// Writes a fake raw recording (same format as SERIAL_MODE 3) from the simulated
// finger, so the PC tools can be tried without the board:
//
//   g++ -std=c++17 -O2 -I tests tests/make_sim_raw.cpp -o make_sim_raw
//   ./make_sim_raw sim_raw.txt
//
// 4 sections of 60 s: pacer off (15/min), 6, 10 and 20 per minute.
// A few samples are dropped on purpose to test the gap handling.

#include <cstdio>
#include "sim_finger.h"

int main(int argc, char** argv) {
  if (argc < 2) { printf("usage: make_sim_raw out.txt\n"); return 1; }
  FILE* f = fopen(argv[1], "w");
  fprintf(f, "# PulseLab Phase 6 raw recording: index,ir,red at 100 Hz (SIMULATED)\n");
  struct { int pace; float rate; } parts[] = {{0, 0.25f}, {6, 0.1f}, {10, 10 / 60.0f}, {20, 20 / 60.0f}};
  unsigned long index = 0;
  unsigned seed = 1;
  for (auto& p : parts) {
    if (p.pace) fprintf(f, "# pace %d from %lu\n", p.pace, index);
    Sim s; s.seconds = 60; s.bpm = 72; s.breathRate = p.rate; s.rsa = 0.04f + 0.4f * (0.25f - p.rate) * (p.rate < 0.25f);
    s.breathing = 0.004f; s.noiseIr = 10;
    SimFinger sim(s, seed++);
    float ir, red;
    while (sim.next(ir, red)) {
      index++;
      if (index % 2000 == 0) { index += 3; continue; }   // drop 3 samples every 20 s
      fprintf(f, "%lu,%d,%d\n", index, (int)ir, (int)red);
    }
  }
  fclose(f);
  printf("wrote %s\n", argv[1]);
  return 0;
}
