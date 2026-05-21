/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#include "Motor.h"
#include "Settings.h"
#include "config.h"

namespace Motor {

namespace {
bool ledcAttached = false;

void writeEnable(uint8_t speed) {
    if (settings.motorEnPin < 0) return;
    if (ledcAttached) {
        ledcWrite(MOTOR_PWM_CHANNEL, speed);
    } else {
        digitalWrite(settings.motorEnPin, speed > 0 ? HIGH : LOW);
    }
}
}  // namespace

void begin() {
    pinMode(settings.motorIn1Pin, OUTPUT);
    pinMode(settings.motorIn2Pin, OUTPUT);
    digitalWrite(settings.motorIn1Pin, LOW);
    digitalWrite(settings.motorIn2Pin, LOW);

    if (settings.motorEnPin >= 0) {
        ledcSetup(MOTOR_PWM_CHANNEL, MOTOR_PWM_FREQ_HZ, MOTOR_PWM_RES_BITS);
        ledcAttachPin(settings.motorEnPin, MOTOR_PWM_CHANNEL);
        ledcWrite(MOTOR_PWM_CHANNEL, 0);
        ledcAttached = true;
    } else {
        ledcAttached = false;
    }
    Serial.println("[Motor] Initialised");
}

void applySettings() {
    // Re-bind pins; safest way is to detach + re-init.
    if (ledcAttached && settings.motorEnPin >= 0) {
        ledcDetachPin(settings.motorEnPin);
        ledcAttached = false;
    }
    begin();
}

void ccw(uint8_t speed) {
    digitalWrite(settings.motorIn1Pin, LOW);
    digitalWrite(settings.motorIn2Pin, HIGH);
    writeEnable(speed);
}

void cw(uint8_t speed) {
    digitalWrite(settings.motorIn1Pin, HIGH);
    digitalWrite(settings.motorIn2Pin, LOW);
    writeEnable(speed);
}

void stop() {
    writeEnable(0);
    digitalWrite(settings.motorIn1Pin, LOW);
    digitalWrite(settings.motorIn2Pin, LOW);
}

void brake() {
    writeEnable(255);
    digitalWrite(settings.motorIn1Pin, HIGH);
    digitalWrite(settings.motorIn2Pin, HIGH);
}

}  // namespace Motor
