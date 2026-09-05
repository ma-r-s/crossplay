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

# Every class the script writes must mean something to the stylesheet.
nav_classes="$(grep -oE 'classList\.(add|toggle)\("[a-z-]+"' "$TOPNAV" | grep -oE '"[a-z-]+"' | tr -d '"' | sort -u)"
if [ -z "$nav_classes" ]; then
  bad "topnav.js writes no classes at all, so the bar can never open"
else
  ok
  for c in $nav_classes; do
    grep -q "\.$c" "$CSS" && ok || bad "topnav.js writes .$c and styles.css has no rule for it"
  done
fi
# ...and the control it looks for must be one the stylesheet can show and both
# pages actually carry.
for sel in topnav-toggle topnav; do
  grep -q "\.$sel" "$TOPNAV" && ok || bad "topnav.js never looks for .$sel"
  grep -q "\.$sel" "$CSS" && ok || bad "styles.css has no .$sel rule"
done
for p in "$HTML" "$SP_HTML"; do
  rel="${p#"$ROOT"/}"
  grep -q 'class="topnav-toggle"' "$p" && ok || bad "$rel has no .topnav-toggle button, so its narrow bar has no navigation"
  grep -qE 'src="/?assets/topnav\.js"' "$p" && ok || bad "$rel does not load assets/topnav.js, so its menu button opens nothing"
  # aria-controls has to name something on the page, or the button announces a
  # relationship to an element that is not there.
  ctl="$(grep -oE 'aria-controls="[A-Za-z0-9_-]+"' "$p" | head -1 | sed -E 's/.*="//; s/"//')"
  if [ -z "$ctl" ]; then
    bad "$rel's menu button has no aria-controls"
  else
    ok
    grep -q "id=\"$ctl\"" "$p" && ok || bad "$rel's menu button controls #$ctl and no such element exists"
  fi
done
# A bar is one line high. Without this the label wraps inside it and the fix is
# only half applied: the panel is right and the wide bar is still two lines at
# any width where a label is long enough.
awk '/^\.topnav a \{/,/^}/' "$CSS" | grep -q 'white-space: *nowrap' \
  && ok || bad ".topnav a does not set white-space: nowrap, so a long label wraps inside the bar again"
# The old rule hid the two product links outright. It may only survive as the
# no-script fallback, which is what :not(.has-menu) scopes it to.
if grep -q 'a\[href="#shelf"\]' "$CSS"; then
  grep -q 'topbar:not(\.has-menu) .topnav a\[href="#shelf"\]' "$CSS" \
    && ok || bad "styles.css hides the #shelf link without scoping it to :not(.has-menu), so it is gone from the menu too"
else
  ok
fi

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
      # Remote first: the page must name that host and must not claim otherwise.
      host="$(printf '%s' "$first_base" | sed -E 's|https?://||; s|/.*||')"
      label="$(printf '%s' "$host" | awk -F. '{print $(NF-1)}')"
      grep -qi "$label" "$SP_HTML" \
        && ok || bad "worker.js loads Pyodide from $host first and study/index.html never says so"
      grep -qi "served from this site" "$SP_HTML" \
        && bad "study/index.html still says Pyodide is served from this site, and worker.js asks $host first" || ok
      ;;
    *)
      # Same-origin first: then "served from this site" is the true claim.
      grep -qi "served from this site" "$SP_HTML" \
        && ok || bad "worker.js loads Pyodide from this site first and study/index.html does not say so"
      ;;
  esac
fi

# -- the report form's version placeholder ------------------------------------
#
# It read 1.12.9 while the site was shipping 1.12.23: a number typed into the
# file, right the day it was written and eleven releases stale by the time
# anyone looked. It is asked for now, from the same release the Install button
# reads. Nothing here can check the answer -- that needs the network -- but it
# can check that no literal has been typed back in, and that the field is
# still filled from somewhere.
ver_ph="$(grep -oE 'id="report-version"[^>]*placeholder="[^"]*"' "$REPORTJS" | grep -oE 'placeholder="[^"]*"' | sed -E 's/placeholder="//; s/"//')"
if printf '%s' "$ver_ph" | grep -qE '[0-9]+\.[0-9]+'; then
  bad "report.js hardcodes a firmware version ($ver_ph) as the placeholder again; it is asked for, not typed"
else
  ok
fi
grep -q 'releases/latest' "$REPORTJS" \
  && ok || bad "report.js no longer asks for the latest release, so the version field has no placeholder at all"
grep -q 'REPO' "$REPORTJS" \
  && ok || bad "report.js has no repository to ask"

# -- the study wizard's steps stay in normal flow ------------------------------
#
# .wiz-step was position:absolute inside a min-height:0 row, so a step taller
# than the window was cropped by it rather than growing the page. At 1440x900
# the step-2 "Next" button hung 29 of its 45 pixels below the cut and
# document.elementFromPoint at the button's own centre returned the footer; at
# 1280x720 none of it was on screen and scrollHeight equalled innerHeight, so
# the page reported nothing more to see.
#
# THIS CHECK CANNOT SEE THAT. Whether a control is reachable is a browser
# question and it was answered in one: elementFromPoint at the centre, then a
# real click, at 1440x900 and 1280x720, before and after. What it can see is
# the mechanism -- that the step is in flow and the page is allowed to grow --
# and that is the thing whose quiet return would bring the rest back with it.
SCSS="$ROOT/site/study/study.css"
[ -f "$SCSS" ] || { echo "FAIL site  missing $SCSS"; exit 1; }
awk '/^\.wiz-step \{/,/^}/' "$SCSS" | grep -qE 'position: *absolute' \
  && bad ".wiz-step is position:absolute again; a step taller than the window is cropped by the row instead of growing the page" || ok
awk '/^\.study-body \{/,/^}/' "$SCSS" | grep -qE 'overflow: *hidden' \
  && bad ".study-body clips again, so a step that does not fit reports no scrollable height and the page looks finished" || ok
awk '/^\.study-body \{/,/^}/' "$SCSS" | grep -qE '^ *height: *100vh' \
  && bad ".study-body is height:100vh again; it has to be a minimum or a tall step cannot push the page" || ok
awk '/^\.wizard \{/,/^}/' "$SCSS" | grep -qE 'min-height: *calc' \
  && ok || bad ".wizard no longer sets a min-height, so a short step stops filling the window"
# The emulator's cap is the only thing left sizing the panel now that the box
# does not crop it, so it has to be a token rather than a number buried in a
# calc that nobody can check against anything.
grep -q -- '--preview-chrome:' "$SCSS" \
  && ok || bad "study.css has no --preview-chrome token; the preview's cap is a bare number again"
awk '/^\.study-preview-frame \{/,/^}/' "$SCSS" | grep -q -- 'var(--preview-chrome)' \
  && ok || bad ".study-preview-frame does not use --preview-chrome, so the token and the cap have drifted"

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]
