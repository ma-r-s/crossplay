#!/bin/bash
# Should a green merge into xteink become a release?
#
# The autorelease used to release on every green merge, a documentation edit
# included: a version bump, a tag, four device images and an update prompt on
# every device, for a change no device can see. The rule that already knows
# which paths can reach a device image is scripts_local/device-build-needed.sh;
# this asks it about the commits since the newest release tag.
#
#   scripts_local/release-needed.sh            # in a checkout of xteink
#
# Exit 0: release (something since the last tag can change a device image, or
#         the question cannot be answered: no tag, no git; unknown means release).
# Exit 1: nothing since the last release tag can alter a device image.
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
if "$HERE/device-build-needed.sh" --range "$last..HEAD" --quiet; then
  echo "something since $last can reach a device image: releasing"
  exit 0
fi
echo "nothing since $last can alter a device image (docs, site, tooling, workflows only): not releasing"
exit 1
