# Deploying read-bridge for the first time

Written to be executed by Mario, or by a session with his direct word, because
every step below touches his server or publishes a service that accepts other
people's passwords. Nothing here should be improvised from a relayed message.

## Order matters, and this is why

The Instapaper application is in **owner only** mode until its review is
approved. The consumer key works immediately for the account that registered
it and for nobody else. So the first deployment proves the whole path on one
real account, and the service stays closed while it does.

**Both conditions that gated `READ_ALLOWLIST=*` are now met, and this section
no longer says wait.** The Instapaper review was approved (Mario, 2026-09-05,
card #252), and the Cloudflare rules exist (card #292, applied 2026-09-05).
Step 6 is the procedure.

**Say the strength of that second condition honestly rather than repeating
what this file used to promise.** It asked for 20 requests per minute with a
one hour block. What exists is **20 requests per 10 seconds with a 10 second
block**, because `ma-r-s.com` is on the Free plan and neither number is
settable by API or by dashboard (`docs/bridge-security.md`, Rule 1). One
address can therefore sustain about 120 auth POSTs a minute, not 20, and is
free again after ten seconds rather than an hour.

**And the mismatch this file has always contained, which the numbers make
worse:** a per-IP edge rule does not defend against DISTRIBUTED credential
stuffing, which is the threat named two paragraphs above. It never could. An
attacker with a thousand addresses is invisible to a per-IP rule at any
setting. What actually bounds that threat is three things, none of them the
edge rule:

- `GLOBAL_LOGIN` in `bridge/app.py`: 30 sign-ins per minute across the whole
  service, with only one of it. Many addresses do not defeat a global ceiling.
- `LOGIN_LOCKOUT`: exponential backoff per username, counting failures rather
  than attempts, so a spray across accounts is slowed account by account.
- Instapaper and AnkiWeb defend their own accounts. We are not the only thing
  between an attacker and a login, and pretending otherwise sizes our layers
  wrong.

The edge rule's real job is smaller and worth keeping: flood volume stops at
Cloudflare instead of costing a one-worker uvicorn on an ARM box a socket and
a scheduling slot.

## Status: all three services are OPEN to strangers since 2026-09-06

Steps 1-4 are done. `/srv/readbridge` holds the service, both containers are
up, the firewall is installed and enabled, and the isolation test passes from
BOTH vantage points with every private probe timing out and egress to
instapaper.com working. The tunnel is `readbridge`
(`43692df2-e226-4e7f-bc72-72cfdf326572`), its single ingress rule maps
`read.ma-r-s.com` to `http://readbridge:8080`, and the zone holds a proxied
CNAME to `<tunnel-id>.cfargotunnel.com` exactly like `sync` and `books`.

Verified from a laptop, not from a deploy command's exit status:
`/healthz` 200, the sign-in page renders, `POST /api/pair/start` returns a
code and a pollToken, `GET /api/pair/poll` answers `{"pending":true}`, and
`POST /api/pair/abandon` cleans the code up.

**Step 6 was applied on 2026-09-06 and verified from outside.** All three
doors are open; the state each one was actually in is recorded at the end of
step 6, and two of the three were not what this file predicted. What remains
is step 5: nobody has signed in and paired a real reader, on either bridge.

The three hostnames are `read.ma-r-s.com`, `sync.ma-r-s.com` and
`books.ma-r-s.com`. **The Study bridge is `sync`, and `study.ma-r-s.com` does
not exist**: it is not in the zone and does not resolve, so a probe aimed at
it returns nothing at all and reads as an outage rather than as a typo. Name
the host from this list before concluding a service is down.

The kill switch is one command, and it leaves everything else on the box
alone:

    ssh orange 'cd /srv/readbridge && docker compose stop cloudflared'

**A THING THE FIRST DEPLOY GOT WRONG, so the next one does not.** `deploy.sh`
ships the service and starts it; it does NOT install the firewall. The
container came up unconfined and the isolation test caught it reaching the
host's SSH, Immich, the router and a tailnet peer. Install the unit BEFORE or
immediately after the first `docker compose up`, and never treat a green deploy
as an isolated one:

    ssh orange 'sudo /srv/readbridge/scripts/firewall.sh'
    # then the systemd unit, so it survives a reboot, modelled on
    # getbooks-firewall.service and ankibridge-firewall.service
    ssh orange 'sudo systemctl enable --now readbridge-firewall.service'

## 1. Secrets on the pi, and only there

`/srv/readbridge/.env`, mode 600. `deploy.sh` excludes it in both directions,
so it is never copied up and never overwritten.

    ssh orange 'umask 077 && cat >> /srv/readbridge/.env' <<'ENV'
    READ_CONSUMER_KEY=<from instapaper.com/main/request_oauth_consumer_token>
    READ_CONSUMER_SECRET=<same page>
    READ_ALLOWLIST=<mario's instapaper address>
    ENV
    ssh orange 'chmod 600 /srv/readbridge/.env && ls -l /srv/readbridge/.env'

Optional, for the board's numbers (`docs/workflow/events.md`; the values are
the URL and ANON key in `<workspace>/.board/supabase.env`, never the service
role key). Without both, the service posts nothing and logs so once:

    SUPABASE_URL=https://<project>.supabase.co
    SUPABASE_ANON_KEY=<the public anon key>

