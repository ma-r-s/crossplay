# Readbridge

The Instapaper half of the reader's read-later app: a FastAPI service
(uvicorn, port 8080 inside the container, `bridge.app:app`) that holds a
user's Instapaper OAuth token, asks Instapaper what changed, turns article
HTML into the flat text the panel draws, and serves it to the device. Runs on
the Orange Pi at `/srv/readbridge` behind a Cloudflare Tunnel.

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

An **Instapaper OAuth consumer token**, which is a form on instapaper.com and
a human review. Without `READ_CONSUMER_KEY` / `READ_CONSUMER_SECRET` the
service starts, logs the reason loudly, and refuses sign-in with a sentence
rather than crash-looping.

## Suites

```sh
uv venv .venv && uv pip install --python .venv/bin/python -r requirements.txt
.venv/bin/python tests/test_oauth.py     # signing, against RFC 5849's vector
.venv/bin/python tests/test_article.py   # HTML -> flat text, rule by rule
.venv/bin/python tests/test_engine.py    # the three silent-failure rules
.venv/bin/python tests/test_api.py       # the whole surface, end to end
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
work; the script exits nonzero on any breach.

## Operating it

```sh
./scripts/deploy.sh                                             # ship and rebuild
ssh orange 'cd /srv/readbridge && docker compose logs -f'       # sync activity
ssh orange 'cd /srv/readbridge && docker compose stop cloudflared'  # kill switch
```

## Secrets

`.env` lives on the pi only, at `/srv/readbridge/.env`, mode 600, never in
git, never rsync'd in either direction (`deploy.sh` excludes it). Keys:
`READ_FERNET_KEY`, `READ_ALLOWLIST`, `READ_CONSUMER_KEY`,
`READ_CONSUMER_SECRET`, `CLOUDFLARE_TUNNEL_TOKEN`.

Generate the Fernet key **on the pi** so it never lands in a transcript:

```sh
ssh orange "docker run --rm python:3.13-slim sh -c \
  'pip -q install cryptography && python -c \
  \"from cryptography.fernet import Fernet;print(Fernet.generate_key().decode())\"'"
```

`READ_ALLOWLIST` fails closed: unset means nobody may sign in. `*` opens it.
