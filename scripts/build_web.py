# OpenLitter - Pre-build helper
# Copyright (C) 2024 David Lopes (https://github.com/davdlic)
# Licensed under the GNU General Public License v3.0
#
# This script runs before each build. It currently only ensures the LittleFS
# data directory exists so PlatformIO can package it. In the future it can be
# extended to minify HTML/CSS/JS or inject a build version into manifest.json.

import os

Import("env")  # noqa: F821 (provided by PlatformIO)

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
DATA_DIR = os.path.join(PROJECT_DIR, "data")

if not os.path.isdir(DATA_DIR):
    os.makedirs(DATA_DIR, exist_ok=True)
    print(f"[OpenLitter] Created missing data dir at {DATA_DIR}")

print("[OpenLitter] Pre-build checks OK.")
