#!/bin/bash
# Agreements between the site's two halves that nothing else checks.
#
# emulator.js reports the frame's orientation by writing an attribute; styles.css
# decides what the panel does about it. Neither file imports the other and no
# build step links them, so a rename on either side leaves a page that renders
# perfectly and is simply wrong: before this attribute existed, a landscape
# frame was letterboxed into the portrait box at 0.6 scale, and Solitaire was
# small for as long as nobody thought to compare it to anything.
#
# That is the failure mode worth a test here -- not how it looks, which needs
# eyes, but whether the two files still refer to the same thing.
#
#   host-tests/site/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
JS="$ROOT/site/assets/emulator.js"
CSS="$ROOT/site/styles.css"

checks=0
failed=0
ok()  { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); failed=$((failed + 1)); echo "FAIL site  $1"; }

for f in "$JS" "$CSS"; do
  [ -f "$f" ] || { echo "FAIL site  missing $f"; exit 1; }
done

# Every orientation emulator.js can write must have somewhere to land. Read out
# of the JS rather than listed here, so adding a third one fails until the
# stylesheet knows about it.
values="$(grep -oE 'dataset\.orient = [^;]+' "$JS" | grep -oE '"[a-z]+"' | tr -d '"' | sort -u)"
if [ -z "$values" ]; then
  bad "emulator.js no longer reports an orientation at all"
else
  ok
  for v in $values; do
    # "portrait" is the default shape, so it needs no rule of its own; only a
    # value that has to change something must be styled.
    if [ "$v" = "portrait" ]; then ok; continue; fi
    if grep -q "data-orient=\"$v\"" "$CSS"; then
      ok
    else
      bad "emulator.js writes orient=\"$v\" and styles.css has no rule for it"
    fi
  done
fi

# The landscape rule has to actually turn the box over. A rule that sets only
# the width leaves aspect-ratio at 480/800 and the panel stays tall, which is
# the bug wearing the fix's clothes.
# Anchored at column 0 so it takes the base rule and not the full-screen
# override, which is a second block matching the same selector text. Unanchored,
# awk returned both and the check passed on whichever one still had the
# aspect-ratio -- including the wrong one.
land="$(awk '/^\.device-screen\[data-orient="landscape"\]/,/^}/' "$CSS")"
if [ -z "$land" ]; then
  bad "no .device-screen[data-orient=\"landscape\"] block in styles.css"
else
  ok
  printf '%s' "$land" | grep -qE 'aspect-ratio: *800 */ *480' \
    && ok || bad "the landscape rule does not set aspect-ratio: 800 / 480"
  printf '%s' "$land" | grep -q 'width:' \
    && ok || bad "the landscape rule does not restate the width, so the panel keeps the portrait cap"
fi

# Both size inputs must survive as custom properties. The two-up rule overrides
# only these; if either is inlined back into the width expression, a second
# device silently stops shrinking.
for prop in --panel-long --panel-room; do
  n="$(grep -c -- "$prop:" "$CSS")"
  if [ "$n" -ge 2 ]; then
    ok
  else
    bad "$prop is set $n time(s); the one-up and two-up cases should each set it"
  fi
done

# -- the install button's ids, spelled in two files that never see each other --
#
# assets/install.js finds every control by getElementById. A renamed id in
# index.html does not break the page, does not log anything, and does not fail a
# build: the panel renders exactly as before and the button quietly does
# nothing, because init() returned early on a null it never checked. That is the
# same class of bug as the orientation attribute above, on the one control here
# that writes to somebody's hardware.
INSTALL="$ROOT/site/assets/install.js"
HTML="$ROOT/site/index.html"
for f in "$INSTALL" "$HTML"; do
  [ -f "$f" ] || { echo "FAIL site  missing $f"; exit 1; }
done

# Quote-agnostic: the repository formatter rewrites JS string quotes, and a
# check that only knows one of them fails on a reformat rather than on a bug.
ids="$(sed -nE 's/.*\$\(["'"'"']([A-Za-z]+)["'"'"']\).*/\1/p' "$INSTALL" | sort -u)"
if [ -z "$ids" ]; then
  bad "install.js looks up no element ids at all"
