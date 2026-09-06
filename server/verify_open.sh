#!/usr/bin/env bash
# Can a STRANGER use these services? Asked from outside, over the real
# internet, exactly the way a stranger asks.
#
#   server/verify_open.sh          the verdict
#   server/verify_open.sh --why    the verdict, then diagnostics from the pi
#
# WHY THIS EXISTS AND WHY NOTHING ELSE ANSWERS IT
#
# Three services are gated, each by one environment variable on the pi, and
# every gate FAILS CLOSED. Every instrument we already have is blind to a shut
# gate:
#
#   docker compose up -d          exits 0 with the gate shut
#   /healthz                      answers 200 with the gate shut
#   the container's healthcheck   passes with the gate shut
#   `board pulse`                 counts books' 401 as ALIVE, which is the
#                                 exact response a stranger's reader gets when
#                                 the public credential pair is unset
#   printenv inside the container is necessary and not sufficient: it says what
#                                 the process holds, never who may sign in
#
# So the only question worth asking is the one a stranger asks, and the answer
# lives in the response BODY rather than the status code. Both bridges answer
# 200 whether they let you in or refuse you.
#
# THE TWO SENTENCES, AND THEY MEAN OPPOSITE THINGS
#
#   "This bridge is invitation-only for now."
#       OUR gate refused. The door is shut. Nobody but the allowlist gets in.
#
#   "AnkiWeb did not accept that email and password."
#   "Instapaper did not accept that email and password."
#       Our gate let the attempt THROUGH and the upstream refused the
#       credential. The door is open and this probe's key is wrong, which is
#       the whole point: the key is deliberately bogus.
#
# A careless reader sees two sign-in failures and calls both "still broken".
# They are opposite verdicts. That confusion is the reason this file is a
# script and not a paragraph in a runbook.
#
# WHAT THIS PROVES AND WHAT IT DOES NOT
#
# It proves OUR gate is open. For read-bridge it does NOT prove a stranger can
# actually sign in: if Instapaper's application were still in owner-only mode,
# a non-owner xAuth returns 403 and the bridge prints the same "Instapaper did
# not accept" sentence. Only a real third-party Instapaper credential separates
# those two. Say so rather than overclaiming.
#
# COST: one bogus sign-in per bridge per run. LOGIN_IP is 5 per 5 minutes per
# address, so about five runs per five minutes before this probe rate-limits
# itself. The usernames are fresh and end in .invalid (RFC 2606), which can
# never be a registrable domain, so no real person's account is ever touched
# and no failure is ever recorded against one.
set -uo pipefail

WHY=0
[ "${1:-}" = "--why" ] && WHY=1

# The real services, and overridable ONLY so verify_open_selftest.sh can point
# the classifier at a local stub and watch every branch fire. Nothing else
# should ever set these: the entire value of this script is that it asks the
# public internet.
BASE_STUDY="${VERIFY_BASE_STUDY:-https://sync.ma-r-s.com}"
BASE_READ="${VERIFY_BASE_READ:-https://read.ma-r-s.com}"
BASE_BOOKS="${VERIFY_BASE_BOOKS:-https://books.ma-r-s.com}"

# The pair CrossPlay's firmware seeds as the Get Books default
# (src/OpdsServerStore.cpp). Public on purpose: it is in a public repository
# and recoverable with `strings` from every release binary. It is a courtesy
# gate against crawlers, never a secret, and never Mario's own login.
BOOKS_USER="crossplay"
BOOKS_PASS="r4ulp-zm4cg-awjtf-z5zfj"

STAMP="$(date +%s)-$RANDOM"
fail=0
inconclusive=0

say() { printf '%s\n' "$*"; }
rule() { printf '%s\n' "------------------------------------------------------------"; }

# ---------------------------------------------------------------- is it up
rule
say "IS THE BOX ANSWERING AT ALL"
rule
up=1
for host in "$BASE_STUDY" "$BASE_READ" "$BASE_BOOKS"; do
  code="$(curl -sS -o /dev/null -w '%{http_code}' --max-time 20 "$host/healthz" 2>/dev/null)"
  printf '  %-28s %s' "${host#https://}" "$code"
  case "$code" in
    200) say "  serving" ;;
    401) say "  serving (auth in front of healthz)" ;;
    530) say "  CLOUDFLARE 530: the tunnel is up, the ORIGIN is not. The pi is down."; up=0 ;;
    502) say "  502: tunnel up, service container not reachable behind it."; up=0 ;;
    000) say "  no answer at all: DNS or network from THIS machine."; up=0 ;;
    *)   say "  unexpected"; up=0 ;;
  esac
done
if [ "$up" -eq 0 ]; then
  say
  say "STOPPING. A gate cannot be verified on a service that is not serving."
  say "Nothing below would mean anything. This is not a bug in the gate."
  exit 2
fi

