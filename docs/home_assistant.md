<!--
OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
Copyright (C) 2024 David Lopes (https://github.com/davdlic)
Licensed under the GNU General Public License v3.0 - see LICENSE
-->

# Home Assistant integration

OpenLitter integrates with Home Assistant in two ways:

1. **MQTT (available today)** — fully self-contained: the ESP32 publishes its state to your broker and HA discovers the entities automatically.
2. **HACS custom integration** (pre-1.0) — the [davdlic/OpenLitter-HA](https://github.com/davdlic/OpenLitter-HA) repo. Adds richer entities (full history, raw HOME/DUMP sensors, firmware-update entity that flashes via `/api/update`) plus a Lovelace card with the rotating-globe animation.

---

## Option 1 — MQTT (recommended today)

### Prerequisites

- A working MQTT broker (Mosquitto, EMQX, or HA's own Mosquitto add-on).
- The MQTT integration enabled in Home Assistant.
- OpenLitter running on the same network as the broker.

### Configure OpenLitter

Open the Web UI → **Settings → MQTT**:

| Field                         | Example value             |
|-------------------------------|---------------------------|
| Enabled                       | ✓                         |
| Broker                        | `192.168.1.10`            |
| Port                          | `1883`                    |
| Username                      | `openlitter` (optional)   |
| Password                      | (optional)                |
| Topic base                    | `openlitter`              |
| Home Assistant discovery      | ✓                         |

Hit **Save**. Within a few seconds the device:

- Connects to the broker (status visible in the same panel).
- Publishes a Last-Will message `openlitter/availability = offline` and the live `online` value once connected.
- If discovery is on, publishes `homeassistant/.../config` payloads describing every entity.

### Entities exposed

| Entity                                | Type           | Notes                              |
|---------------------------------------|----------------|------------------------------------|
| `sensor.openlitter_state`             | sensor         | One of IDLE, CAT_INSIDE, WAITING, CYCLING_CCW, CYCLING_CW, EMPTYING, RESETTING, PAUSED, ERROR |
| `sensor.openlitter_weight`            | sensor (kg)    | Only meaningful if weight sensor enabled |
| `sensor.openlitter_cycle_count`       | sensor         | Total cycles since last reset      |
| `binary_sensor.openlitter_cat_present`| binary_sensor  | `occupancy` device class           |
| `button.openlitter_cycle`             | button         | Trigger a manual cycle             |
| `button.openlitter_empty`             | button         | Empty the globe                    |
| `button.openlitter_reset`             | button         | Reset after empty / from error     |
| `button.openlitter_pause`             | button         | Pause                              |
| `button.openlitter_resume`            | button         | Resume                             |

### Topic reference

OpenLitter publishes to and listens on:

```
openlitter/state              → "IDLE" | "CAT_INSIDE" | ...
openlitter/cat_present        → "true" | "false"
openlitter/weight             → kg as float string
openlitter/cycle_count        → integer
openlitter/last_cycle         → unix timestamp
openlitter/error              → free-text on ERROR
openlitter/availability       → "online" | "offline" (LWT)

openlitter/command/cycle      ← any payload
openlitter/command/empty      ← any payload
openlitter/command/reset      ← any payload
openlitter/command/pause      ← any payload
openlitter/command/resume     ← any payload
openlitter/command/tare       ← any payload (re-tares weight sensor)
```

### Manual entity config (no auto-discovery)

If you'd rather define entities yourself in `configuration.yaml`:

```yaml
mqtt:
  sensor:
    - name: "OpenLitter state"
      state_topic: "openlitter/state"
      availability_topic: "openlitter/availability"
    - name: "OpenLitter weight"
      state_topic: "openlitter/weight"
      unit_of_measurement: "kg"
      device_class: weight

  binary_sensor:
    - name: "OpenLitter cat present"
      state_topic: "openlitter/cat_present"
      payload_on: "true"
      payload_off: "false"
      device_class: occupancy

  button:
    - name: "OpenLitter cycle"
      command_topic: "openlitter/command/cycle"
      payload_press: "press"
```

### Example automations

**Notify when a cat used the box:**

```yaml
- alias: "Notify on cat presence"
  trigger:
    platform: state
    entity_id: binary_sensor.openlitter_cat_present
    to: "on"
  action:
    service: notify.mobile_app
    data:
      message: "Cat just entered the litter box."
```

**Force a cycle every night at 03:00:**

```yaml
- alias: "Nightly forced cleanup"
  trigger:
    platform: time
    at: "03:00:00"
  action:
    service: button.press
    target:
      entity_id: button.openlitter_cycle
```

---

## Option 2 — HACS custom integration

Pre-1.0 — lives at [github.com/davdlic/OpenLitter-HA](https://github.com/davdlic/OpenLitter-HA). Gives you richer entities than the MQTT auto-discovery flow plus a Lovelace card, and works even without an MQTT broker.

### What it adds

- **Local push** via REST + WebSocket on `/ws` — no broker required. If you have HA's MQTT integration configured, the integration also subscribes to the same topics for redundancy.
- **Auto-discovery** via mDNS — the device is offered to you in HA's Discovered panel.
- **Richer entities** than MQTT discovery alone:
  - `sensor.openlitter_state` with friendly label + raw enum + full `history` array as an attribute.
  - `binary_sensor.openlitter_home_position` and `binary_sensor.openlitter_dump_position` — live state of the magnet sensors (handy for diagnosing wiring without USB serial).
  - `update.openlitter_firmware` — HA's native update card; polls GitHub for new firmware releases and installs via `/api/update` on the device, no PC needed.
- **Lovelace card** (`custom:openlitter-card`) with the rotating-globe animation, state badge, sensor pills, and command buttons.

### Installation

1. **HACS → Integrations → ⋮ → Custom repositories**
2. Repository: `https://github.com/davdlic/OpenLitter-HA`, Category: **Integration**
3. Install **OpenLitter**, restart Home Assistant.
4. Either accept the auto-discovery toast, or **Settings → Devices & Services → Add Integration → OpenLitter** and enter the host (`openlitter.local` or the device's IP).

For the Lovelace card:

1. **Settings → Dashboards → ⋮ → Resources → Add resource**
2. URL: `/hacsfiles/openlitter-ha/openlitter-card.js`, Type: **JavaScript Module**
3. Use it in any dashboard:

```yaml
type: custom:openlitter-card
entity: sensor.openlitter_state
```

The card pulls weight, buttons, sensor pills, and history automatically from the rest of the device's entities.
