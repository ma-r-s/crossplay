#!/usr/bin/env bash
# Post-deploy isolation regression for packs: the walls must hold and the
# door must still work. Runs where docker runs, i.e. on the pi:
#
#   ssh orange 'bash -s' < scripts/isolation_test.sh
#
# Every private-network probe must TIME OUT. A refusal is also a breach: the
# firewall DROPs, so any answer at all proves the packet crossed the wall. The
# nginx image has busybox wget and nothing else, so the verdict is read from
# the clock: a drop takes the whole timeout, an answer comes back at once.
set -uo pipefail
fail=0

probe() {
  local label="$1" host="$2" port="$3"
  local t0 t1
  t0=$(date +%s)
  docker exec packs wget -q -T 3 -O /dev/null "http://$host:$port/" 2>/dev/null
  local rc=$?
  t1=$(date +%s)
  if [ $rc -eq 0 ]; then
    echo "FAIL $label ($host:$port) answered"; fail=1
  elif [ $((t1 - t0)) -ge 3 ]; then
    echo "ok   $label ($host:$port) timed out"
  else
    echo "FAIL $label ($host:$port) answered instead of dropping (rc $rc in $((t1 - t0))s)"; fail=1
  fi
}

probe "immich via docker bridge gateway" 172.17.0.1 2283
probe "host ssh via docker bridge gateway" 172.17.0.1 22
probe "router" 192.168.68.1 80
probe "tailnet peer" 100.75.152.70 22

# The door: egress to the internet still works (cloudflared needs it).
if docker exec packs wget -q -T 5 -O /dev/null https://one.one.one.one/ 2>/dev/null; then
  echo "ok   egress to the internet"
else
  echo "FAIL egress to the internet is blocked"; fail=1
fi
exit $fail
