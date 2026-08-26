# The sync bridge: every device syncs, nobody runs a server

Status: v1, critic-reviewed (2026-08-26; all findings below are folded in,
not appended). Decided by Mario: the bridge is a global service on his
Orange Pi 5, shape A (it relays to each user's AnkiWeb, storing a session
key, never storing the password), and the device is the ONLY sync surface.
One product question stays open for Mario in the Website slice.

## What it is

One public service. A CrossPlay user pairs their device once by scanning a
QR, and from then on the SYNC action in Study pulls new and changed cards
from their Anki collection and pushes the device's reviews back, over any
WiFi. Their phone and desktop keep syncing with AnkiWeb exactly as before;
the bridge is one more client of their account, and the device is a client
of the bridge. No user runs anything.

Decided against (see memory anki-sync-research): the device speaking
AnkiWeb's protocol (zstd, SQLite, template rendering, and AnkiWeb forbids
third-party clients); per-user self-hosted servers; shape B, the bridge as
sync home (Mario chose A knowing the custody and blocking risks). Also cut
by the critic: the "degrades into shape B" promise -- if AnkiWeb refuses the
bridge, decks go read-only-stale and reviews keep accumulating safely in the
journal; no mirror-as-truth semantics, ever.

## The paragraph of honesty this design owes its users

The bridge holds, per user: an AnkiWeb session key (equivalent in power to
their password while it lives), a mirror of their collection including
media, and their review history. At signup the plaintext password transits
the bridge's memory once (it is exchanged for the key and discarded, but a
compromised bridge harvests passwords at login time, not merely keys). The
blast radius of a compromise is every paired user's collection and AnkiWeb
account. Everything in Security exists because of this paragraph.

## Server architecture

Lives in this repo at `server/study-bridge/` so it imports the SAME
converter the CLI and web installer use (`anki_to_deck.py`, `make_fonts.py`,
`make_images.py`, `fsrs.py`). One FastAPI service (uvicorn pinned to
`workers=1` in compose with a comment: the per-user asyncio mutex silently
stops being a mutex above one worker); all AnkiWeb traffic through the
official `anki` pip package (`sync_login`, `sync_collection`, `sync_media`)
so the wire behavior is the real client's. CPU-bound work (deck build, font
subsetting, image packing) runs in short-lived subprocesses with capped
concurrency -- both for GIL isolation and so Pillow/fontTools parsing of
user-controlled bytes never happens in the process that holds every user's
data.

Per user on disk (`/data/users/<uid>/`):

- `collection.anki2` + media: the mirror, written only through pylib (and
  the one sanctioned raw-SQL path below, never while a pylib Collection is
  open on the same file -- the Rust backend caches; close, write, reopen).
- `journal.db`: ingested-but-not-yet-pushed device reviews. Authoritative;
  survives any mirror replacement. Tiny.
- `decks/<slug>/<build-id>/`: versioned deck builds (deck.dat, cards.dat,
  meta.dat, fonts/, images.dat). A rebuild writes a NEW build-id; the
  manifest points at versioned paths, so a download can never race a
  rebuild into a torn file. Old builds GC'd after a few days.
- `state.json`: chosen decks, paired devices (token HASHES, names, created,
  last-seen -- schema includes revocation from day one), account status.

Accounts are "log in with AnkiWeb": credentials entered once on the
bridge's HTTPS page, exchanged via `sync_login`, hostKey kept encrypted,
password discarded in the same request. No bridge-specific password. The
login endpoint is a credential-stuffing oracle by construction, so it gets
aggressive per-IP and global caps plus per-username exponential lockout,
and registration stays behind an allowlist until those exist and have been
exercised.

## The sync cycle: a job, not a request

`POST /api/sync` validates, enqueues, and returns a job id immediately; the
device polls `GET /api/sync/status`. A first sync (collection + media, which
for a normal Anki user can be gigabytes) takes however long it takes without
any HTTP request held open. Per-user: one job in flight, mutex-held.

The job, in order:

1. Ingest the device's posted reviews into `journal.db` (idempotent: keyed
   on card id + review ms, same key deck_to_anki uses; same-ms collisions
   nudged the same way). **Only after the journal write is durable does the
   response ack.**
2. Pull: pylib-sync the mirror from AnkiWeb.
3. Apply the journal to the mirror (see USN, below), mark applied entries;
   they leave the journal only after a successful push.
4. Push: pylib-sync again.
5. Rebuild changed decks into new build-ids (subprocess).
6. Status becomes done, with the manifest of versioned deck files + hashes.
   The device downloads what changed, .part + rename (the xkcd pattern).

**Acks are byte offsets into revlog.dat, never timestamps.** The file is
append-only by position; a device RTC reset would make time-based selection
silently skip reviews forever. The ms stays what it is for Anki: the revlog
key.

**"Full sync required" has a no-human rule.** If AnkiWeb demands a one-way
sync (schema change, Check Database on desktop), the bridge freezes pushes,
marks the account "needs attention" on its page, and never picks a side by
itself. After any full download, the journal is re-applied before anything
else. A crash anywhere in the job reduces to a safe retry: acked reviews
are in the journal, and the journal replays idempotently.

