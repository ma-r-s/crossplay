#!/bin/bash
# The calculator's own font cuts.
#
#   ./tools_local/toybox/gen_calc_fonts.sh
#
# Separate from gen_toybox_fonts.sh, and NEW files rather than wider versions of
# the shipped ones, for the reason that script spells out at length: regenerating
# toybox_20/_30 today moves every glyph a pixel and silently shifts text in every
# app in the fork, because the current freetype is not the one that made the
# committed headers. A cut nothing else uses cannot do that to anybody.
#
# What these carry that the Toybox cuts cannot: the Toybox cuts are Jersey 25
# subset to U+0020-007E, so the division sign, the multiplication sign, the
# minus sign and plus-minus draw as NOTHING -- a glyph the face lacks is a hole,
# not a box. Jersey has DIV, MULT and MINUS and lacks PLUSMINUS and RADICAL;
# Ubuntu Bold has all five. That is why the skins that want a plus-minus or a
# root key are the ones set in Ubuntu.
#
# The number cuts carry DIGITS ONLY (plus the point, the sign, the exponent and
# the operators): a display never shows a letter, and a full ASCII cut at 64px
# is 267KB against 35KB for fourteen glyphs.
set -euo pipefail
REPO="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/../.." && pwd)"
cd "$REPO"
OUT=src/apps_local/calculator/fonts
mkdir -p "$OUT"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

MATH="U+00D7,U+00F7,U+2212,U+00B1,U+221A,U+00B2,U+00B7"
# U+007C is not decoration: fontconvert.py line 383 measures the face through
# load_glyph('|'), and a subset without it hands the tail of the script a None
# face and dies on `face.size.height`.
NUMS="U+0030-0039,U+002E,U+002D,U+002B,U+003D,U+0065,U+0045,U+0020,U+007C"

curl -fsSL "https://github.com/google/fonts/raw/main/ofl/jersey25/Jersey25-Regular.ttf" -o "$WORK/jersey25.ttf"
SRCDIR=lib/EpdFont/builtinFonts/source

# name size src unicodes [extra convert args...]
cut() {
  local name="$1" size="$2" src="$3" uni="$4"; shift 4
  uv run --quiet --with fonttools pyftsubset "$src" --unicodes="$uni" --output-file="$WORK/$name.ttf"
  uv run --quiet --with freetype-py --with fonttools \
    python lib/EpdFont/scripts/fontconvert.py "$name" "$size" "$WORK/$name.ttf" "$@" \
    2>/dev/null | grep -v "extracted$" > "$OUT/$name.h"
  sed -i '' -e "s| \* Command used: .*| * Command used: tools_local/toybox/gen_calc_fonts.sh|" "$OUT/$name.h"
  # Formatted like every other committed header here. fontconvert's own wrapping
  # is close but not identical, and the difference is the whole clang-format
  # step going red on a file nobody typed.
  ./bin/clang-format-fix -i "$OUT/$name.h" >/dev/null 2>&1 || true
  echo "wrote $OUT/$name.h ($(wc -c < "$OUT/$name.h" | tr -d ' ') bytes)"
}

# Five cuts, all Jersey 25.
#
# Two for the pad: a calculator sets its WORD keys smaller than its digits, and
# labelFontFor picks between them structurally, by label length, so the host gate
# resolves the same face the panel will.
#
# Four for the display, which steps DOWN as a result gets longer. Four and not
# one because one is a cliff, and no more than four because the result's width is
# BOUNDED -- sixteen characters, set so the SMALLEST of them still clears the
# panel -- so the rungs are provably enough and a result never lands in a label
# cut, which matters because the label cut is the only one with letters in it and
# the number cuts have none.
cut calc_jersey_28 28 "$WORK/jersey25.ttf" "U+0020-007E,$MATH"
cut calc_jersey_20 20 "$WORK/jersey25.ttf" "U+0020-007E,$MATH"
cut calc_jersey_56 56 "$WORK/jersey25.ttf" "$NUMS,$MATH"
cut calc_jersey_44 44 "$WORK/jersey25.ttf" "$NUMS,$MATH"
cut calc_jersey_34 34 "$WORK/jersey25.ttf" "$NUMS,$MATH"
cut calc_jersey_26 26 "$WORK/jersey25.ttf" "$NUMS,$MATH"
