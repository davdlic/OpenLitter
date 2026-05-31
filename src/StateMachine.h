/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace StateMachine {

enum class State : uint8_t {
    IDLE,
    CAT_INSIDE,
    WAITING,
    CYCLING_CCW,
    CYCLING_DUMP_PAUSE,        // motor STOPPED at DUMP for cycle_dump_pause_sec (waste falls)
    CYCLING_CW,
    CYCLING_LEVEL_OVERSHOOT,        // after CW reaches HOME, motor keeps going CW for cycle_level_overshoot_sec (first half of the sand "shake")
    CYCLING_LEVEL_RETURN,           // motor CCW back to HOME after the level overshoot
    CYCLING_LEVEL_BACK_OVERSHOOT,   // motor keeps going CCW past HOME for CYCLE_LEVEL_BACK_OVERSHOOT_SEC (second half of the shake, in the opposite direction)
    CYCLING_LEVEL_BACK_RETURN,      // motor CW back to HOME after the back overshoot, then stop
    EMPTYING,
    EMPTYING_DUMP_PAUSE,       // motor STOPPED at DUMP for empty_dump_pause_sec (pull tray)
    RESETTING,
    BOOT_RECOVERY,             // on boot, globe not at HOME: drive CW until HOME, but reverse to CCW if DUMP triggers first (avoids re-dumping litter)
    PAUSED,
    ERROR,
};

struct CycleRecord {
    time_t   timestamp;
    uint16_t durationSeconds;
    float    catWeightKg;
    bool     completed;
};

void begin();
void loop();

State current();
const char *stateName(State s);
const char *errorMessage();

// Manual commands (Web UI / MQTT).
bool requestCycle();
bool requestEmpty();
bool requestReset();
bool requestPause();
bool requestResume();
bool requestHome();  // shortcut: stop current motion and drive CW directly to HOME (skips the dump phase, still runs the sand shake)

uint32_t cycleCount();
uint32_t lastCycleTimestamp();
uint32_t uptimeSeconds();

// Snapshot of recent history. The caller iterates with begin/end.
size_t historySize();
const CycleRecord &historyAt(size_t i);
void clearHistory();

void serializeStatus(JsonObject obj);
void serializeHistory(JsonArray arr);

// Hooks for layers that need to react to transitions. Multiple subscribers
// are supported (up to a small fixed number); each registered callback is
// invoked on every transition.
typedef void (*StateChangeCallback)(State previous, State next);
void setOnStateChange(StateChangeCallback cb);

}  // namespace StateMachine
