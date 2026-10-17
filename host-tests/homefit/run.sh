#!/bin/bash
# Home's menu must fit under its cover tile in every theme, with a catalog
# configured and the shelf's folders appended (src/activities/home/HomeMenuFit.h).
#
# Before 1.13.29 the gaps alone gave, floored at 1px: with the OPDS row Classic
# cut Apps in half, Lyra Extended drew Apps off the panel, and RoundedRaff paged
# it onto a second page nobody could see. Every theme's numbers are READ from
# its header here, not retyped, so a sync that moves them is tested as moved.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-homefit-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
python3 - > "$BUILD_DIR/theme_metrics.h" <<'PY'
import re, sys
base = open("../../src/components/themes/BaseTheme.h").read()
def field(text, name):
    m = re.search(r"\." + name + r"\s*=\s*(-?\d+)", text)
    return int(m.group(1)) if m else None
themes = {}
def metrics(path, inherit=None):
    t = open(path).read()
    out = dict(inherit or {})
    for k in ["homeTopPadding", "homeCoverTileHeight", "homeMenuTopOffset", "verticalSpacing", "menuRowHeight",
              "menuSpacing", "contentSidePadding"]:
        v = field(t, k)
        if v is not None:
            out[k] = v
    for k in ["homeCoverTileHeight"]:  # Lyra3 assigns v.homeCoverTileHeight = N;
        m = re.search(r"v\." + k + r"\s*=\s*(\d+)", t)
        if m:
            out[k] = int(m.group(1))
    return out
themes["classic"] = metrics("../../src/components/themes/BaseTheme.h")
themes["lyra"] = metrics("../../src/components/themes/lyra/LyraTheme.h")
themes["lyra3"] = metrics("../../src/components/themes/lyra/Lyra3CoversTheme.h", themes["lyra"])
themes["roundedraff"] = metrics("../../src/components/themes/roundedraff/RoundedRaffTheme.h")
need = ["homeTopPadding", "homeCoverTileHeight", "homeMenuTopOffset", "verticalSpacing", "menuRowHeight",
        "menuSpacing", "contentSidePadding"]
print("struct ThemeMetrics { const char* name; int topPadding, tile, menuOffset, verticalSpacing, rowHeight, rowGap, side; };")
print("static const ThemeMetrics kThemes[] = {")
for name, m in themes.items():
    missing = [k for k in need if k not in m]
    if missing:
        sys.exit(f"homefit: could not read {missing} for {name}; the theme headers changed shape")
    print(f'  {{"{name}", {", ".join(str(m[k]) for k in need)}}},')
print("};")
PY
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O1 -I"$BUILD_DIR" -I../../src/activities/home \
  test_homefit.cpp -o "$BUILD_DIR/test_homefit"
"$BUILD_DIR/test_homefit"
