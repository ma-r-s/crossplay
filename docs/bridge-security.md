# Hardening the two bridges

`server/read-bridge` (Instapaper, **read.ma-r-s.com**) and `server/study-bridge`
(AnkiWeb, **sync.ma-r-s.com**) are open to anyone on the internet and their
source is public. Every number below -- the rate limits, the code alphabet, the
lockout windows, the token lifetimes -- is readable by whoever is attacking
them. That is how every real service works, and it means **nothing here may
depend on any of it being unknown.**

This is the whole picture in one place: what the threat model is, what each
layer refuses, what Mario has to click in Cloudflare, and what is still not
covered. `server/read-bridge/README.md` and `server/study-bridge/README.md`
describe the services; this file is only about attacking them.

## The threat model, which is narrower than it looks

Both services do the same three things, and there is nothing else on their
surface:

1. take a password once, exchange it upstream for a token, **never store the
   password** (`bridge/accounts.py` in both),
2. pair a device: the reader shows an 8-character code, a signed-in human
   claims it in a browser, the reader asks for a button press before it stores
   anything (`bridge/pairing.py`, byte-identical twins),
3. sync, and serve files, to a device holding a bearer token.

So the things worth attacking are: the **sign-in** endpoint, which is a
credential-stuffing oracle by construction; the **pairing** flow; the **device
token** path; and whatever the **session cookie** authorises. What is NOT in
the model: the box itself (see each service's `scripts/firewall.sh` and
`isolation_test.sh`, and the `a-green-deploy-is-not-an-isolated-one` memory),
and the upstream accounts themselves.

## Three layers, and each one does a different job

Confusing them is how a limit ends up in the wrong place.

| Layer                          | Answers                                         | Where                 |
| ------------------------------ | ----------------------------------------------- | --------------------- |
| Cloudflare rate limiting rules | "should this request reach the pi at all?"      | dashboard, below      |
| in-process limiters            | "is this account or address being abused?"      | `bridge/app.py`       |
| the isolation firewall         | "if the container is owned, what can it touch?" | `scripts/firewall.sh` |

The edge layer exists because the origin is a small ARM box running one uvicorn
worker with `pids_limit` 128 (read) / 256 (study). Volume that the app would
refuse still costs it a socket, a thread and a scheduling slot; the edge is what
makes that volume free.

The app layer exists because Cloudflare cannot see accounts. A spray that uses
each address once is invisible to a per-IP rule and obvious to a per-account
one, and vice versa. Neither layer replaces the other.

## What the app refuses today

Numbers and their reasons, both services (they are twins, and were made twins
on 2026-09-05 -- study-bridge had the weaker half of every pair):

| Limiter                       | Setting                                                       | Why that number                                                                                                                                                                                                                                                                                                                                                                                    |
| ----------------------------- | ------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `LOGIN_IP`                    | 5 / 5 min / address                                           | A person signs in once. Five covers a mistype and a password-manager retry.                                                                                                                                                                                                                                                                                                                        |
| `LOGIN_LOCKOUT`               | 2 free failures, then 30s doubling to 1h, forgotten after 24h | Counts **failures**, not attempts, so a person who signs in correctly is never slowed however often they do it, and an attacker is slowed by the thing that identifies them. Checked _before_ the upstream call, so a locked-out username costs Instapaper/AnkiWeb nothing.                                                                                                                        |
| `GLOBAL_LOGIN`                | 30 / min, service-wide                                        | The ceiling with only **one** of it. Per-IP and per-username counters are both defeated by having many of each, which is exactly what a credential-stuffing run has. Set far above any real rate, so it is a backstop and not a throttle. It does mean a flood can deny sign-in to everyone while it lasts: a bounded oracle that is briefly unavailable beats an unbounded one that is always up. |
| `CLAIM_IP` / `CLAIM_USER`     | 20 / 5 min each                                               | Guessing a pairing code. 32^8 codes over a five-minute life is not guessable; guessing **for free** was the problem, and until 2026-09-05 the claim endpoint answered unlimited wrong codes to anyone with an account of their own.                                                                                                                                                                |
| `PAIR_IP`                     | 10 / 5 min                                                    | Starting pairings. A person pairs a reader once.                                                                                                                                                                                                                                                                                                                                                   |
| `REPORT_IP` / `GLOBAL_REPORT` | 30 / 5 min, 240 / min                                         | Device crash reports ride the `X-CrossPlay-Report` header of any response under 400 -- including `/healthz`, which takes no token. Uncapped, that is a stranger writing rows to the board as fast as they can open sockets, one thread each.                                                                                                                                                       |
| `SYNC_USER` / `GLOBAL_SYNC`   | 6 / 5 min, 60 / min                                           | A sync is expensive; a reader syncs on a schedule.                                                                                                                                                                                                                                                                                                                                                 |

