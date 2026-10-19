# PulseLab Build Roadmap

Each phase builds on the previous one. Finish a phase, commit it to GitHub with a clear message, then move on. Your commit history becomes proof of how you built it.

Every phase lists: **Goal**, **Done when** (a test you can actually check), and **You learn**.

---

## Stage A — Basics

### Phase 0: Module check and pull-up fix
**Goal:** Confirm the MAX30100 and OLED respond on I²C.
- Many purple MAX30100 boards pull SDA/SCL up to 1.8 V instead of 3.3 V. If the I²C scan doesn't find `0x57`, remove the three 4.7 kΩ pull-up resistors on the module and add 4.7 kΩ pull-ups from SDA and SCL to 3.3 V.
- Run the scan in `firmware/01_sensor_bringup`.

**Done when:** the scan reports `0x57` (MAX30100) and `0x3C` (OLED).
**You learn:** I²C addressing and pull-ups, reading a schematic, debugging hardware with a multimeter.

### Phase 1: Raw signal bring-up
**Goal:** See your heartbeat in the raw IR signal.
- Configure: SpO₂ mode, 100 Hz sampling, 1600 µs pulse width (16-bit), IR LED 50 mA, red LED 27.1 mA.
- Stream raw IR and red values, plus a simple DC-removed IR, to the Serial Plotter.

**Done when:** the DC-removed IR trace shows a clear repeating pulse with your finger resting still.
**You learn:** what PPG is, why IR and red light are used, sampling rate, ADC resolution, LED current trade-offs.

---

## Stage B — Signal processing (the core of the project)

### Phase 2: Filtering and heart rate
**Goal:** Reliable BPM computed by your own code.
- DC removal (high-pass, e.g. exponential moving average).
- Band-pass ~0.5–4 Hz (30–240 BPM). Start with a 2nd-order Butterworth IIR; design the coefficients in Python (`scipy.signal.butter`) and paste them into firmware.
- Peak detection with an adaptive threshold and a refractory period (~250 ms) so one beat isn't counted twice.
- Inter-beat interval (IBI) → BPM, averaged over the last ~8 beats.

**Done when:** BPM stays within ±3 of a reference device while sitting still.
**You learn:** digital filter design (IIR vs FIR), frequency response, real-time DSP on a microcontroller, peak detection.

### Phase 3: SpO₂ and perfusion index
**Goal:** Oxygen saturation from first principles.
- For each beat, compute AC (peak-to-trough) and DC (mean) for both red and IR.
- Ratio of ratios: `R = (AC_red / DC_red) / (AC_ir / DC_ir)`.
- Start with the empirical formula `SpO₂ ≈ 110 − 25·R`, then refine the constants using your Phase 11 data.
- Perfusion index: `PI = AC_ir / DC_ir × 100`.

**Done when:** SpO₂ reads 95–100% on a healthy finger and stays stable (±1%).
**You learn:** Beer–Lambert law, how oxy- and deoxyhaemoglobin absorb light differently, empirical calibration.

### Phase 4: Signal quality and OLED UI
**Goal:** A device that behaves well, not just works.
- Finger detection (IR DC threshold), clipping detection (ADC near max), motion detection (sudden DC jumps).
- Signal-quality score 0–100 shown as a bar. Hide values when quality is low.
- OLED screens: live scrolling waveform + BPM/SpO₂, HRV/stress, breathing coach, settings.
- Button: short press = next screen, long press = start/stop session. Implement as a **state machine**.

**Done when:** removing your finger shows "Place finger"; moving it shows "Hold still" instead of wrong numbers.
**You learn:** robustness thinking, state machines, embedded UI design, frame-rate vs CPU budget.

---

## Stage C — Advanced features

### Phase 5: HRV and stress score
**Goal:** Extract nervous-system information from beat timing.
- Store IBIs in a ring buffer (~60 s).
- Reject artifacts: discard IBIs that differ more than ~20% from the local median.
- Compute **RMSSD** and **SDNN**.
- Map RMSSD to a simple 4-level stress indicator, and document clearly that this is an indicator, not a clinical measure.

