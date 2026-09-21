#!/bin/sh
# A deferred refresh that nobody waits for leaves the UI input-dead.
#
# GfxRenderer::displayBufferAsync() starts the waveform and returns, and it
# deliberately does NOT count the paint: waitRefreshComplete() does, and
# paintclock's counter is what UiAppHost::revealed() reads before it will route
# a single tap. So a screen shown with the deferred path and never waited on
# renders perfectly and then ignores every touch, with nothing in any log.
#
# That is card #546: the reader's toolbar menu opened and then "stopped
# responding" on both CrossPlay boards, while button-only boards were fine
# because they take the blocking displayBuffer(), which counts the paint. It
# cost a release, a demotion to pre-release and two evenings.
#
# The rule: a translation unit that starts a deferred refresh must also wait
# for one. Not a proof of correctness -- the wait could be on another path --
# but it catches the shape that shipped, and it is the only guard available
# without an Arduino and a panel.
#
#   host-tests/deferredpaint/run.sh
set -e
cd "$(dirname "$0")/../.."
checks=0
failed=0
for f in $(grep -rl "displayBufferAsync" src || true); do
  checks=$((checks + 1))
  if ! grep -q "waitRefreshComplete" "$f"; then
    failed=$((failed + 1))
    echo "FAIL deferredpaint  $f starts a deferred refresh and never waits for one, so nothing counts the paint and every tap on that screen is dropped before it reaches a target (card #546)"
  fi
done
checks=$((checks + 1))
if [ "$checks" -le 1 ]; then
  failed=$((failed + 1))
  echo "FAIL deferredpaint  found no displayBufferAsync callers at all; the rule just checked nothing"
fi
echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]
