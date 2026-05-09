/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#include "OTA.h"
#include "Settings.h"

#include <ArduinoOTA.h>

namespace OTA {

namespace {
bool started = false;
}

void begin() {
    if (!settings.otaEnabled) {
        Serial.println("[OTA] Disabled in settings");
        return;
    }
    ArduinoOTA.setHostname(settings.hostname);
    ArduinoOTA.setPassword(settings.otaPassword);
    ArduinoOTA.onStart([]() {
        const char *type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
        Serial.printf("[OTA] Start (%s)\n", type);
    });
    ArduinoOTA.onEnd([]() { Serial.println("[OTA] Done, rebooting"); });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
        Serial.printf("[OTA] %u%%\r", (p * 100) / t);
    });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.printf("[OTA] Error %u\n", e);
    });
    ArduinoOTA.begin();
    started = true;
    Serial.println("[OTA] Ready");
}

void loop() {
    if (started) ArduinoOTA.handle();
}

}  // namespace OTA
