#!/bin/sh
# The wallpaper pack's order is its only index; this asserts it still matches
# the firmware's name table. See test_wallpack.py for why nothing else can.
set -e
cd "$(dirname "$0")"
python3 test_wallpack.py
