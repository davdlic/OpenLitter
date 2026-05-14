/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#include "WebServer.h"
#include "Settings.h"
#include "StateMachine.h"
#include "Sensors.h"
#include "WiFiManager.h"
#include "MQTTClient.h"
#include "Motor.h"
#include "config.h"

#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <Update.h>
#include <Preferences.h>

namespace WebServer {

namespace {

AsyncWebServer server(WEB_PORT);
AsyncWebSocket ws("/ws");

void onStateChange(StateMachine::State, StateMachine::State) {
    broadcastStatus();
}

String buildStatusJson() {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    StateMachine::serializeStatus(obj);
    JsonObject net = obj["network"].to<JsonObject>();
    WiFiManager::serializeStatus(net);
    obj["mqtt_connected"] = MQTTClient::isConnected();
    JsonArray hist = obj["history"].to<JsonArray>();
    StateMachine::serializeHistory(hist);
    String out;
    serializeJson(doc, out);
    return out;
}

void wsEvent(AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type,
             void *, uint8_t *, size_t) {
    if (type == WS_EVT_CONNECT) {
        client->text(buildStatusJson());
    }
}

void sendJson(AsyncWebServerRequest *req, JsonDocument &doc, int code = 200) {
    String out;
    serializeJson(doc, out);
    req->send(code, "application/json", out);
}

void sendOk(AsyncWebServerRequest *req) {
    req->send(200, "application/json", "{\"ok\":true}");
}

void sendErr(AsyncWebServerRequest *req, int code, const char *msg) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = msg;
    String out;
    serializeJson(doc, out);
    req->send(code, "application/json", out);
}

void registerRoutes() {
    // Static PWA assets from LittleFS
    server.serveStatic("/", LittleFS, "/")
          .setDefaultFile("index.html")
          .setCacheControl("max-age=600");

    // --- Status & history ---
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", buildStatusJson());
    });

    server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        StateMachine::serializeHistory(arr);
        sendJson(req, doc);
    });

    server.on("/api/history/clear", HTTP_POST, [](AsyncWebServerRequest *req) {
        StateMachine::clearHistory();
        sendOk(req);
    });

    // --- Commands ---
    server.on("/api/cycle",  HTTP_POST, [](AsyncWebServerRequest *req) {
        bool ok = StateMachine::requestCycle();
        ok ? sendOk(req) : sendErr(req, 409, "Not idle");
    });
    server.on("/api/empty",  HTTP_POST, [](AsyncWebServerRequest *req) {
        bool ok = StateMachine::requestEmpty();
        ok ? sendOk(req) : sendErr(req, 409, "Not idle");
    });
    server.on("/api/reset",  HTTP_POST, [](AsyncWebServerRequest *req) {
        bool ok = StateMachine::requestReset();
        ok ? sendOk(req) : sendErr(req, 409, "Cannot reset from current state");
    });
    server.on("/api/pause",  HTTP_POST, [](AsyncWebServerRequest *req) {
        bool ok = StateMachine::requestPause();
        ok ? sendOk(req) : sendErr(req, 409, "Not in motion");
    });
    server.on("/api/resume", HTTP_POST, [](AsyncWebServerRequest *req) {
        bool ok = StateMachine::requestResume();
        ok ? sendOk(req) : sendErr(req, 409, "Not paused");
    });
    server.on("/api/tare", HTTP_POST, [](AsyncWebServerRequest *req) {
        Sensors::tareWeight();
        sendOk(req);
    });

    // --- Config ---
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();
        settings.toJson(obj, /*redactSecrets=*/true);
        sendJson(req, doc);
    });

    auto configHandler = new AsyncCallbackJsonWebHandler("/api/config",
        [](AsyncWebServerRequest *req, JsonVariant &json) {
            JsonObjectConst obj = json.as<JsonObjectConst>();
            bool changed = settings.applyJson(obj);
            if (changed) settings.save();
            Motor::applySettings();
            Sensors::applySettings();
            MQTTClient::applySettings();
            JsonDocument res;
            res["ok"] = true;
            res["changed"] = changed;
            sendJson(req, res);
        });
    server.addHandler(configHandler);

    // --- Network: scan / provision / launch AP ---
    server.on("/api/network/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        WiFiManager::scanNetworks(arr);
        sendJson(req, doc);
    });

    auto provHandler = new AsyncCallbackJsonWebHandler("/api/network/provision",
        [](AsyncWebServerRequest *req, JsonVariant &json) {
            JsonObjectConst obj = json.as<JsonObjectConst>();
            const char *ssid = obj["ssid"] | "";
            const char *pwd  = obj["password"] | "";
            if (!ssid[0]) { sendErr(req, 400, "Missing ssid"); return; }
            // Reply first, then provision (provisioning is blocking).
            JsonDocument res;
            res["ok"] = true;
            res["message"] = "Connecting...";
            sendJson(req, res);
            WiFiManager::provision(ssid, pwd);
        });
    server.addHandler(provHandler);

    server.on("/api/network/launch_ap", HTTP_POST, [](AsyncWebServerRequest *req) {
        WiFiManager::launchRecoveryAp();
        sendOk(req);
    });

    // --- System ---
    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *req) {
        sendOk(req);
        delay(200);
        ESP.restart();
    });

    server.on("/api/factory_reset", HTTP_POST, [](AsyncWebServerRequest *req) {
        // Wipe everything user-owned: NVS namespace + any legacy LittleFS
        // config files. LittleFS PWA assets are untouched.
        Preferences prefs;
        if (prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) {
            prefs.clear();
            prefs.end();
        }
        LittleFS.remove("/wifi.json");
        LittleFS.remove("/settings.json");
        LittleFS.remove("/history.json");
        sendOk(req);
        delay(200);
        ESP.restart();
    });

    // --- Web-based firmware update ---
    server.on("/api/update", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            bool ok = !Update.hasError();
            JsonDocument doc;
            doc["ok"] = ok;
            doc["error"] = ok ? "" : Update.errorString();
            String out;
            serializeJson(doc, out);
            AsyncWebServerResponse *res = req->beginResponse(ok ? 200 : 500,
                                                             "application/json", out);
            res->addHeader("Connection", "close");
            req->send(res);
            if (ok) { delay(200); ESP.restart(); }
        },
        [](AsyncWebServerRequest *, String filename, size_t index, uint8_t *data,
           size_t len, bool final) {
            if (index == 0) {
                Serial.printf("[Update] Begin %s\n", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
            }
            if (Update.write(data, len) != len) Update.printError(Serial);
            if (final) {
                if (Update.end(true)) Serial.printf("[Update] OK (%u bytes)\n",
                                                    (unsigned)(index + len));
                else Update.printError(Serial);
            }
        });

    server.onNotFound([](AsyncWebServerRequest *req) {
        if (req->method() == HTTP_OPTIONS) {
            req->send(200);
            return;
        }
        // PWA fallback: serve index.html for any non-API path so client routing works.
        if (!req->url().startsWith("/api/")) {
            req->send(LittleFS, "/index.html", "text/html");
            return;
        }
        req->send(404, "application/json", "{\"error\":\"not found\"}");
    });

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
}

}  // namespace

void begin() {
    ws.onEvent(wsEvent);
    server.addHandler(&ws);
    registerRoutes();
    server.begin();
    StateMachine::setOnStateChange(onStateChange);
    Serial.printf("[Web] Listening on port %u\n", WEB_PORT);
}

void loop() {
    ws.cleanupClients();
}

void broadcastStatus() {
    if (ws.count() == 0) return;
    ws.textAll(buildStatusJson());
}

}  // namespace WebServer
