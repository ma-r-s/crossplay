#!/usr/bin/env bash
# Confine the readbridge containers to the public internet only.
#
# Docker's default bridge gives a container free rein over the host and the
# LAN: without these rules the service could open Immich, Jellyfin,
# qBittorrent, Prowlarr, SSH and the router. It holds Instapaper OAuth tokens and
# reading lists, and is reachable from the internet, so it is treated as a
# machine that will eventually be compromised.
#
# Two paths need covering, and it is easy to do only the first:
#   FORWARD (via DOCKER-USER) -- container to other networks and containers
#   INPUT                     -- container to services on the host itself,
#                                which never traverses FORWARD
#
# Idempotent: every rule carries a comment tag and is removed before re-adding.
set -euo pipefail

# 172.31.83.0/24 is getbooks and 172.31.84.0/24 is ankibridge; readbridge has its own pinned /24 (compose.yaml).
SUBNET="172.31.85.0/24"
TAG="readbridge-isolation"

PRIVATE_RANGES=(
  "10.0.0.0/8"        # RFC1918
  "172.16.0.0/12"     # RFC1918, includes every other docker network
  "192.168.0.0/16"    # the home LAN and the router
  "100.64.0.0/10"     # the tailnet
  "169.254.0.0/16"    # link-local and cloud metadata endpoints
)

flush_tagged() {
  local chain="$1"
  # Delete by line number, highest first, so earlier indices stay valid.
  local lines
  lines=$(iptables -L "$chain" --line-numbers -n 2>/dev/null \
          | grep -F "$TAG" | awk '{print $1}' | sort -rn || true)
  for n in $lines; do
    iptables -D "$chain" "$n"
  done
}

flush_tagged DOCKER-USER
flush_tagged INPUT

pos=1
# Siblings first: cloudflared has to reach readbridge, and both live inside
# the 172.16/12 block that the next rules drop.
iptables -I DOCKER-USER "$pos" -s "$SUBNET" -d "$SUBNET" \
  -m comment --comment "$TAG" -j RETURN
pos=$((pos + 1))

for range in "${PRIVATE_RANGES[@]}"; do
  iptables -I DOCKER-USER "$pos" -s "$SUBNET" -d "$range" \
    -m comment --comment "$TAG" -j DROP
  pos=$((pos + 1))
done

# Traffic aimed at the host's own addresses lands in INPUT, never FORWARD, so
# DOCKER-USER alone would leave SSH and every published port wide open.
iptables -I INPUT 1 -s "$SUBNET" -m comment --comment "$TAG" -j DROP

echo "readbridge isolation applied for $SUBNET"
iptables -L DOCKER-USER -n --line-numbers | grep -F "$TAG" || true
iptables -L INPUT -n --line-numbers | grep -F "$TAG" || true
