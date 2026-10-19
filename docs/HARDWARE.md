# Hardware

## Bill of materials

| Qty | Part | Notes |
|---|---|---|
| 1 | ESP32-C3 Super Mini | RISC-V, WiFi + BLE 5, USB-C |
| 1 | MAX30100 module (green board) | On-board pull-ups removed, see below |
| 1 | 1.3" OLED, I²C (4-pin), 128×64, SH1106 driver | Address 0x3C. Needs an SH1106 library (U8g2), not SSD1306 |
| 1 | 3.7 V Li-Po, ~500 mAh | Size it to fit your enclosure |
| 1 | TP4056 module **with protection** (DW01) | USB-C version preferred |
| 1 | Slide switch | Between battery protection output and 5V pin |
| 1 | Tactile push button | Mode / session |
| 1 | Coin vibration motor or active buzzer | Driven through a transistor |
| 1 | NPN transistor (2N2222 / BC547) + 1 kΩ base resistor + 1N4148 diode | Motor driver, diode across motor |
| 2 | 100 kΩ resistors | Battery voltage divider |
| 2 | 4.7 kΩ resistors | External I²C pull-ups (required: the module's own pull-ups were removed) |
| — | Zero PCB, female headers, wire | |

## Pin map (ESP32-C3 Super Mini)

| Function | GPIO | Notes |
|---|---|---|
| I²C SDA | 6 | Shared by MAX30100 and OLED |
| I²C SCL | 7 | Shared by MAX30100 and OLED |
| Button | 4 | `INPUT_PULLUP`, button to GND |
| Vibration motor / buzzer | 10 | Through NPN transistor |
| Battery sense | 3 | ADC, via 100k/100k divider |
| Onboard LED | 8 | Status LED (also a strapping pin) |

**Avoid** GPIO2, GPIO8 and GPIO9 for external parts. They are strapping pins that affect boot, and GPIO9 is the BOOT button.

## Wiring

```
MAX30100   VIN → 3.3V    GND → GND    SDA → GPIO6    SCL → GPIO7    INT → (unused)
OLED       VCC → 3.3V    GND → GND    SDA → GPIO6    SCL → GPIO7
Pull-ups   GPIO6 (SDA) → 4.7kΩ → 3.3V      GPIO7 (SCL) → 4.7kΩ → 3.3V
Button     one leg → GPIO4, other leg → GND
Motor      GPIO10 → 1kΩ → NPN base; emitter → GND; collector → motor(−); motor(+) → 3.3V
           1N4148 across motor (cathode to +)
Battery    BAT+ → 100kΩ → GPIO3 → 100kΩ → GND
Power      Li-Po → TP4056 B+/B− ; TP4056 OUT+ → switch → ESP32 5V pin ; OUT− → GND
```

Powering through the 5V pin lets the onboard regulator produce 3.3 V. Do not connect USB and battery power to the ESP32 at the same time without a diode or power-path circuit.

## The MAX30100 pull-up problem

Many MAX30100 modules (both the common purple and some green boards) have their I²C pull-up resistors connected to the module's internal **1.8 V** regulator. With a 3.3 V microcontroller this often causes the sensor not to be detected, or unreliable readings.

**Symptoms:** the I²C scan doesn't find `0x57`, `begin()` fails, or values freeze.

**Fix:**
1. Find the three 4.7 kΩ resistors (marked `472`) near the I²C pins on the module.
2. Remove them carefully with a soldering iron.
3. Add 4.7 kΩ pull-ups from SDA → 3.3 V and SCL → 3.3 V on your perfboard. (The OLED module often already has its own pull-ups; one set on the bus is enough.)

**Status in this build:** confirmed on our green MAX30100 module (SDA measured ≈ 1.8 V) and fixed by removing the three resistors and adding external 4.7 kΩ pull-ups. Full diagnosis: [`DEBUG_LOG.md`](DEBUG_LOG.md#issue-2-max30100-module-pulls-the-i²c-bus-to-18-v).

## Enclosure design notes

- Ambient light is the biggest enemy of PPG accuracy. The finger cradle should fully cover the sensor.
- Mount the sensor flush with the cradle surface, without pressing hard. Too much pressure squeezes blood out and weakens the signal.
- Leave 0.2–0.3 mm clearance for press-fit parts; 1.6–2 mm wall thickness.
- Cut-outs: OLED window, USB-C (TP4056) slot, button, switch.
- Consider a small vent near the TP4056, which warms up while charging.
