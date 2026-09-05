#!/bin/sh
# Builds and runs the Connections rules tests. No device and no PlatformIO:
# ConnectionsCore is freestanding C++17.
#
#   host-tests/connections/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-connections-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/connections
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror $SRC/ConnectionsCore.cpp \
  test_connections.cpp -o "$BUILD_DIR/test_connections"
"$BUILD_DIR/test_connections"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror $SRC/ConnectionsCore.cpp $SRC/ConnectionsPack.cpp $SRC/ConnectionsResults.cpp \
  $SRC/ConnectionsImport.cpp test_pack.cpp -o "$BUILD_DIR/test_pack"
"$BUILD_DIR/test_pack"

# ---- The paint that has to happen before the download ----------------------
#
# WHY A SOURCE SCAN. GET PUZZLES blocks the loop for about a minute inside
# HttpDownloader::fetchUrl, and the only thing standing between that and "the
# device has crashed" is which frame is on the panel when the socket opens.
# That is a property of an ORDER between two statements in two different
# functions, on a class that needs Storage, WiFi and a renderer to instantiate.
# Nothing freestanding can hold it. host-tests/ui can check that the busy
# screen reads correctly; only this can check that it is ever drawn.
#
# WHAT IT IS THE ONLY CHECK FOR. buildImport() existed, was correct, and was
# called from nowhere for a year: render()'s switch put `default:` on the same
# label as View::Menu, so every import painted the menu. Every gate passed --
# the code compiled, the screen was right, the import worked. See the memory
# "a repair placed where it cannot run": the fix that cannot run is the one
# nothing can distinguish from the fix that does.
ACT="$SRC/ConnectionsActivity.cpp"
[ -f "$ACT" ] || { echo "FAIL connections  $ACT is missing -- the scan below would pass by finding nothing"; exit 1; }
checks=0
failed=0
ok()  { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); failed=$((failed + 1)); echo "FAIL connections  $1"; }

# 1. The import screen is drawn by the activity, not merely defined next to it.
if grep -q "ui::buildImport(screen, model)" "$ACT"; then ok
else bad "render() never calls buildImport(); an import would paint whatever screen View::Importing falls through to"; fi

# 2. Every view is named. `default:` is what swallowed View::Importing, and a
#    case a default absorbs is invisible to -Wswitch -- which this build does
#    not enable anyway, so nothing but this scan can hold the property.
if awk '/switch \(view\) \{/ { inside = 1 }
        inside && /^    default:/ { found = 1 }
        inside && /^  \}/ { exit }
        END { exit found ? 1 : 0 }' "$ACT"; then ok
else bad "render()'s view switch has a default: label again -- a view added tomorrow draws the wrong screen and nothing warns"; fi

# 3. The busy frame is WAITED for, not merely requested. requestUpdate() only
#    notifies the render task and returns, so the paint would race the socket
#    read; requestUpdateAndWait() blocks until the frame is on the panel.
if awk '/importStep == ImportStep::Ready\) \{/ { inside = 1 }
        inside && /requestUpdateAndWait\(\)/ { found = 1 }
        inside && /^  \}/ { exit }
        END { exit found ? 0 : 1 }' "$ACT"; then ok
else bad "the Ready -> Downloading step does not requestUpdateAndWait(); the busy frame races the download instead of preceding it"; fi

# 4. ...and that branch RETURNS, so the fetch happens on a later pass and not
#    on the far side of the same one.
if awk '/importStep == ImportStep::Ready\) \{/ { inside = 1 }
        inside && /^    return;/ { found = 1 }
        inside && /^  \}/ { exit }
        END { exit found ? 0 : 1 }' "$ACT"; then ok
else bad "the Ready -> Downloading step falls through to the fetch in the same loop pass"; fi

# 5. runImport() is reached from exactly one place, and it is after the wait.
#    A second call site is a second path with no paint in front of it -- the
#    shape that has cost this fork a session more than once.
calls=$(grep -c "runImport();" "$ACT")
if [ "$calls" = "1" ]; then ok
else bad "runImport() is called $calls times; each extra call site is a download with no busy frame in front of it"; fi
wait_line=$(grep -n "requestUpdateAndWait();" "$ACT" | head -1 | cut -d: -f1)
call_line=$(grep -n "runImport();" "$ACT" | head -1 | cut -d: -f1)
if [ -n "$wait_line" ] && [ -n "$call_line" ] && [ "$wait_line" -lt "$call_line" ]; then ok
else bad "runImport() is not preceded by requestUpdateAndWait() in loop()"; fi

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]
