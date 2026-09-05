#!/bin/sh
# Builds and runs the trivia tests. No device and no PlatformIO: TriviaCore is
# freestanding C++17, which is what lets the format, the chooser and the option
# shuffle all be checked without a panel.
#
# Two sub-suites, and the second is the one that matters most:
#   test_trivia    -- reader, chooser and shuffle, over a pack built in C++
#   test_report    -- the outbound report queue, pack.meta and the manifest
#                     reader, plus a parity check that the wire codes mean the
#                     same thing here and in tools_local/trivia/reports.py
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
    # ~20% are US-centric. The flag rides bit 7 of the difficulty byte through
    # the real writer, so this checks the writer packs it exactly where the C++
    # reader unpacks it -- and that the difficulty still reads back as 1..5.
    if i % 5 == 4:
        it['us'] = True
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
        us = 1 if it.get('us') else 0
        f.write(f"{it['d']}\t{it['y']}\t{len(it.get('alt',[]))}\t{len(it.get('w',[]))}\t{us}\t{it['q']}\t{it['a']}\n")
PY
"$BUILD_DIR/test_realpack" "$BUILD_DIR/real.dat" "$BUILD_DIR/real.tsv"

# The report queue and pack.meta. Same argument as above: freestanding, so the
# refusals can be checked without a panel -- and they are almost all refusals,
# because the failure they prevent is a report that names the wrong pack, which
# downstream deletes a question nobody reported.
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_report.cpp "$SRC/TriviaReport.cpp" -o "$BUILD_DIR/test_report"
"$BUILD_DIR/test_report"

# The wire codes are shared with the Python side, and a code that means "wrong
# answer" here and "too easy" there is a silent corpus edit that no build error
# would catch. Compared by NAME and VALUE, in both directions, so adding a code
# to one side alone fails too.
python3 - "$SRC" "$TOOLS" <<'PARITY'
import re, sys
src, tools = sys.argv[1], sys.argv[2]
sys.path.insert(0, tools)
import reports

header = open(f"{src}/TriviaReport.h", encoding="utf-8").read()
block = re.search(r"enum class Reason : uint8_t \{(.*?)\};", header, re.S).group(1)
cpp = {}
for name, value in re.findall(r"(\w+)\s*=\s*(\d+)\s*,", block):
    cpp[int(value)] = name
count = cpp.pop(max(cpp))  # the Count sentinel is not a reason
cpp_named = {v: n.lower() for v, n in cpp.items()}

if cpp_named != reports.REASONS:
    print("FAIL trivia  reason codes differ between TriviaReport.h and reports.py")
    print(f"      C++   : {cpp_named}")
    print(f"      Python: {reports.REASONS}")
    sys.exit(1)
if len(reports.REASONS) != int(re.search(r"Count = (\d+)", block).group(1)):
    print("FAIL trivia  Reason::Count does not match the number of Python codes")
    sys.exit(1)
# reasonName() is what actually goes on the wire, so compare THAT too rather
# than only the enumerator spellings -- they are two different facts and only
# one of them reaches the endpoint.
impl = open(f"{src}/TriviaReport.cpp", encoding="utf-8").read()
body = re.search(r"const char\* reasonName\(const Reason reason\) \{(.*?)\n\}", impl, re.S).group(1)
wire = dict(re.findall(r"case Reason::(\w+):\s*\n\s*return \"(\w*)\";", body))
wire_by_value = {v: wire[n] for v, n in cpp.items() if n in wire}
if wire_by_value != reports.REASONS:
    print("FAIL trivia  reasonName() disagrees with reports.py")
    print(f"      wire  : {wire_by_value}")
    print(f"      Python: {reports.REASONS}")
    sys.exit(1)

# The reason SCREEN has to be able to show every reason at once. Its list is
# virtualised -- a row that does not fit is not drawn and not registered for
# interaction -- so an eleventh reason would silently become unreachable in the
# one combination that shows them all (solo, US questions off). ReasonModel::kMax
# is the buffer, and buildReasons' rowHeight comment carries the arithmetic; this
# only makes adding a reason a deliberate act rather than a quiet overflow.
screens = open(f"{src}/TriviaScreens.h", encoding="utf-8").read()
kmax = int(re.search(r"kMax\s*=\s*(\d+)", screens).group(1))
selectable = len(reports.REASONS) - 1  # every code except `none`, which is not a row
if kmax < selectable:
    print(f"FAIL trivia  {selectable} selectable reasons but ReasonModel::kMax is {kmax}")
    print("              The extra rows would not be drawn and could not be tapped.")
    print("              Raise kMax AND re-check the fit in buildReasons.")
    sys.exit(1)

print(f"parity ok: {len(reports.REASONS)} reason codes and wire names agree; "
      f"{selectable} selectable rows fit kMax {kmax}")
PARITY
