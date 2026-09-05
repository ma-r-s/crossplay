#!/bin/sh
# The busy frame has to be ON THE PANEL before the socket opens.
#
#   host-tests/paintfirst/run.sh
#
# WHY A SOURCE SCAN. Whether a slow network step reads as "working" or as "the
# device has died" is decided by which frame is on the glass when the socket
# opens -- a property of the ORDER of statements across functions, on classes
# that need Storage, WiFi, a GfxRenderer and a FreeRTOS render task to
# instantiate. Nothing freestanding can hold it. host-tests/ui can check that a
# busy screen reads correctly; only this can check that it is ever drawn.
#
# WHY EVERY PATTERN HERE IS ANCHORED AT ITS INDENT. The first version of this
# suite used `grep -F` over a function body, and a cold critic made all of it
# pass with the bug fully alive, three ways: a COMMENT containing the text
# "requestUpdateAndWait();" satisfied the ordering checks; wrapping
# displayBuffer() in `if (state != LOADING)` left the string present and the
# frame unreachable; and an unscoped `RenderLock lock(*this)` held across the
# wait -- the exact deadlock a check was meant to prevent -- was satisfied by
# any unrelated two-space `}` above it. So:
#
#   * every pattern matches a STATEMENT, not a substring: `^  foo\(\);` with an
#     optional trailing comment. A comment line begins with `//` and can never
#     match one.
#   * the indent IS the assertion. Exactly two spaces means function-body
#     level, so a call moved inside an `if` or a lambda stops matching -- that
#     is what makes it a reachability check rather than a presence check.
#   * scope is checked by looking at the thing that owns it (a RenderLock at
#     function-body indent), never at a brace that happens to precede.
#
# THE MECHANISM (card #306, established while fixing #244 / PR #118).
# Plain requestUpdate() is DEFERRED: it sets a flag consumed at the tail of
# ActivityManager::loop(), a tail that cannot be reached while a blocking call
# sits in the same call stack, so `requestUpdate(); fetch();` never notifies the
# render task at all. requestUpdate(true) notifies immediately but only RACES
# the socket. requestUpdateAndWait() blocks until the render task has finished
# render(), and a render() ending in renderer.displayBuffer() has been through
# the blocking panel path, which returns only after the waveform.
set -e
cd "$(dirname "$0")/../.."

checks=0
failed=0
ok()  { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); failed=$((failed + 1)); echo "FAIL paintfirst  $1"; }

need_file() {
  [ -f "$1" ] || { echo "FAIL paintfirst  $1 is missing -- every scan below would pass by finding nothing"; exit 1; }
}

# Comment-only lines, gone. Nothing below can be satisfied by prose: a critic
# made the first version of this suite green by writing the call it was looking
# for into a comment.
nocomment() { grep -vE '^[[:space:]]*(//|\*|/\*)' || true; }

# Body of the function whose signature matches $2, in file $1, comments
# stripped. Functions start at column 0 and close on a bare `}` at column 0.
body() {
  awk -v sig="$2" '
    !inside && $0 ~ sig { inside = 1 }
    inside { print }
    inside && /^\}/ { exit }
  ' "$1" | nocomment
}

# A statement at exactly $1 spaces of indent, optionally trailed by a comment.
# Anything inside a comment, a string, or a deeper scope fails to match.
stmt() { printf '^%s%s;[[:space:]]*(//.*)?$' "$1" "$2"; }

# Is the first anchored pattern ahead of the second, in the text on stdin?
ahead_re() {
  text=$(cat)
  a=$(printf '%s\n' "$text" | grep -nE -- "$1" | head -1 | cut -d: -f1)
  b=$(printf '%s\n' "$text" | grep -nE -- "$2" | head -1 | cut -d: -f1)
  [ -n "$a" ] && [ -n "$b" ] && [ "$a" -lt "$b" ]
}

has_re() { printf '%s\n' "$2" | grep -qE -- "$1"; }

# Every call matching $2 must have a call matching $1 within the preceding $3
# lines. Catches the site added tomorrow, which is how the twin was missed.
preceded_within() {
  awk -v guard="$1" -v danger="$2" -v span="$3" '
    $0 ~ guard { last = NR }
    $0 ~ danger { if (last == 0 || NR - last > span) { bad = 1 } }
    END { exit bad ? 1 : 0 }
  '
}

I2="  "
I4="    "