There is deliberately **no** flat per-username window beside `LOGIN_LOCKOUT`.
read-bridge had one and it shadowed the lockout completely -- both keyed on the
username, the flat one fired first, and its cruder message was the only thing a
locked account ever saw. Two limiters on one key, the weaker winning, is worse
than either alone.

### The assumption under every per-IP limit

`client_ip()` trusts the `CF-Connecting-IP` header. That is correct **only**
because cloudflared is the sole route to the origin and Cloudflare overwrites
that header on the way in. Nothing is published on the host and the compose
network is not reachable from the LAN, so today the assumption holds.

It is written down here because it is load-bearing and invisible: if the origin
were ever exposed directly, every per-IP limit becomes worthless in one line of
curl. `GLOBAL_LOGIN` is what still stands in that case, which is the reason it
exists rather than being redundant with the per-IP rule.
`server/attacks.py` spoofs the header on purpose for exactly this reason.

## Cloudflare: what Mario has to click

**Nothing here can be done from this repository.** The tunnel is TOKEN-managed,
so its routes and the zone's rules live in the dashboard rather than in a file
on the pi. There IS an API token now, written 2026-09-05 when Rule 1 was
applied: `~/.cf-waf-token`, mode 600, scoped **Zone WAF: Edit only**. It can
write rules and cannot list zones or accounts, so hold the zone id
(`bd1d38d600c956607a4b5f0c2f79c674`) -- nothing can rediscover it with that
token. Everything below can be done by API or by hand; the dashboard offers the
same entitlements, never more.

Go to **dash.cloudflare.com > the `ma-r-s.com` zone > Security > WAF > Rate
limiting rules**, and add:

### Rule 1 (the one that matters): authentication endpoints

- **Name**: `bridge auth`
- **If incoming requests match** (use the _Edit expression_ box):

  ```
  (http.host in {"read.ma-r-s.com" "sync.ma-r-s.com"}
   and http.request.method eq "POST"
   and http.request.uri.path in {"/login" "/api/pair/claim" "/api/pair/start"})
  ```

- **Characteristics**: IP
- **Rate**: 20 requests per 10 seconds
- **Action**: Block, **Duration** 10 seconds
- **Response**: default (429)

  **These are not the numbers this section used to specify, and the difference
  is the Free plan, not a preference.** It said 20 requests per 1 minute,
  blocked for 1 hour. Neither is settable: the API refuses any period but 10
  seconds (`not entitled to use the period 60, can only use a period among
[10]`) and any mitigation timeout different from the period (`not entitled
to use a mitigation timeout different from 10`). The dashboard offers the
  same choices, because it is the same entitlement. Applied 2026-09-05 by API;
  the rule is live in the zone.

  What that costs, stated plainly so nobody reads more protection into this
  than it has: a single IP can sustain about 120 POSTs a minute to the auth
  paths instead of 20, and a blocked address is free again after ten seconds
  instead of an hour. What it still buys is the thing this layer exists for --
  flood volume stops at Cloudflare instead of costing the pi a socket and a
  scheduling slot. The design intent survives intact in the direction that
  matters: a real person does one to three of these requests, so at twenty per
  ten seconds this rule still only ever fires on traffic the app was already
  going to refuse.

  The app-layer limiters are therefore doing more of the work than this
  section originally implied. `CLAIM_IP` at 20 per 5 minutes is now the
  binding constraint on a slow brute force, not the edge rule. If the zone
  ever moves to Pro, restore 20/minute and a 1 hour block here first.

