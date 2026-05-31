# Changelog

All notable changes to this project will be documented here. The format
loosely follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/).

## [1.0.1] — 2026-06-01

### Fixed
- **Boot recovery could re-dump clean litter.** When the device booted with
  the globe past DUMP on the return leg of an interrupted cycle (typical
  after a mains glitch during the level-overshoot or back-shake phase), the
  old recovery drove CW back to HOME and crossed DUMP on the way, opening
  the dump door and spilling clean litter into the waste tray.

  The recovery is now a two-leg motion under the new `BOOT_RECOVERY` state:
  drive CW first; if DUMP latches before HOME, stop, reverse to CCW and
  reach HOME through the leveling zone instead — never re-crossing DUMP.
  The cycle watchdog applies to both legs.

  Reported and reproduced by the project owner with a simulated mains cut
  mid-cycle.

### Notes
- New state name `BOOT_RECOVERY` exposed in `/api/status`, MQTT topics and
  the Web UI badge (label: **Recovering**). Companion HA integration
  v1.0.1 adds the friendly label to its STATE_LABELS map; older HA
  integrations will show the raw `BOOT_RECOVERY` until they update.

## [1.0.0] — 2026-05-26

First stable release. Public API (REST, WebSocket, MQTT topic layout) is
now considered frozen — breaking changes will require a 2.0.

### Added
- 7-day cycles-per-day bar chart on the dashboard, rendered locally from
  the existing `history` array. No external dependencies.

### Changed
- Promote the firmware out of the pre-1.0 phase: docs and roadmap updated,
  versioned APIs are now public contract.

### Notes
- Companion Home Assistant integration ([davdlic/OpenLitter-HA](https://github.com/davdlic/OpenLitter-HA))
  cuts v1.0.0 in parallel with this release. Compatible firmware ≥ v0.4.1
  remains supported by the integration.

## [0.4.1] — 2026-05-25

### Fixed
- History duration was inflated for cat-triggered auto-cycles because
  `cycleBeginMs` was inherited from the previous cycle (or 0 on first
  boot). Both manual and auto triggers now set `cycleBeginMs` correctly.

### Changed
- Default `cycle_dump_pause_sec` bumped from 5 to 7 seconds.
- Removed the experimental DUMP advance phase after real-hardware testing
  showed it wasn't needed — the existing STOP-at-DUMP behaviour is enough.

## [0.4.0] — 2026-05-24

### Added
- **Home** command — stop current motion and return CW directly to HOME,
  skipping the DUMP phase. Sand-shake leveling still runs on arrival.
- Documented the signed-commit requirement on `main`.

## [0.3.0] — 2026-05-23

### Changed
- Major cycle mechanics rework: STOP at DUMP (waste falls through the dump
  door, motor does not advance past), sand-shake leveling past HOME (CW
  overshoot then CCW back-shake), live HOME / DUMP / CAT sensor pills on
  the dashboard.

## [0.2.0] — 2026-05-22

### Added
- In-browser firmware + filesystem update with progress + post-reboot
  polling — no PC tools required.
- Live Logs view in the PWA over a dedicated WebSocket channel; filter by
  Info / Warn / Error, pause, copy, download.
- Boot-time HOME check: recover from power cuts mid-cycle by parking the
  globe before going IDLE.
- WiFiManager: captive-portal DNS + mDNS announcement in AP mode.

### Changed
- Settings + WiFi credentials + cycle history persisted in NVS rather than
  LittleFS, so reflashing the Web UI no longer wipes user data.
- Default cat sensor type switched to NO (matches the parts shipped on
  most refurbished Litter Robot pedals).
- Distinguish manual pause from anti-pinch pause grace (5 min vs 15 s).
- New default pin map + reference schematic in the docs.

### Fixed
- PWA install banner stayed visible after dismissal on some browsers.

## [0.1.0] — 2026-05-15

Initial firmware release. Web UI (PWA), state machine, MQTT with HA
auto-discovery, REST + WebSocket APIs, ArduinoOTA, optional weight
sensor (HX711) and presence sensor (HLK-LD2410C).

[1.0.1]: https://github.com/davdlic/OpenLitter/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/davdlic/OpenLitter/compare/v0.4.1...v1.0.0
[0.4.1]: https://github.com/davdlic/OpenLitter/compare/v0.4.0...v0.4.1
[0.4.0]: https://github.com/davdlic/OpenLitter/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/davdlic/OpenLitter/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/davdlic/OpenLitter/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/davdlic/OpenLitter/releases/tag/v0.1.0
