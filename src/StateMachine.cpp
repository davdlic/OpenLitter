/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#include "StateMachine.h"
#include "Settings.h"
#include "Motor.h"
#include "Sensors.h"
#include "Log.h"
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
uint32_t overshootStartMs = 0;      // when the current timed phase started (DUMP pause or LEVEL overshoot)
uint32_t cycleBeginMs = 0;          // when the *original* cycle/reset began (for accurate recordCycle duration); cycleStartMs is reset per phase for the watchdog
bool     levelReturnArmed = false;  // CYCLING_LEVEL_RETURN waits for HOME sensor to deactivate before re-arming the stop-at-HOME check
bool     manualPause = false;       // true if PAUSED was entered via requestPause(), false if via anti-pinch
bool     resetInProgress = false;   // true while requestReset() drives the CCW/OVERSHOOT/CW path; UI shows RESETTING and history skips the cycle record
// Coarse globe position zones persisted to NVS. On boot we read the last
// saved zone and pick the SHORT direction back to HOME — critically, for
// "past HOME on CW side" (LEVEL_OVERSHOOT/LEVEL_RETURN) we'd otherwise
// drive CW for over half a revolution and dump litter via the inverted
// globe. Keeping this coarse (not the full State enum) avoids NVS thrash:
// we only write when the *zone* changes, not on every sub-phase.
enum class Zone : uint8_t {
    SAFE          = 0,  // at or very near HOME (IDLE / CAT_INSIDE / WAITING / PAUSED@home / ERROR)
    OUTBOUND      = 1,  // HOME -> DUMP, CCW path before reaching DUMP (CYCLING_CCW / EMPTYING)
    AT_DUMP       = 2,  // motor stopped at DUMP (CYCLING_DUMP_PAUSE / EMPTYING_DUMP_PAUSE)
    RETURN        = 3,  // DUMP -> HOME, CW path before reaching HOME (CYCLING_CW)
    PAST_HOME_CW  = 4,  // just past HOME on the CW side (CYCLING_LEVEL_OVERSHOOT / LEVEL_RETURN)
    PAST_HOME_CCW = 5,  // just past HOME on the CCW side (LEVEL_BACK_OVERSHOOT / LEVEL_BACK_RETURN)
    UNKNOWN       = 6,  // mid-RESETTING or mid-BOOT_RECOVERY when power cut again — fall back to scan
};

Zone lastSavedZone = Zone::SAFE;

Zone stateToZone(State s) {
    switch (s) {
        case State::IDLE:
        case State::CAT_INSIDE:
        case State::WAITING:
        case State::PAUSED:
        case State::ERROR:
            return Zone::SAFE;
        case State::CYCLING_CCW:
        case State::EMPTYING:
            return Zone::OUTBOUND;
        case State::CYCLING_DUMP_PAUSE:
        case State::EMPTYING_DUMP_PAUSE:
            return Zone::AT_DUMP;
        case State::CYCLING_CW:
            return Zone::RETURN;
        case State::CYCLING_LEVEL_OVERSHOOT:
        case State::CYCLING_LEVEL_RETURN:
            return Zone::PAST_HOME_CW;
        case State::CYCLING_LEVEL_BACK_OVERSHOOT:
        case State::CYCLING_LEVEL_BACK_RETURN:
            return Zone::PAST_HOME_CCW;
        case State::RESETTING:
        case State::BOOT_RECOVERY:
            return Zone::UNKNOWN;
    }
    return Zone::UNKNOWN;
}

void persistZone(Zone z) {
    if (z == lastSavedZone) return;  // dedupe — single write per zone change
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) return;
    prefs.putUChar(NVS_KEY_LAST_ZONE, (uint8_t)z);
    prefs.end();
    lastSavedZone = z;
}

Zone loadZone() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) return Zone::UNKNOWN;
    uint8_t v = prefs.getUChar(NVS_KEY_LAST_ZONE, (uint8_t)Zone::UNKNOWN);
    prefs.end();
    if (v > (uint8_t)Zone::UNKNOWN) return Zone::UNKNOWN;
    return (Zone)v;
}

