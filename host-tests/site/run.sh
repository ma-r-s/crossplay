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

# The inbox page looks its controls up by id inside its own file. Same failure
# as install.js: a renamed id renders fine and does nothing.
P="$ROOT/site/inbox/index.html"
if [ -f "$P" ]; then
  page_ids="$(sed -nE 's/.*\$\(["'"'"']([A-Za-z-]+)["'"'"']\).*/\1/p' "$P" | sort -u)"
  [ -n "$page_ids" ] || bad "site/inbox/index.html looks up no element ids"
  for id in $page_ids; do
    case "$id" in inbox-answer|inbox-send|inbox-default|inbox-how|inbox-later) continue;; esac  # rendered by script
    grep -q "id=\"$id\"" "$P" && ok || bad "site/inbox/index.html asks for #$id and has no such element"
  done
else
  bad "site/inbox/index.html is missing"
fi

# -- the report form, one script drawn into two pages --------------------------
#
# assets/report.js draws the form into whichever page carries a mount point and
# then finds every control by id. Those ids live in two places: the markup the
# script renders itself, and index.html for the dialog and the button that
# opens it. An id in neither is a control that renders fine and does nothing --
# and on the front page, a button in the corner that opens nothing.
REPORTJS="$ROOT/site/assets/report.js"
REPORTCSS="$ROOT/site/assets/report.css"
REPORTPAGE="$ROOT/site/report/index.html"
for f in "$REPORTJS" "$REPORTCSS" "$REPORTPAGE"; do
  [ -f "$f" ] || { echo "FAIL site  missing $f"; exit 1; }
done
report_ids="$(sed -nE 's/.*\$\(["'"'"']([A-Za-z-]+)["'"'"']\).*/\1/p' "$REPORTJS" | sort -u)"
if [ -z "$report_ids" ]; then
  bad "report.js looks up no element ids at all"
else
  ok
  for id in $report_ids; do
    if grep -q "id=\"$id\"" "$REPORTJS" || grep -q "id=\"$id\"" "$HTML"; then
      ok
    else
      bad "report.js asks for #$id and neither its own markup nor index.html has such an element"
    fi
  done
fi
# The dialog and its close button are index.html's to provide, and the script
# must be asking for them under those names, or the front page has a dead
# button. The opener is found by attribute, not id, so that the prose link and
# the corner button can both open it; the attribute is what has to agree.
for id in report-dialog report-close; do
  grep -q "id=\"$id\"" "$HTML" && ok || bad "index.html has no #$id"
  printf '%s\n' "$report_ids" | grep -qx "$id" && ok || bad "report.js never looks up #$id, so index.html's is decoration"
done
grep -q '<dialog[^>]*id="report-dialog"' "$HTML" && ok || bad "#report-dialog in index.html is not a <dialog>"
grep -q 'id="report-open"' "$HTML" && ok || bad "index.html has no #report-open button"
grep -q 'id="report-open"[^>]*data-report-open' "$HTML" && ok || bad "#report-open does not carry data-report-open, so report.js will not wire it"
grep -q '\[data-report-open\]' "$REPORTJS" && ok || bad "report.js never looks for [data-report-open], so nothing opens the dialog"
# Both pages mount the form and load the script and its stylesheet.
for p in "$HTML" "$REPORTPAGE"; do
  rel="${p#"$ROOT"/}"
  grep -q 'data-report-mount' "$p" && ok || bad "$rel has nowhere to mount the report form"
  grep -qE 'src="/?assets/report\.js"' "$p" && ok || bad "$rel does not load assets/report.js"
  grep -qE 'href="/?assets/report\.css"' "$p" && ok || bad "$rel does not load assets/report.css"
