#!/bin/bash
# The outside pulse: probe every host in hosts.txt and post one event each.
#
# A service that is down posts nothing, and on the board "no events" reads as
# "no usage". This is the one thing that looks from outside. It runs from a
# GitHub schedule every 30 minutes (.github/workflows/crossplay-pulse.yml) and
# posts to the board with the public key: an info event when a host answers
# as expected, an error event otherwise. The error opens a card by the board's
# own rule (docs/workflow/events.md), one per host thanks to a fixed
# fingerprint; the next info event with the same fingerprint closes it, so an
# outage is a card exactly as long as it lasts, and a new one if it returns.
#
#   server/pulse/pulse.sh [hosts.txt]
#
# Exit status is the number of hosts down, so the workflow run goes red too.
#
# Environment:
#   SUPABASE_URL, SUPABASE_ANON_KEY   where to post; without them it only prints
#   PULSE_TIMEOUT                     seconds per probe (15)
#   PULSE_UPSTREAM=1                  also check the daily upstream sync is not late
#   PULSE_REPO, UPSTREAM_URL          the git tree and upstream to compare (tests)
#   PULSE_PRS_JSON                    a file standing in for `gh pr list` (tests)
set -uo pipefail

HOSTS="${1:-$(dirname "$0")/hosts.txt}"
TIMEOUT="${PULSE_TIMEOUT:-15}"
down=0

post() {  # post <json>
  [ -n "${SUPABASE_URL:-}" ] && [ -n "${SUPABASE_ANON_KEY:-}" ] || return 0
  curl -s -o /dev/null -m 20 -X POST "$SUPABASE_URL/rest/v1/events" \
    -H "apikey: $SUPABASE_ANON_KEY" -H "Authorization: Bearer $SUPABASE_ANON_KEY" \
    -H "Content-Type: application/json" -H "Prefer: return=minimal" \
    --data "$1" || echo "       (could not post that event to the board)"
}

jstr() { python3 -c 'import json, sys; print(json.dumps(sys.argv[1]))' "$1"; }

alive() {  # alive <status> <expected>
  local st="$1" want IFS=,
  for want in $2; do
    case "$want" in
      2xx | 3xx | 4xx) [ "${st:0:1}" = "${want:0:1}" ] && return 0 ;;
      *) [ "$st" = "$want" ] && return 0 ;;
    esac
  done
  return 1
}

probe() {  # probe <name> <method> <url> <expected> <app>
  local name="$1" method="$2" url="$3" expect="$4" app="${5:-tooling}" out st secs ms why
  if [ "$method" = POST ]; then
    out=$(curl -s -o /dev/null -m "$TIMEOUT" -w '%{http_code} %{time_total}' -X POST \
      -H 'Content-Type: application/json' --data '{}' "$url" 2>/dev/null) || out="000 $TIMEOUT"
  else
    out=$(curl -s -o /dev/null -m "$TIMEOUT" -w '%{http_code} %{time_total}' -X "$method" "$url" 2>/dev/null) || out="000 $TIMEOUT"
  fi
  st="${out%% *}"; secs="${out##* }"
  ms=$(python3 -c "print(int(float('$secs') * 1000))" 2>/dev/null || echo 0)
  if alive "$st" "$expect"; then
    echo "  ok   $name $st ${ms}ms"
    post "{\"service\":\"pulse\",\"event\":\"probe\",\"fingerprint\":\"pulse|$name\",\"props\":{\"host\":$(jstr "$name"),\"app\":$(jstr "$app"),\"status\":\"$st\",\"ms\":$ms}}"
  else
    why="answered $st"; [ "$st" = 000 ] && why="no answer in ${TIMEOUT}s"
    echo "  DOWN $name $why"
    down=$((down + 1))
    post "{\"service\":\"pulse\",\"event\":\"probe\",\"level\":\"error\",\"fingerprint\":\"pulse|$name\",\"props\":{\"message\":$(jstr "$name is down: $why from $url"),\"host\":$(jstr "$name"),\"app\":$(jstr "$app"),\"url\":$(jstr "$url"),\"status\":\"$st\",\"ms\":$ms}}"
  fi
}

