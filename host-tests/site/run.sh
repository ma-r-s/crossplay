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
land="$(awk '/\.device-screen\[data-orient="landscape"\]/,/^}/' "$CSS")"
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

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]
