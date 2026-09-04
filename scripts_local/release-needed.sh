#!/bin/bash
# Should a green merge into xteink become a release?
#
# The autorelease used to release on every green merge, a documentation edit
# included: a version bump, a tag, four device images and an update prompt on
# every device, for a change no device can see. Then it asked the BUILD
# question instead -- "do the device builds need to run for this?" -- and cut
# v1.12.21 anyway, because `.gitignore` is genuinely live for a build in a
# throwaway worktree and cannot alter one byte of a published image.
#
# The question this needs is the other column of the same table:
# scripts_local/device-build-needed.sh --ships, asked about the commits since
# the newest release tag. One table, two columns, one definition of each.
#
#   scripts_local/release-needed.sh            # in a checkout of xteink
#
# Exit 0: release (something since the last tag reaches a user, or there is no
#         tag to count from).
# Exit 1: nothing since the last release tag changes what anybody receives.
# Exit 2: REFUSED -- a changed path is in no row of the table, so the answer is
#         not known. Releasing and withholding are both wrong answers to that,
#         and the caller is expected to stop and say which path it was rather
#         than pick one. The message names it.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
last="$(git describe --tags --abbrev=0 --match 'v*' 2>/dev/null)" || last=""
if [ -z "$last" ]; then
  echo "no release tag reachable: releasing"
  exit 0
fi
if [ "$(git rev-list --count "$last..HEAD" 2>/dev/null || echo 1)" = "0" ]; then
  echo "nothing merged since $last: not releasing"
  exit 1
fi
"$HERE/device-build-needed.sh" --range "$last..HEAD" --ships --quiet
case $? in
  0) echo "something since $last reaches a user: releasing"; exit 0 ;;
  1) echo "nothing since $last changes the firmware or what a release publishes: not releasing"; exit 1 ;;
  *) echo "refusing to decide: a path changed since $last that the classification table does not cover (the reason above names it)"
     exit 2 ;;
esac