`READ_FERNET_KEY` is generated ON the pi and never leaves it:

    ssh orange "docker run --rm python:3.13-slim sh -c \
      'pip -q install cryptography && python -c \
      \"from cryptography.fernet import Fernet;print(Fernet.generate_key().decode())\"'"

## 2. Ship and start

    bash server/read-bridge/scripts/deploy.sh

## 3. Verify the isolation, every deploy and not only firewall changes

    ssh orange 'bash -s' < server/read-bridge/scripts/isolation_test.sh

Every private-network probe must time out and egress to instapaper.com must
work. Nonzero exit is a breach; do not continue.

## 4. The hostname, and the reason it is not the one this file used to name

`read.ma-r-s.com` into the tunnel, the way `sync.ma-r-s.com` was. The firmware
constant is `kBridgeHost` in `src/apps_local/instapaper/InstapaperSync.h`, and
the device build has NO override -- it can only ever reach that one name.

**It said `read.crossplay.ma-r-s.com` until 2026-09-03, and that name can
never work on this account.** ma-r-s.com is on Cloudflare's free plan, whose
Universal SSL certificate covers exactly `ma-r-s.com` and `*.ma-r-s.com`: ONE
label. The edge answers a TLS handshake for a deeper name with alert 40 and no
certificate at all, which you can see without deploying anything:

    echo | openssl s_client -connect 104.21.2.163:443 \
      -servername read.crossplay.ma-r-s.com
    # ... tls alert handshake failure ... no peer certificate available

So the DNS record was never the missing piece on its own. A CNAME for that
name would have resolved and then failed TLS in the browser and on the device,
which is a worse failure than a missing record because it looks like a broken
service rather than an unfinished one. Covering `*.crossplay.ma-r-s.com` needs
Advanced Certificate Manager, which is paid; a one-label name is free and is
what both working siblings already use.

Two consequences worth carrying:

- **`app/crossplayhosts` would break Study sync and Get Books the same way.**
  It moves `sync.ma-r-s.com` to `sync.crossplay.ma-r-s.com` and
  `books.ma-r-s.com` to `books.crossplay.ma-r-s.com`. Both are two labels
  deep. Do not ship it without buying the certificate first.
- The certificate now served for `read.ma-r-s.com` is the SAME certificate,
  same serial, that `sync.ma-r-s.com` presents -- and the device's wolfSSL
  root store is already proven against that chain by Study sync on hardware.
  One less unknown in step 5.

Created with the Cloudflare REST API (the `cf` CLI has no `tunnel` command;
its stored OAuth token works as a Bearer):

    POST /accounts/<acct>/cfd_tunnel            {"name":"readbridge","config_src":"cloudflare"}
    PUT  /accounts/<acct>/cfd_tunnel/<id>/configurations
         {"config":{"ingress":[{"hostname":"read.ma-r-s.com","service":"http://readbridge:8080"},
                               {"service":"http_status:404"}],"warp-routing":{"enabled":false}}}
    POST /zones/<zone>/dns_records              CNAME read -> <id>.cfargotunnel.com, proxied

The tunnel token goes into `/srv/readbridge/.env` and nowhere else. Pipe it;
do not paste it, and do not let it reach a terminal:

    GET /accounts/<acct>/cfd_tunnel/<id>/token  |  ssh orange 'umask 077 && cat >> /srv/readbridge/.env'

## 5. Then, and only then, the thing nobody has run

Sign in at `https://read.ma-r-s.com` with the owner's account, pair a reader,
and sync. That path has never executed: the wolfSSL handshake, the ~35KB heap
floor it needs, certificate verification against Cloudflare's chain, and the
four assumptions about Instapaper's API in `docs/apps/instapaper-plan.md`.

Study's equivalent cost three flags and a `-232` that looked like a curve
problem and was a missing hash. Expect to spend time here rather than none.

## 6. Open it (card #348, from GitHub issue #115)