// Boot recovery strategies. Chosen at begin() time from the persisted zone.
// The UNKNOWN case (fresh install, factory reset, NVS corruption) doesn't
// use BOOT_RECOVERY at all — it triggers a full Reset cycle from begin()
// directly, which always ends at HOME in a known-good state. The short
// strategies below are only used when we DO have a persisted zone and
// can pick a path that won't cross DUMP.
enum class BootRecoveryStrategy : uint8_t {
    DIRECT_CW,           // drive CW until HOME — path known safe (e.g. past_home_ccw, short way is CW)
    DIRECT_CCW,          // drive CCW until HOME — path known safe (e.g. past_home_cw, short way is CCW)
};
BootRecoveryStrategy bootRecoveryStrategy = BootRecoveryStrategy::DIRECT_CW;

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
    Log::info("State %s -> %s", stateName(prev), stateName(next));
    // Persist coarse zone so boot recovery knows the safe direction back
    // to HOME if power cuts mid-cycle. persistZone() dedupes — no NVS
    // write if the zone is unchanged (e.g. CYCLING_LEVEL_OVERSHOOT ->
    // CYCLING_LEVEL_RETURN both map to PAST_HOME_CW).
    persistZone(stateToZone(next));
    fireStateChange(prev, next);
}

void enterError(const char *msg) {
    currentError = msg;
    Motor::stop();
    Log::error("State entering ERROR: %s", msg);
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
        Log::warn("History blob incompatible, discarding");
        return;
    }
    memcpy(history, buf + sizeof(hdr), sizeof(history));
    historyCount     = hdr.count;
    historyHead      = hdr.head;
    totalCycleCount  = hdr.totalCycleCount;
    lastCycleTs      = hdr.lastCycleTs;
    Log::info("History restored from NVS (%u entries)", (unsigned)historyCount);
}

bool isMotionState(State s) {
    return s == State::CYCLING_CCW || s == State::CYCLING_CW
        || s == State::EMPTYING    || s == State::RESETTING
        || s == State::BOOT_RECOVERY
        || s == State::CYCLING_DUMP_PAUSE
        || s == State::EMPTYING_DUMP_PAUSE
        || s == State::CYCLING_LEVEL_OVERSHOOT
        || s == State::CYCLING_LEVEL_RETURN
        || s == State::CYCLING_LEVEL_BACK_OVERSHOOT
        || s == State::CYCLING_LEVEL_BACK_RETURN;
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
        Log::warn("Cat detected during motion -> PAUSED");
        Motor::stop();
        stateBeforePause = currentState;
        pauseStartedMs = now;
        manualPause = false;
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
        if (fallback) Log::warn("Cat timeout fallback (assumed left)");
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
        // Time to clean. Reset cycleBeginMs (the "original cycle start"
        // used to record the duration in history) — without this, the
        // automatic cat-triggered cycle inherits cycleBeginMs from the
        // previous cycle, or 0 on first boot, and history shows the
        // device's uptime as the cycle duration. Manual requestCycle()
        // already does this; the auto path was missing it.
        resetInProgress = false;
        Motor::ccw(settings.motorSpeed);
        cycleBeginMs = now;
        cycleStartMs = now;
        lastMotionProgressMs = now;
        transition(State::CYCLING_CCW);
    }
}

void handleCyclingCcw() {
    runMotionWatchdog();
    if (currentState != State::CYCLING_CCW) return;  // watchdog tripped
    if (Sensors::isDumpPosition()) {
        // Hit DUMP. STOP the motor and let waste fall through the dump door
        // for the configured pause. Rotating any further CCW would close the
        // door again on real Litter Robot mechanics.
        Motor::stop();
        overshootStartMs = millis();
        cycleStartMs = millis();
        lastMotionProgressMs = cycleStartMs;
        transition(State::CYCLING_DUMP_PAUSE);
    }
}

void handleCyclingDumpPause() {
    runMotionWatchdog();
    if (currentState != State::CYCLING_DUMP_PAUSE) return;
    if (millis() - overshootStartMs >= (uint32_t)settings.cycleDumpPauseSec * 1000UL) {
        // Pause done — start the CW return. Reset cycleStartMs so the CW
        // phase has its own full watchdog window (motors on real LRs can
        // be slow enough that a whole-cycle watchdog wouldn't fit).
        Motor::cw(settings.motorSpeed);
        cycleStartMs = millis();
        lastMotionProgressMs = cycleStartMs;
        transition(State::CYCLING_CW);
    }
}

void finishCycle(bool atLevelReturn) {
    Motor::stop();
    // Only record this as a cleaning cycle in history if it was a real
    // cycle; a Reset that happened to route through the cycle path is
    // not a cleaning event.
    if (!resetInProgress) {
        uint16_t dur = (uint16_t)((millis() - cycleBeginMs) / 1000UL);
        recordCycle(true, dur, Sensors::getCurrentWeight());
        persistHistory();
    }
    resetInProgress = false;
    (void)atLevelReturn;
    transition(State::IDLE);
}