# ---------------------------------------------------------------------------
# CASE 0: the handshake all three cases rest on.
#
# renderTaskLoop() claims waitingTaskHandle at the TOP of an iteration, before
# the frame is built. When it claimed at the bottom instead, a waiter that
# registered while the render task was ALREADY inside render() was woken by the
# frame that began before it asked -- so requestUpdateAndWait() returned with
# the previous screen still on the panel, and the leftover notification then
# ran the next render CONCURRENTLY with the caller's fetch, which mutates the
# entry list that render() reads with no lock between them. Fixing the six call
# sites on top of that handshake would have traded a cosmetic bug for a
# memory-safety one.
# ---------------------------------------------------------------------------
AM=src/activities/ActivityManager.cpp
need_file "$AM"
rtl=$(body "$AM" '^void ActivityManager::renderTaskLoop[(]')

if printf '%s\n' "$rtl" | ahead_re "$(stmt "$I4" 'waiter = waitingTaskHandle')" '^ +currentActivity->render\('; then ok
else bad "renderTaskLoop() claims waitingTaskHandle AFTER render() (or not at all); a wait registered mid-render is then satisfied by the frame that began before it, and the leftover notification renders concurrently with the caller's fetch"; fi

if printf '%s\n' "$rtl" | ahead_re "$(stmt "$I4" 'waitingTaskHandle = nullptr')" '^ +currentActivity->render\('; then ok
else bad "renderTaskLoop() does not clear waitingTaskHandle before rendering; a second claim could pair one waiter with two frames"; fi

# ...and requestUpdateAndWait() blocks only if it actually claimed the wait.
ruaw=$(body "$AM" '^void ActivityManager::requestUpdateAndWait[(]')
if printf '%s\n' "$ruaw" | ahead_re '^  if \(!claimed\) \{' "$(stmt "$I2" 'ulTaskNotifyTake\(pdTRUE, portMAX_DELAY\)')"; then ok
else bad "requestUpdateAndWait() blocks in ulTaskNotifyTake without first checking that it claimed the wait; a caller the guards rejected then waits forever on a notification nobody sends -- silent and untimed once asserts are compiled out"; fi

# ---------------------------------------------------------------------------
# CASE 1: Get Books' cold start. The worst of the three: it is what a new
# reader hits first.
# ---------------------------------------------------------------------------
OPDS=src/activities/browser/OpdsBookBrowserActivity.cpp
need_file "$OPDS"
fetch=$(body "$OPDS" '^void OpdsBookBrowserActivity::beginFetch[(]')

if [ -n "$fetch" ]; then ok
else bad "OpdsBookBrowserActivity has no beginFetch(): every path into a feed sets its own busy state again, which is how all six got it wrong"; fi

if has_re "$(stmt "$I2" 'state = BrowserState::LOADING')" "$fetch"; then ok
else bad "beginFetch() does not set BrowserState::LOADING at function-body level -- there is no busy frame to paint"; fi

if printf '%s\n' "$fetch" | ahead_re "$(stmt "$I2" 'requestUpdateAndWait\(\)')" "$(stmt "$I2" 'fetchFeed\(path\)')"; then ok
else bad "beginFetch() does not requestUpdateAndWait() before fetchFeed() as an unconditional statement; the LOADING frame races the socket instead of preceding it"; fi

# Every path into a feed goes through the helper, or the next one written will
# not. See the memory bounding-one-of-two-input-paths.
calls=$(grep -cE '^ +fetchFeed\(' "$OPDS" || true)
if [ "$calls" = "1" ]; then ok
else bad "fetchFeed() is called from $calls places in $OPDS; every one that is not beginFetch() is a blocking fetch with no busy frame in front of it"; fi

# Both defective idioms, spelled out. requestUpdate(true) is included because
# it notifies and returns -- it races the socket rather than preceding it, and
# on three of these paths it was what shipped.
if grep -A1 -nE '^ +requestUpdate\((true)?\);' "$OPDS" | grep -qE 'fetchFeed\(|downloadToFile\('; then
  bad "$OPDS still asks for an update and then blocks in the same call stack (requestUpdate() never notifies at all; requestUpdate(true) only races)"
else ok; fi

# The twin in the same file: the download's own connect window.
dl=$(body "$OPDS" '^void OpdsBookBrowserActivity::downloadBook[(]')
if printf '%s\n' "$dl" | ahead_re "$(stmt "$I2" 'requestUpdateAndWait\(\)')" '^ +.*HttpDownloader::downloadToFile\('; then ok
else bad "downloadBook() does not requestUpdateAndWait() before downloadToFile(); the whole TCP+TLS connect window runs before the first progress callback can repaint anything"; fi

