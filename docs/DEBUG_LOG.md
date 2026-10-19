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
