<!--
OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
Copyright (C) 2024 David Lopes (https://github.com/davdlic)
Licensed under the GNU General Public License v3.0 — see LICENSE
-->

# OpenLitter

[![Build](https://github.com/davdlic/OpenLitter/actions/workflows/build.yml/badge.svg)](https://github.com/davdlic/OpenLitter/actions/workflows/build.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-informational.svg)](https://www.espressif.com/en/products/socs/esp32)
[![Latest release](https://img.shields.io/github/v/release/davdlic/OpenLitter?include_prereleases&sort=semver)](https://github.com/davdlic/OpenLitter/releases/latest)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](CONTRIBUTING.md)

**Open source ESP32 replacement firmware for the Litter Robot 1, 2 and 3.**

OpenLitter brings a dead Litter Robot back to life with a cheap ESP32, an L298N motor driver, the original DC motor, and a couple of Hall sensors. It exposes a mobile-friendly Web UI (PWA), real-time WebSocket updates, optional MQTT with Home Assistant auto-discovery, optional weight and mmWave presence sensors, and OTA updates.

> [!IMPORTANT]
> This is community firmware. It is **not** affiliated with Whisker / Litter Robot. Use at your own risk.

---

## Features

- **PWA Web UI** — installable app on your phone, dark mode, fully offline-capable, zero CDN dependencies.
- **Real-time updates** via WebSockets (no polling, no refresh) — live state badge, live HOME/DUMP/CAT sensor pills, live last cycle.
- **Robust state machine** with watchdog (per phase), anti-pinch with grace period, and a boot-time HOME check that recovers from power cuts mid-cycle.
- **Configurable cycle mechanics** — pause at DUMP (waste falls) and sand-shake leveling past HOME (CW + back CCW) tunable from the Web UI; defaults match a real Litter Robot.
- **Manual command set** — Cycle, Empty, Reset (full clean), Home (park without dump), Pause, Resume, Tare. Distinct grace periods for manual pause (5 min) vs anti-pinch pause (15 s).
- **Self-healing WiFi** — falls back to an `OpenLitter-Setup` access point on first boot or repeated failures; cleaning cycles keep working offline.
- **Optional MQTT** with Home Assistant auto-discovery (state sensor, weight, cat-present binary sensor, command buttons, availability LWT).
- **Optional weight sensor** (HX711 + 4 load cells under the feet) for cat detection by weight delta.
- **Optional mmWave presence sensor** (HLK-LD2410C) as an extra confirmation layer.
- **Last 20 cleaning cycles** kept in history (NVS, survives reflashes), with a per-day bar chart on the dashboard for at-a-glance trends over the last 7 days.
- **Live logs in the browser** — Logs tab streams state transitions, WiFi/MQTT/Update events in real time over WebSocket. Filter by Info/Warn/Error, pause, copy or download as `.txt`.
- **In-browser firmware update** — drop a `firmware.bin` / `littlefs.bin` onto the Web UI; progress bar + post-reboot polling, no PC tools required. See [docs/updating.md](docs/updating.md).
- **ArduinoOTA** push from PlatformIO for developers, with password protection.
- **mDNS** — reach the device at `openlitter.local`.
- **100% configurable pins** and switch type (NC/NO) from the Web UI — no recompile needed for most settings.
- **Companion Home Assistant integration + Lovelace card** at [davdlic/OpenLitter-HA](https://github.com/davdlic/OpenLitter-HA) — local-push, auto-discovery, native HA Update entity that flashes both firmware and Web UI from a release.

---

## Compatibility

| Model            | Status        | Notes                                            |
|------------------|---------------|--------------------------------------------------|
| Litter Robot 1   | ✅ Supported  | Same motor + Hall sensor topology                |
| Litter Robot 2   | ✅ Supported  | Same motor + Hall sensor topology                |
| Litter Robot 3   | ✅ Supported  | Drop-in replacement for the original mainboard   |

OpenLitter does **not** support the Litter Robot 4 (different mechanical and electronic architecture).

---

## Hardware

### Required

| Part                 | Notes                                                                   |
|----------------------|-------------------------------------------------------------------------|
| ESP32 dev board      | Any ESP32 (NodeMCU-32S, DevKitC, WROOM-32...)                           |
| L298N H-Bridge       | Drives the original 12 V DC globe motor                                 |
| 12 V power supply    | Sized for the original motor (≥ 2 A recommended)                        |
| 2× position sensors  | Original in globe detect the HOME and DUMP magnets on the globe.        |
| 1× micro switch      | Original in pedal. NC or NO, both supported                             |

### Optional

| Part                            | Notes                                                                    |
|---------------------------------|--------------------------------------------------------------------------|
| HX711 + 4× load cells           | 4 cells (one per foot), 20 kg or 50 kg each, summed in parallel          |
| HLK-LD2410C                     | 24 GHz mmWave presence sensor, mounted near the globe opening            |


---

## Wiring

See [docs/wiring_diagram.md](docs/wiring_diagram.md) for the full pinout table, the reference schematic ([interactive Cirkit Designer view](https://app.cirkitdesigner.com/project/0bc0578e-28ad-4119-8f88-b2d95e84ed99?view=interactive_preview)) and the connection diagram. Defaults:

| Function           | ESP32 pin | Notes                                |
|--------------------|-----------|--------------------------------------|
| Motor IN1          | 19        | L298N IN1                            |
| Motor IN2          | 21        | L298N IN2                            |
| Motor EN (PWM)     | 18        | L298N ENA, set to `-1` to disable    |
| Hall HOME          | 22        | Home position sensor                 |
| Hall DUMP          | 23        | Inverted/dump position sensor        |
| Cat micro switch   | 16        | Pedal switch + cable safety in parallel |
| HX711 DOUT         | 34        | Optional, if weight sensor enabled   |
| HX711 SCK          | 35        | Optional                             |
| LD2410C RX         | 4         | Optional                             |
| LD2410C TX         | 5         | Optional                             |

All pins are configurable from the Web UI — these are only defaults.

---

## Installation

### 1. Get the code

```bash
git clone https://github.com/davdlic/OpenLitter.git
cd OpenLitter
```

### 2. Install PlatformIO

Install [VS Code](https://code.visualstudio.com/) and the [PlatformIO extension](https://platformio.org/install/ide?install=vscode).

### 3. Configure (optional, all of this is also editable from the Web UI later)

Edit `src/config.h` if you want to change the default pin map or enable optional sensors at compile time.

### 4. Build & upload firmware

In PlatformIO: **Build** → **Upload**, or:

```bash
pio run -t upload
```

### 5. Upload the Web UI to LittleFS

```bash
pio run -t buildfs -t uploadfs
```

### 6. First boot

On first boot OpenLitter starts an access point:

- **SSID:** `OpenLitter-Setup`
- **Password:** `openlitter`
- **IP:** `192.168.4.1`

Open `http://192.168.4.1` from your phone, scan and pick your home WiFi, save. The device reboots and joins your network. From then on it is reachable at `http://openlitter.local`.

---

## Configuration

The full list of compile-time defaults lives in [src/config.h](src/config.h). At runtime, every relevant setting is editable from the Web UI under **Settings**:

- **Network** — WiFi credentials, hostname, static IP, recovery AP password.
- **Timing** — wait-after-cat, cat fallback timeout, cycle watchdog (per phase), anti-pinch reverse time, cycle/empty pause at DUMP, cycle level overshoot (sand shake past HOME).
- **Hardware** — motor pins, motor speed, Hall pins, switch pin and type (NC/NO), debounce.
- **Sensors** — weight sensor (capacity, threshold, tare), LD2410C presence sensor.
- **MQTT** — broker, port, credentials, topic base, HA auto-discovery toggle.
- **System** — in-browser firmware/filesystem update, OTA toggle, history size, factory reset, config import/export, restart.

Settings, WiFi credentials and cycle history are persisted in the ESP32 NVS partition (Arduino `Preferences`, namespace `openlitter`). Re-running `pio run -t uploadfs` to update the Web UI no longer wipes user data.

---

## Optional sensors

See [docs/sensors.md](docs/sensors.md) for installation details (load cell placement under the feet, LD2410C mounting position, calibration procedure).

---

## Home Assistant

OpenLitter integrates with Home Assistant in two ways:

1. **MQTT** — enable MQTT in the Web UI and turn on HA auto-discovery. Entities show up automatically: state sensor, weight sensor, cat-present binary sensor, cycle/empty/reset/pause buttons.
2. **HACS custom integration** — lives at [davdlic/OpenLitter-HA](https://github.com/davdlic/OpenLitter-HA). Adds richer entities (full history, raw HOME/DUMP/CAT sensors, firmware-update entity that flashes via `/api/update`), bus events for Logbook + automations, diagnostics download, reconfigure/reauth flows, plus a Lovelace card with rotating-globe animation. See [docs/home_assistant.md](docs/home_assistant.md) for setup.

---

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for the per-release history.

---

## Contributing

PRs are very welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines, code style, and how to report bugs.

---

## License

OpenLitter is licensed under the **GNU General Public License v3.0**. See [LICENSE](LICENSE) for the full text.

This means you are free to use, modify and redistribute the code, but any derivative work must also be released under GPL v3.

---

## Disclaimer

OpenLitter is not affiliated with, endorsed by, or sponsored by Whisker (Litter Robot). All trademarks belong to their respective owners. The firmware is provided **as is**, with no warranty — see the GPL v3 disclaimer.
