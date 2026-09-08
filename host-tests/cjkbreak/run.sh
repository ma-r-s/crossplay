#!/bin/sh
# utf8HasCjkBreakOpportunity, the predicate every CJK line break in the
# firmware goes through: GfxRenderer::wrappedText (every UI label) and
# ParsedText (the reader's own layout) both ask it where a line may end.
#
# It had no coverage at all when it arrived -- defined once, called once,
# tested nowhere -- and a wrong answer here is not a crash but a page that
# reads wrong, which nothing else in the gate can see.
#
#   host-tests/cjkbreak/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-cjkbreak-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -O1 -Wall -Wextra -Werror -I../../lib/Utf8 test_cjkbreak.cpp -o "$BUILD_DIR/test_cjkbreak"
"$BUILD_DIR/test_cjkbreak"
