#!/usr/bin/env bash
# Host side of the screenshot path. Pure stdlib: drive.py imports pyserial
# lazily inside open_port(), and these tests never open a port.
set -euo pipefail
cd "$(dirname "$0")"
exec python3 test_screenshot.py
