/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#include "Settings.h"
#include "Log.h"
#include "config.h"

#include <Preferences.h>

Settings settings;

namespace {

template <size_t N>
void copyString(char (&dst)[N], const char *src) {
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, N - 1);
    dst[N - 1] = '\0';
}

template <size_t N>
void copyJsonString(char (&dst)[N], const JsonVariantConst &v) {
    if (v.is<const char *>()) {
        copyString(dst, v.as<const char *>());
    }
}

}  // namespace

void Settings::resetToDefaults() {
    motorIn1Pin = MOTOR_IN1_PIN;
    motorIn2Pin = MOTOR_IN2_PIN;
    motorEnPin  = MOTOR_EN_PIN;
    motorSpeed  = MOTOR_SPEED;

    hallHomePin   = HALL_HOME_PIN;
    hallDumpPin   = HALL_DUMP_PIN;
    hallActiveLow = (HALL_ACTIVE_STATE == LOW);

    catSensorPin        = CAT_SENSOR_PIN;
    catSensorType       = CAT_SENSOR_TYPE;
    catSensorDebounceMs = CAT_SENSOR_DEBOUNCE_MS;

    waitAfterCatMin    = WAIT_AFTER_CAT_MIN;
    catTimeoutMin      = CAT_TIMEOUT_MIN;
    cycleTimeoutSec    = CYCLE_TIMEOUT_SEC;
    antiPinchReverseMs = ANTI_PINCH_REVERSE_MS;
    cycleDumpAdvanceSec     = CYCLE_DUMP_ADVANCE_SEC;
    emptyDumpAdvanceSec     = EMPTY_DUMP_ADVANCE_SEC;
    cycleDumpPauseSec       = CYCLE_DUMP_PAUSE_SEC;
    emptyDumpPauseSec       = EMPTY_DUMP_PAUSE_SEC;
    cycleLevelOvershootSec  = CYCLE_LEVEL_OVERSHOOT_SEC;

    weightEnabled     = WEIGHT_SENSOR_ENABLED;
    weightCapacityKg  = WEIGHT_SENSOR_CAPACITY;
    weightDoutPin     = WEIGHT_HX711_DOUT_PIN;
    weightSckPin      = WEIGHT_HX711_SCK_PIN;
    weightThresholdKg = WEIGHT_CAT_THRESHOLD_KG;
    weightTareOnBoot  = WEIGHT_TARE_ON_BOOT;

    presenceEnabled = PRESENCE_SENSOR_ENABLED;
    presenceRxPin   = PRESENCE_SENSOR_RX_PIN;
    presenceTxPin   = PRESENCE_SENSOR_TX_PIN;

    copyString(hostname,   HOSTNAME);
    copyString(apPassword, AP_PASSWORD);
    useStaticIp = false;
    staticIp[0] = staticGateway[0] = staticSubnet[0] = '\0';

    mqttEnabled    = MQTT_ENABLED;
    copyString(mqttBroker,    MQTT_BROKER);
    mqttPort       = MQTT_PORT;
    copyString(mqttUser,      MQTT_USER);
    copyString(mqttPassword,  MQTT_PASSWORD);
    copyString(mqttTopicBase, MQTT_TOPIC_BASE);
    mqttHaDiscovery = MQTT_HA_DISCOVERY;

    otaEnabled = OTA_ENABLED;
    copyString(otaPassword, OTA_PASSWORD);

    historyMaxEntries = HISTORY_MAX_ENTRIES;
}

bool Settings::load() {
    resetToDefaults();
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) {
        Log::warn("Settings NVS open failed, using defaults");
        return false;
    }
    String json = prefs.getString(NVS_KEY_SETTINGS, "");
    prefs.end();
    if (json.isEmpty()) {
        Log::info("Settings: no NVS entry, using defaults");
        return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Log::error("Settings NVS JSON parse error: %s", err.c_str());
        return false;
    }
    applyJson(doc.as<JsonObjectConst>());
    Log::info("Settings loaded from NVS");
    return true;
}

