#!/bin/bash
# Regenerate src/apps_local/weather/WeatherIcons.h, the forecast's skies, from
# tools_local/weather/icons.txt (Lucide names).
#
#   brew install librsvg          # rsvg-convert, the only external dependency
#   ./tools_local/weather/gen_weather_icons.sh
#
# Its own header rather than ToyboxIcons.h, which cannot be regenerated whole
# (see the warning at the top of tools_local/toybox/icons.txt). The shelf's own
# weather mark is in ToyboxIcons.h with every other shelf icon.
set -euo pipefail
REPO="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/../.." && pwd)"
cd "$REPO"
uv run --quiet --with pillow python freeink-sdk/libs/assets/Icons/tools/gen_icons.py \
  --manifest tools_local/weather/icons.txt \
  --svgdir freeink-sdk/libs/assets/Icons/lucide/icons \
  --sizes 32,96 \
  --out src/apps_local/weather/WeatherIcons.h
echo "wrote src/apps_local/weather/WeatherIcons.h; run ./bin/clang-format-fix -g before committing"
