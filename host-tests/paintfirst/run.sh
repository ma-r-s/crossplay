#!/bin/sh
# The busy screen has to be ASKED FOR before the socket opens.
#
#   host-tests/paintfirst/run.sh
#
# THE DEFECT (card #306). Activity::requestUpdate() defaults to DEFERRED: it
# sets a flag ActivityManager::loop() consumes at its own tail, and that tail
# cannot be reached while a blocking call sits in the same call stack. So
#
#     requestUpdate();
#     fetchSomethingSlow();
#
# never notifies the render task AT ALL. The busy state is set, the busy screen
# exists and is correct, and the panel holds the PREVIOUS screen for the whole
# transfer. Two cold testers concluded the device had died; see the memory
# a-silent-screen-reads-as-a-crash.
#
# THE FIX is requestUpdate(true), which calls xTaskNotify immediately. The frame
# then RACES the socket rather than being guaranteed ahead of it. That is the
# deliberate trade: rendering is milliseconds and a network call is seconds, so
# it wins in practice, and if it ever loses the reader sees the message slightly
# late instead of never. The ask was "it doesn't feel like it crashed", not a
# guarantee -- and a guarantee (requestUpdateAndWait) costs a synchronous window
# in which a render can overlap the fetch, which is a worse bug than the one it
# fixes.
#
# WHERE A PROGRESS CALLBACK EXISTS, IT IS BETTER THAN ANY OF THIS. Get Books'
# downloadBook() survives a whole transfer because HttpDownloader's abortPoll()
# (src/network/HttpDownloader.cpp) invokes the progress callback every 50ms --
# through the connect and the headers, not just the body -- and that callback
# both repaints and pumps input. That is why Back works mid-download. Nothing
# here changes it; the sites below are the ones with no such callback.
#
# WHY A SOURCE SCAN. Which frame is on the panel when a socket opens is an ORDER
# between statements in different functions, on classes needing Storage, WiFi, a
# GfxRenderer and a FreeRTOS render task. Nothing freestanding can hold it.
#
# WHY EVERY PATTERN IS ANCHORED. A cold critic defeated the first version of
# this suite five ways: a COMMENT containing the call satisfied a grep; a
# displayBuffer() nested in an `if` satisfied a presence check; an early
# `return` above it did too; `auto lock = RenderLock(...)` evaded a check that
# grepped the spelling; and a string literal counted as code. So: comments and
# string literals are stripped properly (block comments too), every pattern
# matches a STATEMENT at an exact indent, and reachability is checked rather
# than presence.
set -e
cd "$(dirname "$0")/../.."

checks=0
failed=0
ok()  { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); failed=$((failed + 1)); echo "FAIL paintfirst  $1"; }

need_file() {
  [ -f "$1" ] || { echo "FAIL paintfirst  $1 is missing -- every scan below would pass by finding nothing"; exit 1; }
}

# Comments AND string literals, gone. Block state is tracked across lines: a
# previous version dropped only lines STARTING with // or /*, so the interior of
# a /* ... */ block survived and commenting out the call kept every check green.
#
# CHARACTER literals are skipped too, and that is not tidiness. A `'"'` -- a
# double quote inside single quotes, which XkcdActivity::fetchOne() has in its
# JSON scraper -- opened string mode that never closed, and every line of that
# function after it came back BLANK. Three checks written against it failed with
# a message about the code, when the truth was that the scanner had stopped
# reading. A stripper that silently blanks the rest of a file is the worst kind
# of instrument: it reports on nothing and looks like it reported.
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
        else if (c == "'\''") {
          i++
          while (i <= n) {
            ch = substr(line, i, 1)
            if (ch == "\\") { i += 2; continue }
            i++
            if (ch == "'\''") break
          }
          out = out " "
        }
        else { out = out c; i++ }
      }
      print out
    }'
}

# Body of the function whose signature matches $2, in file $1, comments stripped.
body() {
  awk -v sig="$2" '
    !inside && $0 ~ sig { inside = 1 }
    inside { print }
    inside && /^\}/ { exit }
  ' "$1" | nocomment
}

# A statement at exactly $1 spaces of indent. Anything in a comment, a string or
# a deeper scope fails to match -- the indent IS the assertion.
stmt() { printf '^%s%s;[[:space:]]*$' "$1" "$2"; }

