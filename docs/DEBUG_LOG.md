# Debug log

Problems I hit while building this, and what fixed them.

## 1. Code wouldn't compile (wrong library)

19 Oct 2026, Phase 1

When I first compiled the sketch I got a lot of errors, the first one being:

```
could not convert 'sensor.MAX30100::begin(pw1600, i50, sr100)' from 'void' to 'bool'
```

and then many `has no member named 'setMode'` type errors.

The code was written for MAX30100lib by OXullo, but I also had another MAX30100 library installed. Both have a file called `MAX30100.h`, and the IDE picked the other one. You can actually see it in the error: that `begin(pw1600, i50, sr100)` doesn't exist in OXullo's library.

Fix: removed the other library, and added `#include "MAX30100_PulseOximeter.h"` at the top of the sketch. That file is only in OXullo's library, so now the IDE always picks the right one.

## 2. I2C scan finding devices at every address

19 Oct 2026, Phase 0

After it compiled, the I2C scan printed "Found device" for almost every address from 0x34 to 0x7D, the scan took a few seconds, and at the end it said `MAX30100 not found`. Only two devices were connected (OLED and MAX30100), so this was clearly wrong.

What I checked:

1. Removed the MAX30100 and kept only the OLED. The scan showed only `0x3C`. So the OLED, the wiring and the ESP32 were fine, and the problem was the sensor board.
2. Powered only the MAX30100 (VIN and GND) and measured SDA to GND with a multimeter. It read about 1.8V.

Why this happens: the pull-up resistors on this board are connected to the board's own 1.8V regulator instead of 3.3V. The ESP32-C3 needs around 2.5V (0.75 x 3.3V) to read a pin as HIGH, so 1.8V looks like LOW all the time. During the scan, SDA being low looks like an ACK, so every address seems to answer.

There were two ways to fix it (I found both in a YouTube video):

- Cut the trace from the 1.8V regulator to the pull-ups and connect them to 3.3V instead.
- Remove the three pull-up resistors and put external pull-ups to 3.3V.

I went with removing the resistors. The 1.8V regulator also powers the MAX30100 chip itself, so if the trace cut went wrong I could put 3.3V on the chip and kill it. The SDA/SCL pins themselves are fine with 3.3V.

What I did:

- Found the three resistors using continuity mode on the multimeter (one probe on SDA/SCL/INT pin, other probe on the resistor ends)
- Desoldered all three
- Added 4.7k from SDA (GPIO6) to 3.3V and 4.7k from SCL (GPIO7) to 3.3V

After the fix SDA and SCL both read about 3.3V, and the scan shows only `0x3C` (OLED) and `0x57` (MAX30100).

![I2C scan after the fix](images/phase1_i2c_scan_fixed.png)

First pulse in the Serial Plotter:

![Raw PPG signal](images/phase1_raw_ppg_plot.png)

Note to self: add before/after photos of the board (`images/module_before.jpg`, `images/module_after.jpg`).

## 3. Serial Plotter only showing part of a heartbeat

19 Oct 2026, Phase 1

The plot looked like one slow wave instead of repeating pulses. The Arduino IDE 2 plotter only shows the last 50 points, and at 100 samples per second that's only half a second, less than one heartbeat. Changed the sketch to print every 4th sample so about 2 seconds fit on screen. The filter still runs on every sample, only the printing is reduced.

## 4. Big jump in the signal after placing the finger

19 Oct 2026, Phase 2

On the first real run the beat marker in the plotter was about 10x taller than the pulse. When the finger goes on, the IR value jumps from ~150 to ~46,000 in a fraction of a second. The DC average (1 s time constant) takes a few seconds to catch up, and the filter turns that into a big swing that was still there after the 2 s settling time. I simulated it in the PC test: the signal after settling was up to 5x bigger than a normal pulse.

Fix: during the 2 s settling time the DC average follows 10x faster, then goes back to normal. In the simulation the leftover jump is gone, and BPM appears about 4-5 s after placing the finger. The plotter also ignores the settling time when scaling the beat marker. Added this case as test 8 in `tests/test_heart_rate.cpp`.

