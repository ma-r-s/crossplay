#!/usr/bin/env bash
# Deploy readbridge to the Orange Pi.
#
# Simpler than ankibridge's deploy: the Dockerfile's build context is entirely
# inside this directory (nothing is vendored from the repo root), so this is a
# straight rsync and rebuild.
set -euo pipefail

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# --exclude .env and --exclude data/ are load-bearing: .env exists only on the
# pi (secrets, mode 600) and data/ is the live credential store; with --delete
# and without them, this line would erase both.
rsync -a --delete \
  --exclude .venv --exclude data/ --exclude .env --exclude __pycache__ \
  --exclude tests \
  "$SRC/" orange:/srv/readbridge/

# The bind mount must belong to the container's uid BEFORE first write, or
# docker creates it as root and the non-root app gets EACCES -- the Getbooks
# gotcha, hit again by ankibridge on its first login. chown-via-docker needs
# no host sudo.
ssh orange 'mkdir -p /srv/readbridge/data \
  && docker run --rm -v /srv/readbridge/data:/data alpine chown 10003:10003 /data'
ssh orange 'cd /srv/readbridge && docker compose up -d --build'

# ---------------------------------------------------------------------------
# ISOLATION IS PART OF DEPLOYING, NOT A STEP AFTERWARDS.
#
# This script used to end by PRINTING the isolation test as a suggestion. On
# this service's first deploy that suggestion was followed and it failed: the
# container was up, healthy, serving its pages, and reaching the host's SSH,
# Immich, the router and a tailnet peer. Nothing about the deploy looked wrong,
# because `docker compose up` had genuinely succeeded -- the firewall unit
# simply did not exist yet for a service nobody had deployed before.
#
# The siblings all have one, which is exactly why the gap was invisible: it
# only appears when a NEW service is added, and that is the moment when
# somebody is least likely to know what is missing.
#
# So the script now installs the isolation and REFUSES TO REPORT SUCCESS until
# the test passes. A deploy that cannot prove it is confined is a failed
# deploy, whatever the containers say.
# ---------------------------------------------------------------------------

if ! ssh orange 'sudo -n true' 2>/dev/null; then
  echo
  echo "FAILED: no passwordless sudo on the pi, so the network isolation cannot"
  echo "be installed or verified from here. The containers are RUNNING and"
  echo "possibly UNCONFINED. Install it by hand before leaving this:"
  echo "  ssh orange 'sudo /srv/readbridge/scripts/firewall.sh'"
  exit 1
fi

echo "installing the network isolation ..."
ssh orange 'sudo -n /srv/readbridge/scripts/firewall.sh' > /dev/null

# Idempotent: writing the same unit twice is free, and a deploy to a rebuilt
# box must not depend on somebody remembering this once, months ago.
ssh orange 'sudo -n tee /etc/systemd/system/readbridge-firewall.service > /dev/null <<UNIT
[Unit]
Description=Network isolation for the readbridge containers
After=docker.service
Requires=docker.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/srv/readbridge/scripts/firewall.sh

[Install]
WantedBy=multi-user.target
UNIT
sudo -n systemctl daemon-reload && sudo -n systemctl enable readbridge-firewall.service' > /dev/null

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