ahead_re() {
  text=$(cat)
  a=$(printf '%s\n' "$text" | grep -nE -- "$1" | head -1 | cut -d: -f1)
  b=$(printf '%s\n' "$text" | grep -nE -- "$2" | head -1 | cut -d: -f1)
  [ -n "$a" ] && [ -n "$b" ] && [ "$a" -lt "$b" ]
}

has_re() { printf '%s\n' "$2" | grep -qE -- "$1"; }

# REACHABILITY, not presence: no `return` of any kind may precede the statement.
# None of these render()s has an early return, so this is strict without being
# brittle; one that grows a return has to say so by failing.
reached() {
  line=$(printf '%s\n' "$2" | grep -nE -- "$1" | head -1 | cut -d: -f1)
  [ -n "$line" ] || return 1
  [ "$(printf '%s\n' "$2" | sed -n "1,$((line - 1))p" | grep -cE '(^|[^A-Za-z_])return([; ]|$)')" -eq 0 ]
}

# Every call matching $2 must have a call matching $1 within $3 CODE lines
# before it. Blank lines do not count (nocomment blanks in place), and the guard
# must be a bare statement -- `if (kNeverTrue) guard();` guards nothing.
preceded_within() {
  awk -v guard="$1" -v danger="$2" -v span="$3" '
    /^[[:space:]]*$/ { next }
    { code++ }
    $0 ~ guard { last = code }
    $0 ~ danger { if (last == 0 || code - last > span) bad = 1 }
    END { exit bad ? 1 : 0 }
  '
}

# Is the statement matching $1 executed while a RenderLock declared in an
# ENCLOSING scope is still alive? Brace depth, not proximity: an unrelated lock
# further up satisfies "is there a lock above" while the write itself runs
# unlocked. A lock protects a SCOPE. Declaration forms only, so a log message
# mentioning RenderLock does not count.
LOCKDECL='(RenderLock[[:space:]]+[A-Za-z_]|=[[:space:]]*RenderLock[(]|new[[:space:]]+RenderLock)'
locked_at() {
  printf '%s\n' "$2" | awk -v target="$1" -v lockdecl="$LOCKDECL" '
    {
      if ($0 ~ target && lockdepth > 0 && depth >= lockdepth) found = 1
      # Character by character, not a net brace count per line: a lock whose
      # scope OPENS AND CLOSES on one line (`if (x) { RenderLock d(...); }`)
      # nets to zero and stayed "alive" for the rest of the function, so a decoy
      # further up satisfied the check while the real write ran unlocked.
      lp = 0
      if (match($0, lockdecl)) lp = RSTART
      n = length($0)
      for (i = 1; i <= n; i++) {
        if (i == lp) lockdepth = (depth > 0 ? depth : 1)
        ch = substr($0, i, 1)
        if (ch == "{") depth++
        else if (ch == "}") { depth--; if (lockdepth > 0 && depth < lockdepth) lockdepth = 0 }
      }
    }
    END { exit found ? 0 : 1 }
  '
}

I2="  "
I4="    "

# ---------------------------------------------------------------------------
# CASE 1: Get Books. The cold start is the worst of them -- it is what a new
# reader hits first, and the screen left behind is the shelf.
# ---------------------------------------------------------------------------
OPDS=src/activities/browser/OpdsBookBrowserActivity.cpp
need_file "$OPDS"

# THE COST OF PAINTING IMMEDIATELY, and the reason this suite still has a lock
# check after the handshake work was dropped. requestUpdate(true) wakes the
# render task on the other core while this one blocks, so a render overlaps the
# fetch BY CONSTRUCTION -- wider than the deferred call it replaced, which could
# not run during a blocking call at all. fetchFeed() replaces `entries` and
# rebuilds `rowItems`, whose ListItems hold const char* INTO entries[i].title.
# Without the lock that is a use-after-free by design rather than by race.
ff=$(body "$OPDS" '^void OpdsBookBrowserActivity::fetchFeed[(]')
if locked_at 'entries = std::move' "$ff"; then ok
else bad "fetchFeed() replaces entries with no RenderLock alive in an enclosing scope; requestUpdate(true) means a render is concurrent by construction, and rowItems points into the strings being freed"; fi

if locked_at 'rebuildRowItems[(][)];' "$ff"; then ok
else bad "fetchFeed() rebuilds rowItems with no RenderLock alive in an enclosing scope"; fi

if locked_at 'swap[(]entries[)]' "$(body "$OPDS" '^void OpdsBookBrowserActivity::releaseEntries[(]')"; then ok
else bad "releaseEntries() frees entries with no RenderLock alive; three navigation paths reach it while a render woken by an earlier requestUpdate(true) may still be reading rowItems"; fi