done
# The standalone page has no script of its own: one implementation, not two.
grep -q '\$(' "$REPORTPAGE" && bad "report/index.html looks up ids itself; the form lives in assets/report.js" || ok
grep -q '<form' "$REPORTPAGE" && bad "report/index.html carries its own <form>; the form lives in assets/report.js" || ok
# The devices the form offers are exactly the ones the function accepts, and
# neither side offers "not sure": a report names a board or is refused.
form_devices="$(grep -oE 'name="device" value="[a-z0-9]+"' "$REPORTJS" | sed -E 's/.*value="//; s/"//' | sort -u | tr '\n' ' ')"
api_devices="$(grep -oE 'DEVICES = \[[^]]*\]' "$ROOT/site/api/report.js" | grep -oE '"[a-z0-9]+"' | tr -d '"' | sort -u | tr '\n' ' ')"
[ -n "$form_devices" ] && ok || bad "report.js offers no device to pick"
[ "$form_devices" = "$api_devices" ] && ok || bad "report.js offers devices [$form_devices] and api/report.js accepts [$api_devices]"
printf '%s' "$api_devices" | grep -qw unknown && bad "api/report.js accepts 'unknown' as a device again; a report names a board or is refused" || ok
grep -qE 'type="radio" name="device"' "$REPORTJS" && bad "device is a radio again; it is two checkboxes, both allowed" || ok

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

# A failed install is a card only when the failure is the page's, not the
# person's (cards 153 and 157 were a closed port picker and a silent cable).
# The decision function is lifted from install.js and run under node.
if install_out="$(node "$HERE/install_fn.js" "$ROOT" 2>&1)"; then
  ok
  n_fail="$(printf '%s\n' "$install_out" | grep -c '^  FAIL' || true)"
  [ "$n_fail" -eq 0 ] && ok || { while IFS= read -r line; do bad "install_fn: $line"; done < <(printf '%s\n' "$install_out" | grep '^  FAIL'); }
else
  bad "install_fn.js could not run, so install.js's error levels went unchecked:"
  while IFS= read -r line; do echo "      $line"; done <<< "$install_out"
fi

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

# api/trivia.js takes question reports off a device. Two of its properties are
# invisible in the code and only a test can hold them: the device id is used to
# build the row key and is then DROPPED, so it appears in no column; and the key
# is per-question, so two reports from one reader cannot be joined into a
# reading history. Both are asserted against what the stub was actually asked to
# store, never against what the source appears to do.
if trivia_out="$(node "$HERE/trivia_fn.js" "$ROOT" 2>&1)"; then
  ok
  n_fail="$(printf '%s\n' "$trivia_out" | grep -c '^  FAIL' || true)"
  [ "$n_fail" -eq 0 ] && ok || { while IFS= read -r line; do bad "trivia_fn: $line"; done < <(printf '%s\n' "$trivia_out" | grep '^  FAIL'); }
else
  bad "trivia_fn.js could not run, so api/trivia.js went unchecked:"
  while IFS= read -r line; do echo "      $line"; done <<< "$trivia_out"
fi
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
  # the list answer too: what load() reads off `res.` must be in fixture.list,
  # or the local page silently drops a line the real one shows
  list_keys="$(grep -oE '\bres\.[A-Za-z]+' "$INBOX_HTML" | sed 's/^res\.//' | sort -u)"
  if list_bad="$(python3 - "$FIXTURE" $list_keys <<'PY' 2>&1
import json, sys
fx = json.load(open(sys.argv[1]))
missing = [k for k in sys.argv[2:] if k not in fx.get("list", {})]
if missing:
    print("fixture.json list lacks " + ", ".join(missing))
    sys.exit(1)
PY
  )"; then ok; else bad "inbox fixture: $list_bad"; fi
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

# -- the top bar's narrow-screen menu -----------------------------------------
#
# Below 720px five labels do not share a line. The stylesheet used to answer
# that by hiding THE SHELF and PLAY NEARBY -- the two links that lead to what
# the site is for -- and keeping ANKI DECKS, which then wrapped to two 49px
# lines inside a 50px bar at 320, 390 and 414; /study/ did the same with
# INSTALL THE FIRMWARE. The links live in a panel now, opened by a button that
# assets/topnav.js wires and styles.css positions.
#
# Same failure shape as the orientation attribute at the top of this file: the
# script writes classes the stylesheet has to know, and neither file imports
# the other. A rename on either side is a bar with a button that opens nothing
# on the width where the button is the only navigation there is.
TOPNAV="$ROOT/site/assets/topnav.js"
SP_HTML="$ROOT/site/study/index.html"
[ -f "$TOPNAV" ] || { echo "FAIL site  missing $TOPNAV"; exit 1; }

