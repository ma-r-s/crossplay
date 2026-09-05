#!/usr/bin/env bash
# Deploy ankibridge to the Orange Pi.
#
# The Dockerfile COPYs bridge/ and tools_local/ from its build context, but
# the converter lives at the repo root, outside server/study-bridge/. So the
# deploy stages a context matching the COPY paths, ships it, and rebuilds.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SRC="$REPO_ROOT/server/study-bridge"
# ---------------------------------------------------------------------------
# THE ATTACK SUITE RUNS BEFORE ANYTHING SHIPS.
#
# Same precedent as the isolation test below/beside this: a claim about safety
# that nothing runs is not a claim. The difference is WHEN -- isolation is a
# property of the deployed box and is checked after, this is a property of the
# code and is checked before, because a service that fails it must never reach
# the pi in the first place.
#
# Hermetic: its own throwaway data directory and its own fake upstream. It
# touches nothing live and needs no network.
# ---------------------------------------------------------------------------
PY_LOCAL="$SRC/.venv/bin/python"
if [ ! -x "$PY_LOCAL" ]; then
  echo "FAILED: no venv at $SRC/.venv, so the attack suite cannot run and this"
  echo "deploy cannot say whether the service is safe to publish. Create it:"
  echo "  cd $SRC && uv venv .venv && uv pip install -r requirements.txt"
  exit 1
fi
echo "attacking the build before shipping it ..."
if ! "$PY_LOCAL" "$SRC/tests/attack_test.py"; then
  echo
  echo "FAILED: the attack suite is red. NOTHING WAS DEPLOYED."
  echo "Each FAIL line above names what an outsider can do to this build."
  echo "See docs/bridge-security.md; server/verify_attacks.sh proves the"
  echo "checks themselves still work."
  exit 1
fi
echo

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

# make_fonts.py shells out to the repo's stock font converter, resolved
# repo-relative (which in the image means /app/lib/EpdFont/scripts). The web
# installer bundles these same two files into tools.zip for the same reason.
mkdir -p "$STAGE/lib/EpdFont/scripts"
rsync -a "$REPO_ROOT/lib/EpdFont/scripts/fontconvert_sdcard.py" \
  "$REPO_ROOT/lib/EpdFont/scripts/cpfont_version.py" \
  "$STAGE/lib/EpdFont/scripts/"

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

# ---------------------------------------------------------------------------
# ISOLATION IS PART OF DEPLOYING, NOT A STEP AFTERWARDS.
#
# read-bridge's deploy has done this since 2026-09-01, when its FIRST deploy
# came up healthy, serving its pages, and reaching the host's SSH, Immich, the
# router and a tailnet peer. Nothing about that deploy looked wrong. THIS
# script never gained the same block: ankibridge-firewall.service exists in
# scripts/ and has to be installed by somebody remembering to, which is the
# state read-bridge was in on the day it bit.
#
# So this now installs the isolation and REFUSES TO REPORT SUCCESS until the
# test passes. A deploy that cannot prove it is confined is a failed deploy,
# whatever the containers say.
# ---------------------------------------------------------------------------

if ! ssh orange 'sudo -n true' 2>/dev/null; then
  echo
  echo "FAILED: no passwordless sudo on the pi, so the network isolation cannot"
  echo "be installed or verified from here. The containers are RUNNING and"
  echo "possibly UNCONFINED. Install it by hand before leaving this:"
  echo "  ssh orange 'sudo /srv/ankibridge/scripts/firewall.sh'"
  exit 1
fi

echo "installing the network isolation ..."
ssh orange 'sudo -n /srv/ankibridge/scripts/firewall.sh' > /dev/null

# Idempotent: writing the same unit twice is free, and a deploy to a rebuilt
# box must not depend on somebody remembering this once, months ago.
ssh orange 'sudo -n cp /srv/ankibridge/scripts/ankibridge-firewall.service \
  /etc/systemd/system/ankibridge-firewall.service \
  && sudo -n systemctl daemon-reload \
  && sudo -n systemctl enable ankibridge-firewall.service' > /dev/null

echo "verifying it ..."
if ssh orange 'bash -s' < "$SRC/scripts/isolation_test.sh"; then
  echo
  echo "deployed, and confined."
else
  echo
  echo "FAILED: the containers are running but the isolation test did NOT pass."
  echo "They may be able to reach the host and the LAN. Investigate before"
  echo "leaving this; do not treat the running containers as a successful"
  echo "deploy."
  exit 1
fi