# ...and every path in is painted, not just the ones an audit happened to list.
# Six reach fetchFeed(); the definition line starts at column 0 and is excluded.
if nocomment < "$OPDS" | preceded_within '^[[:space:]]+requestUpdate[(]true[)];' '^[[:space:]]+fetchFeed[(]' 3; then ok
else bad "a fetchFeed() in $OPDS is not preceded by requestUpdate(true); that path blocks with no repaint asked for -- see bounding-one-of-two-input-paths"; fi

# The render that has to publish the frame, at function-body level and not
# behind an early return. See the memory activity-render-contract.
if reached "$(stmt "$I2" 'renderer\.displayBuffer\(\)')" "$(body "$OPDS" '^void OpdsBookBrowserActivity::render[(]')"; then ok
else bad "OpdsBookBrowserActivity::render() has no REACHABLE displayBuffer() at function-body level; the busy frame would be built and never shown"; fi

# ---------------------------------------------------------------------------
# CASE 2: KOReader authentication, over TLS, reachable straight from onEnter().
# The AUTHENTICATING screen existed, was correct, and had never been seen.
# ---------------------------------------------------------------------------
KOA=src/activities/settings/KOReaderAuthActivity.cpp
need_file "$KOA"
auth=$(body "$KOA" '^void KOReaderAuthActivity::onWifiSelectionComplete[(]')

if has_re "$(stmt "$I4" 'state = AUTHENTICATING')" "$auth"; then ok
else bad "onWifiSelectionComplete() no longer sets AUTHENTICATING -- there is no busy state to paint"; fi

if printf '%s\n' "$auth" | ahead_re "$(stmt "$I2" 'requestUpdate\(true\)')" "$(stmt "$I2" 'performAuthentication\(\)')"; then ok
else bad "performAuthentication() is not preceded by an unconditional requestUpdate(true); it opens TLS on this task and the panel keeps the settings page the reader just left"; fi

sites=$(nocomment < "$KOA" | grep -cE '^ +performAuthentication\(\);' || true)
if [ "$sites" = "1" ]; then ok
else bad "performAuthentication() is called $sites times; each extra site is an unpainted handshake"; fi

if reached "$(stmt "$I2" 'renderer\.displayBuffer\(\)')" "$(body "$KOA" '^void KOReaderAuthActivity::render[(]')"; then ok
else bad "KOReaderAuthActivity::render() has no REACHABLE displayBuffer() at function-body level"; fi

# ---------------------------------------------------------------------------
# CASE 3: Instapaper. Not a mis-ordered busy state -- there was NONE, on both
# of its blocking pairAbandon() calls, one switch branch apart.
# ---------------------------------------------------------------------------
INS=src/apps_local/instapaper/InstapaperActivity.cpp
need_file "$INS"
paint=$(body "$INS" '^void InstapaperActivity::paintBusyNow[(]')

if has_re "$(stmt "$I4" 'phase_ = Phase::Busy')" "$paint"; then ok
else bad "paintBusyNow() sets no busy phase; there is nothing for the repaint to show"; fi

# IMMEDIATE. The default would defer to a loop tail the caller's socket call
# never lets it reach, which is the whole defect.
if has_re "$(stmt "$I2" 'requestUpdate\(true\)')" "$paint"; then ok
else bad "paintBusyNow() does not requestUpdate(true) at function-body level; a deferred update never notifies the render task before the caller blocks"; fi

# EVERY pairAbandon, not the one in front of me. The first fix landed on
# performDisconnect() and left the identical call in the Back handler alone.
if nocomment < "$INS" | preceded_within '^[[:space:]]+paintBusyNow[(]' 'sync_[.]pairAbandon[(]' 8; then ok
else bad "a sync_.pairAbandon() in $INS is not preceded by paintBusyNow(); that is a blocking TLS revoke with whatever screen the reader was on left frozen behind it"; fi

if reached "$(stmt "$I2" 'renderer\.displayBuffer\(\)')" "$(body "$INS" '^void InstapaperActivity::render[(]')"; then ok
else bad "InstapaperActivity::render() has no REACHABLE displayBuffer() at function-body level"; fi

