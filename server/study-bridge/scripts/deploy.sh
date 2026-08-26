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

# The bind mount must belong to the container's uid BEFORE first write, or
# docker creates it as root and the non-root app gets EACCES -- the exact
# Getbooks gotcha, hit again on this service's first login. chown-via-docker
# needs no host sudo.
ssh orange 'mkdir -p /srv/ankibridge/data \
  && docker run --rm -v /srv/ankibridge/data:/data alpine chown 10002:10002 /data'
ssh orange 'cd /srv/ankibridge && docker compose up -d --build'