Twenty POSTs to those three paths in one minute is not a person: a real sign-in
is one request, a real pairing is one claim and one start. It is four times the
app's own per-IP sign-in allowance, deliberately -- the app should be the thing
that shapes a legitimate person's experience, and this rule should only ever
fire on traffic the app was already going to refuse. Its value is that the
traffic stops at Cloudflare instead of at the pi.

**On the Free plan you get one rate limiting rule per zone.** That is why the
expression covers three paths and both hostnames at once rather than being
three tidy rules. If the zone is on Pro or above, split it: `/login` at 20/min,
`/api/pair/claim` at 30/min, `/api/pair/start` at 30/min, and add rule 2.

### Rule 2 (Pro and above only): the device API

- **Name**: `bridge api volume`
- **If**: `(http.host in {"read.ma-r-s.com" "sync.ma-r-s.com"} and starts_with(http.request.uri.path, "/api/"))`
- **Characteristics**: IP
- **Rate**: 600 requests per 1 minute
- **Action**: Block, Duration 10 minutes

Sized from what a real sync does, not from a round number: a first Instapaper
sync downloads one file per article and Mario's own account was 21 of them, so
a reader can legitimately burst 25-30 requests, and a household with two
readers on one address can do that several times an hour. 600/min leaves an
order of magnitude of headroom and still stops a flood dead.

### Two things NOT to turn on

- **Bot Fight Mode** (Security > Bots). It challenges non-browser clients, and
  the readers _are_ non-browser clients: they speak plain HTTPS from wolfSSL
  with no JavaScript. Turning it on breaks every sync on every device, and it
  will look like the bridge is down.
- **A managed challenge action on any `/api/` rule**, for the same reason. For
  API paths the action must be **Block**. Challenges are fine on the human
  pages (`/`, `/pair`, `/devices`) if a rule is ever wanted there.

### After changing anything

Nothing in this repository can read the zone's configuration back, so the only
verification is behavioural:

```bash
cd server/read-bridge && .venv/bin/python tests/attack_test.py --base https://read.ma-r-s.com
cd server/study-bridge && .venv/bin/python tests/attack_test.py --base https://sync.ma-r-s.com
```

That runs the subset of the attack suite which is safe against a service people
are using. It does **not** run the floods -- they would spend a shared rate
limiter and lock real accounts out -- so it cannot confirm the rule fires. To
confirm that, watch **Security > Events** in the dashboard while making the
requests, or accept that this one is unverified from here and say so.

## The gates, and why a shut one is invisible

Three services face the public, and each one is opened by a single environment
variable on the pi that **fails closed**. They do not share a name:

| Service   | On the pi         | Variable                                        |
| --------- | ----------------- | ----------------------------------------------- |
| Read      | `/srv/readbridge` | `READ_ALLOWLIST`                                |
| Study     | `/srv/ankibridge` | `BRIDGE_ALLOWLIST`                              |
| Get Books | `/srv/getbooks`   | `GETBOOKS_PUBLIC_USER` + `GETBOOKS_PUBLIC_PASS` |

Get Books belongs on that list even though it has no allowlist: `getbooks/app.py`
puts HTTP Basic auth in front of everything but `/healthz`, and the second,
deliberately public account is what every shipped reader uses
(`src/OpdsServerStore.cpp`). Unset it and the whole world gets 401.

