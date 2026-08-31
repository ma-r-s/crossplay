#!/usr/bin/env bash
# Post-deploy isolation regression for readbridge: the walls must hold and the
# door must still work. Runs where docker runs, i.e. on the pi:
#
#   ssh orange 'bash -s' < scripts/isolation_test.sh
#
# Every private-network probe must TIME OUT. A refusal is also a breach: the
# firewall DROPs, so any answer at all -- SYN-ACK or RST -- proves the packet
# crossed the wall. Exits nonzero on any breach or on broken egress.
set -uo pipefail

fail=0

probe() {
  local label="$1" host="$2" port="$3"
  docker exec readbridge python -c "
import socket, sys
s = socket.socket()
s.settimeout(3)
try:
    s.connect(('$host', $port))
except TimeoutError:
    sys.exit(0)   # dropped: the wall held
except OSError:
    sys.exit(2)   # refused or unreachable: something answered
sys.exit(1)       # connected outright
"
  local rc=$?
  case $rc in
    0) echo "ok   $label ($host:$port) timed out" ;;
    1) echo "FAIL $label ($host:$port) accepted a connection"; fail=1 ;;
    2) echo "FAIL $label ($host:$port) answered instead of dropping"; fail=1 ;;
    *) echo "FAIL docker exec exited $rc; is the readbridge container running?"; fail=1 ;;
  esac
}

probe "immich via docker bridge gateway" 172.17.0.1 2283
probe "host ssh via docker bridge gateway" 172.17.0.1 22
probe "router" 192.168.68.1 80
probe "tailnet peer" 100.75.152.70 22

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