**The USN reality (settled by reading the code, kept true by a test):**
deck_to_anki's raw SQL already sets `usn=-1` on revlog inserts and card
updates -- row-level pending-sync marking exists. What it never touches is
`col.mod`, and the sync meta exchange starts from collection mod time, so a
cycle whose ONLY change is device reviews can report "synced" while
uploading nothing. The bridge's apply step bumps the collection mod time
through pylib after raw writes. There is no pylib operation for backdated
reviews (answerCards stamps "now"), so the raw path IS the path. The
round-trip test that gates all of this: a cycle containing only device
reviews, then a fresh full download elsewhere, must show those reviews.

## Device API

All endpoints under the device token except pairing start/poll and healthz.

- `POST /api/pair/start` (unauthenticated, its own per-IP rate limit):
  returns {code, poll_token}. Codes expire in 5 minutes. Device shows a QR
  of `https://<bridge>/pair#<code>` plus the code as text.
- `POST /api/pair/claim` (browser, user session): CSRF-protected
  (SameSite=Lax cookie + CSRF token), and the page requires an explicit
  "Pair this e-reader?" confirmation showing the code -- a cross-site POST
  or an auto-submitting form cannot silently bind a device.
- `GET /api/pair/poll` (poll_token): pending | {device_token, username}.
  **The device then shows "Paired to <username> -- confirm?" and stores the
  token only on a button press.** This closes both pairing races: a
  shoulder-surfed code claimed by a stranger shows the wrong name; a
  phished QR pairs the attacker's device to a victim who never confirmed on
  hardware they don't hold.
- `GET /api/decks`, `POST /api/decks/choose`: deck list with counts; picks.
- `POST /api/sync`, `GET /api/sync/status`: the job above.
- `GET /api/deck/<slug>/<build-id>/<file>`: versioned build files.
- `GET /healthz`: static process-liveness only -- no per-user state, no
  AnkiWeb calls -- so it stays safe unauthenticated through the tunnel.

The bridge page has a devices list with per-device revoke (deletes the
token hash; the SD-resident token dies with it). Lost device = one click.

DS rule end to end: the device never shows a URL, token, or error code;
sentences only.

## Security

- **Never the password** (exchanged and discarded; caps and lockout on the
  login endpoint per above). Device tokens: random 32 bytes, stored hashed.
- **What the hostKey encryption actually buys, stated honestly:** the
  Fernet master key lives in `.env`, ciphertext in `data/` -- on a live
  compromised box it buys nothing. It exists so that backups and any stray
  copy of the data directory contain no usable credentials. The invariant
  "a backup never contains .env" is a TESTED property of the backup script,
  not a hope.
- **Blast-radius controls:** per-user and global rate limits on sync,
  per-IP on pairing and login, request size caps, per-user data dirs,
  subprocess parsing (above), logs without secrets, allowlisted
  registration until Mario opens it.
- **AnkiWeb courtesy:** syncs only when a device asks, one per user in
  flight, jittered, exponential backoff per error class.
- **Operator alerting, not just user honesty:** counters of consecutive
  AnkiWeb failures by class (auth, protocol, network), and a
  healthchecks/ntfy heartbeat that goes red when auth-failure or sync-error
  rates cross thresholds -- so Mario learns AnkiWeb started refusing the
  bridge before a user does.

## Deployment (the Getbooks pattern, applied)

The pi already hosts a hardened public service (Getbooks,
`books.ma-r-s.com`); sources of truth are
`~/Projects/Personal/Code/Getbooks/{compose.yaml,Dockerfile,scripts/}` and
its README's Isolation section. The bridge copies it:

- **`/srv/ankibridge/`**, rsync'd from the Mac (`--delete` with
  `--exclude .env --exclude data/` -- load-bearing: secrets and state live
  only on the pi). Native build on the pi; `anki` ships a manylinux_2_35
  aarch64 wheel (checked at 26.8.1), no Rust toolchain.
