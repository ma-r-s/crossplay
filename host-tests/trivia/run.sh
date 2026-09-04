#!/bin/sh
# Builds and runs the trivia tests. No device and no PlatformIO: TriviaCore is
# freestanding C++17, which is what lets the format, the chooser and the option
# shuffle all be checked without a panel.
#
# Two sub-suites, and the second is the one that matters most:
#   test_trivia    -- reader, chooser and shuffle, over a pack built in C++
#   test_realpack  -- the SAME reader over a pack written by the Python writer,
#                     which is the only check that the two halves of the format
#                     actually agree. A reader tested only against its own
#                     writer will agree with a format both of them get wrong.
#
#   host-tests/trivia/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-trivia-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/trivia
TOOLS=../../tools_local/trivia

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_trivia.cpp "$SRC/TriviaCore.cpp" -o "$BUILD_DIR/test_trivia"
"$BUILD_DIR/test_trivia"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_realpack.cpp "$SRC/TriviaCore.cpp" -o "$BUILD_DIR/test_realpack"

# A pack written by the real writer, with a manifest of what went in.
python3 - "$BUILD_DIR" "$TOOLS" <<'PY'
import sys, os, random
build, tools = sys.argv[1], sys.argv[2]
sys.path.insert(0, tools)
import pack_format
rng = random.Random(4)
items = []
for i in range(300):
    it = {'q': f'Clue {i} — about this thing with a café and a naïve rôle',
          'a': f'Answer {i}', 'd': 1 + i % 5, 'y': 1984 + i % 42}
    if i % 3 == 0:
        it['alt'] = [f'The Answer {i}']
    # Three shapes must round-trip through the real writer: 6 distractors (the
    # shipped pack), 3 (what assemble_pack.py now stores, STORED = 3), and no
    # `w` key at all (read-aloud only).
    if i % 2 == 0:
        it['w'] = [f'Wrong {i}.{w}' for w in range(6)]
    elif i % 4 == 1:
        it['w'] = [f'Wrong {i}.{w}' for w in range(3)]
    items.append(it)
pack_format.write(items, os.path.join(build, 'real.dat'))
with open(os.path.join(build, 'real.tsv'), 'w', encoding='utf-8') as f:
    for it in items:
        f.write(f"{it['d']}\t{it['y']}\t{len(it.get('alt',[]))}\t{len(it.get('w',[]))}\t{it['q']}\t{it['a']}\n")
PY
"$BUILD_DIR/test_realpack" "$BUILD_DIR/real.dat" "$BUILD_DIR/real.tsv"
