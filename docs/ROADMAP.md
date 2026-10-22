# Plan

I'm building this in phases. After each one works I commit it, then move to the next.

## Phase 0: check modules (done)

Make sure the sensor and OLED show up on I2C. Had to fix the 1.8V pull-up problem on the MAX30100 board first (see DEBUG_LOG.md).

Done when: scan shows 0x3C and 0x57 only.

## Phase 1: raw signal (done)

Set the sensor to SpO2 mode, 100 Hz, 1600us pulse width, IR LED 50mA, red LED 27.1mA. Print raw IR/red and a DC-removed IR signal to the Serial Plotter.

Done when: I can see my pulse as a repeating wave.

## Phase 2: filtering and heart rate

- remove the DC part of the signal
- band-pass filter around 0.5-4 Hz (30-240 BPM). Plan is a Butterworth IIR, coefficients calculated in Python with scipy and copied into the code
- detect peaks with an adaptive threshold, ignore anything within ~250 ms of the last beat
- BPM from the time between beats, averaged over the last few beats

Done when: BPM is within about 3 of my phone/oximeter while sitting still.

## Phase 3: SpO2 and perfusion index

For each beat, find AC (peak to trough) and DC (average) of red and IR.

```
R = (AC_red / DC_red) / (AC_ir / DC_ir)
SpO2 ≈ 110 - 25 * R      (starting formula, will calibrate in Phase 11)
PI = AC_ir / DC_ir * 100
```

Done when: SpO2 shows 95-100% and stays steady.

## Phase 4: OLED, button, signal quality

- finger detection, and detect clipping and finger movement
- signal quality bar, don't show numbers when the signal is bad
- screens: live waveform + BPM/SpO2, HRV/stress, breathing mode, settings
- button: short press changes screen, long press starts/stops a session (state machine)
- OLED is SH1106 so I'll use U8g2

Done when: it says "place finger" or "hold still" instead of showing wrong values.

## Phase 5: HRV and stress

Keep the beat-to-beat intervals from the last ~60 s, throw out ones that are way off (more than ~20% from the median), then calculate RMSSD and SDNN. Map RMSSD to a simple 4-level stress indicator. It's only an indicator, not a medical measure.

Done when: RMSSD goes up when I do slow deep breathing.

Done (20 Oct 2026): about 36 ms sitting, about 60 ms with slow breathing. See `phase5_hrv.md`.

## Phase 6: breathing rate

Breathing slightly changes the PPG baseline, the pulse height and the time between beats. Take one or more of those, filter around 0.1-0.5 Hz (6-30 breaths per min), then count peaks or use FFT over 30-60 s.

Done when: within about 2 breaths/min of counting my own breaths.

Plan (20 Oct 2026): first record real raw data with a breathing pacer on the OLED, so the true rate is known, then build and tune the estimator on those recordings ([Karlen 2013 "smart fusion"](REFERENCES.md)). See `phase6_breathing.md`.

Done (20 Oct 2026): runs on the device; every shown rate within 2 /min of the pacer at 6/10/15/20, and free breathing 31.3 /min vs ~32 counted. Weak spot: right after exercise (high, falling heart rate) it often shows "--".

## Phase 7: guided breathing mode

Breathing animation on the OLED (in 4 s, out 6 s) with BPM and RMSSD updating live, and a before/after result at the end. Planning to use this for the demo.

Built (20 Oct 2026): 1 min normal / 3 min at 6 per minute / 1 min normal, with heart rate and HRV for each part, and the heart rate swing per breath shown live instead of RMSSD (RMSSD needs 30 s of data, the swing changes every breath, which is more useful while doing it). See `phase7_guided.md`.

Done (20 Oct 2026): first session on the device, HRV 13 -> 28 -> 13 ms, and the PC replay matches the device exactly.

## Phase 8: BLE + web dashboard

- standard BLE Heart Rate Service so fitness apps can read BPM
- my own BLE service for SpO2, PI, RMSSD, breathing rate, signal quality and the waveform
- a web page using Web Bluetooth (works in Chrome) with live graph and CSV download
- split the firmware into FreeRTOS tasks: sampling, processing, display, BLE

Built (20 Oct 2026), see `phase8_ble.md`. The FreeRTOS split is not done on purpose: NimBLE already runs in its own task, and in the single loop the slowest parts are known (OLED frame ~35 ms, breathing step ~29 ms, FIFO full after 160 ms). The status line now prints `loop max`; I'll only split it into tasks if that shows a real problem with Bluetooth running. Splitting adds locking between tasks, which is a new way to get bugs.

## Phase 9: battery

Battery voltage on GPIO3 through a divider, battery % on screen, deep sleep when idle. Measure the actual current and work out battery life.

**Skipped (22 Oct 2026).** PulseLab runs from USB, which is enough for a desk device and the demo. The board layout in Phase 10 has no battery, charger or switch. Moved to future work; GPIO3 is still free for the divider.

## Phase 10: zero PCB and case

Plan the layout on paper first, female headers for the ESP32 and sensor. Case in Fusion 360 with a finger slot that blocks light. STL/STEP files go in `enclosure/`.

Done when: readings in the case are as good as on the breadboard.

Option to decide here: replace the 1.3" OLED with a 2" colour TFT (ST7789, 240x320, SPI). Notes from checking it (20 Oct 2026):
- only `ui.h` (the drawing) would change, the signal processing stays the same
- needs 5 pins, likely SCK GPIO5, MOSI GPIO0, DC GPIO1, CS GPIO20, RST GPIO21, backlight to 3.3V (confirm with the actual module)
- moves the display off the sensor's I2C bus
- library: LovyanGFX (compiled fine for the ESP32-C3 with core 3.3.12), using a sprite for the wave
- costs more power (backlight ~40-60 mA vs ~10-20 mA) and needs a bigger case

**Done (22 Oct 2026).** Stayed with the OLED. Zero PCB soldered (61 x 48 mm, point-to-point wiring, modules on female headers). See `phase10_build.md` and `images/after_zeropcb.jpeg`. The case is designed and checked by `enclosure/case.py`, but not printed for the submission: building it is future work. The "zero PCB vs breadboard" readings will be filled in together with Phase 11.

## Phase 11: accuracy test

Take readings with PulseLab and a commercial oximeter at the same time. Aim for 30+ readings, a few people, resting and after light exercise. Fill `validation/readings_template.csv` and run `python validation/analyze.py readings.csv`.

It gives mean error, bias and Bland-Altman plots. Use the first set to calibrate SpO2, then test again on new readings (not the same ones used for calibrating).

**Pending, to do before submission.** `analyze.py` now also fits the SpO2 calibration on the `cal` readings and tests it on the `test` readings, and takes R from the Serial beat line. Checked on simulated readings only (`validation/sim/`, clearly marked, not results). Procedure: `phase11_accuracy.md`.

## Phase 12: report and demo

README with photos, a GIF of the screen and dashboard, block diagram and results. Report with theory, design, testing and limitations (sensor accuracy, motion, skin tone effects on optical sensors, not a medical device).

**Done except the Phase 11 numbers (22 Oct 2026).** README updated with the board photo, block diagram and results. Report written; the accuracy section has empty slots for the Phase 11 results.

## Future work

- battery, charging and deep sleep (Phase 9)
- print and fit the 3D case (design already in `enclosure/`)
- SpO2 calibration over a wider range (needs a lab with controlled desaturation)
- testing on more people, including different skin tones
- vibration motor for the breathing pacer
- 2" colour TFT (notes in Phase 10)