**A shut gate is invisible to every instrument we have.** `docker compose up`
exits 0, `/healthz` answers 200, the container healthcheck passes, `board pulse`
even counts Get Books' 401 as ALIVE, and the owner's own account keeps working
because he is the one on the list. Writing the WRONG variable name into a `.env`
produces no warning at all whenever the right name is still present with its old
value, which it always is. GitHub issue #115 is what a shut gate looks like from
outside: a stranger, and no signal on our side at all.

**Current state, 2026-09-06: all three are OPEN.** `READ_ALLOWLIST=*`,
`BRIDGE_ALLOWLIST=*`, and `GETBOOKS_PUBLIC_USER`/`_PASS` holding the pair
`src/OpdsServerStore.cpp` ships. Only the Study one was shut when the box came
back; the other two had been open for days while the runbook said otherwise.
Do not take this paragraph as the reading either. Run the script.

So the gates are verified the way a stranger experiences them:

```bash
server/verify_open.sh          # one bogus sign-in per bridge, from outside
server/verify_open_selftest.sh # proves that classifier can reach every verdict
```

The distinction it exists to draw: "This bridge is invitation-only for now."
means our gate refused, and "AnkiWeb / Instapaper did not accept that email and
password." means our gate passed the attempt through and the upstream refused a
deliberately bogus key. Those look alike and mean opposite things.

**A closed allowlist also blinds the live attack run below**, for the same
reason the local suite sets the allowlist to `*`: a bridge that refuses every
attempt at the door reports a clean run for a service with no rate limiting at
all. Opening the gates makes that suite meaningful for the first time.

## The attack suite

`server/attacks.py` is one checklist run against both services. One file, not
two, because every security bug found in these bridges so far has been a fix
that landed on one twin and not the other -- including the cross-user traversal
that this suite was written to catch.

```bash
server/read-bridge/.venv/bin/python  server/read-bridge/tests/attack_test.py
server/study-bridge/.venv/bin/python server/study-bridge/tests/attack_test.py
server/verify_attacks.sh          # the matrix: watch every check go red
```

Both deploy scripts run the hermetic form **before** they ship anything, so a
vulnerable service is never deployed. That follows the precedent set by
`isolation_test.sh`: a claim about safety that nothing runs is not a claim.

`server/verify_attacks.sh` is the part that makes the suite worth having. For
each entry in `server/weaken.py` it breaks the service on purpose, runs the
suite, and asserts that **exactly** the checks that weakening claims went red --
both directions, because a check that stays green under its own weakening does
not work, and a check that reddens under a weakening it does not claim means the
map from "this went red" to "this is broken" is wrong. Run it whenever a check
is added or changed.

## What is not covered, and should be said out loud

- **The Cloudflare rules are unverified from here.** Nothing in the repository
  can read the zone back. If Mario does not click them, the app-layer limits
  are the only ones there are -- which is a real defence, just a more expensive
  one for the pi.
- **The live services have not been attacked.** Every result above is against
  the real ASGI app with a real fake upstream, on a developer machine. The
  `--base` mode exists for the live run and its safe subset is a subset.
- **In-memory limiters die with the process.** A deploy or a reboot forgets
  every counter and every lockout, so a restart loop is a way to reset the
  backoff. The services are pinned to one worker (the per-user asyncio mutex
  stops being a mutex above one), so there is no cross-worker gap, but there is
  a cross-restart one. Fixing it means persisting the lockout, which is a
  different change and is not made here.
- **`/api/pair/poll` is deliberately unlimited.** A reader polls it every
  second or two for up to five minutes while a human types the code, so any
  rate limit tight enough to matter would break pairing. Its token is 24 random
  bytes and is not guessable; the exposure is request volume, which is what the
  edge rule is for.
- **Fernet-at-rest buys nothing against a live compromised box** -- the key is
  in the environment next to the data. It exists so that backups and stray
  copies of a data directory carry no usable credentials. Both `accounts.py`
  files say so; it is repeated here so nobody reads "encrypted at rest" as more
  than it is.