## 5. Wrong first BPM / beats missed after moving the finger

19 Oct 2026, Phase 2

In my screen recording the first BPM was 85.7 and then it slowly climbed to ~105. I tried to reproduce it in the PC simulation: a short bump (finger pressed harder for 150 ms) right after settling made the beat threshold jump up, and with its slow decay, beats were missed for 6-9 s. In one case the first BPM came out as 118 instead of 105.

What I tried:
- Limiting how fast the threshold can rise. It fixed the bump case, but then weak signals got extra beats counted and at 45 BPM it started counting the second wave of each pulse. Removed it.
- Making the threshold drop faster only when beats stop coming (no beat for 1.5x the normal interval). This was enough on its own. Weak signals behave like before.
- Starting the threshold from the pulse size measured during settling instead of zero.
- Not showing the first BPM until the first 3 intervals agree.

Now a bump costs about 2 s of missed beats instead of 6-9 s, and the first BPM is correct. Added as test 9.

## 6. "MAX30100 not found" right after uploading

19 Oct 2026, Phase 2

After uploading, the sketch printed "MAX30100 not found" and stopped. After unplugging and plugging the USB back in, it worked.

Likely reason: the ESP32 was reset in the middle of talking to the sensor. The sensor was still powered and waiting for the rest of the transfer, holding SDA low. Removing power resets the sensor, which is why replugging fixed it.

Fix: before starting I2C, the sketch now sends up to 9 clock pulses on SCL and a STOP condition (standard I2C bus recovery). It also keeps retrying instead of stopping forever. I haven't been able to confirm this fixes it yet, since it doesn't happen every time. If it happens again, check whether it now recovers by itself.

## 7. Nothing at all on Serial Monitor / Plotter with the Phase 3 sketch

20 Oct 2026, Phase 3

Uploaded fine, but no output at all, not even zeros or "MAX30100 not found". Phase 2 had worked the same morning.

Cause: Tools > USB CDC On Boot was set to Disabled for the new sketch. On the ESP32-C3 that sends `Serial` to the UART pins (GPIO20/21) instead of USB, so the IDE gets nothing. Setting it to Enabled fixed it.

So I don't have to guess next time, the sketch now flashes the onboard LED (GPIO8) on every heartbeat. If the LED flashes but Serial is empty, the code is running and it's a Serial setting.

## 8. Sensor library stops reading if its FIFO is ever full

20 Oct 2026, Phase 4 (found by reading the library code, before it happened)

Before adding the OLED I checked how MAX30100lib reads the sensor. It calculates the number of samples waiting as `(write pointer - read pointer) & 15`. The FIFO holds 16 samples. If it's completely full, the two pointers are equal, the result is 0, and it never reads again, so the data would just stop.

At 100 Hz the FIFO is full after 160 ms. The OLED update uses the same I2C bus and takes time, and Serial printing can block for ~100 ms per line if USB is connected but no Serial Monitor is open.

What I did in Phase 4:
- OLED redrawn at 10 fps and the frame time is measured and printed
- watchdog: no samples for 300 ms -> `resetFifo()` and restart the measurement, with a counter in the status line
- `Serial.setTxTimeoutMs(0)` so printing never waits

## 9. Wave flat after moving the finger

20 Oct 2026, Phase 4

After moving my finger, the Wave screen showed the movement spike and then an almost flat line for several seconds. The wave was auto-scaled to the biggest value in the last 5 s, and the spike was much bigger than a pulse.

Fix: the wave now remembers which points were recorded during "Hold still" / settling / "Too bright". Those still get drawn but don't set the scale, and are clipped to the edges. The scale also shrinks smoothly instead of jumping. Before/after renders are in `phase4_display.md`.

## 10. OLED looks like it flickers

20 Oct 2026, Phase 4

The screen seemed to flicker a bit, in the videos and in person. Two different things:

