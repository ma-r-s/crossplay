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
# Comments AND string literals, gone -- properly. The previous version dropped
# lines whose FIRST non-space characters were // or /* , so the INTERIOR of a
# /* ... */ block survived: commenting out requestUpdateAndWait() left every
# statement-anchored check green with no paint before the fetch at all. It also
# left string literals intact, so LOG_DBG("... RenderLock ...") counted as a
# lock. This tracks block state across lines and blanks quoted text, so nothing
# below can be satisfied by prose OR by a message. (Known limit: a /* or a quote
# inside another quote is not parsed; this is a scanner, not a lexer.)
nocomment() {
  awk '
    {
      line = $0; out = ""; i = 1; n = length(line)
      while (i <= n) {
        c = substr(line, i, 1); d = substr(line, i, 2)
        if (inblock) { if (d == "*/") { inblock = 0; i += 2 } else i++ }
        else if (instr) { if (c == "\\") { i += 2 } else { if (c == "\"") instr = 0; out = out " "; i++ } }
        else if (d == "/*") { inblock = 1; i += 2 }
        else if (d == "//") break
        else if (c == "\"") { instr = 1; out = out " "; i++ }
        else { out = out c; i++ }
      }
      print out
    }'
}

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

# REACHABILITY, not presence. `$2` (a render body, comments stripped) must
# contain the statement `$1` with NO `return` of any kind before it -- an early
# return above displayBuffer() leaves the string present at the right indent
# and the frame unreachable, which is how a critic made the previous version of
# this suite green while the busy frame never painted. None of the three
# render()s here has an early return today, so this is strict without being
# brittle; a render that grows one has to say so by failing.
reached() {
  printf '%s\n' "$2" | awk -v want="$1" '
    $0 ~ want { found = NR; exit }
    /(^|[^A-Za-z_])return([; ]|$)/ { ret = NR }
    END { }
  ' > /dev/null
  line=$(printf '%s\n' "$2" | grep -nE -- "$1" | head -1 | cut -d: -f1)
  [ -n "$line" ] || return 1
  before=$(printf '%s\n' "$2" | sed -n "1,$((line - 1))p" | grep -cE '(^|[^A-Za-z_])return([; ]|$)')
  [ "$before" -eq 0 ]
}

# A RenderLock held at function-body level, however it is SPELLED. The previous
# version grepped `^  RenderLock `, so `auto lock = RenderLock(*this);` walked
# straight past the check that exists to prevent a deadlock, and so would
# `RenderLock* p = new RenderLock(...)`. Comments are already stripped, so any
# mention at body indent is a construction.
# A RenderLock DECLARATION -- `RenderLock lock(...)`, `auto x = RenderLock(...)`,
# `new RenderLock(...)` -- not merely the word, which also occurs in log messages
# and in prose. Strings and comments are already stripped by nocomment().
LOCKDECL='(RenderLock[[:space:]]+[A-Za-z_]|=[[:space:]]*RenderLock[(]|new[[:space:]]+RenderLock)'
holds_lock_at_body() { printf '%s\n' "$1" | grep -E '^  [^ ]' | grep -qE "$LOCKDECL"; }

# Is the statement matching $1 executed while a RenderLock declared in an
# ENCLOSING scope is still alive? Brace depth, not proximity: fetchFeed() has
# three error-path locks that textually precede the container swap, and an
# "is there a RenderLock somewhere above" check is satisfied by any of them
# while the swap itself runs unlocked. A lock protects a SCOPE; the check has
# to model the scope or it is checking nothing. The lock declared at depth D
# lives until depth falls below D.
locked_at() {
  printf '%s\n' "$2" | awk -v target="$1" -v lockdecl="$LOCKDECL" '
    function cnt(s, ch,   n, i) { n = 0; for (i = 1; i <= length(s); i++) if (substr(s, i, 1) == ch) n++; return n }
    {
      if ($0 ~ target && lockdepth > 0 && depth >= lockdepth) found = 1
      if ($0 ~ lockdecl) lockdepth = (depth > 0 ? depth : 1)
      depth += cnt($0, "{") - cnt($0, "}")
      if (lockdepth > 0 && depth < lockdepth) lockdepth = 0
    }
    END { exit found ? 0 : 1 }
  '
}

