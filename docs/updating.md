<!--
OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
Copyright (C) 2024 David Lopes (https://github.com/davdlic)
Licensed under the GNU General Public License v3.0 - see LICENSE
-->

# Updating OpenLitter

After the first install over USB, every subsequent update can be done from the Web UI without a cable. This page explains the three ways to update, and what to do if something goes wrong.

---

## Option 1 — Web UI update (recommended)

This is the path designed for end users. No PC tools required.

1. Open OpenLitter at `http://openlitter.local/` (or the device's IP).
2. Go to **Settings → System → Firmware update**.
3. Pick what to update:
   - **Firmware** — the ESP32 program itself. Use `firmware.bin` from a release.
   - **Web UI / filesystem** — the PWA assets (HTML/CSS/JS, icons). Use `littlefs.bin` from a release.
4. Click **Choose file**, select the `.bin` you downloaded.
5. Click **Upload & install** and confirm.
6. The progress bar walks through `Uploading X%` → `Installing...` → `Waiting for device... (Ns)` → `Back online — reloading`. The page reloads itself when the ESP comes back.

The firmware update uses the ESP32 dual-OTA partition table (`min_spiffs.csv`). If a firmware flash fails mid-write, the previous image is still bootable — power-cycle and try again.

> ⚠️ The **filesystem update is more fragile**: if it fails mid-write you lose the Web UI and must reflash via USB. Always prefer reliable WiFi and a wired-power ESP32 when updating the filesystem.

---

## Option 2 — Push from PlatformIO (ArduinoOTA, for developers)

The legacy ArduinoOTA listener is still available (toggle in **Settings → System → OTA enabled**, default on). Add an OTA env to `platformio.ini`:

```ini
[env:esp32dev_ota]
extends = env:esp32dev
upload_protocol = espota
upload_port = openlitter.local
upload_flags =
    --auth=openlitter
```

Then:

```bash
python -m platformio run -e esp32dev_ota -t upload      # firmware
python -m platformio run -e esp32dev_ota -t uploadfs    # Web UI
```

The default OTA password is `openlitter` and is editable in **Settings → System → OTA password**.

---

## Option 3 — USB (recovery / first install)

If the device is unreachable (filesystem update broke the UI, WiFi never came up, etc.), connect the ESP32 over USB:

```bash
python -m platformio run -e esp32dev -t upload          # firmware
python -m platformio run -e esp32dev -t uploadfs        # Web UI
```

User data (WiFi credentials, settings, history) lives in the NVS partition and is **preserved** across firmware flashes. Only **Factory reset** from the Web UI clears it.

---

## Where to get release binaries

Releases are tag-driven: pushing a `v*.*.*` tag triggers a GitHub Actions workflow that builds and publishes:

- `firmware.bin` — the ESP32 firmware
- `littlefs.bin` — the Web UI / filesystem image
- `bootloader.bin`, `partitions.bin` — for first-install USB flashes
- `SHA256SUMS.txt` — checksums for verification

See the **[Releases page](https://github.com/davdlic/OpenLitter/releases)** for the current list.

---

## Watching what happens

The **Logs** tab in the Web UI shows live firmware logs over WebSocket. During an update you'll see:

```
[I]  ...  Update begin: firmware.bin (firmware)
[I]  ...  Update OK (1008016 bytes), rebooting
```

…and after the reboot:

```
[I]  ...  Boot: setup complete (firmware v0.2.0)
```

Filter by **Info / Warn / Error**, pause auto-scroll, or **Download** the buffer as `.txt` for support.

---

## Troubleshooting

| Symptom                                              | Likely cause and fix                                                                                          |
|------------------------------------------------------|----------------------------------------------------------------------------------------------------------------|
| `Update failed: bad magic byte`                      | Wrong file type (e.g. tried to upload `littlefs.bin` with the **Firmware** option). Re-pick the right option.  |
| Web UI version doesn't change after firmware update  | You updated firmware, not the filesystem. Repeat with **Web UI / filesystem** + `littlefs.bin`.                |
| Upload hangs at a non-100% percentage                | Weak WiFi to the ESP32. Move closer, or use Option 2/3.                                                        |
| `Timed out — reload manually`                        | The device didn't answer in 90 s. Wait a bit longer, then reload. If still down, USB recovery (Option 3).      |
| Filesystem update bricked the UI                     | Reflash `littlefs.bin` via Option 2 (if ArduinoOTA still works) or Option 3 (USB).                              |
