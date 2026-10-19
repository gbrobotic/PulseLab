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

## Phase 6: breathing rate

Breathing slightly changes the PPG baseline, the pulse height and the time between beats. Take one or more of those, filter around 0.1-0.5 Hz (6-30 breaths per min), then count peaks or use FFT over 30-60 s.

Done when: within about 2 breaths/min of counting my own breaths.

## Phase 7: guided breathing mode

Breathing animation on the OLED (in 4 s, out 6 s) with BPM and RMSSD updating live, and a before/after result at the end. Planning to use this for the demo.

## Phase 8: BLE + web dashboard

- standard BLE Heart Rate Service so fitness apps can read BPM
- my own BLE service for SpO2, PI, RMSSD, breathing rate, signal quality and the waveform
- a web page using Web Bluetooth (works in Chrome) with live graph and CSV download
- split the firmware into FreeRTOS tasks: sampling, processing, display, BLE

## Phase 9: battery

Battery voltage on GPIO3 through a divider, battery % on screen, deep sleep when idle. Measure the actual current and work out battery life.

## Phase 10: zero PCB and case

Plan the layout on paper first, female headers for the ESP32 and sensor. Case in Fusion 360 with a finger slot that blocks light. STL/STEP files go in `enclosure/`.

Done when: readings in the case are as good as on the breadboard.

## Phase 11: accuracy test

Take readings with PulseLab and a commercial oximeter at the same time. Aim for 30+ readings, a few people, resting and after light exercise. Fill `validation/readings_template.csv` and run `python validation/analyze.py readings.csv`.

It gives mean error, bias and Bland-Altman plots. Use the first set to calibrate SpO2, then test again on new readings (not the same ones used for calibrating).

## Phase 12: report and demo

README with photos, a GIF of the screen and dashboard, block diagram and results. Report with theory, design, testing and limitations (sensor accuracy, motion, skin tone effects on optical sensors, not a medical device).
