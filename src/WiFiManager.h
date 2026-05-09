/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace WiFiManager {

enum class Mode : uint8_t { OFFLINE, STATION, AP, AP_STA };

void begin();
void loop();

bool isConnected();
Mode mode();

// Provisioning: stop AP and try to connect with new credentials. Returns
// true on success and persists /wifi.json.
bool provision(const char *ssid, const char *password);

// Force the recovery AP up (in addition to STA if already connected).
void launchRecoveryAp();

void scanNetworks(JsonArray arr);

void serializeStatus(JsonObject obj);

}  // namespace WiFiManager
