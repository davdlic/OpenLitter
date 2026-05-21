<!--
OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
Copyright (C) 2024 David Lopes (https://github.com/davdlic)
Licensed under the GNU General Public License v3.0 - see LICENSE
-->

# Wiring diagram

This page shows the default pin map. Every pin is configurable from the Web UI under **Settings → Hardware** — you don't have to follow these exact numbers.

> ⚠️ The Litter Robot motor runs on **12 V**. The ESP32 runs on **3.3 V** logic and is typically powered via a 5 V buck converter. **Never** wire the 12 V rail directly to the ESP32.

## Power

```
  12 V supply ──┬── L298N VS (motor power)
                └── buck converter ── 5 V ── ESP32 VIN / 5V

  GND          common across L298N, ESP32, sensors and switches
```

## ESP32 default pin map

| Function                  | ESP32 pin | L298N / sensor pin     | Notes                                  |
|---------------------------|-----------|------------------------|----------------------------------------|
| Motor IN1                 | GPIO 25   | L298N IN1              | Direction control                      |
| Motor IN2                 | GPIO 26   | L298N IN2              | Direction control                      |
| Motor EN (PWM)            | GPIO 27   | L298N ENA              | Set to `-1` to disable PWM speed ctrl  |
| Hall HOME sensor          | GPIO 32   | Hall sensor 1 OUT      | Detects globe at home position         |
| Hall DUMP sensor          | GPIO 33   | Hall sensor 2 OUT      | Detects globe at inverted position     |
| Cat micro switch          | GPIO 18   | Pedal switch           | Switch type configurable: NC or NO     |
| HX711 DOUT *(optional)*   | GPIO 34   | HX711 DT               | Input only on ESP32                    |
| HX711 SCK *(optional)*    | GPIO 35   | HX711 SCK              | Input only on ESP32                    |
| LD2410C RX *(optional)*   | GPIO 16   | LD2410C TX             | Note crossover                         |
| LD2410C TX *(optional)*   | GPIO 17   | LD2410C RX             | Note crossover                         |

> GPIOs 34 and 35 are **input-only** on the ESP32 — they're a great match for HX711, but cannot be used for outputs if you ever swap their role.

## L298N → motor

The L298N has two motor channels; OpenLitter only uses channel A.

| L298N pin | Connect to                                       |
|-----------|--------------------------------------------------|
| VS        | +12 V                                            |
| GND       | Common ground                                    |
| 5V (logic) | Either jumper from the on-board 5V regulator, or feed externally if your board has the jumper removed |
| OUT1      | Motor lead 1                                     |
| OUT2      | Motor lead 2                                     |
| IN1       | ESP32 GPIO 25                                    |
| IN2       | ESP32 GPIO 26                                    |
| ENA       | ESP32 GPIO 27 (PWM). Remove the jumper to use PWM. |

Direction is determined by `IN1`/`IN2`:

| IN1 | IN2 | Result        |
|-----|-----|---------------|
| LOW | LOW | Coast (stop)  |
| LOW | HIGH| CCW (cleaning)|
| HIGH| LOW | CW (return)   |
| HIGH| HIGH| Brake         |

## Hall sensors

OpenLitter expects **bipolar latching Hall sensors** (e.g. A3144). The polarity of the magnets matters — one orientation triggers the sensor, the other doesn't.

```
  3.3 V ─ Vcc
         OUT ── ESP32 GPIO 32 (HOME) or 33 (DUMP)
  GND  ─ GND

  10 kΩ pull-up between OUT and 3.3 V (or use INPUT_PULLUP in firmware)
```

Configure `Hall active state` in **Settings → Hardware** to match how your sensor signals presence (`LOW` is the most common).

## Pedal micro switch

Use a standard microswitch with COM and either NO or NC contact wired to a GPIO. The other contact goes to GND. The pin uses `INPUT_PULLUP`, so:

- **NC type:** open when cat is on the pedal → GPIO reads HIGH when cat present.
- **NO type:** closed when cat is on the pedal → GPIO reads LOW when cat present.

The firmware inverts the reading automatically based on the **Sensor type** setting.

## Optional: HX711 + 4 load cells

Wire the four 20 kg or 50 kg cells in a Wheatstone bridge (E+, E-, A+, A-) so they sum the load on all four feet:

```
                    ┌─────────────┐
                    │  Cell 1     │
                    │  Cell 2     │  →  E+ / A+ / A- / E- bus
                    │  Cell 3     │
                    │  Cell 4     │
                    └─────────────┘
                          │
            ┌────── HX711 ──── ESP32
            │
            │  Vcc/3.3V, GND, DOUT → GPIO 34, SCK → GPIO 35
```

See [sensors.md](sensors.md) for placement under the Litter Robot feet and calibration.

## Optional: HLK-LD2410C mmWave sensor

```
  3.3 V  ─ VCC
  GND    ─ GND
  TX     ─ ESP32 GPIO 16 (RX)
  RX     ─ ESP32 GPIO 17 (TX)
```

Mount the sensor in the base, pointing at the entry hole. Tune detection range and sensitivity through the LD2410 mobile app if you have it; the firmware only reads the binary "presence detected" output.

## ASCII overview

```
                      ┌───────────────┐
                      │    ESP32      │
            ┌─────────┤ GPIO 25 ──── IN1 ┐
            │         │ GPIO 26 ──── IN2 ├─── L298N ──── 12V Motor
            │         │ GPIO 27 ──── ENA ┘
            │         │
            │         │ GPIO 18 ──── Cat micro switch ──── GND
            │         │
            │         │ GPIO 32 ──── Hall HOME ───────── 3.3V/GND
            │         │ GPIO 33 ──── Hall DUMP ───────── 3.3V/GND
            │         │
            │   (opt) │ GPIO 34 ──── HX711 DOUT
            │   (opt) │ GPIO 35 ──── HX711 SCK
            │   (opt) │ GPIO 16 ──── LD2410 TX
            │   (opt) │ GPIO 17 ──── LD2410 RX
            │         └───────────────┘
            │
          5V/GND from buck converter (12V → 5V)
```
