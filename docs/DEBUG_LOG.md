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
