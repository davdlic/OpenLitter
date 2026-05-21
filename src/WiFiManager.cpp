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
#include <DNSServer.h>
#include <Preferences.h>

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

// Captive portal: a wildcard DNS server on the AP that resolves every
// hostname to the AP's IP. Combined with the WebServer's catch-all that
// serves index.html for unknown paths, this triggers the OS captive
// portal popup on iOS / Android / Windows when a phone joins the AP.
DNSServer dnsServer;
bool      dnsStarted = false;
constexpr uint16_t DNS_PORT = 53;

bool loadCreds() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) return false;
    creds.ssid     = prefs.getString(NVS_KEY_WIFI_SSID, "");
    creds.password = prefs.getString(NVS_KEY_WIFI_PASS, "");
    prefs.end();
    return creds.ssid.length() > 0;
}

bool saveCreds(const char *ssid, const char *password) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) return false;
    prefs.putString(NVS_KEY_WIFI_SSID, ssid ? ssid : "");
    prefs.putString(NVS_KEY_WIFI_PASS, password ? password : "");
    prefs.end();
    creds.ssid = ssid ? ssid : "";
    creds.password = password ? password : "";
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

void startCaptiveDns() {
    if (dnsStarted) return;
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    if (dnsServer.start(DNS_PORT, "*", AP_IP)) {
        dnsStarted = true;
        Serial.println("[WiFi] Captive DNS started on port 53");
    } else {
        Serial.println("[WiFi] Captive DNS failed to start");
    }
}

void stopCaptiveDns() {
    if (!dnsStarted) return;
    dnsServer.stop();
    dnsStarted = false;
}

void startRecoveryAp() {
    Serial.println("[WiFi] Starting recovery AP");
    // ESP32 Arduino 3.x: softAP() must be called BEFORE softAPConfig() —
    // otherwise the DHCP server isn't always reconfigured to the new range.
    bool ok = WiFi.softAP(AP_SSID, settings.apPassword);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    Serial.printf("[WiFi] AP '%s' @ %s (softAP %s)\n",
                  AP_SSID, WiFi.softAPIP().toString().c_str(),
                  ok ? "ok" : "FAILED");
    startCaptiveDns();
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
    startMdnsIfNeeded();   // mDNS works on AP too — openlitter.local
                           // resolves before the user has any home WiFi
                           // configured.
    currentMode = haveCreds ? Mode::AP_STA : Mode::AP;
    failedAttempts = haveCreds ? 1 : 0;
}

void loop() {
    if (dnsStarted) dnsServer.processNextRequest();

    if (WiFi.status() == WL_CONNECTED) {
        if (currentMode == Mode::OFFLINE || currentMode == Mode::AP) {
            currentMode = Mode::STATION;
            failedAttempts = 0;
            startMdnsIfNeeded();
            // If recovery AP is up, take it down once we're connected.
            if (WiFi.getMode() == WIFI_AP_STA) {
                WiFi.softAPdisconnect(true);
                WiFi.mode(WIFI_STA);
                stopCaptiveDns();
                currentMode = Mode::STATION;
            }
        }
        return;
    }

    // Disconnected branch
    if (currentMode == Mode::AP) {
        // No creds, just sit in AP — keep serving DNS for the captive portal.
        return;
    }

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
    stopCaptiveDns();
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
    startMdnsIfNeeded();
    currentMode = Mode::AP;
    return false;
}

void launchRecoveryAp() {
    if (currentMode == Mode::AP || currentMode == Mode::AP_STA) return;
    WiFi.mode(WIFI_AP_STA);
    startRecoveryAp();
    startMdnsIfNeeded();
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
