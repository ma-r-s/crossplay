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
#
# The table's third ships value, `quiet`, releases exactly like `yes` here and
# this script does not distinguish them. It is a distinction about the NOTES --
# whether the landing earns a line on the page -- and release_notes.py is where
# it is read. A change to what the release publishes must cut a release; what
# it says on the page is a separate question with a separate answer.
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
# NOT --quiet. The tool's own line names the PATH that decided, and the
# autorelease log is the only place anybody can later ask "why did this one
# release?". check.sh deliberately prints the path that caused a build; the
# release side was silent about the path that caused a release, which is the
# harder question of the two.
why="$("$HERE/device-build-needed.sh" --range "$last..HEAD" --ships 2>&1)"
rc=$?
[ -n "$why" ] && echo "$why"
case $rc in
  0) echo "something since $last reaches a user: releasing"; exit 0 ;;
  3) echo "something since $last changes how the release is packaged: releasing"; exit 0 ;;
  1) echo "nothing since $last changes the firmware or what a release publishes: not releasing"; exit 1 ;;
  *) echo "refusing to decide: a path changed since $last that the classification table does not cover (the reason above names it)"
     exit 2 ;;
esac
