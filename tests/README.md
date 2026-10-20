# Tests

These run on a PC, not on the ESP32. They feed a simulated finger (`sim_finger.h`) into the same signal processing code the device uses, so I can test things that are hard to do by hand (exact heart rates, known SpO2, a bump at exactly 20 s, ...).

Run from the repo root:

```
g++ -std=c++17 -O2 -I firmware/04_display tests/test_heart_rate.cpp -o test_hr && ./test_hr
g++ -std=c++17 -O2 -I firmware/04_display tests/test_spo2.cpp -o test_spo2 && ./test_spo2
g++ -std=c++17 -O2 -I firmware/04_display tests/test_quality.cpp -o test_quality && ./test_quality
```

On Windows this works with MinGW g++ or in WSL.

## Screen renders

`render_screens.cpp` draws the OLED screens into image files using the real U8g2 code:

```
git clone --depth 1 https://github.com/olikraus/u8g2.git
gcc -O2 -c u8g2/csrc/*.c
g++ -std=c++17 -O2 -I u8g2/csrc -I firmware/04_display tests/render_screens.cpp *.o -o render
mkdir screens && ./render screens
pip install pillow
python tests/screens_to_png.py screens docs/images/phase4_screens.png
```