Both preconditions are met; see the top of this file for what the second one
actually delivers. This step is **config, not code**. Nothing in `bridge/` is
edited, nothing is rebuilt, and `deploy.sh` is not the route: a `.env` change
is a recreate.

### THERE ARE THREE DOORS, NOT ONE, AND THEY HAVE THREE DIFFERENT NAMES

Issue #115 reports the Study one. It is one report of a condition affecting
all three services, because each fails closed on its own unset variable and a
shut door is invisible to everybody who already has access:

| Service   | On the pi         | Variable                                        |
| --------- | ----------------- | ----------------------------------------------- |
| Read      | `/srv/readbridge` | `READ_ALLOWLIST`                                |
| Study     | `/srv/ankibridge` | `BRIDGE_ALLOWLIST`                              |
| Get Books | `/srv/getbooks`   | `GETBOOKS_PUBLIC_USER` + `GETBOOKS_PUBLIC_PASS` |

**`READ_ALLOWLIST` and `BRIDGE_ALLOWLIST` are different names for the same
idea and this is the whole trap of this step.** Write the wrong one into the
wrong `.env` and: the file is edited, compose is silent, the container comes
up, the healthcheck passes, `/healthz` answers 200, and every stranger still
reads "This bridge is invitation-only for now." Compose only warns when the
CORRECT name is absent entirely, and it is not absent: both currently hold an
owner address. So a wrong-name edit produces no warning, no log line, and no
symptom Mario can see, because his own account keeps working either way.

Get Books is the same shape wearing different clothes. It is **not** open by
design: `getbooks/app.py` puts HTTP Basic auth in front of everything but
`/healthz`, with a second, deliberately public account for the firmware. The
pair CrossPlay ships is in `src/OpdsServerStore.cpp` (`crossplay` /
`r4ulp-zm4cg-awjtf-z5zfj`, public on purpose: it is in a public repo and in
every release binary). With `GETBOOKS_PUBLIC_USER`/`PASS` unset, every stock
reader in the world gets 401 while Mario's own credentials work perfectly.
`board pulse` counts that 401 as ALIVE, so the board would show it green.

### Why `*` and not an enumerated list

`*`. An enumerated list cannot serve "anyone in the world with the OS
installed", which is the objective: there is no registration flow and no way
onto the list except opening a GitHub issue and waiting for Mario, which is
precisely the failure #115 reports. The list is also not the security control
and never was -- it is a binary switch. The controls are `LOGIN_IP`,
`GLOBAL_LOGIN` and `LOGIN_LOCKOUT`, `CLAIM_IP`/`CLAIM_USER`, the edge rule,
and the upstreams authenticating their own accounts. Keeping a list would add
a per-user manual edit across two files with two different variable names,
which is a drift generator, not a defence.

### Apply

Edit in place on the pi, in an editor, on the box. The `.env` files belong to
the ssh user (`deploy.sh` writes `/srv/<service>/` without sudo), so no sudo is
needed. **REPLACE the existing line rather than appending a second one:** a
`.env` with a key twice is legal and compose takes the last occurrence, which
is a fine way to leave a file nobody can read confidently.

    ssh orange     # then, on the box, one editor session per file:
    #  /srv/readbridge/.env   READ_ALLOWLIST=*            (was: owner address)
    #  /srv/ankibridge/.env   BRIDGE_ALLOWLIST=*          (was: owner address)
    #  /srv/getbooks/.env     GETBOOKS_PUBLIC_USER=crossplay
    #                         GETBOOKS_PUBLIC_PASS=<the pair in OpdsServerStore.cpp>

**Do not `export READ_ALLOWLIST=*` in a shell.** In a `.env` file the `*` is a
literal; in a shell it is a glob and expands to whatever files are in the
directory, which would write a nonsense allowlist that fails closed and looks
exactly like every other way of getting this wrong.

Then recreate. **`docker compose restart` is the wrong command and it fails
silently:** it restarts the existing container with the environment it was
created with, so the edit appears to do nothing. `up -d` re-interpolates
`.env`, sees a changed service config, and recreates. No `--build`: the image
has not changed.

    ssh orange 'cd /srv/readbridge && docker compose up -d'
    ssh orange 'cd /srv/ankibridge && docker compose up -d'
    ssh orange 'cd /srv/getbooks   && docker compose up -d'

### Verify from OUTSIDE, as a stranger

A deploy's exit status, a 200 on `/healthz`, a passing container healthcheck
and `printenv` inside the box are ALL compatible with the door still shut.
The only question worth asking is the one a stranger asks:

    server/verify_open.sh          # the verdict
    server/verify_open.sh --why    # then diagnostics, if a door is shut

