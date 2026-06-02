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

// ============================================================
// OpenLitter - Configuration File
// Edit this file to match your hardware setup.
// Most of these values are also editable at runtime from the
// Web UI under Settings, and persisted in /settings.json.
// ============================================================

#pragma once

#include <Arduino.h>

// --- FIRMWARE METADATA ---
#define OPENLITTER_VERSION  "1.0.4"

// --- MOTOR PINS (L298N) ---
// Every pin is also editable from Settings -> Hardware at runtime.
#define MOTOR_IN1_PIN       19
#define MOTOR_IN2_PIN       21
#define MOTOR_EN_PIN        18      // PWM enable pin (set -1 if not used)
#define MOTOR_SPEED         200     // 0-255, PWM speed
#define MOTOR_PWM_CHANNEL   0       // ESP32 LEDC channel for motor PWM
#define MOTOR_PWM_FREQ_HZ   20000   // 20 kHz, above audible range
#define MOTOR_PWM_RES_BITS  8       // 8-bit (0-255)

// --- HALL SENSORS ---
#define HALL_HOME_PIN       22      // Hall sensor 1 - Home position
#define HALL_DUMP_PIN       23      // Hall sensor 2 - Dump position
#define HALL_ACTIVE_STATE   LOW     // LOW or HIGH when magnet detected
#define HALL_DEBOUNCE_MS    20

// --- CAT SENSOR (micro switch on pedal + cable safety in parallel) ---
// Wiring values for CAT_SENSOR_TYPE
#define CAT_SENSOR_NC           0   // Normally Closed
#define CAT_SENSOR_NO           1   // Normally Open

#define CAT_SENSOR_PIN          16
#define CAT_SENSOR_TYPE         CAT_SENSOR_NO
#define CAT_SENSOR_DEBOUNCE_MS  50

// --- TIMING ---
#define WAIT_AFTER_CAT_MIN      7    // Minutes to wait after cat leaves before cycling
#define CAT_TIMEOUT_MIN         20   // Max minutes cat can be "inside" before assuming it left
#define CYCLE_TIMEOUT_SEC       120  // Max seconds for a full cycle (safety watchdog)
#define ANTI_PINCH_REVERSE_MS   2000 // Milliseconds to reverse on pinch detection
#define PAUSED_AUTO_RESUME_SEC  15   // Anti-pinch pause (cat detected during motion): seconds to wait before resuming
#define MANUAL_PAUSE_AUTO_RESUME_SEC 300 // Manual pause (user pressed Pause): seconds to wait before resuming. Longer than anti-pinch so the user has time to intervene without the machine auto-restarting in 15 s
#define CYCLE_DUMP_PAUSE_SEC      7   // Seconds to PAUSE (motor off) at DUMP so waste falls through the door before the CW return.
#define EMPTY_DUMP_PAUSE_SEC      5   // Seconds to PAUSE (motor off) at DUMP during a manual empty (gives time to pull the tray)
#define CYCLE_LEVEL_OVERSHOOT_SEC 15  // Seconds the motor keeps going CW past HOME at the end of a cycle, then reverses CCW back to HOME. First half of the sand "shake". 0 disables both phases.
#define CYCLE_LEVEL_BACK_OVERSHOOT_SEC 5 // After the CCW return reaches HOME again, the motor keeps going CCW for this many seconds (back-shake in the opposite direction), then reverses CW back to HOME and stops. Hardcoded — not exposed in Settings to keep the timing tab simple.

// --- WEIGHT SENSOR (optional) ---
#define WEIGHT_SENSOR_ENABLED   false   // Set true to enable
#define WEIGHT_SENSOR_CAPACITY  20      // Cell capacity in kg: 20 or 50
#define WEIGHT_HX711_DOUT_PIN   34
#define WEIGHT_HX711_SCK_PIN    35
#define WEIGHT_CAT_THRESHOLD_KG 2.0f    // Minimum kg increase to detect a cat
#define WEIGHT_TARE_ON_BOOT     true    // Auto-tare on startup
#define WEIGHT_SAMPLE_RATE_MS   200     // How often to read the cell

// --- ADVANCED PRESENCE SENSOR (optional, HLK-LD2410C) ---
// Moved off GPIO 16/17 (the original UART2 defaults) because GPIO 16
// is now the cat micro-switch by default. UART can be remapped to any
// free GPIOs on ESP32, so 4/5 work fine.
#define PRESENCE_SENSOR_ENABLED false
#define PRESENCE_SENSOR_RX_PIN  4
#define PRESENCE_SENSOR_TX_PIN  5
#define PRESENCE_SENSOR_BAUD    256000

// --- NETWORK ---
#define WIFI_SSID           "your_wifi_ssid"
#define WIFI_PASSWORD       "your_wifi_password"
#define HOSTNAME            "openlitter"      // Access via openlitter.local
#define WEB_PORT            80
#define WIFI_CONNECT_TIMEOUT_SEC    20
#define WIFI_RECONNECT_INTERVAL_SEC 30
#define WIFI_MAX_FAILED_ATTEMPTS    3

// --- ACCESS POINT (recovery) ---
#define AP_SSID             "OpenLitter-Setup"
#define AP_PASSWORD         "openlitter"
#define AP_IP               IPAddress(192, 168, 4, 1)

// --- MQTT (optional) ---
#define MQTT_ENABLED        false
#define MQTT_BROKER         "192.168.1.100"
#define MQTT_PORT           1883
#define MQTT_USER           ""
#define MQTT_PASSWORD       ""
#define MQTT_TOPIC_BASE     "openlitter"
#define MQTT_HA_DISCOVERY   true
#define MQTT_HA_PREFIX      "homeassistant"
#define MQTT_RECONNECT_INTERVAL_MS 5000

// --- OTA Updates ---
#define OTA_ENABLED         true
#define OTA_PASSWORD        "openlitter"

// --- HISTORY ---
#define HISTORY_MAX_ENTRIES 20      // Number of cleaning cycles to store

// --- PERSISTENCE (NVS / ESP32 Preferences) ---
// User data (wifi creds, settings, history) lives in NVS so that
// re-flashing the LittleFS partition (pio run -t uploadfs) doesn't
// wipe anything the user has configured.
#define NVS_NAMESPACE       "openlitter"
#define NVS_KEY_WIFI_SSID   "wifi_ssid"
#define NVS_KEY_WIFI_PASS   "wifi_pass"
#define NVS_KEY_SETTINGS    "settings"
#define NVS_KEY_HISTORY     "history"