void handleCyclingCw() {
    runMotionWatchdog();
    if (currentState != State::CYCLING_CW) return;
    if (Sensors::isHomePosition()) {
        if (settings.cycleLevelOvershootSec == 0) {
            // Levelling phase disabled — finish here.
            finishCycle(false);
            return;
        }
        // Keep motor running CW past HOME for the configured overshoot
        // (sand "shake"); then CYCLING_LEVEL_RETURN reverses CCW back
        // to HOME and stops there.
        overshootStartMs = millis();
        cycleStartMs = millis();
        lastMotionProgressMs = cycleStartMs;
        transition(State::CYCLING_LEVEL_OVERSHOOT);
    }
}

void handleCyclingLevelOvershoot() {
    runMotionWatchdog();
    if (currentState != State::CYCLING_LEVEL_OVERSHOOT) return;
    if (millis() - overshootStartMs >= (uint32_t)settings.cycleLevelOvershootSec * 1000UL) {
        Motor::ccw(settings.motorSpeed);
        cycleStartMs = millis();
        lastMotionProgressMs = cycleStartMs;
        // Globe is still sitting on the HOME magnet; LEVEL_RETURN waits
        // for the sensor to deactivate first before re-arming the
        // stop-at-HOME check, otherwise it would stop immediately.
        levelReturnArmed = false;
        transition(State::CYCLING_LEVEL_RETURN);
    }
}

void handleCyclingLevelReturn() {
    runMotionWatchdog();
    if (currentState != State::CYCLING_LEVEL_RETURN) return;
    if (!levelReturnArmed) {
        if (!Sensors::isHomePosition()) levelReturnArmed = true;
        return;
    }
    if (Sensors::isHomePosition()) {
        // Second half of the sand shake: keep going CCW past HOME for the
        // hardcoded back overshoot, then reverse CW back to HOME and stop.
        overshootStartMs = millis();
        cycleStartMs = millis();
        lastMotionProgressMs = cycleStartMs;
        transition(State::CYCLING_LEVEL_BACK_OVERSHOOT);
    }
}

void handleCyclingLevelBackOvershoot() {
    runMotionWatchdog();
    if (currentState != State::CYCLING_LEVEL_BACK_OVERSHOOT) return;
    if (millis() - overshootStartMs >= (uint32_t)CYCLE_LEVEL_BACK_OVERSHOOT_SEC * 1000UL) {
        Motor::cw(settings.motorSpeed);
        cycleStartMs = millis();
        lastMotionProgressMs = cycleStartMs;
        levelReturnArmed = false;
        transition(State::CYCLING_LEVEL_BACK_RETURN);
    }
}

void handleCyclingLevelBackReturn() {
    runMotionWatchdog();
    if (currentState != State::CYCLING_LEVEL_BACK_RETURN) return;
    if (!levelReturnArmed) {
        if (!Sensors::isHomePosition()) levelReturnArmed = true;
        return;
    }
    if (Sensors::isHomePosition()) {
        finishCycle(true);
    }
}

void handleEmptying() {
    runMotionWatchdog();
    if (currentState != State::EMPTYING) return;
    if (Sensors::isDumpPosition()) {
        // STOP at DUMP, same as the cycle path. The motor stays off for
        // emptyDumpPauseSec so the user can remove the tray (or so the
        // last bit of waste settles), before we start the CW return.
        Motor::stop();
        overshootStartMs = millis();
        cycleStartMs = millis();
        lastMotionProgressMs = cycleStartMs;
        transition(State::EMPTYING_DUMP_PAUSE);
    }
}

void handleEmptyingDumpPause() {
    runMotionWatchdog();
    if (currentState != State::EMPTYING_DUMP_PAUSE) return;
    if (millis() - overshootStartMs >= (uint32_t)settings.emptyDumpPauseSec * 1000UL) {
        // Pause done — head back home. Reset cycleStartMs so the return
        // phase gets its own watchdog window.
        cycleStartMs = millis();
        lastMotionProgressMs = cycleStartMs;
        transition(State::RESETTING);
    }
}

void handleResetting() {
    runMotionWatchdog();
    if (currentState != State::RESETTING) return;
    if (Sensors::isHomePosition()) {
        Motor::stop();
        resetInProgress = false;
        transition(State::IDLE);
    } else if (millis() - cycleStartMs > 1000) {
        // Need to start moving back to home.
        Motor::cw(settings.motorSpeed);
    }
}