- In the first videos, every 6th frame had horizontal strips missing. That's the phone camera catching the OLED's row-by-row refresh, not the display.
- In a second video I measured the screen brightness in all 1,212 frames: the change at 10 Hz (the update rate) was 0.08%, so the brightness doesn't flicker. What looks like flicker is the wave moving in 2-3 pixel jumps 10 times a second (new wave point every 40 ms, screen redrawn every 100 ms).

Fix: redraw at 25 fps, one frame per new wave point so it scrolls 1 pixel at a time. To make that fit, only the pages of the screen that changed are sent (a full redraw takes 33 ms). Details in `phase4_display.md`.

## 11. Slow breathing gave less HRV data, not more

20 Oct 2026, Phase 5

First real HRV test: one log sitting (with some fast breathing), one with slow breathing (5 s in, 5 s out). Three problems:

- HRV stayed at "5%" for about a minute. The first intervals were junk (327, 311, 963 ms while still "Measuring...") and became the reference for the artifact check, so good intervals after them were thrown out. Fix: only use intervals from a good signal, and compare each interval with its neighbours on both sides (median of 2 before, itself, 2 after).
- In the slow breathing log 15 real intervals were rejected and it went back to "Measuring" 4 times, then picked up fake short beats (328-388 ms). The heart rate code from Phase 2 only allowed +-20% from the median, but slow deep breathing swings my intervals from ~700 to over 1000 ms. Fix: wider range on the long side (0.70-1.45x), accept smooth beat-to-beat changes (within 25% of the previous interval), and take 3 rejected intervals that agree as the new rate instead of restarting. Simulator: rejected 36 -> 1, restarts 12 -> 0.
- While my heart rate was rising it said "Relaxed". RMSSD goes up when the heart rate changes, not only when relaxed. Fix: if the first and last 20 s of the window differ by more than 12%, it shows "HR changing" and no level.

Also: some lines in the recorded log were split in two (`BPM  8` / `8.7`). `capture_serial.py` wrote each read as a line; now it waits for the newline.

Lesson: the simulator's breathing was too gentle (+-5%), so the tests passed. I added slow-breathing and rising-heart-rate cases to `tests/test_hrv.cpp`.

## 12. Heart rate jumped to 140-160 BPM after the Phase 5 fix

20 Oct 2026, Phase 5

After the fix in #11 the heart rate sometimes went up to 140-160 BPM, while the version before it didn't.

Cause: my pulse has a fairly big second bump (the dicrotic wave) about 300-400 ms after each beat. In the logs it shows up as intervals like 306, 318, 328, 379 ms. The 300 ms block after each beat is just too short for it. The old code mostly hid this, because after 3 odd intervals it went back to "Measuring...". The #11 fix made the code accept intervals more easily (needed for slow breathing), and it could then lock onto beat + second bump, so double the heart rate.

I reproduced it in the simulator by moving the second bump later (`dicroticAt` in `sim_finger.h`): the BPM was more than 25% off 40% of the time, up to 2.6x the true rate.

Fix in `ppg_dsp.h`:
- a peak that comes early (before 70% of the normal interval, or 550 ms at the start) and is smaller than 70% of the previous beat's slope is skipped as the second bump. The real beat has the steepest rise of the pulse, the second bump much less
- 3 rejected intervals are only taken as the new heart rate if they're 0.65-1.6x the old one. Near half or double is almost always double counting or missed beats, and a real heart rate doesn't jump that much in 3 beats

Over 288 simulated recordings (4 pulse shapes, still and moving finger, 60-100 BPM, normal and slow breathing): BPM more than 25% off went from 18.7% of the time to 2.6%. The Phase 4 code was at 20.8% on the same set (most of it with the late second bump). A real fast heart rate (160 BPM) is still measured within 2.5 BPM.

The Serial log now prints the slope height of each beat (`h 245`) and a line for each skipped second bump, so I can see how big my real second bump is.

