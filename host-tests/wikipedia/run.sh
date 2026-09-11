#!/bin/sh
# Builds and runs the Wikipedia tests. No device and no PlatformIO: WikipediaCore
# is freestanding C++17, which is what lets the manifest, the block directory,
# the title index and the block layout be checked without a panel.
#
#   test_core      -- the fold against the vectors the builder emitted, the
#                     manifest and state JSON, a hand-built index and block
#   test_realpack  -- the SAME reader over a pack written by the Python writer
#                     (tools_local/wikipedia/pack_format.py), which is the only
#                     check that the two halves of the format agree
#
#   host-tests/wikipedia/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-wikipedia-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/wikipedia
TOOLS=../../tools_local/wikipedia

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_core.cpp "$SRC/WikipediaCore.cpp" -o "$BUILD_DIR/test_core"
if [ -f "$TOOLS/fold_vectors.tsv" ]; then
  "$BUILD_DIR/test_core" "$TOOLS/fold_vectors.tsv"
else
  echo "note: $TOOLS/fold_vectors.tsv not present; fold vectors not checked"
  "$BUILD_DIR/test_core"
fi

# A pack written by the real writer, from a few articles with links, headings,
# redirects and one article too big for a block.
if [ -f "$TOOLS/pack_format.py" ]; then
  ZSTD=../../lib/zstd/src
  [ -f "$BUILD_DIR/zstddeclib.o" ] || "${CC:-cc}" -std=c99 -O2 -w -c "$ZSTD/zstddeclib.c" -o "$BUILD_DIR/zstddeclib.o"
  "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC -I$ZSTD \
    test_realpack.cpp "$SRC/WikipediaCore.cpp" "$BUILD_DIR/zstddeclib.o" -o "$BUILD_DIR/test_realpack"
  rm -rf "$BUILD_DIR/pack"
  python3 "$TOOLS/pack_format.py" --self-test-pack "$BUILD_DIR/pack" > "$BUILD_DIR/pack.tsv"
  "$BUILD_DIR/test_realpack" "$BUILD_DIR/pack" "$BUILD_DIR/pack.tsv"
else
  echo "note: $TOOLS/pack_format.py not present; cross-language pack check skipped"
fi
