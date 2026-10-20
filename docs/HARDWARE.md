# Hardware

## Parts

| Qty | Part | Notes |
|---|---|---|
| 1 | ESP32-C3 Super Mini | |
| 1 | MAX30100 module (green) | removed its pull-up resistors, see debug log |
| 1 | 1.3" OLED, I2C, 4 pin | SH1106 driver, address 0x3C (U8G2_SH1106_128X64_NONAME_F_HW_I2C) |
| 2 | 4.7k resistor | I2C pull-ups to 3.3V |
| 1 | 3.7V Li-Po (~500mAh) | later |
| 1 | TP4056 charger with protection | later |
| 1 | slide switch | later |
| 1 | push button | GPIO4 to GND |
| 1 | vibration motor + NPN transistor + 1k + 1N4148 | later |
| 2 | 100k resistor | battery voltage divider, later |
| - | zero PCB, female headers, wires | |

## Pins

| What | GPIO |
|---|---|
| SDA (sensor + OLED) | 6 |
| SCL (sensor + OLED) | 7 |
| Button | 4 |
| Motor | 10 |
| Battery voltage | 3 |

I'm not using GPIO 8 and 9 even though some pinout diagrams mark them as SDA/SCL. GPIO8 has the onboard LED and GPIO9 is the BOOT button, and both affect how the board boots. On the C3 any GPIO can be used for I2C, so I set the pins in code with `Wire.begin(6, 7)`.

## Wiring (current, Phase 4)

```
MAX30100  VIN -> 3.3V   GND -> G   SDA -> GPIO6   SCL -> GPIO7
OLED      VCC -> 3.3V   GND -> G   SDA -> GPIO6   SCL -> GPIO7
4.7k      GPIO6 -> 3.3V
4.7k      GPIO7 -> 3.3V
Button    GPIO4 -> button -> GND   (internal pull-up, no resistor)
```

The onboard blue LED (GPIO8) flashes on every heartbeat. It's a boot pin, but driving the onboard LED after start-up is fine.

Everything runs on 3.3V, not 5V.

## Wiring (planned, later phases)

```
Motor     GPIO10 -> 1k -> NPN base, emitter -> GND, collector -> motor -, motor + -> 3.3V
          1N4148 across the motor
Battery   BAT+ -> 100k -> GPIO3 -> 100k -> GND
Power     Li-Po -> TP4056 -> switch -> ESP32 5V pin
```

Don't connect USB and battery to the ESP32 at the same time without a diode.

## MAX30100 pull-up problem

A lot of MAX30100 boards (purple ones and some green ones like mine) have their I2C pull-ups going to 1.8V instead of 3.3V, which the ESP32 can't read properly. On mine SDA measured 1.8V. I removed the three pull-up resistors and added my own 4.7k pull-ups to 3.3V. Full details in [DEBUG_LOG.md](DEBUG_LOG.md).

Quick check for any board: power only the sensor and measure SDA to GND. If it's around 1.8V, you have this problem.

## Notes for the 3D case

- The sensor needs to be covered from outside light, otherwise readings get noisy. Finger slot should block light fully.
- Sensor should sit flush. If the finger presses too hard the signal gets weaker.
- Keep around 0.2-0.3 mm clearance for parts that press in, walls about 2 mm.
- Cutouts for OLED, USB-C, button and switch.
- Small vent near the TP4056, it gets warm when charging.
