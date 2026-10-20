# Tests

These run on a PC, not on the ESP32. They feed a simulated finger (`sim_finger.h`) into the same signal processing code the device uses, so I can test things that are hard to do by hand (exact heart rates, known SpO2, a bump at exactly 20 s, ...).

Run from the repo root:

```
g++ -std=c++17 -O2 -I firmware/06_breathing tests/test_heart_rate.cpp -o test_hr && ./test_hr
g++ -std=c++17 -O2 -I firmware/06_breathing tests/test_spo2.cpp -o test_spo2 && ./test_spo2
g++ -std=c++17 -O2 -I firmware/06_breathing tests/test_quality.cpp -o test_quality && ./test_quality
g++ -std=c++17 -O2 -I firmware/06_breathing tests/test_hrv.cpp -o test_hrv && ./test_hrv
g++ -std=c++17 -O2 -I firmware/06_breathing tests/test_breathing.cpp -o test_breathing && ./test_breathing
```

(The tests use the newest firmware folder. Older folders still work with the tests from their own commit.)

Raw recordings (Phase 6, `SERIAL_MODE 3`):

```
g++ -std=c++17 -O2 -I firmware/06_breathing tests/replay_raw.cpp -o replay_raw
./replay_raw validation/logs/raw_pace6.txt          summary per pacer section
./replay_raw validation/logs/raw_pace6.txt beats    plus one line per beat

g++ -std=c++17 -O2 -I tests tests/make_sim_raw.cpp -o make_sim_raw
./make_sim_raw sim_raw.txt                          fake recording to try the tools without the board
python validation/plot_raw.py sim_raw.txt 60 100
```

Replay a recorded Serial log through the HRV code:

```
g++ -std=c++17 -O2 -I firmware/06_breathing tests/replay_log.cpp -o replay_log
./replay_log validation/logs/phase3.txt
./replay_log validation/logs/hrv_slow.txt all
```

Without `all`, intervals marked "rej" in the log leave a gap. With `all`, every interval is used and the HRV code decides. That's for logs recorded with the old Phase 5 code, which rejected real intervals during slow breathing.

The screen renders need `-I tests` as well (they use the simulated finger).

On Windows this works with MinGW g++ or in WSL.

## Screen renders

`render_screens.cpp` draws the OLED screens into image files using the real U8g2 code:

```
git clone --depth 1 https://github.com/olikraus/u8g2.git
gcc -O2 -c u8g2/csrc/*.c
g++ -std=c++17 -O2 -I u8g2/csrc -I firmware/06_breathing -I tests tests/render_screens.cpp *.o -o render
mkdir -p screens/movement && ./render screens
pip install pillow
python tests/screens_to_png.py screens docs/images/phase6_screens.png
```

(`docs/images/phase5_hrv_screens.png` is the same thing with only the four HRV screens copied into their own folder.)
