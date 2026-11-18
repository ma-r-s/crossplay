#!/bin/bash
# The Picco upload writes its app slot and nothing else; see test_appslot.py.
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 host-tests/appslot/test_appslot.py
