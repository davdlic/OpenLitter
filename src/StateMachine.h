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
    CYCLING_OVERSHOOT_DUMP,  // motor stays CCW past DUMP for cycle_overshoot_sec
    CYCLING_CW,
    EMPTYING,
    EMPTYING_OVERSHOOT,      // motor stays CCW past DUMP for empty_overshoot_sec
    RESETTING,
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