# Selectors only, comments and :not() contents stripped -- see css_selectors.py.
# Grepping the whole stylesheet for a class name used to be answered YES by the
# sentence " * .has-menu is added by assets/topnav.js" in a comment, and a
# reviewer restored the original bug in full with this check still green.
if ! css_sels="$(python3 "$HERE/css_selectors.py" "$CSS" 2>&1)"; then
  bad "css_selectors.py could not run, so the menu's classes went unchecked:"
  while IFS= read -r line; do echo "      $line"; done <<< "$css_sels"
  css_sels=""
fi
[ -n "$css_sels" ] && ok || bad "styles.css yielded no selectors at all"

nav_classes="$(grep -oE 'classList\.(add|toggle)\("[a-z-]+"' "$TOPNAV" | grep -oE '"[a-z-]+"' | tr -d '"' | sort -u)"
if [ -z "$nav_classes" ]; then
  bad "topnav.js writes no classes at all, so the bar can never open"
else
  ok
  for c in $nav_classes; do
    if printf '%s\n' "$css_sels" | grep -qE "\.$c([^A-Za-z0-9_-]|\$)"; then
      ok
    else
      bad "topnav.js writes .$c and no styles.css SELECTOR uses it (a comment does not count)"
    fi
  done
fi
# ...and the control it looks for must be one the stylesheet can show and both
# pages actually carry.
for sel in topnav-toggle topnav; do
  grep -q "\.$sel" "$TOPNAV" && ok || bad "topnav.js never looks for .$sel"
  printf '%s\n' "$css_sels" | grep -qE "\.$sel([^A-Za-z0-9_-]|\$)" \
    && ok || bad "styles.css has no .$sel selector"
done
for p in "$HTML" "$SP_HTML"; do
  rel="${p#"$ROOT"/}"
  grep -q 'class="topnav-toggle"' "$p" && ok || bad "$rel has no .topnav-toggle button, so its narrow bar has no navigation"
  grep -qE 'src="/?assets/topnav\.js"' "$p" && ok || bad "$rel does not load assets/topnav.js, so its menu button opens nothing"
  ctl="$(grep -oE 'aria-controls="[A-Za-z0-9_-]+"' "$p" | head -1 | sed -E 's/.*="//; s/"//')"
  if [ -z "$ctl" ]; then
    bad "$rel's menu button has no aria-controls"
  else
    ok
    grep -q "id=\"$ctl\"" "$p" && ok || bad "$rel's menu button controls #$ctl and no such element exists"
  fi
done

# nowrap has to be SCOPED to the bar the script has taken over. Unscoped it made
# the no-script bar worse rather than leaving it alone: links still inline and no
# longer allowed to wrap ran to x=339 past a 320px viewport, and .topbar is
# position:fixed, so there was no scrollbar to reach the last one with. Wrapping
# is ugly and reachable; overflowing a fixed bar is neither.
nowrap_sels="$(python3 - "$CSS" <<'PYEOF'
import pathlib, re, sys
css = re.sub(r"/\*.*?\*/", "", pathlib.Path(sys.argv[1]).read_text(), flags=re.S)
for m in re.finditer(r"([^{}]+)\{([^{}]*)\}", css):
    sel, decls = m.group(1).strip(), m.group(2)
    # A DESCENDANT of .topnav -- the links. .topnav-toggle is a different class
    # and its nowrap is unconditional on purpose: the button is one short word
    # and it only exists at all where the menu does.
    if re.search(r"white-space\s*:\s*nowrap", decls) and re.search(r"\.topnav(?![\w-])\s+\S", sel):
        print(" ".join(sel.split()))
PYEOF
)"
if [ -z "$nowrap_sels" ]; then
  bad "nothing stops a .topnav label wrapping inside the bar"
else
  ok
  while IFS= read -r sel; do
    case "$sel" in
      *has-menu*) ok ;;
      *) bad "\`$sel\` sets nowrap without .has-menu, so the no-script bar overflows a fixed bar instead of wrapping" ;;
    esac
  done <<< "$nowrap_sels"