Lesson: the simulator's second bump came 260 ms after the beat, always inside the 300 ms block, so this case was never tested. Real logs showed it from the start (306/617/318 ms in the first lines of `hrv_slow.txt`); I should have looked at them more carefully.

Checked on the device afterwards (`hrv_sit2.txt`, `hrv_slow2.txt`): no jumps to double in 5 minutes of recording, BPM 65-94.

## 13. Raw recording saved 0 lines

20 Oct 2026, Phase 6

First try of the raw recording mode: `capture_serial.py` opened COM9 without an error, ran for 150 s and saved 0 lines. Not even the `#` lines came through, so it wasn't the data format.

Same symptom as #7. Most likely cause: the new sketch `06_breathing` was compiled with Tools > USB CDC On Boot = Disabled, so `Serial` went to GPIO20/21 instead of USB. (I checked the ESP32 core's USB code too: when the PC is slow it can drop some bytes, but it never stops completely.)

So that it can't happen silently a third time:
- the sketch now stops compiling with a clear `#error` if Serial is used and USB CDC On Boot is Disabled (tested both settings with arduino-cli)
- `capture_serial.py` says after 5 s if nothing is arriving, with what to check, instead of waiting the whole time

## 14. Breathing rate "agreed" on pure noise

20 Oct 2026, Phase 6

The breathing rate is only shown when the 3 breathing effects (beat timing, pulse height, baseline) agree. On my paced recordings it was always right. But a simulator test with no breathing at all, just random beat-to-beat noise, still showed a rate 62% of the time.

Looking at which ones agreed: always height and baseline. The baseline is the average IR over each beat, and a deeper pulse lowers that average, so the two are partly the same measurement and agree with each other on noise.

Fix: the timing effect has to be one of the agreeing ones (or all 3 agree), and at least 30% of the combined power has to be near the peak. Pure noise: 62% -> 20% shown. Real recordings: 80% -> 75% shown, still all right. Details in `phase6_breathing.md`, step 4.

Lesson: "two methods agree" only means something if they don't share the same error. I only found this because I wrote a test for the case where the answer should be "I don't know".

## Open: occasional FIFO reset in raw recording mode

20 Oct 2026, Phases 6-7

Two recordings out of eight have one `# fifo reset` (no samples arrived for 300 ms): `raw_pace20.txt` (25 s into the recording) and `raw_guided1.txt` (8 s after the board started, before the session). Both with `SERIAL_MODE 3`, which sends ~2 KB/s over USB. The breathing code's longest step is 25-29 ms and the slowest OLED frame 35 ms, so neither explains 300 ms. My guess is a pause on the USB side (Windows opening the port, or the USB Serial buffer), but I haven't proven it. The watchdog handles it (reset, measurement starts again), and the replay tools follow the reset note. To check later: does it ever happen with `SERIAL_MODE 0` or 2?

## 15. "HRV went down while breathing slowly"

20 Oct 2026, Phase 8

Second guided session, started from the phone: heart rate swing 20 BPM per breath, breathing 6.1 /min, so the slow breathing clearly worked. But the result said RMSSD 36 -> 30 ms, and the page said "HRV went down 15% while breathing slowly".

Not a bug in the numbers, a wrong choice of number. RMSSD is the difference between neighbouring beats. At 6 breaths a minute the heart rate goes up and down over ~13 beats, so each step is small even when the whole swing is big. The slow wave shows up in SDNN and the swing. Shaffer & Ginsberg (REFERENCES ref 6): "The RMSSD is less affected by respiration than is RSA". On top of that, my "before" minute wasn't calm (heart rate rising from 74 to 91 BPM).

Fix: the session also keeps SDNN for each part and whether the heart rate was steady. The page compares SDNN and warns when "before" wasn't steady. My first session replayed: SDNN 20 -> 65 -> 38 ms.

Lesson: in Phase 5 RMSSD happened to go up with slow breathing, and I took it as "the" HRV number for everything. A second test in different conditions showed it measures something else than what this exercise changes.
