# Readbridge

The Instapaper half of the reader's read-later app: a FastAPI service
(uvicorn, port 8080 inside the container, `bridge.app:app`) that holds a
user's Instapaper OAuth token, asks Instapaper what changed, turns article
HTML into the flat text the panel draws, and serves it to the device. Runs on
the Orange Pi at `/srv/readbridge`, published at **https://read.ma-r-s.com**
through the `readbridge` Cloudflare Tunnel.

The host is ONE label below the apex on purpose. The zone's free-plan
Universal SSL certificate covers `ma-r-s.com` and `*.ma-r-s.com` and nothing
deeper, so a name like `read.crossplay.ma-r-s.com` gets no certificate and
fails the TLS handshake outright. See `scripts/DEPLOY-RUNBOOK.md` step 4.

The design and its arguments live in `docs/apps/instapaper-plan.md`. The two
things worth knowing before reading any code here:

- **There is no mirror.** Instapaper's `have` parameter makes the device's own
  index the delta input, so this service stores a credential and a cache of
  converted text and nothing else that could drift out of step with the
  account.
- **It cannot delete anything.** `bookmarks/delete` is not wrapped, not
  proxied, and not reachable from the device protocol. `tests/test_api.py`
  asserts it was never called.

## What it needs to run

An **Instapaper OAuth consumer token**, from instapaper.com. Without
`READ_CONSUMER_KEY` / `READ_CONSUMER_SECRET` the service starts, logs the
reason loudly, and refuses sign-in with a sentence rather than crash-looping.

**Registration is instant; the human review gates OTHER PEOPLE only.** This
file used to say the token itself waits on a review, and that was wrong in a
way that cost real time: two sessions planned around a blocker that did not
exist. A newly registered application is in **owner only** mode -- the keys
work immediately for the account that registered them, and for nobody else
until a separate "Submit for review" is approved.

That distinction decides the deployment order, so it is worth stating as a
rule rather than a fact: **`READ_ALLOWLIST` must stay the owner's address
until the review is approved.** Opening it to `*` beforehand lets strangers
reach the sign-in endpoint with credentials that CANNOT succeed -- Instapaper
will refuse every one of them -- so it buys nothing and exposes the one
endpoint that is a credential-stuffing oracle by construction. All risk, no
capability. Open it when the review lands, and not before.

## Suites

```sh
uv venv .venv && uv pip install --python .venv/bin/python -r requirements.txt
.venv/bin/python tests/test_oauth.py     # signing, against RFC 5849's vector
.venv/bin/python tests/test_article.py   # HTML -> flat text, rule by rule
.venv/bin/python tests/test_engine.py    # the three silent-failure rules
.venv/bin/python tests/test_api.py       # the whole surface, end to end
.venv/bin/python tests/test_events.py    # the board poster, HTTP stubbed
```

`tests/fake_instapaper.py` stands in for the real API and **verifies OAuth
signatures**, so the suites prove the signing without a consumer key or a
network. `check.sh` runs all four and FAILS rather than skips when it cannot.

One more, deliberately outside the gate because it builds and drives the
simulator:

```sh
tests/sim_stack.sh      # the reader, this bridge and the fake, end to end
```

It runs the pairing handshake for real -- the simulator shows a code, the
script claims it over HTTP as the signed-in human, the simulator confirms with
a button press -- then syncs and checks what landed on the card. That is the
only thing here that exercises the DEVICE's half of the protocol; the suites
above drive the bridge with curl and would pass against a firmware that
composed its `have` string wrongly.

## Isolation

The box also runs Immich, Jellyfin, Getbooks, Ankibridge and the *arr stack,
so this container is treated as one that will eventually be compromised.

- runs as uid 10003 (10001 getbooks, 10002 ankibridge), `read_only` root
  filesystem, `cap_drop: ALL`, `no-new-privileges`, capped memory/pids/cpu