else
  ok
  for id in $ids; do
    if grep -q "id=\"$id\"" "$HTML"; then
      ok
    else
      bad "install.js asks for #$id and index.html has no such element"
    fi
  done
fi

# -- the device ids, spelled in four files ------------------------------------
#
# The radio value is what index.html sends, install.js keys its restart text
# off, api/firmware.js turns into a filename, and serve.py mirrors for local
# work. Any one of them out of step is a button that downloads a 404 for
# whichever device nobody tested.
API="$ROOT/site/api/firmware.js"
SERVE="$ROOT/site/serve.py"
for f in "$API" "$SERVE"; do
  [ -f "$f" ] || { echo "FAIL site  missing $f"; exit 1; }
done

devices="$(grep -oE 'name="install-device" value="[a-z0-9]+"' "$HTML" | sed -E 's/.*value="//; s/"//' | sort -u)"
if [ -z "$devices" ]; then
  bad "index.html offers no device to install onto"
else
  ok
  for d in $devices; do
    grep -qE "^ *$d: \{" "$INSTALL" && ok || bad "index.html offers device '$d' and install.js has no entry for it"
    grep -qE "^ *$d: [\"']" "$API" && ok || bad "index.html offers device '$d' and api/firmware.js cannot name its image"
    grep -qE "\"$d\": \"" "$SERVE" && ok || bad "index.html offers device '$d' and serve.py cannot name its image, so it is untestable locally"
  done
fi

# The two halves of the endpoint have to agree on the filename, or local work
# passes against a name production never asks for.
api_names="$(grep -oE 'crossplay-\{tag\}-[a-z0-9]+-full\.bin' "$API" | sort -u)"
serve_names="$(grep -oE 'crossplay-\{tag\}-[a-z0-9]+-full\.bin' "$SERVE" | sort -u)"
if [ -n "$api_names" ] && [ "$api_names" = "$serve_names" ]; then
  ok
else
  bad "api/firmware.js and serve.py disagree about the image filename"
fi

# -- every game and app on the shelf is showcased on the page ------------------
#
# The one gap here is not a mismatch between two files, it is an ABSENCE: a game
# that shipped and was never written up looks exactly like a game that does not
# exist. Two of them had been on the shelf for eleven releases. The list comes
# out of Shelf.cpp, so a new app fails this until somebody writes it up.
# The status is checked, not just the output, and that distinction is the whole
# reason this block is four lines longer than it looks like it should be. This
# check reports gaps by PRINTING them, so "printed nothing" is its success
# signal -- and a script that crashes also prints nothing to stdout. Wired the
# obvious way, a shelf_coverage.py with a syntax error made the suite report
# "29 checks, 0 failed" and exit 0, which is the worst possible outcome: a
# guard that has stopped guarding while still saying it is fine. Verified by
# replacing the script with `raise SystemExit("boom")` and watching it pass.
if shelf_missing="$(python3 "$HERE/shelf_coverage.py" "$ROOT" 2>&1)"; then
  if [ -z "$shelf_missing" ]; then
    ok
  else
    while IFS= read -r line; do bad "$line"; done <<< "$shelf_missing"
  fi
else
  bad "shelf_coverage.py could not run, so the shelf went unchecked:"
  while IFS= read -r line; do echo "      $line"; done <<< "$shelf_missing"
fi

# -- the page's own structure --------------------------------------------------
#
# Everything above compares two files. These two faults live inside index.html
# alone, and both reached the live page: a card missing its </article>, which
# swallowed the next card into its own grid cell, and shots stored at twice the
# size the page declares. Neither breaks a build, a render or a link, and the
# coverage check above passes straight through both. Status checked as well as
# output, for the reason spelled out in the block before this one.
if page_bad="$(python3 "$HERE/page_structure.py" "$ROOT" 2>&1)"; then
  if [ -z "$page_bad" ]; then
    ok
  else
    while IFS= read -r line; do bad "$line"; done <<< "$page_bad"
  fi
