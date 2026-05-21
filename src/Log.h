/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#pragma once

#include <Arduino.h>

namespace Log {

enum class Level : uint8_t { Info = 0, Warn = 1, Error = 2 };

// Callback invoked once per log line. Implementations should be fast and
// non-blocking (e.g. push to a WS broadcast queue, not flush synchronously).
typedef void (*Subscriber)(Level level, uint32_t bootMs, const char *msg);

void begin();
void setSubscriber(Subscriber cb);

void info (const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void warn (const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Dump the current ring buffer contents into `out`, newest at the bottom.
// Each line is prefixed with `[I]`/`[W]`/`[E]` and a millisecond timestamp.
void dump(String &out);

const char *levelTag(Level l);

}  // namespace Log
