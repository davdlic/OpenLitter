/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#include "Sensors.h"
#include "Settings.h"
#include "config.h"

#include <HX711.h>
#include <ld2410.h>
#include <HardwareSerial.h>

namespace Sensors {

namespace {

// --- Cat switch debounce ---
struct DebouncedInput {
    int8_t   pin = -1;
    bool     activeLow = true;
    uint16_t debounceMs = 50;
    bool     stable = false;
    bool     last = false;
    uint32_t lastChangeMs = 0;

    void begin(int8_t p, bool lowActive, uint16_t deb) {
        pin = p;
        activeLow = lowActive;
        debounceMs = deb;
        if (pin < 0) return;
        pinMode(pin, INPUT_PULLUP);
        bool raw = digitalRead(pin);
        last = activeLow ? !raw : raw;
        stable = last;
        lastChangeMs = millis();
    }

    void update() {
        if (pin < 0) return;
        bool raw = digitalRead(pin);
        bool active = activeLow ? !raw : raw;
        if (active != last) {
            last = active;
            lastChangeMs = millis();
            return;
        }
        if (active != stable && (millis() - lastChangeMs) >= debounceMs) {
            stable = active;
        }
    }
};

DebouncedInput catInput;
DebouncedInput hallHome;
DebouncedInput hallDump;

HX711 scale;
float lastWeightKg = 0.0f;
uint32_t lastWeightReadMs = 0;
bool scaleReady = false;

ld2410 presenceSensor;
HardwareSerial presenceSerial(2);
bool presenceReady = false;

// HX711 calibration factors per cell capacity. With four 20 kg cells in
// parallel ((R+R)||(R+R)) on a single HX711 channel the calibration value
// is roughly half of a single cell. Values below are typical starting
// points and can be re-tuned by the user.
float scaleFactor() {
    return (settings.weightCapacityKg == 50) ? -10500.0f : -22000.0f;
}

void initWeightSensor() {
    scaleReady = false;
    if (!settings.weightEnabled) return;
    if (settings.weightDoutPin < 0 || settings.weightSckPin < 0) return;

    scale.begin(settings.weightDoutPin, settings.weightSckPin);
    if (!scale.wait_ready_timeout(1000)) {
        Serial.println("[Sensors] HX711 not responding");
        return;
    }
    scale.set_scale(scaleFactor());
    if (settings.weightTareOnBoot) {
        Serial.println("[Sensors] HX711 taring on boot...");
        scale.tare(20);
    }
    scaleReady = true;
    Serial.println("[Sensors] Weight sensor ready");
}

void initPresenceSensor() {
    presenceReady = false;
    if (!settings.presenceEnabled) return;
    if (settings.presenceRxPin < 0 || settings.presenceTxPin < 0) return;
    presenceSerial.begin(PRESENCE_SENSOR_BAUD, SERIAL_8N1,
                         settings.presenceRxPin, settings.presenceTxPin);
    if (presenceSensor.begin(presenceSerial)) {
        presenceReady = true;
        Serial.println("[Sensors] LD2410C ready");
    } else {
        Serial.println("[Sensors] LD2410C not responding");
    }
}

}  // namespace

void begin() {
    catInput.begin(settings.catSensorPin,
                   /*lowActive=*/(settings.catSensorType == 0),  // NC: open=cat, low=present
                   settings.catSensorDebounceMs);
    hallHome.begin(settings.hallHomePin, settings.hallActiveLow, HALL_DEBOUNCE_MS);
    hallDump.begin(settings.hallDumpPin, settings.hallActiveLow, HALL_DEBOUNCE_MS);

    initWeightSensor();
    initPresenceSensor();
    Serial.println("[Sensors] Initialised");
}

void applySettings() {
    begin();
}

void loop() {
    catInput.update();
    hallHome.update();
    hallDump.update();

    if (scaleReady && (millis() - lastWeightReadMs) >= WEIGHT_SAMPLE_RATE_MS) {
        lastWeightReadMs = millis();
        if (scale.is_ready()) {
            lastWeightKg = scale.get_units(1);
            if (lastWeightKg < 0) lastWeightKg = 0;
        }
    }

    if (presenceReady) {
        presenceSensor.read();
    }
}

bool isCatPresent()    { return catInput.stable; }
bool isHomePosition() { return hallHome.stable; }
bool isDumpPosition() { return hallDump.stable; }

float getCurrentWeight() { return scaleReady ? lastWeightKg : 0.0f; }
bool  isCatOnScale()     { return scaleReady && lastWeightKg >= settings.weightThresholdKg; }
bool  weightSensorReady(){ return scaleReady; }

void tareWeight() {
    if (!scaleReady) return;
    Serial.println("[Sensors] Manual tare");
    scale.tare(20);
}

bool isPresenceDetected() {
    if (!presenceReady) return false;
    return presenceSensor.presenceDetected();
}

}  // namespace Sensors