else
  bad "page_structure.py could not run, so index.html went unchecked:"
  while IFS= read -r line; do echo "      $line"; done <<< "$page_bad"
fi

# -- the report box and the inbox ---------------------------------------------
#
# api/report.js is the one place a stranger's input meets the board's write
# key. It runs here under node with the board stubbed, and every refusal is
# asserted, including the one thing it must never do (store a honeypot hit).
# Status checked as well as output, same reason as the two blocks above.
if report_out="$(node "$HERE/report_fn.js" "$ROOT" 2>&1)"; then
  ok
  n_fail="$(printf '%s\n' "$report_out" | grep -c '^  FAIL' || true)"
  [ "$n_fail" -eq 0 ] && ok || { while IFS= read -r line; do bad "report_fn: $line"; done < <(printf '%s\n' "$report_out" | grep '^  FAIL'); }
else
  bad "report_fn.js could not run, so api/report.js went unchecked:"
  while IFS= read -r line; do echo "      $line"; done <<< "$report_out"
fi

# Each of the two new pages looks its controls up by id inside its own file.
# Same failure as install.js: a renamed id renders fine and does nothing.
for page in report inbox; do
  P="$ROOT/site/$page/index.html"
  [ -f "$P" ] || { bad "site/$page/index.html is missing"; continue; }
  page_ids="$(sed -nE 's/.*\$\(["'"'"']([A-Za-z-]+)["'"'"']\).*/\1/p' "$P" | sort -u)"
  [ -n "$page_ids" ] || { bad "site/$page/index.html looks up no element ids"; continue; }
  for id in $page_ids; do
    case "$id" in inbox-answer|inbox-send|inbox-default|inbox-how|inbox-later) continue;; esac  # rendered by script
    grep -q "id=\"$id\"" "$P" && ok || bad "site/$page/index.html asks for #$id and has no such element"
  done
done

# -- the study installer's ids, spelled in two files that never see each other -
#
# Same failure as install.js above, on the page that was actually caught by it:
# study.js finds every control with a $("id") helper. On 2026-09-03 the dead
# mode tablist was removed from study/index.html, and the two $("modeInstall")
# calls in study.js would have thrown inside goTo() on the first step change --
# taking the whole wizard down, from an edit whose point was to remove
# something inert. install.js, report and inbox each had this check; study, the
# biggest page here, did not.
SP="$ROOT/site/study/index.html"
SJ="$ROOT/site/study/study.js"
for f in "$SP" "$SJ"; do
  [ -f "$f" ] || { bad "site/study is missing $(basename "$f")"; }
done
study_ids="$(sed -nE 's/.*\$\(["'"'"']([A-Za-z][A-Za-z0-9_-]*)["'"'"']\).*/\1/p' "$SJ" | sort -u)"
if [ -z "$study_ids" ]; then
  bad "study.js looks up no element ids at all"
else
  ok
  for id in $study_ids; do
    grep -q "id=\"$id\"" "$SP" && ok || bad "study.js asks for #$id and study/index.html has no such element"
  done
fi

# -- composite ARIA roles with nothing to choose between -----------------------
#
# A tablist holding one tab, a radiogroup holding one radio: well-formed markup
# that renders perfectly and lies to a screen reader about there being a choice.
# Study shipped one for a week, styled as the page's primary button and wired to
# a handler that re-rendered the step you were already on. Status checked as
# well as output, for the reason spelled out two blocks above.
if aria_bad="$(python3 "$HERE/aria_roles.py" "$ROOT" 2>&1)"; then
  if [ -z "$aria_bad" ]; then
    ok
  else
    while IFS= read -r line; do bad "$line"; done <<< "$aria_bad"
  fi
else
  bad "aria_roles.py could not run, so the pages' roles went unchecked:"
  while IFS= read -r line; do echo "      $line"; done <<< "$aria_bad"
fi