# ------------------------------------------------------------------ bridges
# $1 base url  $2 human name  $3 the sentence that means OUR gate let it through
probe_bridge() {
  local host="$1" name="$2" through="$3"
  local user="crossplay-gate-probe-$STAMP@example.invalid"
  local body
  say
  rule
  say "$name  (${host#https://})"
  rule
  say "  posting a bogus sign-in as $user"
  body="$(curl -sS --max-time 45 -X POST "$host/login" \
    --data-urlencode "username=$user" \
    --data-urlencode "password=not-a-real-password-$STAMP" 2>/dev/null)"

  if printf '%s' "$body" | grep -qF "This bridge is invitation-only for now."; then
    say "  SHUT.  Our own gate refused before the credential went anywhere."
    say "         A stranger cannot use this service."
    say "         Fix: the allowlist variable on the pi. Mind WHICH ONE:"
    say "           /srv/readbridge/.env   -> READ_ALLOWLIST"
    say "           /srv/ankibridge/.env   -> BRIDGE_ALLOWLIST"
    say "         They are DIFFERENT NAMES. Setting the other one is silent."
    fail=1
    return
  fi
  if printf '%s' "$body" | grep -qF "$through"; then
    say "  OPEN.  Our gate passed the attempt through and the upstream refused"
    say "         the bogus credential, which is correct. The door is open."
    return
  fi
  if printf '%s' "$body" | grep -qF "Slow down"; then
    say "  INCONCLUSIVE: rate-limited. Wait five minutes and run this again."
    say "  This says nothing about the gate either way."
    inconclusive=1
    return
  fi
  if printf '%s' "$body" | grep -qF "Not configured"; then
    say "  MISCONFIGURED: the bridge has no upstream application credentials."
    say "  The gate may be open; the service still cannot sign anyone in."
    fail=1
    return
  fi
  say "  UNRECOGNISED. The sentence the service actually returned:"
  printf '%s' "$body" | sed -n 's/.*<p class=lede>\(.*\)<\/p>.*/    "\1"/p' | head -3
  say "  Read it before deciding. Do not assume."
  fail=1
}

probe_bridge "$BASE_STUDY" "STUDY  (ankibridge, AnkiWeb)" \
  "AnkiWeb did not accept that email and password."
probe_bridge "$BASE_READ" "READ   (readbridge, Instapaper)" \
  "Instapaper did not accept that email and password."

# ---------------------------------------------------------------- get books
# NOT "open by design". It is HTTP Basic auth with a second, deliberately
# public account for the firmware, and it fails closed the same way the two
# allowlists do: with GETBOOKS_PUBLIC_USER/PASS unset, every shipped reader in
# the world gets 401 while Mario's own credentials keep working perfectly, so
# nothing he does would ever show him the outage.
say
rule
say "GET BOOKS  (${BASE_BOOKS#https://})"
rule
anon="$(curl -sS -o /dev/null -w '%{http_code}' --max-time 25 "$BASE_BOOKS/opds" 2>/dev/null)"
say "  with no credentials at all:            $anon  (401 expected: auth is on)"
pub="$(curl -sS --max-time 25 -u "$BOOKS_USER:$BOOKS_PASS" \
  -w '\n%{http_code}' "$BASE_BOOKS/opds" 2>/dev/null)"
pubcode="$(printf '%s' "$pub" | tail -1)"
say "  with the pair the FIRMWARE ships:      $pubcode"
if [ "$pubcode" = "200" ] && printf '%s' "$pub" | grep -q "<feed"; then
  say "  OPEN.  A stock reader gets a real OPDS feed."
elif [ "$pubcode" = "401" ]; then
  say "  SHUT.  The shipped public pair is refused, so EVERY stranger's reader"
  say "         gets 401 on Get Books. Fix: GETBOOKS_PUBLIC_USER and"
  say "         GETBOOKS_PUBLIC_PASS in /srv/getbooks/.env, matching"
  say "         src/OpdsServerStore.cpp, then docker compose up -d."
  fail=1
elif [ "$pubcode" = "503" ]; then
  say "  MISCONFIGURED: GETBOOKS_USER/GETBOOKS_PASS are unset, so the service"
  say "  refuses everyone including Mario. See its log."
  fail=1
else
  say "  UNRECOGNISED: $pubcode, and the body is not a feed. Read it."
  fail=1
fi

# ------------------------------------------------------------- diagnostics
# Deliberately AFTER the verdict and never part of it. What a container holds
# is not who may sign in; this only tells you WHICH repair you need when the
# verdict above is SHUT.
if [ "$WHY" -eq 1 ]; then
  say
  rule
  say "DIAGNOSTICS (not evidence: this is what the container HOLDS)"
  rule
  for pair in "readbridge:READ_ALLOWLIST" "ankibridge:BRIDGE_ALLOWLIST"; do
    svc="${pair%%:*}"; var="${pair##*:}"
    say "  $svc $var:"
    ssh -o BatchMode=yes -o ConnectTimeout=10 orange \
      "docker exec $svc printenv $var 2>/dev/null || echo '(unset or container down)'" \
      2>&1 | sed 's/^/    /'
  done
  say "  getbooks GETBOOKS_PUBLIC_USER:"
  ssh -o BatchMode=yes -o ConnectTimeout=10 orange \
    "docker exec getbooks printenv GETBOOKS_PUBLIC_USER 2>/dev/null || echo '(unset or container down)'" \
    2>&1 | sed 's/^/    /'
  say
  say "  Reading these against the verdict:"
  say "    verdict SHUT + variable holds *      -> the container was RESTARTED,"
  say "                                            not recreated. docker compose"
  say "                                            restart does NOT re-read .env."
  say "                                            Use: docker compose up -d"
  say "    verdict SHUT + variable empty/absent -> the WRONG NAME was written"
  say "                                            into .env. Compose is silent"
  say "                                            about it whenever the right"
  say "                                            name is still present with"
  say "                                            its old owner-only value."
fi

say
rule
if [ "$inconclusive" -eq 1 ] && [ "$fail" -eq 0 ]; then
  say "INCONCLUSIVE. Something was rate-limited. Run it again in five minutes."
  exit 2
fi
if [ "$fail" -eq 0 ]; then
  say "ALL THREE ARE OPEN TO STRANGERS."
  say
  say "One honest caveat, on read-bridge only: this proves OUR gate is open."
  say "It does not prove Instapaper's application has left owner-only mode,"
  say "because a non-owner 403 prints the same sentence as a wrong password."
  say "Only a real third-party Instapaper account settles that."
  exit 0
fi
say "AT LEAST ONE DOOR IS STILL SHUT. Read the section above that says SHUT."
exit 1