fi
# The old rule hid the two product links outright. It may only survive as the
# no-script fallback, which is what :not(.has-menu) scopes it to.
if grep -q 'a\[href="#shelf"\]' "$CSS"; then
  grep -q 'topbar:not(\.has-menu) .topnav a\[href="#shelf"\]' "$CSS" \
    && ok || bad "styles.css hides the #shelf link without scoping it to :not(.has-menu), so it is gone from the menu too"
else
  ok
fi
# Escape hides the panel with display:none, which drops focus on BODY and makes
# the next Tab restart at the top of the document, so the toggle has to be given
# it back. Only the call can be checked here; that it lands on the button, and
# that clicking the WORDMARK (an anchor in the bar but outside the panel) closes
# the panel too, were measured in a browser at 390x844 and cannot be asserted
# from a file.
grep -q 'btn\.focus()' "$TOPNAV" \
  && ok || bad "topnav.js never returns focus to the toggle, so Escape drops the caret on BODY"

# -- the study page's own account of where Pyodide comes from ------------------
#
# study/worker.js loads the runtime from the first base that answers, and the
# page's footer tells the reader where that is. The footer said "served from
# this site" while the worker had asked jsDelivr first since 2026-08-25 --
# nothing renders wrong, the page is simply not true. Read the host out of the
# worker so the claim cannot drift from the code again.
WORKER="$ROOT/site/study/worker.js"
[ -f "$WORKER" ] || { echo "FAIL site  missing $WORKER"; exit 1; }
first_base="$(awk '/PYODIDE_BASES = \[/,/\]/' "$WORKER" | grep -oE '"[^"]+"' | head -1 | tr -d '"')"
if [ -z "$first_base" ]; then
  bad "study/worker.js lists no Pyodide base at all"
else
  ok
  case "$first_base" in
    http*)
      host="$(printf '%s' "$first_base" | sed -E 's|https?://||; s|/.*||')"
      label="$(printf '%s' "$host" | awk -F. '{print $(NF-1)}')"
      grep -qi "$label" "$SP_HTML" \
        && ok || bad "worker.js loads Pyodide from $host first and study/index.html never says so"
      grep -qi "served from this site" "$SP_HTML" \
        && bad "study/index.html still says Pyodide is served from this site, and worker.js asks $host first" || ok
      ;;
    *)
      grep -qi "served from this site" "$SP_HTML" \
        && ok || bad "worker.js loads Pyodide from this site first and study/index.html does not say so"
      ;;
  esac
fi

# -- one release request per page ---------------------------------------------
#
# The Install button names the version and the report form uses it as the
# version field's placeholder. Both used to ask GitHub themselves, which put two
# identical requests on every front-page load against an unauthenticated limit
# of 60 an hour per IP -- the very limit install.js's own comment gives as the
# reason it asks from the visitor's browser at all. assets/release.js is the one
# asker now; a caller that goes back to fetching for itself is the regression.
RELEASE="$ROOT/site/assets/release.js"
[ -f "$RELEASE" ] || { echo "FAIL site  missing $RELEASE"; exit 1; }
grep -q 'releases/latest' "$RELEASE" && ok || bad "release.js does not ask for the latest release"
helper="$(grep -oE 'window\.[A-Za-z]+ = function' "$RELEASE" | head -1 | sed -E 's/window\.//; s/ = function//')"
if [ -z "$helper" ]; then
  bad "release.js publishes no function, so nothing can call it"
else
  ok
  for f in "$REPORTJS" "$INSTALL"; do
    rel="${f#"$ROOT"/}"
    grep -q "$helper" "$f" && ok || bad "$rel does not call window.$helper, so it is not sharing the request"
    grep -q 'releases/latest' "$f" \
      && bad "$rel asks GitHub for the release itself again; one page load must not spend two of the sixty" || ok
  done
fi
# Every page carrying either caller has to load the helper, and load it first.
for p in "$HTML" "$REPORTPAGE"; do
  rel="${p#"$ROOT"/}"
  if grep -qE 'src="/?assets/(report|install)\.js"' "$p"; then
    ok
    grep -qE 'src="/?assets/release\.js"' "$p" \
      && ok || bad "$rel loads a script that needs window.$helper and never loads assets/release.js"
    rl="$(grep -nE 'src="/?assets/release\.js"' "$p" | head -1 | cut -d: -f1)"
    cl="$(grep -nE 'src="/?assets/(report|install)\.js"' "$p" | head -1 | cut -d: -f1)"
    if [ -n "$rl" ] && [ -n "$cl" ] && [ "$rl" -lt "$cl" ]; then
      ok
    else
      bad "$rel loads assets/release.js after the script that uses it"
    fi
  else
    ok
  fi
