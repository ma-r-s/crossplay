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

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]
