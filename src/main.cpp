/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <time.h>

#include "config.h"
#include "Settings.h"
#include "Motor.h"
#include "Sensors.h"
#include "StateMachine.h"
#include "WiFiManager.h"
#include "WebServer.h"
#include "MQTTClient.h"
#include "OTA.h"

namespace {

// Hardware watchdog - reset the chip if the main loop ever stalls for too
// long (e.g. a deadlock in a library). We feed it on every loop iteration.
constexpr uint32_t HW_WDT_TIMEOUT_SEC = 30;

void mountFilesystem() {
    if (LittleFS.begin(true)) {
        Serial.println("[FS] LittleFS mounted");
    } else {
        Serial.println("[FS] LittleFS mount failed even after format");
    }
}

void setupTime() {
    // Best-effort NTP sync. If we are offline this just keeps relative time.
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.println();
    Serial.println("==========================================");
    Serial.printf ("  OpenLitter v%s\n", OPENLITTER_VERSION);
    Serial.println("  https://github.com/davdlic/OpenLitter");
    Serial.println("  Licensed under GPL v3");
    Serial.println("==========================================");

    esp_task_wdt_init(HW_WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(nullptr);

    mountFilesystem();
    settings.load();

    Motor::begin();
    Sensors::begin();
    StateMachine::begin();

    WiFiManager::begin();
    setupTime();
    WebServer::begin();
    MQTTClient::begin();
    OTA::begin();

    Serial.println("[Boot] Setup complete");
}

void loop() {
    esp_task_wdt_reset();

    Sensors::loop();
    StateMachine::loop();

    WiFiManager::loop();
    WebServer::loop();
    MQTTClient::loop();
    OTA::loop();
}
