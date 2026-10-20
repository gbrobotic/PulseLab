// Phase 8: the Bluetooth packets. Packs known values with firmware/08_ble/ble_packets.h
// and prints them as JSON lines (bytes + what they should decode to). The web
// page's decoder (dashboard/pulselab.js) is then checked against them:
//
//   g++ -std=c++17 -O2 -I firmware/08_ble tests/test_ble_packets.cpp -o test_ble_packets
//   ./test_ble_packets | node tests/test_ble_decode.js

#include <cstdio>
#include "ble_packets.h"

using namespace pulselab;

static void hex(const uint8_t* p, int n) {
  printf("\"hex\":\"");
  for (int i = 0; i < n; i++) printf("%02x", p[i]);
  printf("\"");
}

int main() {
  uint8_t p[32];
  int n;

  n = packHeartRate(p, 72.4f, true, 812);
  printf("{\"type\":\"hr\",\"len\":%d,", n); hex(p, n); printf(",\"expect\":{\"bpm\":72,\"contact\":true,\"rrMs\":[812]}}\n");
  n = packHeartRate(p, 151.6f, false, 0);
  printf("{\"type\":\"hr\",\"len\":%d,", n); hex(p, n); printf(",\"expect\":{\"bpm\":152,\"contact\":false,\"rrMs\":[]}}\n");

  HrResult h{}; h.finger = true; h.bpm = 73.46f;
  Spo2Result s{}; s.spo2 = 97.84f; s.pi = 1.876f;
  QualityResult q{}; q.state = SignalState::Good; q.score = 93; q.showBpm = true; q.showSpo2 = false;
  HrvResult v{}; v.ready = true; v.steady = true; v.rmssd = 61.37f; v.progress = 1.0f; v.level = StressLevel::Relaxed;
  BreathResult b{}; b.valid = true; b.rate = 6.24f; b.progress = 0.5f;
  GuidedResult g{}; g.phase = GuidedPhase::Breathe; g.phaseLength = 18000; g.phaseSamples = 4150;
  packVitals(p, h, s, q, v, b, g);
  printf("{\"type\":\"vitals\",\"len\":%d,", VITALS_LEN); hex(p, VITALS_LEN);
  printf(",\"expect\":{\"version\":1,\"state\":6,\"stateText\":\"Good signal\",\"quality\":93,\"showBpm\":true,\"showSpo2\":false,"
         "\"hrvReady\":true,\"hrSteady\":true,\"breathValid\":true,\"finger\":true,\"bpm\":73.5,\"spo2\":97.8,\"pi\":1.88,"
         "\"rmssd\":61.4,\"breath\":6.2,\"hrvProgress\":100,\"breathProgress\":50,\"stress\":4,\"stressText\":\"Relaxed\","
         "\"guidedPhase\":2,\"guidedLeft\":139}}\n");

  // no finger, nothing valid, a huge value that has to be clamped
  HrResult h0{}; h0.bpm = 9999.0f; Spo2Result s0{}; QualityResult q0{}; HrvResult v0{}; BreathResult b0{}; b0.rate = 12; GuidedResult g0{};
  packVitals(p, h0, s0, q0, v0, b0, g0);
  printf("{\"type\":\"vitals\",\"len\":%d,", VITALS_LEN); hex(p, VITALS_LEN);
  printf(",\"expect\":{\"state\":0,\"stateText\":\"Place finger\",\"finger\":false,\"breathValid\":false,\"bpm\":6553.5,\"breath\":0,\"guidedPhase\":0,\"guidedLeft\":0}}\n");

  float pts[BLE_WAVE_POINTS] = {0, 123.4f, -123.6f, 32767, -32768, 40000, -40000, 7};
  packWave(p, 255, pts);
  printf("{\"type\":\"wave\",\"len\":%d,", BLE_WAVE_LEN); hex(p, BLE_WAVE_LEN);
  printf(",\"expect\":{\"seq\":255,\"points\":[0,123,-124,32767,-32768,32767,-32768,7]}}\n");

  GuidedResult gr{}; gr.phase = GuidedPhase::Breathe; gr.paused = true; gr.phaseLength = 18000; gr.phaseSamples = 17950; gr.lastSwing = 17.36f;
  PacerState ps{}; ps.perMin = 6; ps.inhale = true; ps.level = 0.5f;
  packGuided(p, gr, ps);
  printf("{\"type\":\"guided\",\"len\":%d,", GUIDED_LEN); hex(p, GUIDED_LEN);
  printf(",\"expect\":{\"phase\":2,\"phaseText\":\"slow breathing\",\"paused\":true,\"inhale\":true,\"level\":0.502,\"left\":1,\"lastSwing\":17.4,\"pace\":6}}\n");

  GuidedResult done{}; done.phase = GuidedPhase::Done;
  done.before = {95.9f, 12.9f, 70}; done.during = {92.0f, 27.6f, 270}; done.after = {95.7f, 13.1f, 95};
  done.swing = 20.9f; done.swingBreaths = 15;
  packResult(p, done, 6.2f);
  printf("{\"type\":\"result\",\"len\":%d,", RESULT_LEN); hex(p, RESULT_LEN);
  printf(",\"expect\":{\"hr\":[95.9,92,95.7],\"rmssd\":[12.9,27.6,13.1],\"swing\":20.9,\"swingBreaths\":15,\"follow\":6.2}}\n");

  printf("{\"type\":\"sizes\",\"vitals\":%d,\"wave\":%d,\"guided\":%d,\"result\":%d}\n", VITALS_LEN, BLE_WAVE_LEN, GUIDED_LEN, RESULT_LEN);
  return 0;
}
