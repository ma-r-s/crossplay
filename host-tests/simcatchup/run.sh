#!/bin/bash
# sim_catchup.py patches the fetched simulator source up to this fork's HAL, as a
# pre: hook on every simulator build. Card #140: three patches had gone dead
# (the simulator caught up) and printed "no longer applies" every build for
# weeks, and that noise is how a REAL non-applying patch hid -- its anchor was a
# line another patch inserts later, so on a fresh tree it silently did nothing
# and printed the identical line, surfacing 30s later as undefined symbols in a
# file it never touched.
#
# The fix, exercised here against the REAL module (imported, not copied):
#   * a non-applying patch is COLLECTED and FAILS the build by name
#     (require_all_applied), so a real one cannot scroll past as a warning;
#   * a REMOVED patch leaves nothing behind -- no line, no failure -- which is
#     what makes a stale patch distinguishable from a deleted one, the card's ask.
#
# sim_catchup guards its SCons `Import("env")` so it imports with env=None outside
# a build, exposing patch() and require_all_applied() to drive against a fixture.
# No simulator checkout or build is needed here.
#
#   host-tests/simcatchup/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
CATCHUP="$HERE/../../scripts_local/sim_catchup.py"
[ -f "$CATCHUP" ] || { echo "FAIL cannot find $CATCHUP"; echo "1 checks, 1 failed"; exit 1; }

CATCHUP="$CATCHUP" python3 <<'PY'
import contextlib
import importlib.util
import io
import os
import pathlib
import tempfile

path = os.environ["CATCHUP"]
spec = importlib.util.spec_from_file_location("sim_catchup", path)
sc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sc)  # Import("env") -> NameError -> env=None, wiring skipped

PASS = 0
FAIL = 0


def ok(cond, what):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  ok   {what}")
    else:
        FAIL += 1
        print(f"  FAIL {what}")


def reset():
    sc._UNAPPLIED.clear()


ok(sc.env is None, "imports outside a build with env=None (no wiring runs)")

tmp = pathlib.Path(tempfile.mkdtemp(prefix="simcatchup-"))
hal = tmp / "Hal.h"
REPL = "  bool newThing();\n  bool removeDir();"

# 1. a present anchor applies, and is not collected as a failure.
reset()
hal.write_text("class X {\n  bool removeDir();\n};\n")
sc.patch(hal, "  bool removeDir();", REPL, "newThing", marker="newThing")
ok("newThing" in hal.read_text() and not sc._UNAPPLIED,
   "a present anchor applies and is not collected")

# 2. a second run is idempotent (marker present) and adds nothing.
reset()
before = hal.read_text()
sc.patch(hal, "  bool removeDir();", REPL, "newThing", marker="newThing")
ok(hal.read_text() == before and not sc._UNAPPLIED,
   "a re-run is idempotent: no duplicate, nothing collected")

# 3. a missing anchor is COLLECTED and printed distinctly (not the old warning).
reset()
other = tmp / "Other.h"
other.write_text("nothing relevant here\n")
buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    sc.patch(other, "this anchor is not present", "x", "stale-patch")
out = buf.getvalue()
ok(len(sc._UNAPPLIED) == 1 and "DID NOT APPLY" in out and "stale-patch" in out,
   "a missing anchor is collected and printed as DID NOT APPLY")

# 4. require_all_applied FAILS the build, naming the stale patch. THIS is what a
#    real no-longer-applying patch does now instead of scrolling past.
raised = False
msg = ""
try:
    sc.require_all_applied()
except SystemExit as e:
    raised = True
    msg = str(e.code)
ok(raised and "stale-patch" in msg and "did not apply" in msg,
   "a stale patch FAILS the build, by name")

# 5. a REMOVED patch (nothing collected) is DIFFERENT: no failure, no output.
#    This is the card's distinction -- a deleted patch and a dead one must not
#    look the same.
reset()
raised2 = False
buf2 = io.StringIO()
with contextlib.redirect_stdout(buf2):
    try:
        sc.require_all_applied()
    except SystemExit:
        raised2 = True
ok(not raised2 and buf2.getvalue() == "",
   "a removed patch leaves nothing behind: no failure, no line")

# 6. a missing simulator FILE is collected too (it is equally a non-apply).
reset()
sc.patch(tmp / "NoSuchFile.h", "anchor", "repl", "ghost-file")
ok(len(sc._UNAPPLIED) == 1 and sc._UNAPPLIED[0][0] == "ghost-file",
   "a missing simulator file is collected, not silently skipped")

print(f"{PASS + FAIL} checks, {FAIL} failed")
raise SystemExit(1 if FAIL else 0)
PY
