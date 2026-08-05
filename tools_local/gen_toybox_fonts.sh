#!/bin/bash
# Regenerate the generated font cuts in src/apps_local/ui/fonts/.
#
#   ./tools_local/gen_toybox_fonts.sh [size ...]     # default: the cuts we ship
#
# The output is committed, because regenerating needs the TTF and two Python
# packages, and a checkout should build without either. Edit the size list, run
# this, commit the headers.
#
# Jersey 25 is SIL OFL (github.com/scfried/soft-type-jersey, via Google Fonts).
# It is fetched rather than vendored because lib/EpdFont/fontsrc/ is gitignored
# upstream and the original conversion used a file in /tmp that is long gone --
# which is exactly why this script exists rather than a note in a header.
#
# Two details that matter, both learned by diffing a regenerated cut against the
# committed one:
#
#   * Subset to ASCII first. Converting the whole font produces a 16KB table
#     instead of a 4KB one, because fontconvert exports every glyph it finds.
#   * 1-bit, never --2bit. GfxRenderer's BW path paints a pixel for ANY coverage
#     above zero, so an antialiased cut floods to solid black and turns to mush.
#     See docs/design-language.md.
set -euo pipefail
REPO="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
cd "$REPO"

SIZES=("$@")
[ ${#SIZES[@]} -eq 0 ] && SIZES=(10 20 30)

SRCDIR=lib/EpdFont/builtinFonts/source
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

curl -fsSL "https://github.com/google/fonts/raw/main/ofl/jersey25/Jersey25-Regular.ttf" -o "$WORK/jersey25.ttf"
uv run --quiet --with fonttools pyftsubset "$WORK/jersey25.ttf" \
  --unicodes="U+0020-007E" --output-file="$WORK/jersey25-ascii.ttf"

# The reading face is a different family and a different argument. Noto Serif,
# because it is what the EPUB reader sets books in, converted at 1 bit HERE
# rather than taken from lib/EpdFont/builtinFonts/ -- those cuts are --2bit and
# flood to mush through the BW glyph path. --force-autohint keeps stem widths
# even at 14px, which is what stops a 1-bit serif going ragged.
#
# Latin-1 as well as ASCII: Hacker News prose is full of accented names, and a
# codepoint with no glyph draws as nothing at all.
if [ ${#SIZES[@]} -eq 3 ]; then
  SERIF="$SRCDIR/NotoSerif/NotoSerif-Regular.ttf"
  uv run --quiet --with fonttools pyftsubset "$SERIF" \
    --unicodes="U+0020-007E,U+00A0-00FF" --output-file="$WORK/notoserif-subset.ttf"
  out="src/apps_local/ui/fonts/reading_serif_14.h"
  uv run --quiet --with freetype-py --with fonttools \
    python lib/EpdFont/scripts/fontconvert.py reading_serif_14 14 "$WORK/notoserif-subset.ttf" \
    --force-autohint 2>/dev/null | grep -v "extracted$" > "$out"
  sed -i '' -e "s| \* Command used: .*| * Command used: tools_local/gen_toybox_fonts.sh (fontconvert.py reading_serif_14 14 NotoSerif-Regular subset, --force-autohint)|" "$out"
  echo "wrote $out"
fi

for size in "${SIZES[@]}"; do
  out="src/apps_local/ui/fonts/toybox_${size}.h"
  uv run --quiet --with freetype-py --with fonttools \
    python lib/EpdFont/scripts/fontconvert.py "toybox_${size}" "${size}" "$WORK/jersey25-ascii.ttf" \
    2>/dev/null | grep -v "extracted$" > "$out"
  # fontconvert records the absolute path it was handed; keep it reproducible.
  sed -i '' -e "s| \* Command used: .*| * Command used: tools_local/gen_toybox_fonts.sh (fontconvert.py toybox_${size} ${size} jersey25-ascii.ttf)|" "$out"
  echo "wrote $out"
done
