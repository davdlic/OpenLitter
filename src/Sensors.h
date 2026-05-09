/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#pragma once

#include <Arduino.h>

namespace Sensors {

void begin();
void loop();
void applySettings();

// Cat micro switch on the pedal. Debounced and inverted according to
// the configured NC/NO type.
bool isCatPresent();

// Hall sensors on the globe.
bool isHomePosition();
bool isDumpPosition();

// Weight sensor (HX711). Returns 0.0 if disabled.
float getCurrentWeight();
bool  isCatOnScale();
void  tareWeight();
bool  weightSensorReady();

// HLK-LD2410C presence sensor. Returns false if disabled.
bool isPresenceDetected();

}  // namespace Sensors
