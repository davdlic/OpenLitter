/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#include "StateMachine.h"
#include "Settings.h"
#include "Motor.h"
#include "Sensors.h"
#include "config.h"

#include <Preferences.h>
#include <time.h>

namespace StateMachine {

namespace {

State currentState = State::IDLE;
State stateBeforePause = State::IDLE;

constexpr size_t MAX_CALLBACKS = 4;
StateChangeCallback onChangeCallbacks[MAX_CALLBACKS] = {nullptr};
size_t onChangeCount = 0;

void fireStateChange(State prev, State next) {
    for (size_t i = 0; i < onChangeCount; ++i) {
        if (onChangeCallbacks[i]) onChangeCallbacks[i](prev, next);
    }
}

const char *currentError = "";

uint32_t bootMillis = 0;
uint32_t cycleStartMs = 0;       // motion start time, for watchdog
uint32_t catLeftMs = 0;          // when WAITING started
uint32_t catEnteredMs = 0;       // when CAT_INSIDE started
uint32_t pauseStartedMs = 0;
uint32_t lastMotionProgressMs = 0;  // for anti-pinch
uint32_t overshootStartMs = 0;      // when an OVERSHOOT_* state began

uint32_t totalCycleCount = 0;
uint32_t lastCycleTs = 0;

// Circular buffer of CycleRecord
CycleRecord history[HISTORY_MAX_ENTRIES];
size_t      historyCount = 0;
size_t      historyHead  = 0;     // index of next slot to write

bool antiPinchActive = false;
uint32_t antiPinchStartedMs = 0;
State    antiPinchPrevState = State::IDLE;

void transition(State next) {
    if (next == currentState) return;
    State prev = currentState;
    currentState = next;
    Serial.printf("[State] %s -> %s\n", stateName(prev), stateName(next));
    fireStateChange(prev, next);
}

void enterError(const char *msg) {
    currentError = msg;
    Motor::stop();
    Serial.printf("[State] ERROR: %s\n", msg);
    transition(State::ERROR);
}

void recordCycle(bool completed, uint16_t durationSec, float weightKg) {
    CycleRecord rec;
    time_t now;
    time(&now);
    rec.timestamp = now;
    rec.durationSeconds = durationSec;
    rec.catWeightKg = weightKg;
    rec.completed = completed;
    history[historyHead] = rec;
    historyHead = (historyHead + 1) % HISTORY_MAX_ENTRIES;
    if (historyCount < HISTORY_MAX_ENTRIES) historyCount++;
    totalCycleCount++;
    lastCycleTs = (uint32_t)now;
}

// History is persisted to NVS as a single binary blob: a header with the
// ring-buffer metadata followed by the raw CycleRecord array. Versioned so
// we can grow the struct in future and detect/discard incompatible data.
struct HistoryBlobHeader {
    uint16_t magic;            // 'OL' marker
    uint8_t  version;          // bump when CycleRecord changes
    uint8_t  entrySize;        // sanity-check sizeof(CycleRecord)
    uint16_t capacity;         // HISTORY_MAX_ENTRIES at write time
    uint16_t count;            // valid records in ring
    uint16_t head;             // next write index
    uint16_t _reserved;        // align to 12 bytes
    uint32_t totalCycleCount;
    uint32_t lastCycleTs;
};

constexpr uint16_t HISTORY_MAGIC   = 0x4F4C;  // 'OL'
constexpr uint8_t  HISTORY_VERSION = 1;

void persistHistory() {
    HistoryBlobHeader hdr {
        HISTORY_MAGIC,
        HISTORY_VERSION,
        (uint8_t)sizeof(CycleRecord),
        (uint16_t)HISTORY_MAX_ENTRIES,
        (uint16_t)historyCount,
        (uint16_t)historyHead,
        0,
        totalCycleCount,
        lastCycleTs,
    };
    uint8_t buf[sizeof(hdr) + sizeof(history)];
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), history, sizeof(history));

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) return;
    prefs.putBytes(NVS_KEY_HISTORY, buf, sizeof(buf));
    prefs.end();
}

