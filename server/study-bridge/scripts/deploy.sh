#!/usr/bin/env bash
# Deploy ankibridge to the Orange Pi.
#
# The Dockerfile COPYs bridge/ and tools_local/ from its build context, but
# the converter lives at the repo root, outside server/study-bridge/. So the
# deploy stages a context matching the COPY paths, ships it, and rebuilds.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SRC="$REPO_ROOT/server/study-bridge"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

# The service dir as-is, minus local-only noise that must never ship.
rsync -a --exclude .venv --exclude data --exclude .env \
  --exclude __pycache__ "$SRC/" "$STAGE/"

# The converter, vendored into the layout the Dockerfile expects
# (COPY tools_local -> /app/tools_local, imported via /app/tools_local/study).
mkdir -p "$STAGE/tools_local"
rsync -a --exclude __pycache__ \
  "$REPO_ROOT/tools_local/study/" "$STAGE/tools_local/study/"

# --exclude .env and --exclude data/ are load-bearing: .env exists only on
# the pi (secrets, mode 600) and data/ is the live user state; with --delete
# and without them, this line would erase both.
rsync -a --delete --exclude .env --exclude data/ \
  "$STAGE/" orange:/srv/ankibridge/

ssh orange 'cd /srv/ankibridge && docker compose up -d --build'
