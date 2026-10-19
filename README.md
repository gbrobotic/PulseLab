# PulseLab 🫀

**A pocket vitals analyzer built on the MAX30100 and ESP32-C3 Super Mini, with its own signal processing, HRV-based stress estimation, breathing-rate extraction, a Web Bluetooth dashboard, and accuracy validated against a commercial pulse oximeter.**

> ⚠️ PulseLab is an educational engineering project, **not a medical device**. Do not use it for diagnosis or treatment.

---

## Why this project is different

Most MAX30100 projects call a library and print a BPM number. PulseLab works from the **raw photoplethysmography (PPG) signal** and does the processing itself:

| Feature | How it works |
|---|---|
| Heart rate | DC removal → band-pass filter → adaptive-threshold peak detection |
| SpO₂ | Ratio-of-ratios of red/IR AC and DC components, calibrated empirically |
| Perfusion index | AC/DC ratio of the IR signal |
| HRV + stress score | RMSSD and SDNN from beat-to-beat intervals, after artifact rejection |
| Breathing rate | Respiratory modulation of the PPG baseline and beat intervals |
| Signal quality | Finger detection, motion/clipping detection, confidence bar |
| Guided breathing | OLED breathing coach with live HR/HRV feedback |
| Wireless | BLE (standard Heart Rate Service + custom service) and a Web Bluetooth dashboard, no app install |
| Validation | Bland–Altman analysis vs a commercial oximeter |

## Hardware

- ESP32-C3 Super Mini
- MAX30100 pulse oximeter module
- 1.3" SH1106 OLED (I²C)
- 3.7 V Li-Po + TP4056 charger (with protection)
- Vibration motor / buzzer, push button, slide switch
- Zero PCB (perfboard) + 3D-printed enclosure

Full parts list, wiring and the MAX30100 pull-up fix: [`docs/HARDWARE.md`](docs/HARDWARE.md)

Problems found during the build and how they were diagnosed: [`docs/DEBUG_LOG.md`](docs/DEBUG_LOG.md)

## Repository layout

```
PulseLab/
├── docs/
│   ├── ROADMAP.md        # Build plan, basic → advanced, with learning goals
│   ├── HARDWARE.md       # BOM, pinout, wiring, known module issues
│   ├── DEBUG_LOG.md      # Engineering log of faults found and fixed
│   └── images/           # Photos and screenshots
├── firmware/
│   └── 01_sensor_bringup/  # Phase 1: I2C scan + raw IR/Red signal
├── dashboard/            # Web Bluetooth dashboard (Phase 8)
├── enclosure/            # 3D design files (STEP/STL) (Phase 10)
├── hardware/             # Perfboard layout, photos (Phase 10)
└── validation/
    ├── readings_template.csv
    └── analyze.py        # Bland–Altman accuracy analysis
```

## Getting started

1. Install the Arduino IDE and the **esp32** board package by Espressif.
2. Board: **ESP32C3 Dev Module**, and set **USB CDC On Boot → Enabled** so Serial works over USB.
3. Install the library **MAX30100lib** by OXullo Intersecans from the Library Manager.
4. Wire the sensor as described in `docs/HARDWARE.md`.
5. Flash `firmware/01_sensor_bringup`, open **Serial Plotter** at 115200 baud, and place your finger on the sensor.

## Project status

- [x] Phase 0 — Module check and pull-up fix
- [x] Phase 1 — Raw signal bring-up
- [ ] Phase 2 — Filtering and heart rate
- [ ] Phase 3 — SpO₂ and perfusion index
- [ ] Phase 4 — OLED UI and modes
- [ ] Phase 5 — HRV and stress score
- [ ] Phase 6 — Breathing rate
- [ ] Phase 7 — Guided breathing mode
- [ ] Phase 8 — BLE + Web Bluetooth dashboard
- [ ] Phase 9 — Battery and power management
- [ ] Phase 10 — Perfboard build and enclosure
- [ ] Phase 11 — Validation study
- [ ] Phase 12 — Documentation and demo

## Results

_Validation results go here after Phase 11 (mean error, bias, limits of agreement, Bland–Altman plots)._

## License

MIT