void loadHistory() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) return;
    size_t expected = sizeof(HistoryBlobHeader) + sizeof(history);
    size_t actual = prefs.getBytesLength(NVS_KEY_HISTORY);
    if (actual != expected) {
        prefs.end();
        return;
    }
    uint8_t buf[expected];
    prefs.getBytes(NVS_KEY_HISTORY, buf, expected);
    prefs.end();

    HistoryBlobHeader hdr;
    memcpy(&hdr, buf, sizeof(hdr));
    if (hdr.magic != HISTORY_MAGIC ||
        hdr.version != HISTORY_VERSION ||
        hdr.entrySize != sizeof(CycleRecord) ||
        hdr.capacity != HISTORY_MAX_ENTRIES) {
        Serial.println("[State] History blob incompatible, discarding");
        return;
    }
    memcpy(history, buf + sizeof(hdr), sizeof(history));
    historyCount     = hdr.count;
    historyHead      = hdr.head;
    totalCycleCount  = hdr.totalCycleCount;
    lastCycleTs      = hdr.lastCycleTs;
    Serial.printf("[State] History restored from NVS (%u entries)\n",
                  (unsigned)historyCount);
}

bool isMotionState(State s) {
    return s == State::CYCLING_CCW || s == State::CYCLING_CW
        || s == State::EMPTYING    || s == State::RESETTING
        || s == State::CYCLING_OVERSHOOT_DUMP
        || s == State::EMPTYING_OVERSHOOT;
}

void runMotionWatchdog() {
    if (!isMotionState(currentState)) return;
    uint32_t now = millis();
    if (now - cycleStartMs > (uint32_t)settings.cycleTimeoutSec * 1000UL) {
        enterError("Cycle watchdog timeout");
        return;
    }
    // Anti-pinch: if cat switch activates during motion, pause immediately.
    if (Sensors::isCatPresent()) {
        Serial.println("[State] Cat detected during motion -> PAUSED");
        Motor::stop();
        stateBeforePause = currentState;
        pauseStartedMs = now;
        transition(State::PAUSED);
        return;
    }
}

void handleIdle() {
    if (Sensors::isCatPresent() || Sensors::isCatOnScale() || Sensors::isPresenceDetected()) {
        catEnteredMs = millis();
        transition(State::CAT_INSIDE);
    }
}

bool catStillThere() {
    if (Sensors::isCatPresent()) return true;
    if (settings.weightEnabled && Sensors::isCatOnScale()) return true;
    if (settings.presenceEnabled && Sensors::isPresenceDetected()) return true;
    return false;
}

void handleCatInside() {
    uint32_t now = millis();
    bool fallback = (now - catEnteredMs) > (uint32_t)settings.catTimeoutMin * 60UL * 1000UL;
    if (!catStillThere() || fallback) {
        if (fallback) Serial.println("[State] Cat timeout fallback (assumed left)");
        catLeftMs = now;
        transition(State::WAITING);
    }
}

void handleWaiting() {
    uint32_t now = millis();
    if (catStillThere()) {
        // Cat came back: restart timer and go back to CAT_INSIDE.
        catEnteredMs = now;
        transition(State::CAT_INSIDE);
        return;
    }
    uint32_t waitMs = (uint32_t)settings.waitAfterCatMin * 60UL * 1000UL;
    if (now - catLeftMs >= waitMs) {
        // Time to clean.
        Motor::ccw(settings.motorSpeed);
        cycleStartMs = now;
        lastMotionProgressMs = now;
        transition(State::CYCLING_CCW);
    }
}

void handleCyclingCcw() {
    runMotionWatchdog();
    if (currentState != State::CYCLING_CCW) return;  // watchdog tripped
    if (Sensors::isDumpPosition()) {
        // Hit DUMP. Keep motor running CCW for the configured overshoot
        // so the globe goes a bit past 180 degrees — improves the dump
        // and leaves the litter level when the cycle finishes.
        overshootStartMs = millis();
        transition(State::CYCLING_OVERSHOOT_DUMP);
    }
}

void handleCyclingOvershootDump() {
    runMotionWatchdog();
    if (currentState != State::CYCLING_OVERSHOOT_DUMP) return;
    if (millis() - overshootStartMs >= (uint32_t)settings.cycleOvershootSec * 1000UL) {
        Motor::cw(settings.motorSpeed);
        lastMotionProgressMs = millis();
        transition(State::CYCLING_CW);
    }
}

void handleCyclingCw() {
    runMotionWatchdog();
    if (currentState != State::CYCLING_CW) return;
    if (Sensors::isHomePosition()) {
        Motor::stop();
        uint16_t dur = (uint16_t)((millis() - cycleStartMs) / 1000UL);
        recordCycle(true, dur, Sensors::getCurrentWeight());
        persistHistory();
        transition(State::IDLE);
    }
}