bool Settings::save() const {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    toJson(obj, /*redactSecrets=*/false);
    String json;
    serializeJson(doc, json);

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) {
        Log::error("Settings NVS open RW failed");
        return false;
    }
    size_t written = prefs.putString(NVS_KEY_SETTINGS, json);
    prefs.end();
    if (written == 0) {
        Log::error("Settings NVS putString returned 0 bytes");
        return false;
    }
    Log::info("Settings saved to NVS (%u bytes)", (unsigned)written);
    return true;
}

bool Settings::applyJson(const JsonObjectConst &obj) {
    if (obj.isNull()) return false;
    bool changed = false;

    // ArduinoJson 7 only ships Converter specialisations for int / long /
    // float / bool, so for narrow integer fields (int8_t, uint8_t, uint16_t)
    // we read as long and let the destination type do the narrowing. This
    // also keeps the generic lambda's body well-formed for every call site.
    auto setI = [&](const char *k, auto &dst) {
        if (!obj[k].isNull()) {
            dst = (typename std::decay<decltype(dst)>::type)(obj[k].as<long>());
            changed = true;
        }
    };
    auto setB = [&](const char *k, bool &dst) {
        if (!obj[k].isNull()) { dst = obj[k].as<bool>(); changed = true; }
    };
    auto setF = [&](const char *k, float &dst) {
        if (!obj[k].isNull()) { dst = obj[k].as<float>(); changed = true; }
    };

    setI("motor_in1_pin", motorIn1Pin);
    setI("motor_in2_pin", motorIn2Pin);
    setI("motor_en_pin",  motorEnPin);
    setI("motor_speed",   motorSpeed);

    setI("hall_home_pin", hallHomePin);
    setI("hall_dump_pin", hallDumpPin);
    setB("hall_active_low", hallActiveLow);

    setI("cat_sensor_pin",         catSensorPin);
    setI("cat_sensor_type",        catSensorType);
    setI("cat_sensor_debounce_ms", catSensorDebounceMs);

    setI("wait_after_cat_min",     waitAfterCatMin);
    setI("cat_timeout_min",        catTimeoutMin);
    setI("cycle_timeout_sec",      cycleTimeoutSec);
    setI("anti_pinch_reverse_ms",  antiPinchReverseMs);
    setI("cycle_dump_advance_sec",    cycleDumpAdvanceSec);
    setI("empty_dump_advance_sec",    emptyDumpAdvanceSec);
    setI("cycle_dump_pause_sec",      cycleDumpPauseSec);
    setI("empty_dump_pause_sec",      emptyDumpPauseSec);
    setI("cycle_level_overshoot_sec", cycleLevelOvershootSec);

    setB("weight_enabled",      weightEnabled);
    setI("weight_capacity_kg",  weightCapacityKg);
    setI("weight_dout_pin",     weightDoutPin);
    setI("weight_sck_pin",      weightSckPin);
    setF("weight_threshold_kg", weightThresholdKg);
    setB("weight_tare_on_boot", weightTareOnBoot);

    setB("presence_enabled", presenceEnabled);
    setI("presence_rx_pin",  presenceRxPin);
    setI("presence_tx_pin",  presenceTxPin);

    if (!obj["hostname"].isNull())     { copyJsonString(hostname,     obj["hostname"]);     changed = true; }
    if (!obj["ap_password"].isNull())  { copyJsonString(apPassword,   obj["ap_password"]);  changed = true; }
    setB("use_static_ip", useStaticIp);
    if (!obj["static_ip"].isNull())      { copyJsonString(staticIp,      obj["static_ip"]);      changed = true; }
    if (!obj["static_gateway"].isNull()) { copyJsonString(staticGateway, obj["static_gateway"]); changed = true; }
    if (!obj["static_subnet"].isNull())  { copyJsonString(staticSubnet,  obj["static_subnet"]);  changed = true; }

    setB("mqtt_enabled", mqttEnabled);
    if (!obj["mqtt_broker"].isNull())     { copyJsonString(mqttBroker,    obj["mqtt_broker"]);     changed = true; }
    setI("mqtt_port", mqttPort);
    if (!obj["mqtt_user"].isNull())       { copyJsonString(mqttUser,      obj["mqtt_user"]);       changed = true; }
    if (!obj["mqtt_password"].isNull())   { copyJsonString(mqttPassword,  obj["mqtt_password"]);   changed = true; }
    if (!obj["mqtt_topic_base"].isNull()) { copyJsonString(mqttTopicBase, obj["mqtt_topic_base"]); changed = true; }
    setB("mqtt_ha_discovery", mqttHaDiscovery);

    setB("ota_enabled", otaEnabled);
    if (!obj["ota_password"].isNull()) { copyJsonString(otaPassword, obj["ota_password"]); changed = true; }

    setI("history_max_entries", historyMaxEntries);

    return changed;
}