# ---------------------------------------------------------------------------
# CASE 4: xkcd's catch-up. Not a mis-ordered busy state and not a missing one:
# the busy state is correct, it is painted before the run starts, and then the
# app spends MINUTES on a frame that has stopped being true.
#
# runUpdate() fetches every comic published since the last update, one at a
# time. Each fetchOne() is a metadata fetch, an artwork download and a PNG
# decode -- seconds -- and xkcd publishes three a week, so a reader a season
# behind sits through forty of them. The panel said "Asking xkcd.com what is
# new" for the whole run: true for the first HTTP request, a lie for the rest
# of the minutes. Painting once at the start does not survive a loop.
#
# THE FIRST VERSION OF THIS CASE ASSERTED AN INDENT AND A COLD CRITIC BEAT IT
# FIVE WAYS. An indent is not a loop: hoisting the caption and the wait into a
# bare `{ ... }` block above the `for` kept every check green while restoring
# one frame for the whole run. So the scope is now taken by BRACE DEPTH from the
# `for` that actually contains fetchOne(), a constant caption is refused by
# requiring the per-comic counter among its arguments, the Back read has to set
# the SAME flag the break tests (reading it into a dead variable passed before),
# `lastGot` has to be assigned exactly once (a copy on the failure path at a
# deeper indent passed before), and the header field is checked for the absence
# of `latest` rather than the presence of a spelling.
# ---------------------------------------------------------------------------
XKCD=src/apps_local/xkcd/XkcdActivity.cpp
need_file "$XKCD"
upd=$(body "$XKCD" '^void XkcdActivity::runUpdate[(]')
one=$(body "$XKCD" '^bool XkcdActivity::fetchOne[(]')

