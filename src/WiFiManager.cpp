/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#include "WiFiManager.h"
#include "Settings.h"
#include "config.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

namespace WiFiManager {

namespace {

struct WifiCreds {
    String ssid;
    String password;
};

WifiCreds creds;
Mode currentMode = Mode::OFFLINE;
uint32_t lastReconnectAttemptMs = 0;
uint8_t  failedAttempts = 0;
bool     mdnsStarted = false;

bool loadCreds() {
    if (!LittleFS.exists(FS_WIFI_CONFIG)) return false;
    File f = LittleFS.open(FS_WIFI_CONFIG, "r");
    if (!f) return false;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[WiFi] wifi.json parse error: %s\n", err.c_str());
        return false;
    }
    creds.ssid = (const char *)(doc["ssid"] | "");
    creds.password = (const char *)(doc["password"] | "");
    return creds.ssid.length() > 0;
}

bool saveCreds(const char *ssid, const char *password) {
    JsonDocument doc;
    doc["ssid"] = ssid;
    doc["password"] = password;
    File f = LittleFS.open(FS_WIFI_CONFIG, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    creds.ssid = ssid;
    creds.password = password;
    return true;
}

void applyHostname() {
    WiFi.setHostname(settings.hostname);
}

void applyStaticIpIfConfigured() {
    if (!settings.useStaticIp) return;
    IPAddress ip, gw, sn;
    if (ip.fromString(settings.staticIp) &&
        gw.fromString(settings.staticGateway) &&
        sn.fromString(settings.staticSubnet)) {
        WiFi.config(ip, gw, sn);
    }
}

bool tryConnectStation(uint32_t timeoutMs) {
    if (creds.ssid.isEmpty()) return false;
    Serial.printf("[WiFi] Connecting to '%s'...\n", creds.ssid.c_str());
    WiFi.mode(WIFI_STA);
    applyHostname();
    applyStaticIpIfConfigured();
    WiFi.begin(creds.ssid.c_str(), creds.password.c_str());
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WiFi] Connected: IP=%s, RSSI=%d dBm\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
            return true;
        }
        delay(200);
    }
    Serial.println("[WiFi] Connect timeout");
    return false;
}

void startRecoveryAp() {
    Serial.println("[WiFi] Starting recovery AP");
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID, settings.apPassword);
    Serial.printf("[WiFi] AP '%s' @ %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

void startMdnsIfNeeded() {
    if (mdnsStarted) return;
    if (MDNS.begin(settings.hostname)) {
        MDNS.addService("http", "tcp", WEB_PORT);
        mdnsStarted = true;
        Serial.printf("[WiFi] mDNS at %s.local\n", settings.hostname);
    }
}

}  // namespace

void begin() {
    bool haveCreds = loadCreds();
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);

    if (haveCreds && tryConnectStation(WIFI_CONNECT_TIMEOUT_SEC * 1000UL)) {
        currentMode = Mode::STATION;
        startMdnsIfNeeded();
        return;
    }

    // Either no creds or failed to connect: bring up the recovery AP.
    WiFi.mode(haveCreds ? WIFI_AP_STA : WIFI_AP);
    applyHostname();
    startRecoveryAp();
    currentMode = haveCreds ? Mode::AP_STA : Mode::AP;
    failedAttempts = haveCreds ? 1 : 0;
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (currentMode == Mode::OFFLINE || currentMode == Mode::AP) {
            currentMode = Mode::STATION;
            failedAttempts = 0;
            startMdnsIfNeeded();
            // If recovery AP is up, take it down once we're connected.
            if (WiFi.getMode() == WIFI_AP_STA) {
                WiFi.softAPdisconnect(true);
                WiFi.mode(WIFI_STA);
                currentMode = Mode::STATION;
            }
        }
        return;
    }

    // Disconnected branch
    if (currentMode == Mode::AP) return;  // No creds, just sit in AP.

    uint32_t now = millis();
    if (now - lastReconnectAttemptMs < WIFI_RECONNECT_INTERVAL_SEC * 1000UL) return;
    lastReconnectAttemptMs = now;

    Serial.println("[WiFi] STA disconnected, retrying...");
    WiFi.reconnect();
    delay(500);
    if (WiFi.status() != WL_CONNECTED) {
        failedAttempts++;
        if (failedAttempts >= WIFI_MAX_FAILED_ATTEMPTS &&
            currentMode != Mode::AP_STA) {
            Serial.println("[WiFi] Too many failures, raising recovery AP");
            WiFi.mode(WIFI_AP_STA);
            startRecoveryAp();
            currentMode = Mode::AP_STA;
        }
    }
}

bool isConnected() { return WiFi.status() == WL_CONNECTED; }

Mode mode() { return currentMode; }

bool provision(const char *ssid, const char *password) {
    Serial.printf("[WiFi] Provisioning '%s'\n", ssid);
    if (!saveCreds(ssid, password)) {
        Serial.println("[WiFi] Failed to write wifi.json");
        return false;
    }
    WiFi.disconnect(true, true);
    delay(200);
    WiFi.mode(WIFI_STA);
    applyHostname();
    applyStaticIpIfConfigured();
    WiFi.begin(ssid, password);
    uint32_t start = millis();
    while (millis() - start < WIFI_CONNECT_TIMEOUT_SEC * 1000UL) {
        if (WiFi.status() == WL_CONNECTED) {
            currentMode = Mode::STATION;
            failedAttempts = 0;
            startMdnsIfNeeded();
            Serial.printf("[WiFi] Provisioned, IP=%s\n",
                          WiFi.localIP().toString().c_str());
            return true;
        }
        delay(200);
    }
    Serial.println("[WiFi] Provisioning failed, AP stays up");
    WiFi.mode(WIFI_AP);
    startRecoveryAp();
    currentMode = Mode::AP;
    return false;
}

void launchRecoveryAp() {
    if (currentMode == Mode::AP || currentMode == Mode::AP_STA) return;
    WiFi.mode(WIFI_AP_STA);
    startRecoveryAp();
    currentMode = isConnected() ? Mode::AP_STA : Mode::AP;
}

void scanNetworks(JsonArray arr) {
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; ++i) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = WiFi.SSID(i);
        o["rssi"] = WiFi.RSSI(i);
        o["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();
}

void serializeStatus(JsonObject obj) {
    const char *modeStr = "offline";
    switch (currentMode) {
        case Mode::STATION: modeStr = "station"; break;
        case Mode::AP:      modeStr = "ap"; break;
        case Mode::AP_STA:  modeStr = "ap_sta"; break;
        default: break;
    }
    obj["mode"]     = modeStr;
    obj["ssid"]     = WiFi.SSID();
    obj["ip"]       = isConnected() ? WiFi.localIP().toString() : String("");
    obj["gateway"]  = isConnected() ? WiFi.gatewayIP().toString() : String("");
    obj["rssi"]     = isConnected() ? WiFi.RSSI() : 0;
    obj["ap_ip"]    = (currentMode == Mode::AP || currentMode == Mode::AP_STA)
                      ? WiFi.softAPIP().toString() : String("");
    obj["hostname"] = settings.hostname;
}

}  // namespace WiFiManager
