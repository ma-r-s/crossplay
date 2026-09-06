# Ankibridge

An Anki sync bridge for the study app: a FastAPI service (uvicorn, port 8080
inside the container, `bridge.app:app`) that syncs a user's collection from
AnkiWeb with the `anki` library, converts it with the shared converter from
`tools_local/study`, and serves it to the device. Runs on the Orange Pi at
`/srv/ankibridge` behind a Cloudflare Tunnel. It holds AnkiWeb credentials
(Fernet-encrypted at rest) and user collections, and only usernames in
`BRIDGE_ALLOWLIST` are served, so the hardening bar is Getbooks' or higher.

## Attacking it

The service is open to the world and this repository is public, so nothing here
may depend on any of its numbers being secret. `docs/bridge-security.md` is the
threat model, the rate limits and their reasoning, and the Cloudflare rules that
only Mario can add.

`server/attacks.py` is the executable half: one shared checklist run against
both bridges (credential stuffing, pairing-code brute force, replay, cross-user
files and jobs, forged tokens, oversized and malformed bodies), and
`scripts/deploy.sh` runs it BEFORE it ships anything, so a vulnerable build
never reaches the pi. `server/verify_attacks.sh` breaks the service on purpose,
one property at a time, and proves every check can still go red.

## Isolation

The box also runs Immich, Jellyfin and the *arr stack, so this container is
treated as one that will eventually be compromised.

- runs as uid 10002 (10001 is getbooks), `read_only` root filesystem,
  `cap_drop: ALL`, `no-new-privileges`, capped memory/pids/cpu, `/tmp` a
  512 MB tmpfs because the anki library needs a writable temp dir and a
  hostile sync payload must not fill the disk
- its own bridge on a pinned subnet (`172.31.84.0/24`; `172.31.83.0/24` is
  getbooks), nothing published on the host: cloudflared reaches it over the
  compose network, so nothing on the LAN can hit it unauthenticated
- `scripts/firewall.sh` drops traffic from that subnet to every RFC1918
  range, the tailnet and link-local, on **both** `DOCKER-USER` and `INPUT` --
  host services never traverse FORWARD, so DOCKER-USER alone leaves SSH open.
  Installed as `ankibridge-firewall.service` so it survives reboots.
- DNS is pinned to 1.1.1.1/9.9.9.9. Docker's embedded resolver forwards from
  inside the container's namespace, so walling off the host otherwise breaks
  name resolution; pointing it at the internet avoids a port 53 hole back home.

Verify after **every** deploy, not just firewall changes:

```sh
ssh orange 'bash -s' < scripts/isolation_test.sh
```

Every private-network probe must time out and egress to AnkiWeb must work;
the script exits nonzero on any breach.

## Operating it

```sh
./scripts/deploy.sh                                        # stage, ship, rebuild
ssh orange 'cd /srv/ankibridge && docker compose up -d --build'   # rebuild only
ssh orange 'cd /srv/ankibridge && docker compose logs -f'         # sync activity, failures
ssh orange 'cd /srv/ankibridge && docker compose stop cloudflared'  # kill switch: public endpoint off
```

The containers are labelled `com.centurylinklabs.watchtower.enable=false`
because watchtower updates every other container on that box.

Backup: nightly snapshot of the authoritative state only (the credential store
and per-user sync state under `/srv/ankibridge/data`, not the rebuildable
converted output) to `/mnt/hdd/backups/ankibridge`. **TODO: wiring not done.**

## Events

Every finished sync posts one event to the board (`docs/workflow/events.md`):
`anki`/`sync` with `{cards, reviews, seconds}`, or the same event at level
`error` with `{message}` when the job died or froze. It is counted under the
device's own id when the request carried one (`X-CrossPlay-Device`, with
`X-CrossPlay-Board` and the version from the User-Agent, and the report's
`battery_pct`, `heap_min_kb`, `uptime_h` copied into the props), else under
a salted hash of the token hash. Whatever the device had to report rides the
same headers on every request, so a middleware reads them on every accepted
answer and posts `firmware`/`crash` and `firmware`/`update` events for a
crash or an OTA attempt the report carries (`events.Client.report`).
`bridge/events.py` sends from its own thread with a 3 s timeout and drops
the event after one log line if the board does not take it, so a board
outage cannot slow or fail a sync (`tests/test_api.py` proves both, and the
header bodies). The module is a byte-identical twin of
`read-bridge/bridge/events.py`; `tests/test_events.py` fails if the two
drift.

Where to post comes from two more `.env` keys, both optional:

```sh
SUPABASE_URL=https://<project>.supabase.co
SUPABASE_ANON_KEY=<the public anon key, the one that can only insert>
```

They are the URL and ANON key from `<workspace>/.board/supabase.env` (never
the service role key). With either missing the service runs exactly as
before and logs `events are off` once at startup. To turn them on: append
the two lines to `/srv/ankibridge/.env` on the pi, ship this code with
`./scripts/deploy.sh` (once it is running there, a bare `ssh orange 'cd
/srv/ankibridge && docker compose up -d'` is enough, because an `.env`
change is a recreate and not a rebuild), then check for `events on` in
`docker compose logs ankibridge`.

## Secrets

`.env` lives on the pi only, at `/srv/ankibridge/.env`, mode 600, never in
git. Keys: `BRIDGE_FERNET_KEY`, `CLOUDFLARE_TUNNEL_TOKEN`, `BRIDGE_ALLOWLIST`,
and the optional `SUPABASE_URL` / `SUPABASE_ANON_KEY` pair above.
It is never rsync'd in either direction (`deploy.sh` excludes it, and that
exclude is load-bearing) and never pasted into commands, where it would land
in shell history and `ps` output. Edit it in place on the pi.

**`BRIDGE_ALLOWLIST` is this service's gate and it fails closed: unset means
nobody may sign in, `*` opens it.** The twin at `/srv/readbridge` calls the
same idea `READ_ALLOWLIST`, and writing one name into the other's `.env` is
completely silent -- no warning, no log line, and the same
"This bridge is invitation-only for now." a stranger already saw. Verify a
change from outside with `server/verify_open.sh`, never with `printenv` or a
200 on `/healthz`. See `server/read-bridge/scripts/DEPLOY-RUNBOOK.md` step 6
and `docs/bridge-security.md`.

## The pages people see

`bridge/chrome.py` is the whole look: the band, the three-step rail, the
figures, the CSS. `bridge/app.py` only decides which words and which step.

It is the site's aesthetic (`site/styles.css`) restated inline, because this
service is on its own subdomain and cannot link that stylesheet. The two
typefaces are vendored under `bridge/static/` with their licences and are
served by an allowlisted `/assets/<name>` route; the Dockerfile's `COPY
bridge` and the deploy rsync both carry them with no extra step.

The file is the same in both bridges apart from three strings at the top
(`SERVICE`, `ACCOUNT`, and the hostname in the docstring). `tests/test_pages.py`
asserts that, so a change to one that is not made to the other goes red.

Every SVG attribute in there is quoted. An unquoted one eats the tag's own
self-closing slash and the figure renders as an empty box with a caption under
it, with every suite still green; the same test file refuses that too.
