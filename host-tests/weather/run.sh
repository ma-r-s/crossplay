#!/bin/sh
# Builds and runs the weather core's tests. No device and no PlatformIO:
# WeatherCore is freestanding C++17.
#
#   host-tests/weather/run.sh
#
# Only the standard library and lib/Utf8 are on the include path. If the core
# ever reaches for HttpDownloader, ArduinoJson or the card, this build fails
# instead of the parsing quietly becoming device-only and untested. lib/Utf8 is
# there because place names are composed and folded where they enter.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, so two worktrees never run each other's binary.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-weather-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  -I../../lib/Utf8 \
  ../../lib/Utf8/Utf8.cpp \
  ../../src/apps_local/weather/WeatherCore.cpp \
  test_weather.cpp -o "$BUILD_DIR/test_weather"
"$BUILD_DIR/test_weather"

# Every request can be abandoned with Back within a poll (see abandon.py).
python3 abandon.py ../..
