<!--
OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
Copyright (C) 2024 David Lopes (https://github.com/davdlic)
Licensed under the GNU General Public License v3.0 — see LICENSE
-->

# Contributing to OpenLitter

Thanks for your interest in OpenLitter. This is a small open source project run by hobbyists for hobbyists, and any kind of help is welcome — from typo fixes to new features and hardware variants.

## Ways to contribute

- **Report bugs** — open an issue with logs (`Serial` output @115200 baud), the model of your Litter Robot, your hardware variant (motor driver, sensors), and steps to reproduce.
- **Request features** — open an issue describing the use case before sending code, so we can agree on scope.
- **Improve docs** — wiring photos, install walkthroughs, translation of the Web UI into other languages.
- **Submit code** — bug fixes, new sensors, MQTT improvements, HA integration, etc.

## Development setup

1. Install [VS Code](https://code.visualstudio.com/) + [PlatformIO](https://platformio.org/install/ide?install=vscode).
2. Clone your fork: `git clone https://github.com/<your-user>/OpenLitter.git`.
3. Open the folder in VS Code — PlatformIO installs dependencies on first build.
4. Build & flash to a real ESP32 (`pio run -t upload`) and upload the LittleFS image (`pio run -t uploadfs`).
5. Watch the serial monitor: `pio device monitor`.

## Code style

- **All code, comments, log messages, JSON keys, and identifiers must be in English.** No Portuguese or other languages in source files. The project is open to a worldwide audience.
- C++ is written for the Arduino framework on ESP32. Keep it readable — this is hobbyist firmware, not a kernel.
- Public APIs (functions exposed in `.h` files) get a short Doxygen-style comment. Private helpers don't need comments unless the *why* is non-obvious.
- Naming:
  - `camelCase` for variables and functions (`catPresent`, `startCycle()`).
  - `PascalCase` for classes and structs (`StateMachine`, `CycleRecord`).
  - `UPPER_SNAKE_CASE` for compile-time constants and pin defines (`MOTOR_IN1_PIN`).
  - JSON keys use `snake_case` (`cat_present`, `last_cycle`).
- **Never block the main loop.** No `delay()` in production code paths — use `millis()` and non-blocking state.
- Every file must include the GPL v3 header (see existing files for the canonical block).

## Pull request workflow

1. Fork the repo and create a topic branch off `main` (`git checkout -b feature/short-name`).
2. Make your changes. Keep the PR focused — one feature or fix per PR.
3. Test on real hardware. State in the PR description **what you tested** (LR1/2/3, sensors used, scenarios covered).
4. Update [README.md](README.md) and [docs/](docs/) if you add user-visible behaviour or new config keys.
5. Open a PR against `main`. Link related issues with `Closes #N`.

## Reporting bugs

When opening an issue please include:

- **Hardware:** Litter Robot model, ESP32 board, motor driver, optional sensors.
- **Firmware version** (commit SHA or release tag).
- **Steps to reproduce.**
- **Expected vs. actual behaviour.**
- **Serial log** at `CORE_DEBUG_LEVEL=3` covering at least one full reproduction.
- **Web UI screenshots** if relevant.

## Roadmap

| Phase   | Scope                                                            | Status        |
|---------|------------------------------------------------------------------|---------------|
| Phase 1 | Core firmware: state machine, Web UI, MQTT, OTA                  | In progress   |
| Phase 2 | HACS custom integration + Lovelace card (`openlitter-ha` repo)   | Planned       |
| Phase 3 | Advanced PWA: push notifications, local history graphs           | Planned       |
| Phase 4 | Custom PCB replacing the ESP32 + L298N wiring                    | Planned       |

## License

By contributing to OpenLitter you agree that your contributions are licensed under the **GNU General Public License v3.0**, the same license as the project itself. See [LICENSE](LICENSE).