done

# -- the report form's version placeholder ------------------------------------
#
# It read 1.12.9 while the site was shipping 1.12.23: a number typed into the
# file, right the day it was written and eleven releases stale by the time
# anyone looked. It is asked for now. Nothing here can check the answer -- that
# needs the network -- but it can check that no literal has been typed back in.
#
# Every placeholder in the file is scanned, not the one attribute after an id.
# The first version of this check grepped `id="report-version"[^>]*placeholder=`
# and a reviewer put the attribute in FRONT of the id, restored 1.12.9, and got
# zero failures; renaming the id emptied the capture and passed just as quietly.
ph_bad="$(python3 - "$REPORTJS" <<'PYEOF'
import pathlib, re, sys
src = pathlib.Path(sys.argv[1]).read_text()
tags = re.findall(r"<input\b[^>]*>", src)
if not any(re.search(r'name="version"', t) for t in tags):
    print("report.js draws no version field at all")
for value in re.findall(r'placeholder="([^"]*)"', src):
    if re.fullmatch(r"v?\d+(\.\d+)+", value.strip()):
        print(f'report.js hardcodes the version "{value}" as a placeholder again; it is asked for, not typed')
PYEOF
)"
if [ -z "$ph_bad" ]; then
  ok
else
  while IFS= read -r line; do bad "$line"; done <<< "$ph_bad"
fi

# -- the study wizard's steps stay in normal flow ------------------------------
#
# Properties, not spellings, and the class names come out of the markup. The
# first version of this check forbade three exact declarations and a reviewer
# reintroduced the identical breakage through three others with nothing
# reported; renaming .wiz-step emptied its awk range and passed all three at
# once. study_layout.py says at length what it does and does not see -- the one
# thing it cannot see is whether the button can be clicked, which is a browser
# question that was answered in a browser. Status checked as well as output.
if layout_bad="$(python3 "$HERE/study_layout.py" "$ROOT" 2>&1)"; then
  if [ -z "$layout_bad" ]; then
    ok
  else
    while IFS= read -r line; do bad "$line"; done <<< "$layout_bad"
  fi
else
  bad "study_layout.py could not run, so the wizard's layout went unchecked:"
  while IFS= read -r line; do echo "      $line"; done <<< "$layout_bad"
fi
# The emulator's cap is the only thing sizing the panel now that the box does
# not crop it, so it has to be a token that can be found and reasoned about
# rather than a number buried in a calc.
SCSS="$ROOT/site/study/study.css"
grep -q -- '--preview-chrome:' "$SCSS" \
  && ok || bad "study.css has no --preview-chrome token; the preview's cap is a bare number again"
awk '/^\.study-preview-frame \{/,/^}/' "$SCSS" | grep -q -- 'var(--preview-chrome)' \
  && ok || bad ".study-preview-frame does not use --preview-chrome, so the token and the cap have drifted"
# A step change has to arrive at the top of itself. The page is the scroll
# container now; the step box was, and its scrollTop reset went dead with it.
# That the reset lands on a step CHANGE and not a redraw was measured in a
# browser; only its presence can be checked here.
grep -q 'window.scrollTo' "$ROOT/site/study/study.js" \
  && ok || bad "study.js never resets the page scroll, so a step arrives at the previous step's offset"

# A deck dropped on the zone must run the open-and-convert pipeline ONCE. Two
# drop listeners reach takeFile -- one on the dropzone, one on document for a
# deck dropped anywhere on the page -- and a drop on the zone bubbles to both,
# so the zone's handler has to stop the bubble or the pipeline fires twice.
# That it fires exactly once was counted in a browser (twice without the guard,
# once with it); only the guard's presence can be checked here. Scoped to the
# zone's own drop handler so a stray stopPropagation elsewhere cannot satisfy it.
awk '/dropzone\.addEventListener\("drop"/,/\}\);/' "$ROOT/site/study/study.js" \
  | grep -q 'stopPropagation' \
  && ok || bad "study.js's dropzone drop handler does not stopPropagation, so a dropped deck bubbles to the document handler and runs the pipeline twice"

