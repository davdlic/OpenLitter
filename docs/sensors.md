<!--
OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
Copyright (C) 2024 David Lopes (https://github.com/davdlic)
Licensed under the GNU General Public License v3.0 - see LICENSE
-->

# Optional sensors

OpenLitter works fine with just the two Hall sensors and the pedal micro switch. The two optional sensors below add extra reliability and features:

- **HX711 + 4 load cells** — measures cat weight, makes presence detection independent of the pedal switch (great for cats that jump in/out without pressing the pedal).
- **HLK-LD2410C** — 24 GHz mmWave radar, an extra confirmation that a cat is inside the globe.

You can enable either, both, or none. All toggles live in **Settings → Sensors**.

---

## Weight sensor: HX711 + 4 load cells

### Hardware

| Part                         | Notes                                              |
|------------------------------|----------------------------------------------------|
| HX711 ADC                    | The cheap red breakout board is fine               |
| 4× load cells (20 kg or 50 kg) | One under each foot of the Litter Robot          |
| M3 / M4 hardware             | To bolt the cells between the foot and the floor   |

The 20 kg cells are enough for cats up to ~10 kg with safety margin (cells in parallel sum capacity but you only ever load one at a time when the cat is centred — keep the margin). 50 kg cells are overkill but more readily available.

### Wiring

The four cells are wired as a single Wheatstone bridge by connecting their colour-coded wires in parallel:

```
  All 4 RED   wires → HX711 E+
  All 4 BLACK wires → HX711 E-
  All 4 WHITE wires → HX711 A-
  All 4 GREEN wires → HX711 A+
```

(Some cells use yellow instead of green or different colour conventions — check the datasheet.)

Then HX711 to the ESP32:

| HX711 | ESP32        |
|-------|--------------|
| VCC   | 3V3          |
| GND   | GND          |
| DT    | GPIO 34      |
| SCK   | GPIO 35      |

### Mechanical install

Disassemble the original feet of the Litter Robot. Install one load cell under each foot, sandwiched between the foot itself and the floor:

```
   Litter Robot foot
        │
        ├── M-style screw down through the cell's "fixed" mounting hole
        ▼
   ┌─────────┐
   │  Cell   │   ← bend direction must align with vertical force
   └─────────┘
        ▲
        ├── M-style screw up through the cell's "load" mounting hole
        │
   Floor / base plate
```

Make sure each cell can flex freely and that the foot of the machine doesn't bottom out on anything other than the cell itself.

### Calibration

Default calibration factors in [`src/Sensors.cpp`](../src/Sensors.cpp) are reasonable starting points for 4×20 kg or 4×50 kg cells in parallel, but you almost certainly want to tune them.

1. Boot OpenLitter with **weight enabled**, no cat near the machine.
2. Open Settings → Sensors. The "Live weight" reading should hover near 0 kg. If it's far off, use the **Tare now** button.
3. Place a known weight (a 5 kg dumbbell, a bag of cat litter you've weighed on a kitchen scale) in the centre of the globe.
4. If the reading is e.g. 4.0 kg when the real weight is 5.0 kg, multiply the calibration factor by `4.0 / 5.0 = 0.8`. (The factor is currently embedded in `Sensors.cpp::scaleFactor()` — adjust there and reflash, or expose it via the Web UI in your fork.)

### Tuning the cat threshold

In **Settings → Sensors → Cat threshold (kg)** set the minimum weight increase that counts as "cat present". Default is `2.0 kg` — anything lighter is treated as noise (e.g. someone bumping the machine).

If your weight reading is noisy at idle, increase the threshold. If you have a small kitten and the threshold is too high, lower it.

---

## Presence sensor: HLK-LD2410C (24 GHz mmWave)

### Why mmWave?

The pedal microswitch only fires when the cat steps onto the entry. A mmWave sensor fires when *anything alive moves inside the radar field*, even if the cat jumped over the pedal or is sitting still. OpenLitter uses it as an OR with the pedal switch — either signal counts as "cat inside".

### Wiring

| LD2410C | ESP32     |
|---------|-----------|
| VCC     | 3V3 (or 5 V if your board accepts it — check the silkscreen) |
| GND     | GND       |
| TX      | GPIO 16 (ESP32 RX) |
| RX      | GPIO 17 (ESP32 TX) |

### Mounting

Mount the sensor on the **base of the Litter Robot, pointing up at the entry hole**, so the cat passes through the radar cone every time it enters or exits.

The sensor "sees" through plastic — you can hide it inside the base for a clean look.

### Range / sensitivity

The default firmware just reads the binary "presence detected" output. To tune detection range, sensitivity, and unattended timeout, use the **HLKRadarTool** Bluetooth app (Android/iOS):

1. Pair with the LD2410C (default password `HiLink`).
2. Set **maximum distance** to ~2 m (more than enough for a Litter Robot globe).
3. Set **stationary detection** to maximum sensitivity (gates 1–2). Cats often sit still while doing their business.
4. Save settings to the sensor — they persist across reboots.

---

## Verifying everything works

After wiring and enabling each sensor, observe the dashboard:

- **Cat present** flips to "Yes" when you press the pedal microswitch by hand, or place ≥ threshold weight on the cells, or wave a hand across the LD2410C field.
- **Weight** shows a stable kg reading near 0 when nothing is on the cells.
- The **state** badge transitions: `IDLE → CAT_INSIDE` on detection, `→ WAITING → CYCLING_CCW` after the configured wait timer.

If something doesn't react, check the Serial monitor at 115200 baud — every state transition and sensor init is logged.