- its own bridge on a pinned subnet (`172.31.85.0/24`), nothing published on
  the host: cloudflared reaches it over the compose network
- `scripts/firewall.sh` drops traffic from that subnet to every RFC1918
  range, the tailnet and link-local, on **both** `DOCKER-USER` and `INPUT` --
  host services never traverse FORWARD, so DOCKER-USER alone leaves SSH open.
  Install it as `readbridge-firewall.service` so it survives reboots.
- DNS pinned to 1.1.1.1/9.9.9.9, because walling off the host otherwise
  breaks name resolution through Docker's embedded resolver.

Verify after **every** deploy, not just firewall changes:

```sh
ssh orange 'bash -s' < scripts/isolation_test.sh
```

Every private-network probe must time out and egress to instapaper.com must
work; the script exits nonzero on any breach. It probes from **two** vantage
points: inside the service container, and from a throwaway container on
cloudflared's pinned subnet. cloudflared is distroless and cannot probe from
inside itself, and it is the container facing the internet, so the run that
covered only the service was reporting clean about the half nobody had
checked.

## Operating it

```sh
./scripts/deploy.sh                                             # ship and rebuild
ssh orange 'cd /srv/readbridge && docker compose logs -f'       # sync activity
ssh orange 'cd /srv/readbridge && docker compose stop cloudflared'  # kill switch
```

## Events

Every finished sync posts one event to the board (`docs/workflow/events.md`):
`instapaper`/`sync` with `{articles, seconds}`, or the same event at level
`error` with `{message}` when the job was refused or died. It is counted
under the device's own id when the request carried one (`X-CrossPlay-Device`,
with `X-CrossPlay-Board` and the version from the User-Agent, and the
report's `battery_pct`, `heap_min_kb`, `uptime_h` copied into the props),
else under a salted hash of the account id. Whatever the device had to
report rides the same headers on every request, so a middleware reads them
on every accepted answer and posts `firmware`/`crash` and `firmware`/`update`
events for a crash or an OTA attempt the report carries
(`events.Client.report`). `bridge/events.py` sends from its own thread with
a 3 s timeout and drops the event after one log line if the board does not
take it, so a board outage cannot slow or fail a sync (`tests/test_api.py`
proves both, and the header bodies). The module is a byte-identical twin of
`study-bridge/bridge/events.py`; `tests/test_events.py` fails if the two
drift.

Where to post comes from two more `.env` keys, both optional:

```sh
SUPABASE_URL=https://<project>.supabase.co
SUPABASE_ANON_KEY=<the public anon key, the one that can only insert>
```

They are the URL and ANON key from `<workspace>/.board/supabase.env` (never
the service role key). With either missing the service runs exactly as
before and logs `events are off` once at startup. To turn them on: append
the two lines to `/srv/readbridge/.env` on the pi, ship this code with
`./scripts/deploy.sh` (once it is running there, a bare `ssh orange 'cd
/srv/readbridge && docker compose up -d'` is enough, because an `.env`
change is a recreate and not a rebuild), then check for `events on` in
`docker compose logs readbridge`.

## Secrets

`.env` lives on the pi only, at `/srv/readbridge/.env`, mode 600, never in
git, never rsync'd in either direction (`deploy.sh` excludes it). Keys:
`READ_FERNET_KEY`, `READ_ALLOWLIST`, `READ_CONSUMER_KEY`,
`READ_CONSUMER_SECRET`, `CLOUDFLARE_TUNNEL_TOKEN`, and the optional
`SUPABASE_URL` / `SUPABASE_ANON_KEY` pair above.

Generate the Fernet key **on the pi** so it never lands in a transcript:

```sh
ssh orange "docker run --rm python:3.13-slim sh -c \
  'pip -q install cryptography && python -c \
  \"from cryptography.fernet import Fernet;print(Fernet.generate_key().decode())\"'"
```

`READ_ALLOWLIST` fails closed: unset means nobody may sign in. `*` opens it.
