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

echo
echo "Now verify the isolation, every deploy and not just firewall changes:"
echo "  ssh orange 'bash -s' < $SRC/scripts/isolation_test.sh"
