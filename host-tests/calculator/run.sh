#!/bin/sh
# The calculator, checked without a panel: its arithmetic, its key semantics,
# the geometry of all five skins, and every label measured in the cut its skin
# actually resolves. CalcEngine.h, CalcLayout.h and CalcSkin.h are freestanding
# C++17 for exactly this reason -- CalcFontIds.h exists so a Skin can name a
# face without dragging GfxRenderer onto this include path.
#
#   host-tests/calculator/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-calculator-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/calculator
DEC=../../lib/decNumber

# The vendored decimal library, compiled once, under the SAME -Werror as our own
# code. It needs no suppression -- decNumber 3.68 is clean under -Wall -Wextra at
# -Os and -O2 on both this host and the xtensa toolchain -- and compiling it
# leniently would mean a future bump could introduce a warning nobody saw.
for unit in decnumber_impl deccontext_impl; do
  if [ ! -f "$BUILD_DIR/$unit.o" ] || [ "$DEC/src/$unit.c" -nt "$BUILD_DIR/$unit.o" ] ||
     [ "$DEC/src/DecNumberConfig.h" -nt "$BUILD_DIR/$unit.o" ]; then
    "${CC:-cc}" -std=c99 -Wall -Wextra -Werror -O2 -I$DEC/src -c "$DEC/src/$unit.c" -o "$BUILD_DIR/$unit.o"
  fi
done

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC -I$DEC/src \
  test_calculator.cpp "$BUILD_DIR/decnumber_impl.o" "$BUILD_DIR/deccontext_impl.o" \
  -o "$BUILD_DIR/test_calculator"
"$BUILD_DIR/test_calculator"

# The half a C++ host test cannot do: a test cannot parse a font header, so the
# widths every label will really be drawn at are measured here instead. The
# binary emits which cut each label resolves to; the script does the arithmetic.
"$BUILD_DIR/test_calculator" --labels > "$BUILD_DIR/labels.txt"
python3 ./label_fit.py "$BUILD_DIR/labels.txt"

# decNumber allocates when a working buffer would exceed DECBUFFER digits, and on
# this device a failed allocation in the middle of a sum has nowhere to go. The
# working precision is set well under that, and this is what proves it rather
# than asserts it: the same library rebuilt with malloc redirected to a counter,
# driven through the same workout, and required to have called it zero times.
"${CC:-cc}" -std=c99 -O2 -I$DEC/src -Dmalloc=calc_probe_malloc -Dfree=calc_probe_free \
  -include no_malloc_probe.h -c "$DEC/src/decnumber_impl.c" -o "$BUILD_DIR/probe_dn.o"
"${CC:-cc}" -std=c99 -O2 -I$DEC/src -Dmalloc=calc_probe_malloc -Dfree=calc_probe_free \
  -include no_malloc_probe.h -c "$DEC/src/deccontext_impl.c" -o "$BUILD_DIR/probe_dc.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC -I$DEC/src -I. \
  no_malloc_probe.cpp "$BUILD_DIR/probe_dn.o" "$BUILD_DIR/probe_dc.o" -o "$BUILD_DIR/no_malloc_probe"
"$BUILD_DIR/no_malloc_probe"
