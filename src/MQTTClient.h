/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#pragma once

#include <Arduino.h>

namespace MQTTClient {

void begin();
void loop();
void applySettings();

bool isConnected();

// Publishes the full status payload to MQTT_TOPIC_BASE/status as JSON, plus
// individual sensor topics. Called automatically on every state change.
void publishStatus();

}  // namespace MQTTClient