# -- the emulator, which is no longer in the repository -----------------------
#
# site/emulator/ is 3.7MB of generated wasm that used to be committed on every
# firmware merge (111 revisions, ~357MB of history, ~20MB a day). It is now
# published to a GitHub release by tools_local/site/publish_emulator.py, pointed
# at by site/emulator-manifest.json, and pulled back in during Vercel's build by
# site/fetch-emulator.mjs. Three files that never see each other, on the path to
# the front page's headline feature, and every way they can disagree deploys
# green and 404s in a browser.
MANIFEST="$ROOT/site/emulator-manifest.json"
FETCH="$ROOT/site/fetch-emulator.mjs"
VERCEL="$ROOT/site/vercel.json"
for f in "$MANIFEST" "$FETCH" "$VERCEL"; do
  [ -f "$f" ] || { echo "FAIL site  missing $f"; exit 1; }
done

# vercel.json is the only thing that makes the fetch run at all. Two properties:
# the command must name a script that is really there, and an outputDirectory
# must be declared -- an Output Directory override left on and empty in the
# Vercel dashboard SKIPS the build step entirely, and declaring one here is what
# takes that decision away from a setting nobody can see from the repository.
build_cmd="$(sed -nE 's/.*"buildCommand": *"([^"]*)".*/\1/p' "$VERCEL")"
if [ -z "$build_cmd" ]; then
  bad "site/vercel.json has no buildCommand, so nothing fetches the emulator and the demo 404s"
else
  ok
  script="$(printf '%s' "$build_cmd" | awk '{print $NF}')"
  [ -f "$ROOT/site/$script" ] \
    && ok || bad "vercel.json's buildCommand runs '$script' and site/$script does not exist"
fi
grep -q '"outputDirectory"' "$VERCEL" \
  && ok || bad "site/vercel.json declares no outputDirectory; a dashboard override can then skip the build step and the emulator is never fetched"

# Only xteink deploys. Every pull request used to get a preview deployment,
# and on the free plan a night of ten agents hit the deployment rate limit, so
# the Vercel check went red on every PR; every agent explained it away, which
# is how a real red gets ignored. Nobody used the previews. The ignore command
# is run here as Vercel runs it (exit 0 skips the build, anything else builds),
# in a repository with and without a change under site/.
IGN="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["ignoreCommand"])' "$VERCEL")"
IGNREPO="$(mktemp -d)"; trap 'rm -rf "$IGNREPO"' EXIT; ( cd "$IGNREPO" && git init -q -b xteink && git config user.email t@t && git config user.name t \
  && echo a > index.html && mkdir -p ../x && git add -A && git commit -qm one && echo b > index.html && git commit -qam two ) >/dev/null 2>&1
ignore() {  # ref, "changed"|"same" -> prints build|skip
  local rev=HEAD; [ "$2" = same ] && rev=HEAD^ && ( cd "$IGNREPO" && git commit -q --allow-empty -m empty ) >/dev/null 2>&1
  ( cd "$IGNREPO" && VERCEL_GIT_COMMIT_REF="$1" sh -c "$IGN" ) >/dev/null 2>&1 && echo skip || echo build
  [ "$2" = same ] && ( cd "$IGNREPO" && git reset -q --hard HEAD^ ) >/dev/null 2>&1
}
[ "$(ignore xteink changed)" = build ] && ok || bad "vercel.json ignoreCommand: xteink with a site change must build"
[ "$(ignore xteink same)" = skip ] && ok || bad "vercel.json ignoreCommand: xteink with no site change must skip, as before"
[ "$(ignore app/anything changed)" = skip ] && ok || bad "vercel.json ignoreCommand: a pull request branch must not deploy a preview"

# .vercelignore exists to keep laptop-only tooling off a public URL, and both
# halves of the fetch look exactly like that. Ignoring either leaves the build
# unable to find the file it was told to run.
VIGNORE="$ROOT/site/.vercelignore"
if [ -f "$VIGNORE" ]; then
  for needed in "$(basename "$MANIFEST")" "$(basename "$FETCH")"; do
    if grep -qE "^[^#]*$(printf '%s' "$needed" | sed 's/\./\\./g')" "$VIGNORE"; then
      bad ".vercelignore excludes $needed, so Vercel's build cannot see it and the emulator never reaches the site"
    else
      ok
    fi
  done
  # *.mjs or *.json as a blanket rule takes them both out just as effectively.
  grep -qE '^[^#]*\*\.(mjs|json)' "$VIGNORE" \
    && bad ".vercelignore excludes all .mjs or .json, which removes the build command or the manifest it reads" \
    || ok
fi

# The manifest and the script that reads it, on the field names. Read out of the
# script rather than listed here, so a rename on either side fails this instead
# of failing a deploy.
# The KEY, not the local name it is destructured into: `built_from: builtFrom`
# asks the manifest for built_from and nothing else.
fields="$(sed -nE 's/.*const \{ ([^}]*) \} = manifest.*/\1/p' "$FETCH" | tr ',' '\n' \
  | sed -E 's/ *:.*//; s/^ *//; s/ *$//' | grep -v '^$' | sort -u)"
entry_fields="$(sed -nE 's/.*const \{ ([^}]*) \} = file;.*/\1/p' "$FETCH" | tr ',' '\n' \
  | sed -E 's/^ *//; s/ *$//; s/^([A-Za-z0-9_]+):.*/\1/' | grep -v '^$' | sort -u)"
if [ -z "$fields" ] || [ -z "$entry_fields" ]; then
  bad "cannot tell which manifest fields fetch-emulator.mjs reads; the check has stopped checking"
else
  ok
  if man_bad="$(python3 - "$MANIFEST" "$fields" "$entry_fields" <<'PY' 2>&1
import json, sys
m = json.load(open(sys.argv[1]))
top = [f for f in sys.argv[2].split() if f]
per = [f for f in sys.argv[3].split() if f]
for f in top:
    if f not in m:
        print(f"fetch-emulator.mjs reads manifest.{f} and the manifest has no such key")
files = m.get("files") or []
if not files:
    print("the manifest names no files, so a deploy would fetch nothing and still be green")
for e in files:
    for f in per:
        if f not in e:
            print(f"fetch-emulator.mjs reads entry.{f} and {e.get('name', '?')} has no such key")
if not str(m.get("base", "")).endswith("/"):
    print("manifest base does not end in '/', so new URL(asset, base) drops the last path segment")
PY
  )"; then
    if [ -z "$man_bad" ]; then ok; else while IFS= read -r line; do bad "$line"; done <<< "$man_bad"; fi
  else
    bad "the emulator manifest could not be checked:"
    while IFS= read -r line; do echo "      $line"; done <<< "$man_bad"
  fi
fi

# The pre-compression contract, which the manifest now rides on: everything
# under site/emulator/ is served with Content-Encoding: br, so publish and fetch
# both hash brotli bytes. Either half of that removed alone ships a wasm no
# browser can decode, with nothing on the wire wrong until the decoder gives up.
grep -q '"emulator"' "$ROOT/tools_local/site/precompress.py" \
  && ok || bad "precompress.py no longer compresses site/emulator/, but vercel.json still promises brotli for it"
grep -q '"/emulator/(.\*)"' "$VERCEL" || grep -q '/emulator/' "$VERCEL" \
  && ok || bad "vercel.json no longer sets Content-Encoding for /emulator/, but the files are stored brotli"

# And the script itself, run for real against a local server -- including the
# case that matters, bytes that arrive corrupted. Status checked as well as
# output, for the reason spelled out in the shelf block above.
if fetch_out="$(node "$HERE/fetch_emulator.js" "$ROOT" 2>&1)"; then
  ok
  n_fail="$(printf '%s\n' "$fetch_out" | grep -c '^  FAIL' || true)"
  [ "$n_fail" -eq 0 ] && ok || { while IFS= read -r line; do bad "fetch_emulator: $line"; done < <(printf '%s\n' "$fetch_out" | grep '^  FAIL'); }
else
  bad "fetch_emulator.js could not run, so site/fetch-emulator.mjs went unchecked:"
  while IFS= read -r line; do echo "      $line"; done <<< "$fetch_out"
fi

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]