# The render that has to publish the frame the wait is waiting for -- at
# function-body level, so a conditional displayBuffer() is not mistaken for a
# reachable one. See the memory activity-render-contract.
if has_re "$(stmt "$I2" 'renderer\.displayBuffer\(\)')" "$(body "$OPDS" '^void OpdsBookBrowserActivity::render[(]')"; then ok
else bad "OpdsBookBrowserActivity::render() has no unconditional displayBuffer() at function-body level; the wait would return with nothing on the panel"; fi

# ---------------------------------------------------------------------------
# CASE 2: KOReader authentication, over TLS, reachable straight from onEnter().
# ---------------------------------------------------------------------------
KOA=src/activities/settings/KOReaderAuthActivity.cpp
need_file "$KOA"
auth=$(body "$KOA" '^void KOReaderAuthActivity::onWifiSelectionComplete[(]')

if has_re "$(stmt "$I4" 'state = AUTHENTICATING')" "$auth"; then ok
else bad "onWifiSelectionComplete() no longer sets AUTHENTICATING -- there is no busy state to paint"; fi

if printf '%s\n' "$auth" | ahead_re "$(stmt "$I2" 'requestUpdateAndWait\(\)')" "$(stmt "$I2" 'performAuthentication\(\)')"; then ok
else bad "performAuthentication() is not preceded by an unconditional requestUpdateAndWait() in onWifiSelectionComplete(); it opens TLS on this task and the panel keeps the settings page the reader just left"; fi

# The wait must not sit inside the RenderLock scope above it. Checked by
# looking at the LOCK -- a RenderLock at function-body indent is one that is
# still held at the wait. A brace preceding the wait proves nothing.
if has_re '^  RenderLock ' "$auth"; then
  bad "onWifiSelectionComplete() declares a RenderLock at function-body level; it is still held at requestUpdateAndWait(), which asserts and would deadlock with asserts off"
else ok; fi

sites=$(grep -cE '^ +performAuthentication\(\);' "$KOA" || true)
if [ "$sites" = "1" ]; then ok
else bad "performAuthentication() is called $sites times; each extra site is an unpainted handshake"; fi

if has_re "$(stmt "$I2" 'renderer\.displayBuffer\(\)')" "$(body "$KOA" '^void KOReaderAuthActivity::render[(]')"; then ok
else bad "KOReaderAuthActivity::render() has no unconditional displayBuffer() at function-body level; the wait would return with nothing on the panel"; fi

# ---------------------------------------------------------------------------
# CASE 3: Instapaper. Not a mis-ordered busy state -- there was none at all, on
# BOTH of its blocking pairAbandon() calls, one switch branch apart.
# ---------------------------------------------------------------------------
INS=src/apps_local/instapaper/InstapaperActivity.cpp
need_file "$INS"
paint=$(body "$INS" '^void InstapaperActivity::paintBusyNow[(]')

if has_re "$(stmt "$I4" 'phase_ = Phase::Busy')" "$paint"; then ok
else bad "paintBusyNow() sets no busy phase; there is nothing for the wait to put on the panel"; fi

if has_re "$(stmt "$I2" 'requestUpdateAndWait\(\)')" "$paint"; then ok
else bad "paintBusyNow() does not requestUpdateAndWait() at function-body level; the busy frame is never on the glass before the caller's TLS call blocks"; fi

if has_re '^  RenderLock ' "$paint"; then
  bad "paintBusyNow() declares a RenderLock at function-body level; it is still held at requestUpdateAndWait(), which asserts and would deadlock with asserts off"
else ok; fi

# EVERY pairAbandon, not the one in front of me. The first fix landed on
# performDisconnect() and left the identical call in the Back handler alone;
# see the memory fix-the-twin-too.
if nocomment < "$INS" | preceded_within 'paintBusyNow[(]' 'sync_[.]pairAbandon[(]' 8; then ok
else bad "a sync_.pairAbandon() in $INS is not preceded by paintBusyNow(); that is a blocking TLS revoke with whatever screen the reader was on left frozen behind it"; fi

if has_re "$(stmt "$I2" 'renderer\.displayBuffer\(\)')" "$(body "$INS" '^void InstapaperActivity::render[(]')"; then ok
else bad "InstapaperActivity::render() has no unconditional displayBuffer() at function-body level; the wait would return with nothing on the panel"; fi

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]
