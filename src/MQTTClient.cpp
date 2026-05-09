/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#include "MQTTClient.h"
#include "Settings.h"
#include "StateMachine.h"
#include "Sensors.h"
#include "config.h"

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

namespace MQTTClient {

namespace {

WiFiClient   netClient;
PubSubClient mqtt(netClient);
uint32_t lastReconnectMs = 0;
String   uniqueId;
bool     discoveryPublished = false;

String topic(const char *suffix) {
    String t = settings.mqttTopicBase;
    t += '/';
    t += suffix;
    return t;
}

void onMessage(char *topic, byte *payload, unsigned int length) {
    String t(topic);
    String b;
    b.reserve(length);
    for (unsigned int i = 0; i < length; ++i) b += (char)payload[i];
    Serial.printf("[MQTT] %s = %s\n", t.c_str(), b.c_str());

    if (t.endsWith("/command/cycle"))   StateMachine::requestCycle();
    else if (t.endsWith("/command/empty"))  StateMachine::requestEmpty();
    else if (t.endsWith("/command/reset"))  StateMachine::requestReset();
    else if (t.endsWith("/command/pause"))  StateMachine::requestPause();
    else if (t.endsWith("/command/resume")) StateMachine::requestResume();
    else if (t.endsWith("/command/tare"))   Sensors::tareWeight();
}

void publishHaDiscovery() {
    if (!settings.mqttHaDiscovery || discoveryPublished) return;
    String availTopic = topic("availability");
    String stateTopic = topic("state");

    auto buildDevice = [&](JsonObject dev) {
        dev["identifiers"][0] = uniqueId;
        dev["name"] = "OpenLitter";
        dev["model"] = "ESP32";
        dev["manufacturer"] = "davdlic";
        dev["sw_version"] = OPENLITTER_VERSION;
    };

    auto pub = [&](const char *component, const char *objectId, JsonDocument &d) {
        String t = String(MQTT_HA_PREFIX) + "/" + component +
                   "/openlitter/" + objectId + "/config";
        char buf[1024];
        size_t n = serializeJson(d, buf, sizeof(buf));
        mqtt.publish(t.c_str(), (const uint8_t *)buf, n, true);
    };

    {
        JsonDocument d;
        d["name"] = "OpenLitter State";
        d["uniq_id"] = uniqueId + "_state";
        d["stat_t"] = stateTopic;
        d["avty_t"] = availTopic;
        buildDevice(d["dev"].to<JsonObject>());
        pub("sensor", "state", d);
    }
    {
        JsonDocument d;
        d["name"] = "OpenLitter Weight";
        d["uniq_id"] = uniqueId + "_weight";
        d["stat_t"] = topic("weight");
        d["unit_of_meas"] = "kg";
        d["dev_cla"] = "weight";
        d["avty_t"] = availTopic;
        buildDevice(d["dev"].to<JsonObject>());
        pub("sensor", "weight", d);
    }
    {
        JsonDocument d;
        d["name"] = "OpenLitter Cycle Count";
        d["uniq_id"] = uniqueId + "_cycle_count";
        d["stat_t"] = topic("cycle_count");
        d["stat_cla"] = "total_increasing";
        d["avty_t"] = availTopic;
        buildDevice(d["dev"].to<JsonObject>());
        pub("sensor", "cycle_count", d);
    }
    {
        JsonDocument d;
        d["name"] = "OpenLitter Cat Present";
        d["uniq_id"] = uniqueId + "_cat";
        d["stat_t"] = topic("cat_present");
        d["pl_on"]  = "true";
        d["pl_off"] = "false";
        d["dev_cla"] = "occupancy";
        d["avty_t"] = availTopic;
        buildDevice(d["dev"].to<JsonObject>());
        pub("binary_sensor", "cat", d);
    }

    struct Btn { const char *id; const char *name; const char *cmd; };
    Btn buttons[] = {
        {"cycle",  "OpenLitter Cycle",  "command/cycle"},
        {"empty",  "OpenLitter Empty",  "command/empty"},
        {"reset",  "OpenLitter Reset",  "command/reset"},
        {"pause",  "OpenLitter Pause",  "command/pause"},
        {"resume", "OpenLitter Resume", "command/resume"},
    };
    for (auto &b : buttons) {
        JsonDocument d;
        d["name"] = b.name;
        d["uniq_id"] = uniqueId + "_btn_" + b.id;
        d["cmd_t"] = topic(b.cmd);
        d["pl_prs"] = "press";
        d["avty_t"] = availTopic;
        buildDevice(d["dev"].to<JsonObject>());
        pub("button", b.id, d);
    }

    discoveryPublished = true;
    Serial.println("[MQTT] HA discovery published");
}

void onStateChange(StateMachine::State, StateMachine::State) {
    publishStatus();
}

bool reconnect() {
    if (mqtt.connected()) return true;
    if (WiFi.status() != WL_CONNECTED) return false;
    String clientId = String("openlitter-") + uniqueId;
    String avail = topic("availability");
    bool ok;
    if (strlen(settings.mqttUser) > 0) {
        ok = mqtt.connect(clientId.c_str(),
                          settings.mqttUser, settings.mqttPassword,
                          avail.c_str(), 0, true, "offline");
    } else {
        ok = mqtt.connect(clientId.c_str(),
                          nullptr, nullptr,
                          avail.c_str(), 0, true, "offline");
    }
    if (!ok) {
        Serial.printf("[MQTT] Connect failed, rc=%d\n", mqtt.state());
        return false;
    }
    mqtt.publish(avail.c_str(), "online", true);
    String cmd = topic("command/+");
    mqtt.subscribe(cmd.c_str());
    publishHaDiscovery();
    publishStatus();
    Serial.println("[MQTT] Connected");
    return true;
}

}  // namespace

void begin() {
    if (!settings.mqttEnabled) {
        Serial.println("[MQTT] Disabled");
        return;
    }
    uniqueId = WiFi.macAddress();
    uniqueId.replace(":", "");
    mqtt.setServer(settings.mqttBroker, settings.mqttPort);
    mqtt.setCallback(onMessage);
    mqtt.setBufferSize(1024);
    StateMachine::setOnStateChange(onStateChange);
    Serial.printf("[MQTT] Configured for %s:%u\n",
                  settings.mqttBroker, settings.mqttPort);
}

void applySettings() {
    discoveryPublished = false;
    if (mqtt.connected()) mqtt.disconnect();
    if (settings.mqttEnabled) {
        mqtt.setServer(settings.mqttBroker, settings.mqttPort);
    }
}

void loop() {
    if (!settings.mqttEnabled) return;
    if (!mqtt.connected()) {
        uint32_t now = millis();
        if (now - lastReconnectMs >= MQTT_RECONNECT_INTERVAL_MS) {
            lastReconnectMs = now;
            reconnect();
        }
        return;
    }
    mqtt.loop();
}

bool isConnected() { return mqtt.connected(); }

void publishStatus() {
    if (!mqtt.connected()) return;
    mqtt.publish(topic("state").c_str(),
                 StateMachine::stateName(StateMachine::current()), true);
    mqtt.publish(topic("cat_present").c_str(),
                 Sensors::isCatPresent() ? "true" : "false", true);
    if (settings.weightEnabled) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f", Sensors::getCurrentWeight());
        mqtt.publish(topic("weight").c_str(), buf, true);
    }
    char cnt[16];
    snprintf(cnt, sizeof(cnt), "%lu", (unsigned long)StateMachine::cycleCount());
    mqtt.publish(topic("cycle_count").c_str(), cnt, true);

    char ts[16];
    snprintf(ts, sizeof(ts), "%lu", (unsigned long)StateMachine::lastCycleTimestamp());
    mqtt.publish(topic("last_cycle").c_str(), ts, true);

    const char *err = StateMachine::errorMessage();
    if (err && err[0]) {
        mqtt.publish(topic("error").c_str(), err, true);
    }
}

}  // namespace MQTTClient