**Done when:** RMSSD rises noticeably during slow, deep breathing compared with normal breathing.
**You learn:** time-domain HRV, autonomic nervous system basics, outlier rejection, ring buffers.

### Phase 6: Breathing rate
**Goal:** Estimate breaths per minute from the pulse sensor alone.
- Breathing modulates the PPG in three ways: baseline (DC) drift, pulse amplitude, and IBI (respiratory sinus arrhythmia).
- Extract one or more of these series, low-pass/band-pass ~0.1–0.5 Hz (6–30 breaths/min), then count peaks or use an FFT over a 30–60 s window.

**Done when:** within ±2 breaths/min of counting your own breaths for a minute.
**You learn:** signal modulation, resampling unevenly spaced data, FFT and spectral analysis, sensor fusion.

### Phase 7: Guided breathing mode
**Goal:** The demo moment.
- OLED animation: inhale 4 s, exhale 6 s (~6 breaths/min).
- Show BPM and RMSSD updating live, plus a before/after summary at the end.

**Done when:** a 2-minute session shows a measurable before/after change.
**You learn:** biofeedback, UX design for a physical device.

### Phase 8: BLE + Web Bluetooth dashboard
**Goal:** Wireless live data with no app install.
- Expose the standard **Heart Rate Service (0x180D)** so fitness apps can read BPM.
- Add a custom GATT service for SpO₂, PI, RMSSD, breathing rate, signal quality and a downsampled waveform.
- Build `dashboard/index.html` using the Web Bluetooth API (Chrome on Android/desktop): connect button, live waveform, metric cards, CSV export of a session.
- Use FreeRTOS tasks: sampling (highest priority), processing, display, BLE.

**Done when:** the dashboard connects from a phone and plots the live waveform smoothly.
**You learn:** BLE GATT (services, characteristics, notifications), web development, RTOS task design and priorities.

### Phase 9: Battery and power management
**Goal:** Portable and efficient.
- Battery voltage via 100k/100k divider on GPIO3 → battery % on screen.
- Reduce LED current when idle; deep sleep after inactivity, wake on button.
- Measure actual current draw and calculate battery life.

**Done when:** you can state measured runtime on one charge in your report.
**You learn:** Li-ion safety, voltage dividers, ADC calibration, power budgeting, sleep modes.

---

## Stage D — Build and prove it

### Phase 10: Perfboard build and enclosure
- Plan the perfboard layout on paper or in Fritzing first. Use female headers for the ESP32 and sensor.
- Design the enclosure in Fusion 360: two-part shell, OLED window, USB-C slot, button cap, **light-blocking finger cradle** with the sensor flush to the surface.
- Matte black interior around the sensor to block ambient light.
- Export STEP and STL into `enclosure/`.

**Done when:** readings are as good or better inside the enclosure than on the breadboard.
**You learn:** CAD, design for manufacturing (tolerances, wall thickness, print orientation), soldering, cable management.

### Phase 11: Validation study (Level 4)
**Goal:** Prove how accurate it is.
- Take simultaneous readings with PulseLab and a commercial fingertip oximeter.
- Aim for 30+ paired readings across several people and conditions (rest, after light exercise).
- Fill `validation/readings_template.csv` and run `python validation/analyze.py`.
- Report mean absolute error, bias, and 95% limits of agreement (Bland–Altman).
- Use the data to recalibrate the SpO₂ constants from Phase 3, then validate again on **new** readings.

**Done when:** you have Bland–Altman plots and an honest results table in the README.
**You learn:** experimental design, statistics for measurement agreement, why correlation alone is a misleading accuracy metric, avoiding calibrating and testing on the same data.

### Phase 12: Documentation and demo
- README with photos, a GIF of the OLED waveform and the dashboard, a block diagram, and the results table.
- Project report: problem, theory, design, implementation, validation, limitations, future work.
- Limitations section: MAX30100 accuracy limits, motion sensitivity, skin tone and perfusion effects on optical sensors, not a medical device.

**You learn:** technical writing and presenting engineering work honestly.