void handlePaused() {
    uint32_t now = millis();
    uint32_t graceMs = (manualPause ? MANUAL_PAUSE_AUTO_RESUME_SEC
                                    : PAUSED_AUTO_RESUME_SEC) * 1000UL;
    if ((now - pauseStartedMs) < graceMs) return;
    if (!Sensors::isCatPresent()) {
        Log::info("Resuming after %s pause grace period",
                  manualPause ? "manual" : "anti-pinch");
        cycleStartMs = now;
        switch (stateBeforePause) {
            case State::CYCLING_CCW:                  Motor::ccw(settings.motorSpeed); break;
            case State::CYCLING_DUMP_PAUSE:           overshootStartMs = now; break;  // motor stays stopped during pause-at-DUMP
            case State::CYCLING_CW:                   Motor::cw(settings.motorSpeed);  break;
            case State::CYCLING_LEVEL_OVERSHOOT:      Motor::cw(settings.motorSpeed); overshootStartMs = now; break;
            case State::CYCLING_LEVEL_RETURN:         Motor::ccw(settings.motorSpeed); levelReturnArmed = false; break;
            case State::CYCLING_LEVEL_BACK_OVERSHOOT: Motor::ccw(settings.motorSpeed); overshootStartMs = now; break;
            case State::CYCLING_LEVEL_BACK_RETURN:    Motor::cw(settings.motorSpeed);  levelReturnArmed = false; break;
            case State::EMPTYING:                     Motor::ccw(settings.motorSpeed); break;
            case State::EMPTYING_DUMP_PAUSE:          overshootStartMs = now; break;  // motor stays stopped during pause-at-DUMP
            case State::RESETTING:                    Motor::cw(settings.motorSpeed);  break;
            default: break;
        }
        transition(stateBeforePause);
        return;
    }
    enterError(manualPause ? "Cat still detected after manual pause grace period"
                           : "Cat still detected after pause grace period");
}

}  // namespace

void begin() {
    bootMillis = millis();
    loadHistory();
    lastSavedZone = loadZone();
    if (Sensors::isHomePosition()) {
        // Globe already at HOME — clear any stale zone and go IDLE.
        Log::info("Initialised in IDLE (globe at HOME, last_zone=%u)",
                  (unsigned)lastSavedZone);
        if (lastSavedZone != Zone::SAFE) persistZone(Zone::SAFE);
        return;
    }
    // Globe not at HOME. Two paths from here:
    //
    //   1. Persisted zone tells us roughly where we were interrupted — we
    //      pick the SHORT direction back to HOME (BOOT_RECOVERY state) and
    //      we know the path won't cross DUMP.
    //
    //   2. No persisted zone (fresh install, factory reset, NVS corruption)
    //      or zone says SAFE but globe isn't at HOME (someone moved it by
    //      hand) — we have no reliable info, so we run a FULL RESET cycle
    //      (CCW -> DUMP -> pause -> CW -> leveling -> HOME). Costs a few
    //      seconds and may dump some clean litter, but always ends in a
    //      known-good state at HOME.
    Zone z = lastSavedZone;
    uint32_t now = millis();
    cycleStartMs = now;
    cycleBeginMs = now;
    lastMotionProgressMs = now;

    switch (z) {
        case Zone::PAST_HOME_CW:
            bootRecoveryStrategy = BootRecoveryStrategy::DIRECT_CCW;
            Motor::ccw(settings.motorSpeed);
            Log::warn("Boot: globe not at HOME, BOOT_RECOVERY DIRECT_CCW (short way, was past HOME on CW side)");
            transition(State::BOOT_RECOVERY);
            return;
        case Zone::PAST_HOME_CCW:
            bootRecoveryStrategy = BootRecoveryStrategy::DIRECT_CW;
            Motor::cw(settings.motorSpeed);
            Log::warn("Boot: globe not at HOME, BOOT_RECOVERY DIRECT_CW (short way, was past HOME on CCW side)");
            transition(State::BOOT_RECOVERY);
            return;
        case Zone::OUTBOUND:
        case Zone::AT_DUMP:
        case Zone::RETURN:
            bootRecoveryStrategy = BootRecoveryStrategy::DIRECT_CW;
            Motor::cw(settings.motorSpeed);
            Log::warn("Boot: globe not at HOME, BOOT_RECOVERY DIRECT_CW (return path, won't cross DUMP)");
            transition(State::BOOT_RECOVERY);
            return;
        case Zone::SAFE:
        case Zone::UNKNOWN:
        default:
            break;  // fall through to the full-reset path below
    }

    // No usable zone info. Run a full reset cycle — same path as the user
    // pressing Reset from the Web UI. Ends at HOME in a known-good state.
    resetInProgress = true;
    if (Sensors::isDumpPosition()) {
        // Already AT DUMP — skip the outbound leg, go straight to dump pause.
        overshootStartMs = now;
        Motor::stop();
        Log::warn("Boot: globe not at HOME, no zone info, AT DUMP -> full reset (skip outbound)");
        transition(State::CYCLING_DUMP_PAUSE);
    } else {
        Motor::ccw(settings.motorSpeed);
        Log::warn("Boot: globe not at HOME, no zone info -> full reset cycle (CCW -> DUMP -> CW -> leveling -> HOME)");
        transition(State::CYCLING_CCW);
    }
}

