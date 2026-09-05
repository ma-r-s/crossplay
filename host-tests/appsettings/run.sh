#!/bin/bash
# An app's own option never renders in the device's global Settings.
# See host-tests/appsettings/test_appsettings.py for what this is and why.
#
#   host-tests/appsettings/run.sh
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE/../.." || exit 1
exec python3 host-tests/appsettings/test_appsettings.py