void Settings::toJson(JsonObject obj, bool redactSecrets) const {
    obj["motor_in1_pin"] = motorIn1Pin;
    obj["motor_in2_pin"] = motorIn2Pin;
    obj["motor_en_pin"]  = motorEnPin;
    obj["motor_speed"]   = motorSpeed;

    obj["hall_home_pin"]   = hallHomePin;
    obj["hall_dump_pin"]   = hallDumpPin;
    obj["hall_active_low"] = hallActiveLow;

    obj["cat_sensor_pin"]         = catSensorPin;
    obj["cat_sensor_type"]        = catSensorType;
    obj["cat_sensor_debounce_ms"] = catSensorDebounceMs;

    obj["wait_after_cat_min"]    = waitAfterCatMin;
    obj["cat_timeout_min"]       = catTimeoutMin;
    obj["cycle_timeout_sec"]     = cycleTimeoutSec;
    obj["anti_pinch_reverse_ms"] = antiPinchReverseMs;
    obj["cycle_dump_advance_sec"]    = cycleDumpAdvanceSec;
    obj["empty_dump_advance_sec"]    = emptyDumpAdvanceSec;
    obj["cycle_dump_pause_sec"]      = cycleDumpPauseSec;
    obj["empty_dump_pause_sec"]      = emptyDumpPauseSec;
    obj["cycle_level_overshoot_sec"] = cycleLevelOvershootSec;

    obj["weight_enabled"]      = weightEnabled;
    obj["weight_capacity_kg"]  = weightCapacityKg;
    obj["weight_dout_pin"]     = weightDoutPin;
    obj["weight_sck_pin"]      = weightSckPin;
    obj["weight_threshold_kg"] = weightThresholdKg;
    obj["weight_tare_on_boot"] = weightTareOnBoot;

    obj["presence_enabled"] = presenceEnabled;
    obj["presence_rx_pin"]  = presenceRxPin;
    obj["presence_tx_pin"]  = presenceTxPin;

    obj["hostname"]    = hostname;
    obj["ap_password"] = redactSecrets ? "" : apPassword;
    obj["use_static_ip"]   = useStaticIp;
    obj["static_ip"]       = staticIp;
    obj["static_gateway"]  = staticGateway;
    obj["static_subnet"]   = staticSubnet;

    obj["mqtt_enabled"]      = mqttEnabled;
    obj["mqtt_broker"]       = mqttBroker;
    obj["mqtt_port"]         = mqttPort;
    obj["mqtt_user"]         = mqttUser;
    obj["mqtt_password"]     = redactSecrets ? "" : mqttPassword;
    obj["mqtt_topic_base"]   = mqttTopicBase;
    obj["mqtt_ha_discovery"] = mqttHaDiscovery;

    obj["ota_enabled"]  = otaEnabled;
    obj["ota_password"] = redactSecrets ? "" : otaPassword;

    obj["history_max_entries"] = historyMaxEntries;
}
