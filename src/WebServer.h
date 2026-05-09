/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

#pragma once

namespace WebServer {

void begin();
void loop();

// Push the full status payload to all WebSocket clients.
void broadcastStatus();

}  // namespace WebServer