void handleEmptying() {
    runMotionWatchdog();
    if (currentState != State::EMPTYING) return;
    if (Sensors::isDumpPosition()) {
        // Same idea as cycle overshoot, but tuned for emptying (typically
        // a bit shorter — we only need full dump, not nice levelling).
        overshootStartMs = millis();
        transition(State::EMPTYING_OVERSHOOT);
    }
}

void handleEmptyingOvershoot() {
    runMotionWatchdog();
    if (currentState != State::EMPTYING_OVERSHOOT) return;
    if (millis() - overshootStartMs >= (uint32_t)settings.emptyOvershootSec * 1000UL) {
        Motor::stop();
        transition(State::RESETTING);
    }
}

void handleResetting() {
    runMotionWatchdog();
    if (currentState != State::RESETTING) return;
    if (Sensors::isHomePosition()) {
        Motor::stop();
        transition(State::IDLE);
    } else if (millis() - cycleStartMs > 1000) {
        // Need to start moving back to home.
        Motor::cw(settings.motorSpeed);
    }
}

void handlePaused() {
    uint32_t now = millis();
    if (!Sensors::isCatPresent() && (now - pauseStartedMs) >= PAUSED_AUTO_RESUME_SEC * 1000UL) {
        Serial.println("[State] Resuming after PAUSED grace period");
        cycleStartMs = now;
        switch (stateBeforePause) {
            case State::CYCLING_CCW:            Motor::ccw(settings.motorSpeed); break;
            case State::CYCLING_OVERSHOOT_DUMP: Motor::ccw(settings.motorSpeed); overshootStartMs = now; break;
            case State::CYCLING_CW:             Motor::cw(settings.motorSpeed);  break;
            case State::EMPTYING:               Motor::ccw(settings.motorSpeed); break;
            case State::EMPTYING_OVERSHOOT:     Motor::ccw(settings.motorSpeed); overshootStartMs = now; break;
            case State::RESETTING:              Motor::cw(settings.motorSpeed);  break;
            default: break;
        }
        transition(stateBeforePause);
        return;
    }
    if (Sensors::isCatPresent() && (now - pauseStartedMs) >= PAUSED_AUTO_RESUME_SEC * 1000UL) {
        enterError("Cat still detected after pause grace period");
    }
}

}  // namespace

void begin() {
    bootMillis = millis();
    loadHistory();
    if (Sensors::isHomePosition()) {
        Serial.println("[State] Initialised in IDLE (globe at HOME)");
        return;
    }
    // Globe is not at HOME on boot — could be a power cut mid-cycle, manual
    // repositioning, or first install. Drive CW into RESETTING; the existing
    // handleResetting() takes it the rest of the way and transitions to IDLE
    // once the HOME sensor latches. Motion watchdog applies as usual.
    Serial.println("[State] Boot: globe not at HOME, returning to HOME");
    Motor::cw(settings.motorSpeed);
    cycleStartMs = millis();
    lastMotionProgressMs = cycleStartMs;
    transition(State::RESETTING);
}

void loop() {
    switch (currentState) {
        case State::IDLE:                   handleIdle();                  break;
        case State::CAT_INSIDE:             handleCatInside();             break;
        case State::WAITING:                handleWaiting();               break;
        case State::CYCLING_CCW:            handleCyclingCcw();            break;
        case State::CYCLING_OVERSHOOT_DUMP: handleCyclingOvershootDump();  break;
        case State::CYCLING_CW:             handleCyclingCw();             break;
        case State::EMPTYING:               handleEmptying();              break;
        case State::EMPTYING_OVERSHOOT:     handleEmptyingOvershoot();     break;
        case State::RESETTING:              handleResetting();             break;
        case State::PAUSED:                 handlePaused();                break;
        case State::ERROR:                  /* wait for manual reset */    break;
    }
}

State current() { return currentState; }

const char *stateName(State s) {
    switch (s) {
        case State::IDLE:                   return "IDLE";
        case State::CAT_INSIDE:             return "CAT_INSIDE";
        case State::WAITING:                return "WAITING";
        case State::CYCLING_CCW:            return "CYCLING_CCW";
        case State::CYCLING_OVERSHOOT_DUMP: return "CYCLING_OVERSHOOT_DUMP";
        case State::CYCLING_CW:             return "CYCLING_CW";
        case State::EMPTYING:               return "EMPTYING";
        case State::EMPTYING_OVERSHOOT:     return "EMPTYING_OVERSHOOT";
        case State::RESETTING:              return "RESETTING";
        case State::PAUSED:                 return "PAUSED";
        case State::ERROR:                  return "ERROR";
    }
    return "UNKNOWN";
}

