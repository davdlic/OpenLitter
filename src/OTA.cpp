/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#include "OTA.h"
#include "Settings.h"
#include "Log.h"

#include <ArduinoOTA.h>

namespace OTA {

namespace {
bool started = false;
}

void begin() {
    if (!settings.otaEnabled) {
        Log::info("OTA disabled in settings");
        return;
    }
    ArduinoOTA.setHostname(settings.hostname);
    ArduinoOTA.setPassword(settings.otaPassword);
    ArduinoOTA.onStart([]() {
        const char *type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
        Log::info("OTA start (%s)", type);
    });
    ArduinoOTA.onEnd([]() { Log::info("OTA done, rebooting"); });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
        // Use plain Serial here: progress is high-frequency and the ring
        // buffer + WS broadcast can't keep up at every percent tick.
        Serial.printf("[OTA] %u%%\r", (p * 100) / t);
    });
    ArduinoOTA.onError([](ota_error_t e) {
        Log::error("OTA error %u", (unsigned)e);
    });
    ArduinoOTA.begin();
    started = true;
    Log::info("OTA ready");
}

void loop() {
    if (started) ArduinoOTA.handle();
}

}  // namespace OTA
