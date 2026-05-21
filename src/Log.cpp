/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#include "Log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace Log {

namespace {

// Ring buffer holds the last N log lines for the /api/logs endpoint and
// for clients connecting after the fact. Each entry is one line.
constexpr size_t LOG_RING_CAPACITY = 64;
constexpr size_t LOG_LINE_MAX      = 160;

struct Entry {
    Level    level;
    uint32_t bootMs;
    char     msg[LOG_LINE_MAX];
};

Entry      ring[LOG_RING_CAPACITY];
size_t     head  = 0;     // next slot to write
size_t     count = 0;     // number of valid entries
Subscriber subscriber = nullptr;

void appendLine(Level lvl, const char *msg) {
    Entry &e = ring[head];
    e.level  = lvl;
    e.bootMs = millis();
    strncpy(e.msg, msg, LOG_LINE_MAX - 1);
    e.msg[LOG_LINE_MAX - 1] = '\0';
    head = (head + 1) % LOG_RING_CAPACITY;
    if (count < LOG_RING_CAPACITY) count++;
}

void emit(Level lvl, const char *fmt, va_list ap) {
    char buf[LOG_LINE_MAX];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    // Strip trailing newline so the ring stores clean lines.
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[--n] = '\0';
    }
    Serial.printf("[%s] %s\n", levelTag(lvl), buf);
    appendLine(lvl, buf);
    if (subscriber) subscriber(lvl, ring[(head + LOG_RING_CAPACITY - 1) % LOG_RING_CAPACITY].bootMs, buf);
}

}  // namespace

void begin() {
    head = 0;
    count = 0;
    subscriber = nullptr;
}

void setSubscriber(Subscriber cb) { subscriber = cb; }

void info(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); emit(Level::Info,  fmt, ap); va_end(ap);
}
void warn(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); emit(Level::Warn,  fmt, ap); va_end(ap);
}
void error(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); emit(Level::Error, fmt, ap); va_end(ap);
}

void dump(String &out) {
    out = "";
    if (count == 0) return;
    size_t start = (head + LOG_RING_CAPACITY - count) % LOG_RING_CAPACITY;
    char prefix[24];
    for (size_t i = 0; i < count; i++) {
        const Entry &e = ring[(start + i) % LOG_RING_CAPACITY];
        snprintf(prefix, sizeof(prefix), "[%s] %8lu  ",
                 levelTag(e.level), (unsigned long)e.bootMs);
        out += prefix;
        out += e.msg;
        out += '\n';
    }
}

const char *levelTag(Level l) {
    switch (l) {
        case Level::Info:  return "I";
        case Level::Warn:  return "W";
        case Level::Error: return "E";
    }
    return "?";
}

}  // namespace Log