It posts one deliberately bogus sign-in per bridge from outside and reads the
sentence that comes back, because the two failures look alike and mean
opposite things:

- "This bridge is invitation-only for now." -- OUR gate refused. Door shut.
- "AnkiWeb / Instapaper did not accept that email and password." -- our gate
  passed it through and the upstream refused a bogus key. Door OPEN.

`server/verify_open_selftest.sh` proves that classifier can reach every one of
its verdicts, including the two above, against a local stub.

**One thing `verify_open.sh` cannot settle, on read-bridge only.** If the
Instapaper application were still in owner-only mode, a non-owner xAuth
returns 403 and the bridge prints the same "did not accept" sentence as a
wrong password. So a green run proves OUR gate is open; only a real
third-party Instapaper credential proves a stranger can actually finish
signing in. Do not report the first as the second.

Replying on GitHub issue #115 is Mario's, not a session's.

### What the three doors ACTUALLY held when the box came back (2026-09-06)

Recorded because two of the three contradicted what this file and card #348
both predicted, and because the prediction was the reason the step was sized
as three edits rather than one.

| Service   | Variable                              | Before                                   | After |
| --------- | ------------------------------------- | ---------------------------------------- | ----- |
| Read      | `READ_ALLOWLIST`                      | `*` already, set 2026-09-04              | `*`   |
| Study     | `BRIDGE_ALLOWLIST`                    | the owner's address only. THE ONLY SHUT DOOR | `*` |
| Get Books | `GETBOOKS_PUBLIC_USER` / `_PASS`      | already `crossplay` / the shipped pair   | unchanged |

So exactly one variable needed writing. Read had been opened two days earlier
and this file still said it was "untouched and still the owner's address
only"; Get Books already carried the pair from `src/OpdsServerStore.cpp`
byte for byte. **A runbook's own status line is a claim about a machine, not
a reading of it.** Read the three `.env` files and the three RUNNING
containers before deciding how big this step is.

Verified after the change, from a laptop over the public internet:

    server/verify_open.sh   ->  ALL THREE ARE OPEN TO STRANGERS  (exit 0)

with `server/verify_open_selftest.sh` run in the same session to prove the
classifier still reaches its SHUT verdicts, because an all-green from a probe
nobody has watched fail is not evidence.

The container is the thing to check, not only the file. `docker inspect
<name> --format '{{range .Config.Env}}{{println .}}{{end}}'` and `docker exec
<name> printenv <VAR>` must AGREE; when they disagree the container predates
the edit and needs `up -d`. Both said `*` afterwards, and ankibridge's
`Created` timestamp moved, which is what distinguishes a recreate from the
`restart` that would have changed nothing.

**Isolation was re-tested after the recreate**, because a new container is a
new network namespace and the firewall units were installed against the old
one. Both `scripts/isolation_test.sh` pass with every private probe timing
out and egress working, and all three `*-firewall.service` units are enabled
and active after the reboot.

Still NOT verified, and not to be reported as if it were: no stranger has
completed a sign-in on either bridge. `verify_open.sh` proves our gate lets
the attempt through, and on read-bridge a non-owner Instapaper 403 is
indistinguishable from a wrong password.

## Before any of it: can this machine even reach the pi?

    ssh orange 'echo up'                          # the deploy path
    curl -sS https://sync.ma-r-s.com/healthz      # is the pi serving at all?

Those answer different questions and on 2026-08-31 they disagreed, which sent
one session to the wrong conclusion. The Anki bridge answered 200 -- the box
was up, cloudflared was up, it was serving the public internet -- while `ssh
orange` could not resolve, because this machine had no route to it under that
name: no `~/.ssh/config` entry, and `known_hosts` holding 192.168.68.x,
192.168.1.x and tailscale 100.x addresses while the machine sat on
192.168.20.0/24.

So: a failing `ssh` says nothing about the pi. Check the tunnel before
concluding anything about the box, and read the failure carefully: a DNS error
on `read.ma-r-s.com` means the record is gone, a TLS alert means the name is
too deep for the zone's certificate (step 4), a **530 means the tunnel is up
and the ORIGIN is not** (the box is off, which is where it was on 2026-09-05),
and a 502 means the tunnel is up but cannot reach the service container. Those
are four different repairs.

**The name resolves through Tailscale, and there is no `~/.ssh/config` entry**
-- checked 2026-09-05, because this file records a session losing time to
exactly that. `orange` is `orange.tail77e8d2.ts.net`, `100.75.152.70`, via
MagicDNS. So if `ssh orange` ever fails to RESOLVE rather than to connect, the
repair is the tailnet on this Mac, not the pi and not `known_hosts`.
