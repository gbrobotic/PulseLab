# Phase 8: Bluetooth (BLE) and web dashboard

Code: `firmware/08_ble/` (new: `ble_packets.h`, `ble_link.h`) and `dashboard/` (`index.html`, `pulselab.js`).

Status: built and tested on the PC (packets C++ -> JavaScript, page in a headless Chrome). Compiles for the ESP32-C3. Waiting for the first test with the real board.

## What it does

PulseLab now also sends everything over Bluetooth Low Energy, and a web page shows it live. No app needed: Chrome and Edge can talk to Bluetooth devices directly (Web Bluetooth).

![Dashboard](images/phase8_dashboard.png)

(This is the page's demo mode, with made-up data, so it can be shown without the board.)

## Two Bluetooth services

**1. The standard Heart Rate service (0x180D).** This is defined by the Bluetooth SIG, so any app that knows heart rate sensors (nRF Connect, sports apps, ...) shows PulseLab's BPM without knowing anything about it. Each beat sends the BPM and that beat's interval (RR interval, in 1/1024 s, the standard unit). It also says "finger" as the sensor location. BPM is only sent when it's shown on the OLED too, so the phone never gets a value the device itself doesn't trust.

**2. The PulseLab service** (own 128-bit UUIDs) for everything else:

| Characteristic | How often | Size | What's in it |
|---|---|---|---|
| vitals | 2x per second | 20 bytes | state, quality, BPM, SpO2, PI, RMSSD, breathing rate, progress, stress level, guided phase + time left |
| wave | every 320 ms | 18 bytes | 8 points of the filtered pulse (25 per second) + a counter to spot lost packets |
| guided | 4x per second in a session | 8 bytes | phase, paused, breathing in/out, circle size, time left, last swing |
| result | at the end of a session (and readable) | 17 bytes | HR and HRV before / slow / after, swing, breathing rate |
| control | written by the page | 1 byte | 1 = start a guided session, 2 = cancel / close |

The exact byte layout is written down at the top of `ble_packets.h`. Numbers are sent as integers (e.g. BPM x 10 as a 16-bit number) instead of text: smaller, and no parsing problems.

Every packet is **20 bytes or less**. That's the most that fits in one notification with the default BLE packet size (MTU 23). Chrome usually agrees on a bigger size, but this way it also works with apps that don't.

### On the ESP32-C3

- It uses the BLE library that comes with the ESP32 Arduino core. On the C3 that runs on **NimBLE**, a smaller Bluetooth stack.
- Bluetooth makes the program much bigger: 383 KB before, 720 KB now (54% of the flash). RAM: 37 KB of 320 KB for variables, plus what NimBLE takes while running.
- The notification descriptor (0x2902) that the older Bluedroid stack needs is added automatically by NimBLE, so `ble_link.h` only adds it for Bluedroid. (The compiler warned that adding it by hand is deprecated.)
- Nothing is packed or sent when no phone is connected.
- When the phone disconnects, advertising starts again after half a second, so it can reconnect.
- Commands from the page are only stored by the Bluetooth callback and carried out in `loop()`. The callback runs in the Bluetooth task, so this way the guided session is never changed from two places at once.
- The roadmap said to split the firmware into FreeRTOS tasks. I didn't: NimBLE already has its own task, and keeping one loop avoids locking between tasks. If `loop max` shows a problem on the board, that's when to split.
- The status line on Serial now shows `loop max` (the longest loop in the last 2 s), whether BLE is connected, and messages per second. That's to check that Bluetooth doesn't slow the loop down enough to fill the sensor FIFO (160 ms).

## The web page

`dashboard/index.html` + `dashboard/pulselab.js`, plain HTML and JavaScript, no libraries.

- 6 tiles: heart rate (the heart blinks on every beat from the Heart Rate service), SpO2, HRV with the stress level, breathing rate, signal state with quality bar, perfusion index. "--" in the same cases as on the OLED.
- The pulse wave, last 10 s, the same filtered signal as the OLED's wave screen.
- Last 5 minutes of heart rate, HRV, breathing rate or SpO2.
- Guided breathing: a Start button (it sends the command, the device runs the session), a breathing circle that follows the device's pacer, and the before / slow / after result at the end.
- Download CSV of everything received.
- Light and dark mode (follows the system), works at phone width.

![Phone](images/phase8_phone.png) ![Guided](images/phase8_guided.png)

### Opening it

Browsers only allow Bluetooth on secure pages (`https://` or `http://localhost`), in Chrome or Edge, and not on iPhone. The simplest way is GitHub Pages: in the repo, Settings > Pages > Deploy from branch `main`, folder `/ (root)`. Then it's at `https://gbrobotic.github.io/PulseLab/dashboard/`, also on an Android phone. Details in `dashboard/README.md`.

## Tests

| Test | How | Result |
|---|---|---|
| Packets: firmware vs page | `tests/test_ble_packets.cpp` packs known values with the firmware's code, `tests/test_ble_decode.js` decodes them with the page's code | all 7 packets decode to the right values (incl. clamping, negative wave values, no finger) |
| Packet size | same | all <= 20 bytes (vitals 20, wave 18, guided 8, result 17) |
| Page | headless Chrome (Playwright), demo mode, clock fast-forwarded | no JavaScript errors, no sideways scrolling at 1200 px or 390 px, guided session goes through to the result |
| Firmware | arduino-cli, ESP32-C3, Serial modes 0-3 | compiles, 54% flash |
| Everything from Phases 2-7 | the old tests on the Phase 8 code | all pass |

While testing the page I found a bug in the demo: the pulse phase was worked out as time x heart rate, but the demo heart rate changes over time, so after a minute the wave had ~30 beats in 10 s instead of 12. It now adds up the phase step by step (same idea as `nextBeat_ +=` in the simulator).

## Testing with the board

1. Upload `firmware/08_ble` (Serial Monitor mode). The status line should say `BLE advertising`.
2. **nRF Connect** (free app, Android): scan, connect to "PulseLab". You should see the Heart Rate service; switch on notifications for Heart Rate Measurement and the BPM comes in on every beat.
3. **The page**: open it (GitHub Pages or localhost), Connect, choose PulseLab. Compare the numbers with the OLED.
4. Start a guided session from the page.
5. Watch the status line for a few minutes while connected: `loop max` should stay well under 160 ms and `FIFO resets` at 0.

| Check | Result |
|---|---|
| nRF Connect sees the Heart Rate service and BPM | |
| Page connects, numbers match the OLED | |
| Guided session started from the page | |
| loop max while connected (ms) | |
| FIFO resets in 5 minutes connected | |
| Wave packets lost (shown at the bottom of the page) | |
