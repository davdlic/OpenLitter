/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Runtime-mutable settings, persisted in LittleFS as /settings.json.
// Compile-time defaults come from config.h.
struct Settings {
    // --- Motor / hardware ---
    int8_t  motorIn1Pin;
    int8_t  motorIn2Pin;
    int8_t  motorEnPin;
    uint8_t motorSpeed;

    int8_t  hallHomePin;
    int8_t  hallDumpPin;
    bool    hallActiveLow;

    int8_t   catSensorPin;
    uint8_t  catSensorType;     // 0 = NC, 1 = NO
    uint16_t catSensorDebounceMs;

    // --- Timing ---
    uint16_t waitAfterCatMin;
    uint16_t catTimeoutMin;
    uint16_t cycleTimeoutSec;
    uint16_t antiPinchReverseMs;
    uint16_t cycleDumpAdvanceSec;     // after the DUMP sensor fires, motor keeps running CCW for this many seconds so the door opens fully (then stops for cycleDumpPauseSec)
    uint16_t emptyDumpAdvanceSec;     // same idea for manual empty
    uint16_t cycleDumpPauseSec;       // pause-at-DUMP seconds during a cleaning cycle (motor stopped, waste falls)
    uint16_t emptyDumpPauseSec;       // pause-at-DUMP seconds during a manual empty (motor stopped, pull tray)
    uint16_t cycleLevelOvershootSec;  // seconds the motor keeps going CW past HOME at the end of a cycle (sand levelling), then reverses CCW back to HOME

    // --- Weight sensor ---
    bool    weightEnabled;
    uint8_t weightCapacityKg;   // 20 or 50
    int8_t  weightDoutPin;
    int8_t  weightSckPin;
    float   weightThresholdKg;
    bool    weightTareOnBoot;

    // --- Presence sensor (LD2410C) ---
    bool   presenceEnabled;
    int8_t presenceRxPin;
    int8_t presenceTxPin;

    // --- Network ---
    char hostname[32];
    char apPassword[33];
    bool useStaticIp;
    char staticIp[16];
    char staticGateway[16];
    char staticSubnet[16];

    // --- MQTT ---
    bool     mqttEnabled;
    char     mqttBroker[64];
    uint16_t mqttPort;
    char     mqttUser[32];
    char     mqttPassword[64];
    char     mqttTopicBase[32];
    bool     mqttHaDiscovery;

    // --- OTA ---
    bool otaEnabled;
    char otaPassword[33];

    // --- History ---
    uint16_t historyMaxEntries;

    // Reset all fields to compile-time defaults from config.h.
    void resetToDefaults();

    // Load from LittleFS, returns true on success. On failure, fields are
    // left initialised to defaults.
    bool load();

    // Persist to LittleFS atomically (write to .tmp + rename).
    bool save() const;

    // Apply a partial JSON patch (any field present overwrites the current
    // value). Unknown keys are ignored. Returns true if at least one field
    // was updated.
    bool applyJson(const JsonObjectConst &obj);

    // Serialise the full settings to a JsonObject (for GET /api/config).
    // The `redactSecrets` flag blanks out passwords for the network response.
    void toJson(JsonObject obj, bool redactSecrets) const;
};

extern Settings settings;