# The inbox page and serve.py must agree on the config endpoint's shape, or
# local work passes against a key production never sends.
grep -q 'anonKey' "$ROOT/site/api/board-config.js" && grep -q '"anonKey"' "$SERVE" && ok || bad "board-config: api and serve.py do not agree on anonKey"

# The inbox gate: a passphrase, checked by api/inbox.js against a hash. Run
# install.js reads the board's address from api/board-config.js by two field
# names; a rename on either side leaves an installer that reports nothing and
# says nothing about it.
for field in url anonKey; do
  grep -q "cfg\.$field" "$ROOT/site/assets/install.js" && grep -q "$field" "$ROOT/site/api/board-config.js" \
    && ok || bad "install.js and board-config.js disagree on the field '$field'"
done

# under node with the board stubbed; the wrong passphrase must read nothing.
if inbox_out="$(node "$HERE/inbox_fn.js" "$ROOT" 2>&1)"; then
  ok
  n_fail="$(printf '%s\n' "$inbox_out" | grep -c '^  FAIL' || true)"
  [ "$n_fail" -eq 0 ] && ok || { while IFS= read -r line; do bad "inbox_fn: $line"; done < <(printf '%s\n' "$inbox_out" | grep '^  FAIL'); }
else
  bad "inbox_fn.js could not run, so api/inbox.js went unchecked:"
  while IFS= read -r line; do echo "      $line"; done <<< "$inbox_out"
fi
# and the page talks only to that gate, never to the board directly
grep -q '"/api/inbox"' "$ROOT/site/inbox/index.html" && ok || bad "the inbox page does not call /api/inbox"
grep -q 'supabase.co\|/rest/v1/\|/auth/v1/' "$ROOT/site/inbox/index.html" && bad "the inbox page still talks to the board directly" || ok

# -- the inbox fixture, spelled in three files that never see each other -------
#
# serve.py answers /api/inbox from inbox/fixture.json so the page can be laid
# out without a passphrase. Three things can drift and none of them fails
# loudly: an operation api/inbox.js handles that serve.py does not (the page
# gets a 400 locally and works in production, or the reverse), a numbers key
# the page starts reading that the fixture lacks (the section renders
# "nothing yet" and looks like an empty board), and a fixture that stopped
# being JSON (every local load shows a 500 dressed as a board error).
INBOX_HTML="$ROOT/site/inbox/index.html"
FIXTURE="$ROOT/site/inbox/fixture.json"
ops="$(grep -oE 'body\.op === "[a-z]+"' "$ROOT/site/api/inbox.js" | grep -oE '"[a-z]+"' | tr -d '"' | sort -u)"
if [ -z "$ops" ]; then
  bad "api/inbox.js handles no operations at all"
else
  ok
  for op in $ops; do
    grep -qE "op == \"$op\"" "$SERVE" && ok || bad "api/inbox.js handles op '$op' and serve.py's fixture does not"
  done
fi
if [ -f "$FIXTURE" ]; then
  ok
  num_keys="$(grep -oE '\bn\.[A-Za-z]+' "$INBOX_HTML" | sed 's/^n\.//' | sort -u)"
  if fixture_bad="$(python3 - "$FIXTURE" $num_keys <<'PY' 2>&1
import json, sys
fx = json.load(open(sys.argv[1]))
for part in ("list", "numbers"):
    if part not in fx:
        print(f"fixture.json has no '{part}' object")
for k in ("inbox", "cards"):
    if not fx.get("list", {}).get(k):
        print(f"fixture.json list.{k} is empty")
for key in sys.argv[2:]:
    if key not in fx.get("numbers", {}):
        print(f"inbox/index.html reads n.{key} and fixture.json numbers has no '{key}'")
PY
  )"; then
    if [ -z "$fixture_bad" ]; then ok; else while IFS= read -r line; do bad "$line"; done <<< "$fixture_bad"; fi
  else
    bad "fixture.json could not be checked:"
    while IFS= read -r line; do echo "      $line"; done <<< "$fixture_bad"
  fi
else
  bad "site/inbox/fixture.json is missing, so the inbox cannot be looked at without a passphrase"
fi

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]
