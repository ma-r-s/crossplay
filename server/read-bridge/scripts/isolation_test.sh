#!/usr/bin/env bash
# Post-deploy isolation regression for readbridge: the walls must hold and the
# door must still work. Runs where docker runs, i.e. on the pi:
#
#   ssh orange 'bash -s' < scripts/isolation_test.sh
#
# Every private-network probe must TIME OUT. A refusal is also a breach: the
# firewall DROPs, so any answer at all -- SYN-ACK or RST -- proves the packet
# crossed the wall. Exits nonzero on any breach or on broken egress.
#
# TWO VANTAGE POINTS, and the second one is the point. Until 2026-09-03 this
# script probed only from inside the `readbridge` container, and reported a
# clean deploy while saying nothing at all about `readbridge-cloudflared` --
# which is the container with a live connection to the public internet, and
# therefore the one an attacker reaches first. The wall is a SUBNET rule
# (172.31.85.0/24 in scripts/firewall.sh), so it does cover both; but "does"
# and "was proved to" are different things, and an audit that cannot see a
# container reports it clean.
set -uo pipefail

fail=0

PY='
import socket, sys
s = socket.socket()
s.settimeout(3)
try:
    s.connect((sys.argv[1], int(sys.argv[2])))
except TimeoutError:
    sys.exit(0)   # dropped: the wall held
except OSError:
    sys.exit(2)   # refused or unreachable: something answered
sys.exit(1)       # connected outright
'

# cloudflared ships a distroless image: no shell, no python, so it cannot be
# made to probe from inside itself. A throwaway container on the same network
# stands in for it exactly -- docker hands it an address in the same pinned
# /24, so it is matched by the same iptables rules. The service's own image is
# used because it is the one image guaranteed to exist on this box.
probe() {
  local where="$1" label="$2" host="$3" port="$4"
  case "$where" in
    service)   docker exec readbridge python -c "$PY" "$host" "$port" ;;
    cfd-subnet) docker run --rm --network readbridge readbridge:local \
                  python -c "$PY" "$host" "$port" ;;
  esac
  local rc=$?
  case $rc in
    0) echo "ok   [$where] $label ($host:$port) timed out" ;;
    1) echo "FAIL [$where] $label ($host:$port) accepted a connection"; fail=1 ;;
    2) echo "FAIL [$where] $label ($host:$port) answered instead of dropping"; fail=1 ;;
    *) echo "FAIL [$where] docker exited $rc; are the readbridge containers running?"; fail=1 ;;
  esac
}

for where in service cfd-subnet; do
  probe "$where" "immich via docker bridge gateway" 172.17.0.1 2283
  probe "$where" "host ssh via docker bridge gateway" 172.17.0.1 22
  probe "$where" "router" 192.168.68.1 80
  probe "$where" "tailnet peer" 100.75.152.70 22
done

# The door: DNS and outbound HTTPS must succeed, or the service is walled in
# rather than walled off and sync is silently dead.
if docker exec readbridge python -c \
    "import socket; socket.getaddrinfo('www.instapaper.com', 443)"; then
  echo "ok   DNS resolves www.instapaper.com"
else
  echo "FAIL DNS resolution is broken"; fail=1
fi

if docker exec readbridge python -c "
import urllib.request, urllib.error
req = urllib.request.Request('https://www.instapaper.com', method='HEAD')
try:
    urllib.request.urlopen(req, timeout=10)
except urllib.error.HTTPError:
    pass   # any HTTP status is still proof the internet is reachable
"; then
  echo "ok   HTTPS HEAD to www.instapaper.com"
else
  echo "FAIL outbound HTTPS to www.instapaper.com is broken"; fail=1
fi

exit $fail
