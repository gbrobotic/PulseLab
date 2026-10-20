# PulseLab

Pocket vitals analyzer using a MAX30100 sensor and an ESP32-C3 Super Mini. This is my minor project.

The idea is simple: instead of just using a library and printing BPM, I want to work with the raw PPG signal from the sensor and do the processing myself. From the same signal I'm planning to get heart rate, SpO2, HRV (for a stress estimate) and breathing rate, show it on an OLED and send it over BLE to a web page. At the end I'll compare my readings with a normal fingertip pulse oximeter to see how accurate it really is.

Note: this is a student project, not a medical device.

## Hardware

- ESP32-C3 Super Mini
- MAX30100 module (green board)
- 1.3" OLED, I2C (SH1106)
- 2 x 4.7k resistors (I2C pull-ups)
- Li-Po battery + TP4056 charger (later)
- Push button, vibration motor (later)
- Zero PCB + 3D printed case (later)

Wiring and parts list: [docs/HARDWARE.md](docs/HARDWARE.md)

Problems I ran into and how I fixed them: [docs/DEBUG_LOG.md](docs/DEBUG_LOG.md)

![Screens](docs/images/phase4_screens.png)

## Folders

```
docs/          plan, hardware notes, debug log, images
firmware/      Arduino sketches, one folder per phase
tests/         PC tests for the signal processing code
validation/    accuracy test script, Serial recorder, beat interval checker, raw logs
dashboard/     web bluetooth page (later)
enclosure/     3D design files (later)
hardware/      perfboard layout and photos (later)
```

## How to run

1. Arduino IDE with the esp32 board package (Espressif).
2. Board: ESP32C3 Dev Module, and turn on "USB CDC On Boot", otherwise nothing shows in Serial.
3. Install MAX30100lib by OXullo Intersecans and U8g2 by olikraus. If you have any other MAX30100 library installed, remove it (see debug log).
4. Wire it as in HARDWARE.md and upload the latest phase folder (now `firmware/04_display`).
5. Keep your finger lightly on the sensor. The OLED shows the values, and Serial Monitor (115200) shows one line per beat.

## Progress

- [x] Phase 0: check the modules, fix the I2C pull-up problem
- [x] Phase 1: get the raw signal
- [x] Phase 2: filtering and heart rate
- [x] Phase 3: SpO2 and perfusion index
- [x] Phase 4: OLED screens, button, signal quality
- [ ] Phase 5: HRV and stress score
- [ ] Phase 6: breathing rate
- [ ] Phase 7: guided breathing mode
- [ ] Phase 8: BLE + web dashboard
- [ ] Phase 9: battery
- [ ] Phase 10: zero PCB and 3D case
- [ ] Phase 11: accuracy test vs commercial oximeter
- [ ] Phase 12: final report and demo

Full plan with what each phase involves: [docs/ROADMAP.md](docs/ROADMAP.md)

Notes per phase:
- [Phase 2: filtering and heart rate](docs/phase2_heart_rate.md)
- [Phase 3: SpO2 and perfusion index](docs/phase3_spo2.md)
- [Phase 4: OLED screens, button and signal quality](docs/phase4_display.md)

PC tests and screen renders: [tests/README.md](tests/README.md)

## Results

Will add after testing.

## License

MIT
