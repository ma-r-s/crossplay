#!/usr/bin/env bash
# Deploy the pack host to the Orange Pi.
#
#   server/packs/scripts/deploy.sh
#
# Ships compose.yaml, nginx.conf and scripts/ to orange:/srv/packs, starts the
# containers, installs the isolation unit, and runs the isolation test. The
# packs themselves are not shipped here: scripts/publish_pack.sh does that.
# .env (the tunnel token) is written once by hand and never touched here.
set -euo pipefail
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

ssh orange 'mkdir -p /srv/packs/data /srv/packs/scripts'
rsync -a --exclude data --exclude .env "$SRC/compose.yaml" "$SRC/nginx.conf" "$SRC/README.md" orange:/srv/packs/
rsync -a "$SRC/scripts/" orange:/srv/packs/scripts/
ssh orange 'test -f /srv/packs/.env || { echo "no /srv/packs/.env: write CLOUDFLARE_TUNNEL_TOKEN=... there (mode 600) first" >&2; exit 1; }'
ssh orange 'cd /srv/packs && docker compose pull -q && docker compose up -d'
ssh orange 'sudo -n install -m 644 /srv/packs/scripts/packs-firewall.service /etc/systemd/system/packs-firewall.service \
  && sudo -n systemctl daemon-reload && sudo -n systemctl enable --now packs-firewall.service \
  && sudo -n systemctl restart packs-firewall.service'
echo "isolation test:"
ssh orange 'bash -s' < "$SRC/scripts/isolation_test.sh"