# The catch-up loop's own body: from the `for` enclosing fetchOne() to the brace
# that closes it, counted character by character. Nothing above the loop and
# nothing below it can satisfy a check written against this.
fetch_loop() {
  printf '%s\n' "$1" | awk '
    { L[NR] = $0 }
    /fetchOne\(/ && !fetchline { fetchline = NR }
    END {
      for (i = fetchline; i >= 1; i--) if (L[i] ~ /for[[:space:]]*\(/) { start = i; break }
      if (!start) exit 1
      depth = 0; started = 0
      for (i = start; i <= NR; i++) {
        print L[i]
        n = length(L[i])
        for (j = 1; j <= n; j++) {
          ch = substr(L[i], j, 1)
          if (ch == "{") { depth++; started = 1 }
          else if (ch == "}") { depth--; if (started && depth == 0) exit 0 }
        }
      }
    }'
}

loop=$(fetch_loop "$upd") || loop=""
if [ -n "$loop" ]; then ok
else bad "no loop enclosing fetchOne() found in runUpdate(); every scan below would pass by finding nothing"; fi

# The caption is rewritten INSIDE the loop...
if has_re 'snprintf\(noticeBody_,' "$loop"; then ok
else bad "runUpdate()'s catch-up loop does not rewrite noticeBody_; one frame stands for the whole run and the reader cannot tell a working device from a hung one"; fi

# ...and it is a DIFFERENT caption each time. A constant string repainted forty
# times is forty full-screen e-ink flashes carrying no new information, which is
# worse than not repainting at all.
cap=$(printf '%s\n' "$loop" | awk '/snprintf\(noticeBody_,/{c=1} c{printf "%s ", $0} c && /;[[:space:]]*$/{exit}')
if has_re 'attempted' "$cap" && has_re 'toGet' "$cap"; then ok
else bad "the caption does not take the per-comic counter and the total as arguments, so it cannot say which comic this is; a constant caption is a flash, not a message"; fi

# The repaint that publishes it, in the loop, and the wait rather than the
# deferred form: loop() is blocked, so the tail that consumes a deferred flag is
# never reached and nothing would repaint at all.
if has_re 'requestUpdateAndWait\(\);' "$loop"; then ok
else bad "runUpdate()'s catch-up loop has no requestUpdateAndWait(); a plain requestUpdate() cannot help here because loop() is blocked for the whole run"; fi

if printf '%s\n' "$loop" | ahead_re 'snprintf\(noticeBody_,' 'requestUpdateAndWait\(\);'; then ok
else bad "the loop repaints before it rewrites the caption; every frame would name the comic before the one being fetched"; fi

if printf '%s\n' "$loop" | preceded_within 'requestUpdateAndWait[(][)];' 'fetchOne[(]' 10; then ok
else bad "a fetchOne() in the catch-up loop is not preceded by requestUpdateAndWait(); that comic is fetched with a stale frame on the panel"; fi

# The loop pumps too, not only the download's callback. fetchOne()'s progress
# callback covers the transfer; the PNG decode and the index append after it are
# ours and have no callback to hang a pump on, so a Back pressed during those is
# seen here or not at all.
if has_re 'mappedInput\.update\(\);' "$loop"; then ok
else bad "the catch-up loop never pumps input itself; the PNG decode and index append have no progress callback, so a Back pressed during them is never seen"; fi

# BACK. The flag the loop breaks on is read out of the source, and the Back read
# has to set THAT flag -- reading the button into a variable nobody tests looks
# identical to a working cancel.
flag=$(printf '%s\n' "$loop" | sed -n 's/^[[:space:]]*if (\([A-Za-z_][A-Za-z0-9_]*\)) break;[[:space:]]*$/\1/p' | head -1)
if [ -n "$flag" ]; then ok
else bad "the catch-up loop has no bare 'if (<flag>) break;' -- there is no cancel for Back to reach"; fi

if [ -n "$flag" ] && has_re "wasReleased\(MappedInputManager::Button::Back\)\) $flag = true" "$loop"; then ok
else bad "the loop's Back read does not set '$flag', the flag it breaks on; a read into a dead variable stops nothing while the panel says Back stops it"; fi

if [ -n "$flag" ] && printf '%s\n' "$loop" | ahead_re "if \($flag\) break;" 'fetchOne\('; then ok
else bad "the loop does not break on cancel BEFORE fetchOne(); a Back that still waits out one whole comic reads as ignored"; fi

# THE PUMP THAT MAKES BACK REAL, and the finding that nearly shipped a lying
# caption. InputManager::update() commits a state change on a LATER call: the
# first update() to see a new level only stamps lastDebounceTime. Nothing else
# polls during this run (beginAsync is never called in the fork), so a pump that
# fires once per comic needs the button HELD across comics, and a normal tap
# between two pumps is invisible. HttpDownloader's abortPoll() calls the
# progress callback every 50ms through connect, headers and body -- so the
# artwork download is where the pump has to be.
if has_re 'downloadToFile\([^)]*,[^)]*,' "$one"; then ok
else bad "fetchOne()'s artwork download takes no progress callback, so nothing pumps input for the seconds it runs; Back then needs a multi-second hold and a tap is dropped entirely"; fi

if [ -n "$flag" ] && has_re "downloadToFile\(.*&$flag\)" "$one"; then ok
else bad "fetchOne()'s download is not handed &$flag, the flag the loop breaks on; Back could not abort a transfer already in progress"; fi

if [ -n "$flag" ] && has_re 'mappedInput\.update\(\);' "$one" && has_re "wasReleased\(MappedInputManager::Button::Back\)\) $flag = true" "$one"; then ok
else bad "fetchOne()'s progress callback does not pump input and set $flag; passing a callback that does not read the button buys nothing"; fi

# THE COST OF OFFERING A STOP. The header patched at the end of runUpdate() IS
# the archive's maxNum (XkcdCore reads it back from index.dat offset 12) and the
# next update fetches everything above it, so publishing `latest` after a run
# that stopped early claims comics the card does not hold and every later update
# answers UP TO DATE with the gap unreachable. Checked as the ABSENCE of
# `latest` on that line: the first version grepped for the fix's spelling and a
# cast or a ternary walked straight through it.
topline=$(printf '%s\n' "$upd" | grep -E 'uint32_t top[[:space:]]*=' | head -1)
if [ -n "$topline" ] && ! has_re 'latest' "$topline"; then ok
else bad "the header's highest-comic field is derived from 'latest' rather than from what arrived; after a stopped or failed run that claims comics the card does not have. Line: $topline"; fi

# ...and the value it publishes is recorded in exactly ONE place. A second
# assignment on the failure path restores the same corruption from a deeper
# scope, which an indent-anchored check cannot see.
sets=$(printf '%s\n' "$upd" | grep -cE '^[[:space:]]*lastGot = ' || true)
if [ "$sets" = "1" ]; then ok
else bad "lastGot is assigned $sets times in runUpdate(); it must be recorded once, immediately after a comic is counted, or the header can name one that never arrived"; fi

if printf '%s\n' "$loop" | ahead_re '\+\+fetched_;' 'lastGot = n;'; then ok
else bad "lastGot is not recorded immediately after the comic is counted; the header patch has nothing honest to publish"; fi

if reached "$(stmt "$I2" 'renderer\.displayBuffer\(\)')" "$(body "$XKCD" '^void XkcdActivity::render[(]')"; then ok
else bad "XkcdActivity::render() has no REACHABLE displayBuffer() at function-body level; every frame above would be built and never shown"; fi

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]
