/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#pragma once

#include <Arduino.h>

namespace Motor {

// Configures pins (IN1, IN2 and optional EN/PWM) and stops the motor.
// If MOTOR_EN_PIN is -1 the LEDC PWM channel is not used and IN1/IN2
// are driven directly with digitalWrite at full speed.
void begin();

// Counter-clockwise: cleaning direction (idle -> dump position).
void ccw(uint8_t speed);

// Clockwise: return direction (dump position -> home).
void cw(uint8_t speed);

// Coast / free-wheel stop (IN1 = IN2 = LOW, EN = 0).
void stop();

// Active brake (IN1 = IN2 = HIGH).
void brake();

// Apply settings changes (re-binding LEDC channel etc.) live.
void applySettings();

}  // namespace Motor