# Nothing that blocks may run BEFORE the wait: the frame is only a guarantee
# for what comes after it. A denylist, and therefore incomplete by
# construction -- it names the blocking calls this fork actually has rather
# than proving the absence of all of them.
BLOCKING='HttpDownloader::|ensureConnected|KOReaderSyncClient::|sync_\.|downloadToFile|WiFi\.begin|\.pairAbandon'

# Every call matching $2 must have a call matching $1 within the preceding $3
# lines. Catches the site added tomorrow, which is how the twin was missed.
preceded_within() {
  awk -v guard="$1" -v danger="$2" -v span="$3" '
    # Blank lines do not count toward the span: nocomment() blanks comments in
    # place to keep line numbers honest, and counting those made a guard look
    # near when it was far (performDisconnect passed on that alone).
    /^[[:space:]]*$/ { next }
    { code++ }
    # The guard must be a BARE statement at its own indent. `void
    # X::paintBusyNow(...)` is the definition, and `if (kNeverTrue)
    # paintBusyNow(...)` is dead code -- neither guards anything.
    /^[ ]+[A-Za-z_]/ && $0 ~ guard { last = code }
    $0 ~ danger { if (last == 0 || code - last > span) { bad = 1 } }
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

# ...and the waiter is WOKEN after the frame, not before it. Claiming early and
# notifying early is the same bug wearing the fix's clothes: requestUpdateAndWait()
# would return before any frame existed. The claim and the notify are two facts
# and each needs its own check.
if printf '%s\n' "$rtl" | ahead_re '^ +currentActivity->render\(' 'xTaskNotify[(]waiter'; then ok
else bad "renderTaskLoop() notifies the waiter BEFORE currentActivity->render(); requestUpdateAndWait() then returns with no frame on the panel, which is the entire guarantee"; fi

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
dl=$(body "$OPDS" '^void OpdsBookBrowserActivity::downloadBook[(]')

if [ -n "$fetch" ]; then ok
else bad "OpdsBookBrowserActivity has no beginFetch(): every path into a feed sets its own busy state again, which is how all six got it wrong"; fi

if has_re "$(stmt "$I4" 'state = BrowserState::LOADING')" "$fetch"; then ok
else bad "beginFetch() does not set BrowserState::LOADING inside its lock scope -- there is no busy frame to paint"; fi

# POSITIVE lock checks. The two holds_lock_at_body checks below are ANTI-checks
# (no lock held ACROSS the wait); without a positive counterpart, deleting a
# lock outright stays green. statusMessage is a std::string the render task
# reads through .c_str(), so writing it unlocked is the same use-after-free
# class as the entries swap -- in the TAKE/CLAIM window this branch admits it
# cannot close.
if locked_at 'statusMessage = ' "$fetch"; then ok
else bad "beginFetch() writes statusMessage with no RenderLock; the render task reads it through .c_str() and a reallocating operator= frees the buffer under it"; fi

if locked_at 'downloadAuthor = ' "$dl"; then ok
else bad "downloadBook() writes downloadAuthor with no RenderLock; same use-after-free class, same window"; fi

if printf '%s\n' "$fetch" | ahead_re "$(stmt "$I2" 'requestUpdateAndWait\(\)')" "$(stmt "$I2" 'fetchFeed\(path\)')"; then ok
else bad "beginFetch() does not requestUpdateAndWait() before fetchFeed() as an unconditional statement; the LOADING frame races the socket instead of preceding it"; fi

# Nothing blocking may run BEFORE the wait, or the frame guarantees the wrong
# half of the function.
if printf '%s\n' "$fetch" | grep -nE -- "$BLOCKING" | head -1 | cut -d: -f1 | {
     read -r first || first=""
     w=$(printf '%s\n' "$fetch" | grep -nE -- "$(stmt "$I2" 'requestUpdateAndWait\(\)')" | head -1 | cut -d: -f1)
     [ -z "$first" ] || { [ -n "$w" ] && [ "$w" -lt "$first" ]; }
   }; then ok
else bad "beginFetch() runs a blocking call BEFORE requestUpdateAndWait(); the busy frame then guarantees nothing about the part that blocks"; fi

# THE LOCK, not the notification ordering. fetchFeed() replaces `entries` and
# rebuilds `rowItems` from it while the render task may read rowItems -- whose
# ListItems hold const char* into entries[i].title. The handshake fix in
# ActivityManager narrows when a render can overlap this; it cannot eliminate
# it, because the render task is pinned to the OTHER core and a waiter that
# registers between its take and its claim leaves one notification unconsumed.
# A timing argument does not survive a second core. See the memory
# a-rect-whose-meaning-changes for the general shape: only the lock is the
# invariant.
ff=$(body "$OPDS" '^void OpdsBookBrowserActivity::fetchFeed[(]')
if locked_at 'entries = std::move' "$ff"; then ok
else bad "fetchFeed() replaces entries with no RenderLock alive in an enclosing scope; a render overlapping that swap reads freed strings through rowItems -- a use-after-free, not a torn frame"; fi

if locked_at 'rebuildRowItems[(][)];' "$ff"; then ok
else bad "fetchFeed() rebuilds rowItems with no RenderLock alive in an enclosing scope"; fi

# The same containers, freed from four other callers.
re=$(body "$OPDS" '^void OpdsBookBrowserActivity::releaseEntries[(]')
if locked_at 'swap[(]entries[)]' "$re"; then ok
else bad "releaseEntries() frees entries with no RenderLock alive; four navigation paths reach it while the render task may be mid-frame"; fi

# Every path into a feed goes through the helper, or the next one written will
# not. See the memory bounding-one-of-two-input-paths.
# Any spelling of the call, not just an unqualified one at an indent:
# `this->fetchFeed(...)` walked past the check written to stop exactly this --
# which is bounding-one-of-two-input-paths reproduced by its own guard. The
# definition line starts at column 0 and is excluded.
calls=$(nocomment < "$OPDS" | grep -E 'fetchFeed[(]' | grep -vE '^[A-Za-z].*::fetchFeed[(]' | grep -c . || true)
if [ "$calls" = "1" ]; then ok
else bad "fetchFeed() is called from $calls places in $OPDS; every one that is not beginFetch() is a blocking fetch with no busy frame in front of it"; fi

# Both defective idioms, spelled out. requestUpdate(true) is included because
# it notifies and returns -- it races the socket rather than preceding it, and
# on three of these paths it was what shipped.
if grep -A3 -nE '^ +requestUpdate\((true)?\);' "$OPDS" | grep -qE '^[0-9]+.[ ]+(fetchFeed\(|.*HttpDownloader::downloadToFile\()'; then
  bad "$OPDS still asks for an update and then blocks in the same call stack (requestUpdate() never notifies at all; requestUpdate(true) only races)"
else ok; fi

# The twin in the same file: the download's own connect window.
if printf '%s\n' "$dl" | ahead_re "$(stmt "$I2" 'requestUpdateAndWait\(\)')" '^ +.*HttpDownloader::downloadToFile\('; then ok
else bad "downloadBook() does not requestUpdateAndWait() before downloadToFile(); the whole TCP+TLS connect window runs before the first progress callback can repaint anything"; fi

# The download screen is a screen ENTRY, so it announces itself like one, and
# the one place a touch is routed during a download is gated on that
# announcement. Painting synchronously (above) means the panel has finished
# refreshing before the first progress callback, so Cancel is reliably live by
# the time a finger that tapped DOWNLOAD lifts -- a-tap-is-a-touch-down.
if printf '%s\n' "$dl" | ahead_re "$(stmt "$I2" 'resetUi\(\)')" "$(stmt "$I2" 'requestUpdateAndWait\(\)')"; then ok
else bad "downloadBook() does not resetUi() before its paint; the download screen publishes an interaction table no screen entry announced, and the reveal gate is never armed for it"; fi

# The contact that tapped DOWNLOAD must be suppressed, not merely read. A
# read-and-discard of wasScreenTapped() suppresses NOTHING -- it is a pure query
# and only update() clears the event -- so the next routeTouch() sees it again.
if printf '%s\n' "$dl" | ahead_re "$(stmt "$I2" 'mappedInput\.swallowCurrentTouch\(\)')" '^ +const auto result = HttpDownloader::downloadToFile'; then ok
else bad "downloadBook() does not swallowCurrentTouch() after its paint; the DOWNLOAD tap's own release routes against the download screen's live Cancel target"; fi

# EVERY routeTouch, not "a guarded one exists". A dead-code copy beside a live
# unguarded call satisfies an existence check while the unguarded call is what
# runs -- the same shape as bounding-one-of-two-input-paths. Requiring the guard
# on the same line as the call means an unguarded second copy cannot hide.
unguarded=$(printf '%s\n' "$dl" | grep -E 'routeTouch[(]' | grep -vc 'routingReady[(][)]' || true)
if [ "$unguarded" = "0" ]; then ok
else bad "$unguarded routeTouch() call(s) in downloadBook() carry no routingReady() gate on the same line; the only place input is read during a download routes against an unannounced table"; fi

# The render that has to publish the frame the wait is waiting for -- at
# function-body level, so a conditional displayBuffer() is not mistaken for a
# reachable one. See the memory activity-render-contract.
if reached "$(stmt "$I2" 'renderer\.displayBuffer\(\)')" "$(body "$OPDS" '^void OpdsBookBrowserActivity::render[(]')"; then ok
else bad "OpdsBookBrowserActivity::render() has no REACHABLE displayBuffer() at function-body level (absent, nested, or behind an early return); the wait would return with nothing on the panel"; fi

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
if holds_lock_at_body "$auth"; then
  bad "onWifiSelectionComplete() declares a RenderLock at function-body level; it is still held at requestUpdateAndWait(), which asserts and would deadlock with asserts off"
else ok; fi

sites=$(grep -cE '^ +performAuthentication\(\);' "$KOA" || true)
if [ "$sites" = "1" ]; then ok
else bad "performAuthentication() is called $sites times; each extra site is an unpainted handshake"; fi

if reached "$(stmt "$I2" 'renderer\.displayBuffer\(\)')" "$(body "$KOA" '^void KOReaderAuthActivity::render[(]')"; then ok
else bad "KOReaderAuthActivity::render() has no REACHABLE displayBuffer() at function-body level (absent, nested, or behind an early return); the wait would return with nothing on the panel"; fi

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

if holds_lock_at_body "$paint"; then
  bad "paintBusyNow() declares a RenderLock at function-body level; it is still held at requestUpdateAndWait(), which asserts and would deadlock with asserts off"
else ok; fi

# ...and it must hold one SOMEWHERE. The check above is an ANTI-check; on its
# own, deleting the lock outright satisfies it. phase_ and busyMessage_ are read
# by the render task, so that write needs the lock exactly like the OPDS sites.
if locked_at 'phase_ = Phase::Busy' "$paint"; then ok
else bad "paintBusyNow() writes phase_ with no RenderLock alive; the render task reads it, and the anti-check above is satisfied by having no lock at all"; fi

if printf '%s\n' "$paint" | grep -nE -- "$BLOCKING" | head -1 | cut -d: -f1 | {
     read -r first || first=""
     w=$(printf '%s\n' "$paint" | grep -nE -- "$(stmt "$I2" 'requestUpdateAndWait\(\)')" | head -1 | cut -d: -f1)
     [ -z "$first" ] || { [ -n "$w" ] && [ "$w" -lt "$first" ]; }
   }; then ok
else bad "paintBusyNow() runs a blocking call BEFORE requestUpdateAndWait()"; fi

# EVERY pairAbandon, not the one in front of me. The first fix landed on
# performDisconnect() and left the identical call in the Back handler alone;
# see the memory fix-the-twin-too.
if nocomment < "$INS" | preceded_within '^[[:space:]]+paintBusyNow[(]' 'sync_[.]pairAbandon[(]' 8; then ok
else bad "a sync_.pairAbandon() in $INS is not preceded by paintBusyNow(); that is a blocking TLS revoke with whatever screen the reader was on left frozen behind it"; fi

if reached "$(stmt "$I2" 'renderer\.displayBuffer\(\)')" "$(body "$INS" '^void InstapaperActivity::render[(]')"; then ok
else bad "InstapaperActivity::render() has no REACHABLE displayBuffer() at function-body level (absent, nested, or behind an early return); the wait would return with nothing on the panel"; fi

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]