void handleBootRecovery() {
    runMotionWatchdog();
    if (currentState != State::BOOT_RECOVERY) return;
    if (Sensors::isHomePosition()) {
        Motor::stop();
        Log::info("Boot recovery complete, HOME reached");
        transition(State::IDLE);
    }
    // No DUMP guard needed — begin() only enters BOOT_RECOVERY when we have
    // a persisted zone that guarantees a DUMP-free path back to HOME. The
    // UNKNOWN/SAFE-but-not-home cases skip BOOT_RECOVERY entirely and run a
    // full reset cycle instead (see begin()).
}

void loop() {
    switch (currentState) {
        case State::IDLE:                   handleIdle();                  break;
        case State::CAT_INSIDE:             handleCatInside();             break;
        case State::WAITING:                handleWaiting();               break;
        case State::CYCLING_CCW:            handleCyclingCcw();            break;
        case State::CYCLING_DUMP_PAUSE:  handleCyclingDumpPause();      break;
        case State::CYCLING_CW:          handleCyclingCw();             break;
        case State::CYCLING_LEVEL_OVERSHOOT: handleCyclingLevelOvershoot();     break;
        case State::CYCLING_LEVEL_RETURN:    handleCyclingLevelReturn();        break;
        case State::CYCLING_LEVEL_BACK_OVERSHOOT: handleCyclingLevelBackOvershoot(); break;
        case State::CYCLING_LEVEL_BACK_RETURN:    handleCyclingLevelBackReturn();    break;
        case State::EMPTYING:                handleEmptying();              break;
        case State::EMPTYING_DUMP_PAUSE:     handleEmptyingDumpPause();     break;
        case State::RESETTING:              handleResetting();             break;
        case State::BOOT_RECOVERY:          handleBootRecovery();          break;
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
        case State::CYCLING_DUMP_PAUSE:   return "CYCLING_DUMP_PAUSE";
        case State::CYCLING_CW:           return "CYCLING_CW";
        case State::CYCLING_LEVEL_OVERSHOOT:      return "CYCLING_LEVEL_OVERSHOOT";
        case State::CYCLING_LEVEL_RETURN:         return "CYCLING_LEVEL_RETURN";
        case State::CYCLING_LEVEL_BACK_OVERSHOOT: return "CYCLING_LEVEL_BACK_OVERSHOOT";
        case State::CYCLING_LEVEL_BACK_RETURN:    return "CYCLING_LEVEL_BACK_RETURN";
        case State::EMPTYING:                return "EMPTYING";
        case State::EMPTYING_DUMP_PAUSE:     return "EMPTYING_DUMP_PAUSE";
        case State::RESETTING:              return "RESETTING";
        case State::BOOT_RECOVERY:          return "BOOT_RECOVERY";
        case State::PAUSED:                 return "PAUSED";
        case State::ERROR:                  return "ERROR";
    }
    return "UNKNOWN";
}

const char *errorMessage() { return currentError; }

bool requestCycle() {
    if (currentState != State::IDLE) return false;
    resetInProgress = false;
    Motor::ccw(settings.motorSpeed);
    cycleBeginMs = millis();
    cycleStartMs = cycleBeginMs;
    lastMotionProgressMs = cycleStartMs;
    transition(State::CYCLING_CCW);
    return true;
}

bool requestEmpty() {
    if (currentState != State::IDLE) return false;
    resetInProgress = false;
    Motor::ccw(settings.motorSpeed);
    cycleBeginMs = millis();
    cycleStartMs = cycleBeginMs;
    lastMotionProgressMs = cycleStartMs;
    transition(State::EMPTYING);
    return true;
}