open_sync_prs() {  # how many open pull requests come from a sync/upstream-* branch
  if [ -n "${PULSE_PRS_JSON:-}" ]; then cat "$PULSE_PRS_JSON"
  else gh pr list -R ma-r-s/crossplay --state open --json headRefName 2>/dev/null || echo "[]"
  fi | python3 -c 'import json, sys
try: prs = json.load(sys.stdin)
except Exception: prs = []
print(sum(1 for p in prs if str(p.get("headRefName", "")).startswith("sync/upstream-")))'
}

# The daily sync is a cloud routine nobody watches. Late means: upstream has
# commits xteink lacks, the oldest is more than 30 hours old, and no
# sync/upstream-* pull request is open to carry them. A routine that stopped
# on a conflict it could not resolve leaves exactly that state behind.
upstream() {
  local repo="${PULSE_REPO:-.}" up="${UPSTREAM_URL:-https://github.com/crosspoint-reader/crosspoint-reader.git}"
  local base n oldest oldest_h open
  git -C "$repo" fetch -q "$up" develop 2>/dev/null || { echo "  ?    upstream: could not fetch develop from $up"; return; }
  base=$(git -C "$repo" rev-parse -q --verify origin/xteink 2>/dev/null || git -C "$repo" rev-parse -q --verify xteink 2>/dev/null) \
    || { echo "  ?    upstream: no xteink branch in $repo"; return; }
  n=$(git -C "$repo" rev-list --count "$base..FETCH_HEAD")
  if [ "$n" = 0 ]; then
    echo "  ok   upstream in sync"
    post '{"service":"pulse","event":"probe","fingerprint":"pulse|upstream-sync","props":{"host":"upstream-sync","app":"tooling","behind":0}}'
    return
  fi
  oldest=$(git -C "$repo" log --format=%ct "$base..FETCH_HEAD" | sort -n | head -1)
  oldest_h=$(( ($(date +%s) - oldest) / 3600 ))
  open=$(open_sync_prs)
  if [ "$open" -gt 0 ]; then
    echo "  ok   upstream $n commits behind, a sync pull request is open"
  elif [ "$oldest_h" -lt 30 ]; then
    echo "  ok   upstream $n new commits, oldest ${oldest_h}h, sync due"
  else
    echo "  DOWN upstream sync late: $n commits behind, oldest ${oldest_h}h, no sync pull request"
    down=$((down + 1))
    post "{\"service\":\"pulse\",\"event\":\"probe\",\"level\":\"error\",\"fingerprint\":\"pulse|upstream-sync\",\"props\":{\"message\":$(jstr "upstream sync late: $n commits not merged, oldest ${oldest_h}h, no sync pull request open"),\"host\":\"upstream-sync\",\"app\":\"tooling\",\"behind\":$n,\"oldest_h\":$oldest_h}}"
    return
  fi
  post "{\"service\":\"pulse\",\"event\":\"probe\",\"fingerprint\":\"pulse|upstream-sync\",\"props\":{\"host\":\"upstream-sync\",\"app\":\"tooling\",\"behind\":$n,\"oldest_h\":$oldest_h,\"open_prs\":$open}}"
}

[ -f "$HOSTS" ] || { echo "pulse: no hosts file at $HOSTS"; exit 1; }
while read -r name method url expect app _; do
  case "$name" in "" | \#*) continue ;; esac
  [ -n "$expect" ] || { echo "  ?    $name: no expected status on its line, skipped"; continue; }
  probe "$name" "$method" "$url" "$expect" "$app"
done < "$HOSTS"
[ "${PULSE_UPSTREAM:-0}" = 1 ] && upstream
echo "pulse: $down down"
exit "$down"