const char *errorMessage() { return currentError; }

bool requestCycle() {
    if (currentState != State::IDLE) return false;
    Motor::ccw(settings.motorSpeed);
    cycleStartMs = millis();
    lastMotionProgressMs = cycleStartMs;
    transition(State::CYCLING_CCW);
    return true;
}

bool requestEmpty() {
    if (currentState != State::IDLE) return false;
    Motor::ccw(settings.motorSpeed);
    cycleStartMs = millis();
    lastMotionProgressMs = cycleStartMs;
    transition(State::EMPTYING);
    return true;
}

bool requestReset() {
    Motor::stop();
    currentError = "";
    if (currentState == State::ERROR || currentState == State::PAUSED ||
        currentState == State::EMPTYING || currentState == State::RESETTING) {
        if (Sensors::isHomePosition()) {
            transition(State::IDLE);
        } else {
            Motor::cw(settings.motorSpeed);
            cycleStartMs = millis();
            lastMotionProgressMs = cycleStartMs;
            transition(State::RESETTING);
        }
        return true;
    }
    return false;
}

bool requestPause() {
    if (!isMotionState(currentState)) return false;
    Motor::stop();
    stateBeforePause = currentState;
    pauseStartedMs = millis();
    transition(State::PAUSED);
    return true;
}

bool requestResume() {
    if (currentState != State::PAUSED) return false;
    uint32_t now = millis();
    cycleStartMs = now;
    switch (stateBeforePause) {
        case State::CYCLING_CCW:            Motor::ccw(settings.motorSpeed); break;
        case State::CYCLING_OVERSHOOT_DUMP: Motor::ccw(settings.motorSpeed); overshootStartMs = now; break;
        case State::CYCLING_CW:             Motor::cw(settings.motorSpeed);  break;
        case State::EMPTYING:               Motor::ccw(settings.motorSpeed); break;
        case State::EMPTYING_OVERSHOOT:     Motor::ccw(settings.motorSpeed); overshootStartMs = now; break;
        case State::RESETTING:              Motor::cw(settings.motorSpeed);  break;
        default: break;
    }
    transition(stateBeforePause);
    return true;
}

uint32_t cycleCount()        { return totalCycleCount; }
uint32_t lastCycleTimestamp(){ return lastCycleTs; }
uint32_t uptimeSeconds()     { return (millis() - bootMillis) / 1000UL; }

size_t historySize() { return historyCount; }

const CycleRecord &historyAt(size_t i) {
    // Index 0 = most recent
    size_t pos = (historyHead + HISTORY_MAX_ENTRIES - 1 - i) % HISTORY_MAX_ENTRIES;
    return history[pos];
}

void clearHistory() {
    historyCount = 0;
    historyHead = 0;
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) {
        prefs.remove(NVS_KEY_HISTORY);
        prefs.end();
    }
}

void serializeStatus(JsonObject obj) {
    obj["state"] = stateName(currentState);
    obj["cat_present"] = Sensors::isCatPresent();
    obj["weight_kg"] = Sensors::getCurrentWeight();
    obj["weight_enabled"] = settings.weightEnabled;
    obj["presence_enabled"] = settings.presenceEnabled;
    obj["cycle_count"] = totalCycleCount;
    obj["last_cycle"] = lastCycleTs;
    obj["uptime_sec"] = uptimeSeconds();
    obj["error"] = currentError;
    obj["version"] = OPENLITTER_VERSION;
}

void serializeHistory(JsonArray arr) {
    for (size_t i = 0; i < historyCount; ++i) {
        const CycleRecord &r = historyAt(i);
        JsonObject o = arr.add<JsonObject>();
        o["timestamp"] = (uint32_t)r.timestamp;
        o["duration"] = r.durationSeconds;
        o["weight_kg"] = r.catWeightKg;
        o["completed"] = r.completed;
    }
}

void setOnStateChange(StateChangeCallback cb) {
    if (!cb) return;
    if (onChangeCount >= MAX_CALLBACKS) {
        Serial.println("[State] setOnStateChange: callback table full");
        return;
    }
    onChangeCallbacks[onChangeCount++] = cb;
}

}  // namespace StateMachine