bool requestReset() {
    Motor::stop();
    currentError = "";
    if (currentState == State::ERROR || currentState == State::PAUSED ||
        currentState == State::EMPTYING || currentState == State::EMPTYING_DUMP_PAUSE ||
        currentState == State::CYCLING_DUMP_PAUSE ||
        currentState == State::CYCLING_LEVEL_OVERSHOOT ||
        currentState == State::CYCLING_LEVEL_RETURN ||
        currentState == State::CYCLING_LEVEL_BACK_OVERSHOOT ||
        currentState == State::CYCLING_LEVEL_BACK_RETURN ||
        currentState == State::RESETTING) {
        uint32_t now = millis();
        if (Sensors::isHomePosition()) {
            // Already home — nothing to do.
            resetInProgress = false;
            transition(State::IDLE);
        } else if (Sensors::isDumpPosition()) {
            // Already at DUMP — pause briefly (motor stays stopped), then
            // return CW to HOME.
            resetInProgress = true;
            cycleBeginMs = now;
            overshootStartMs = now;
            cycleStartMs = now;
            lastMotionProgressMs = now;
            Motor::stop();
            transition(State::CYCLING_DUMP_PAUSE);
        } else {
            // Somewhere in between — drive CCW to DUMP first, pause for
            // waste to fall, then CW back to HOME plus the level shake.
            // Same path as a regular cycle so the litter ends up properly
            // dumped and levelled.
            resetInProgress = true;
            cycleBeginMs = now;
            cycleStartMs = now;
            lastMotionProgressMs = now;
            Motor::ccw(settings.motorSpeed);
            transition(State::CYCLING_CCW);
        }
        return true;
    }
    return false;
}

bool requestHome() {
    // Quick return to HOME — skips the cycle's CCW + dump-pause path, but
    // still runs the sand-shake on arrival so the litter ends levelled.
    // Available from any non-IDLE state.
    if (currentState == State::IDLE) return false;
    Motor::stop();
    currentError = "";
    resetInProgress = true;
    uint32_t now = millis();
    cycleBeginMs = now;
    cycleStartMs = now;
    lastMotionProgressMs = now;
    if (Sensors::isHomePosition()) {
        // Already at HOME, no motion required.
        resetInProgress = false;
        transition(State::IDLE);
        return true;
    }
    // Motor CW until HOME; handleCyclingCw will then route into the level
    // overshoot sequence automatically.
    Motor::cw(settings.motorSpeed);
    transition(State::CYCLING_CW);
    return true;
}

bool requestPause() {
    if (!isMotionState(currentState)) return false;
    Motor::stop();
    stateBeforePause = currentState;
    pauseStartedMs = millis();
    manualPause = true;
    transition(State::PAUSED);
    return true;
}

bool requestResume() {
    if (currentState != State::PAUSED) return false;
    uint32_t now = millis();
    cycleStartMs = now;
    switch (stateBeforePause) {
        case State::CYCLING_CCW:                  Motor::ccw(settings.motorSpeed); break;
        case State::CYCLING_DUMP_PAUSE:           overshootStartMs = now; break;  // motor stays stopped during pause-at-DUMP
        case State::CYCLING_CW:                   Motor::cw(settings.motorSpeed);  break;
        case State::CYCLING_LEVEL_OVERSHOOT:      Motor::cw(settings.motorSpeed); overshootStartMs = now; break;
        case State::CYCLING_LEVEL_RETURN:         Motor::ccw(settings.motorSpeed); levelReturnArmed = false; break;
        case State::CYCLING_LEVEL_BACK_OVERSHOOT: Motor::ccw(settings.motorSpeed); overshootStartMs = now; break;
        case State::CYCLING_LEVEL_BACK_RETURN:    Motor::cw(settings.motorSpeed);  levelReturnArmed = false; break;
        case State::EMPTYING:                     Motor::ccw(settings.motorSpeed); break;
        case State::EMPTYING_DUMP_PAUSE:          overshootStartMs = now; break;  // motor stays stopped during pause-at-DUMP
        case State::RESETTING:                    Motor::cw(settings.motorSpeed);  break;
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
    obj["reset_in_progress"] = resetInProgress;
    obj["cat_present"] = Sensors::isCatPresent();
    obj["home_position"] = Sensors::isHomePosition();
    obj["dump_position"] = Sensors::isDumpPosition();
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
        Log::warn("setOnStateChange: callback table full");
        return;
    }
    onChangeCallbacks[onChangeCount++] = cb;
}

}  // namespace StateMachine