- **Compose hardening, same lines:** `user: "10002:10002"` (10001 is
  Getbooks), `read_only: true` + capped tmpfs, `cap_drop: [ALL]`,
  `no-new-privileges`, `pids_limit`/`cpus`, `restart: unless-stopped`,
  healthcheck on `/healthz`, watchtower label OFF on every container (the
  box's watchtower updates anything unlabelled), `workers=1` with its
  comment. `mem_limit` sized for a media sync plus one rebuild subprocess,
  not Getbooks' 1g.
- **No host ports.** `expose:` only; ingress is a `cloudflared` sidecar on
  a fresh pinned /24 (172.31.83.0/24 is Getbooks'). TLS terminates at
  Cloudflare's edge; `ma-r-s.com` is already a Cloudflare zone and the `cf`
  CLI's OAuth token drives tunnel + DNS over the REST API. Kill switch:
  `docker compose stop cloudflared`.
- **iptables isolation copied and renamed** from Getbooks
  (`firewall.sh` + systemd oneshot): sibling RETURN first, DROP to every
  private range in DOCKER-USER AND INPUT, container DNS pinned to public
  resolvers. The Getbooks isolation regression test (private targets all
  time out; DNS and outbound 443 work) runs after every deploy.
- **Secrets:** `/srv/ankibridge/.env`, mode 600, written on the pi over
  ssh, never rsync'd, never pasted into commands that land in transcripts
  (Getbooks' basic-auth password is sitting in one). Holds the Fernet
  master key and tunnel token only.
- **Backup: tiny and authoritative, not big and torn.** Mirror, media and
  built decks are all re-derivable from AnkiWeb plus the journal, and a tar
  of live SQLite is corruption-at-restore anyway. Nightly job snapshots
  only `journal.db` (sqlite backup API, under the user mutex),
  `state.json`, and the encrypted hostKeys to
  `/mnt/hdd/backups/ankibridge`, pruned. Restore is rehearsed once before
  strangers are let in.
- **Cloudflare-side rate limiting stays open on this box** (the `cf` token
  has no WAF scope; dashboard is Mario's hands). The app enforces its own
  limits regardless.

## Firmware slice

- Study gains SYNC on the deck screen (KOSync-shaped WiFi flow: user
  initiated, foreground, radio off after). States: SYNCING (job running,
  short), **PREPARING** (first sync / big media: "Preparing your decks --
  this first time can take a while. Come back later; it keeps working if
  you leave.") -- distinct screens, because an hour-long e-ink spinner gets
  force-rebooted.
- First SYNC with no token: the pairing story starts at WiFi, not at the
  QR -- joining the network is step one of pairing and the screen says so.
  Then QR + code, poll, and the "Paired to <username> -- confirm?" screen
  before the token (SD, `/study/.bridge`) exists.
- When the account is in needs-login state, sync against the mirror still
  completes but the device says, once per sync: "Synced. Reconnect Anki at
  <bridge> soon." One human sentence, no error code, no lying UP TO DATE.
- **TLS: a multi-root bundle, SD-updatable, baked-in fallback.** Cloudflare
  Universal SSL rotates issuing CAs across a pool (Let's Encrypt, Google
  Trust Services, more); pinning one root bricks SYNC at some renewal. The
  firmware embeds a small bundle covering the pool, verifies bridge
  connections against it, and can load a newer bundle from SD so a CA
  change never requires reflashing. Releases-fetching paths keep today's
  behavior for now. Heap cost measured against the KOSync TLS gate figures
  before committing.
- Device keeps writing revlog.dat exactly as today; sync posts the tail
  after the acked byte offset; the file is never truncated.

## Website slice (last)

**DECIDED (Mario, 2026-08-26): removed completely.** The critic's
demote-to-a-link counterpoint was put to him explicitly and he chose full
removal; the device is the only sync surface, period. Timing: the section
is removed in the SAME release train that ships the firmware SYNC button --
removing it earlier would leave zero sync paths for everyone, Mario
included. The CLI (`study.py sync`) is untouched. Install (convert + fonts

- first copy) stays on the page; the device QR empty-state keeps pointing
  there.

## Status (2026-08-26)

Steps 0-2 of the rollout are BUILT: the bridge is live at sync.ma-r-s.com
(Mario paired and synced his real collection through it via a curl stand-in
device), and the firmware slice exists on this branch -- SYNC on the deck
screen, QR pairing with the on-device confirm gate (instrumented: the log
names which branch passed it, after a stale-input bypass was caught in the
simulator and fixed with an input drain), the job-poll flow with the
PREPARING wording, batch-atomic deck downloads, and TLS verification against
the root bundle with the SD override. Verified end to end in the simulator
against a local bridge + local sync server (pairing held unclaimed, held
claimed-but-unconfirmed, passed only on the confirm tap; a crafted review
acked at its byte offset; a server-built deck landed on the card). The
website sync section is removed. Remaining: the release itself, and the
on-hardware pass of the device TLS path (the simulator's transport is curl,
so wolfSSL-verifies-Cloudflare is untested until a real device syncs).

## Rollout

0. Pre-work, already chipped: fix deck_to_anki's QUEUE_LEARNING_INTRADAY
   NameError (crashes any sync touching a mid-learning card; the branch is
   untested, which the bridge inherits until the test exists).
1. Bridge on the pi, allowlist = Mario. Fake-device tests via curl; the USN
   round-trip test green; then his real X4 Pro paired and a week of real
   reviews round-tripping.
2. Firmware release with SYNC + pairing + the TLS bundle.
3. Website decision executed + docs; allowlist opened when Mario says so.

## Open items

- The Website slice decision (above) -- Mario.
- Domain name for the bridge -- Mario (user-facing).
- hostKey lifetime in practice (does AnkiWeb expire them; the needs-login
  flow above already covers the UX either way).
- `anki` pin upgrade ritual: bump deliberately, run the converter and the
  USN round-trip test against a migrated mirror before deploying
  (anki_to_deck reads the schema raw; migrations are irreversible).
