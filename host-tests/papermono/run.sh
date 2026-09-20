#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 host-tests/papermono/test_config.py
