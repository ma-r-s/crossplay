# Events: one table for every number, and every error

Every service and every device posts what happens to one table on the board
(`events`, `server/board/supabase/migrations/20260903000200_events.sql`).
Mario's "how many people use this" is a view over it; an error posted there
becomes a card on the board by itself, once per distinct problem.

## Posting an event

One HTTP request, no library, the public key, from anything that can reach
the internet:

```
POST https://<project>.supabase.co/rest/v1/events
apikey: <anon key>
Authorization: Bearer <anon key>
Content-Type: application/json
Prefer: return=minimal

{"service":"getbooks","event":"download","device":"<hashed id>","version":"1.12.9","board":"x4pro","props":{"format":"epub","bytes":515054}}
```

The address and the key come from `/api/board-config` on the site or from
`<workspace>/.board/supabase.env`; the pi services get them as environment
variables. The public key can only insert; it cannot read anything back.

| Field     | What goes in it                                                                                                                                      |
| --------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `service` | `firmware`, `getbooks`, `anki`, `instapaper`, `site`, `release`. One word, lowercase, the same word every time.                                      |
| `event`   | What happened: `heartbeat`, `download`, `search`, `sync`, `install`, `report`, `update`, `error`. Same rule.                                         |
| `level`   | `info` (default) or `error`.                                                                                                                         |
| `device`  | sha256 of the MAC and a secret the device made once and keeps in NVS: pseudonymous, the same on every post from one device, and not matchable to a MAC without that device's secret. Never the MAC, never a name. Optional for services that have no device. |
| `version` | Firmware version as printed, `1.12.9`. Optional.                                                                                                     |
| `board`   | `x4pro` or `sticky`. Optional.                                                                                                                       |
| `props`   | Anything else, small: a format, a byte count, a duration, a book id. For errors, `message` is required.                                              |

## Errors become cards

An event with `level: "error"` and a `props.message` is fingerprinted: the
service, the event, and the message with every number and hex run replaced
by `#`, so "book 4127 timed out" and "book 9 timed out" are one fingerprint.
The first time a fingerprint is seen, a card opens on the board in
`triaged` with the service as its app, and the orchestrator dispatches it
like any other card. Every later occurrence adds one to the count in
`error_fingerprints` and attaches to the same card. A fingerprint whose card
was closed and that comes back opens a new card: that is a regression.

An `info` event that carries a `fingerprint` says that problem is gone:
the open card for it closes by itself with a "recovered" line. The pulse
does this on every host that answers; a service that can tell its own
error is over may do the same.

Send `props.app` when the card belongs to another app's owner than the
poster (the pulse posts as `pulse`; a dead books host is Get Books').
Send a `fingerprint` yourself when you know better than the message what
makes two errors the same (for example the book id is what matters, not the
timeout).

## The heartbeat

Once a day, when the device has Wi-Fi up for some other reason (Developer
Mode, an app that went online; it never brings the radio up for this), the
firmware posts one event:

```
{"service":"firmware","event":"heartbeat","device":"<hash>","version":"1.12.12","board":"x4pro",
 "props":{"apps":["trivia","hackernews"],"uptime_h":31,"battery_pct":84,"heap_min_kb":112,
          "ota":{"attempted":true,"ok":false,"error":"too_large","path":"sd"}}}
```

`apps` is the set opened since the last heartbeat (shelf titles, lowercased,
`HACKER NEWS` is `hackernews`). `uptime_h` is hours since boot, and deep
sleep is a boot. `heap_min_kb` is the lowest free heap since boot. `ota` is
the install attempted since the last heartbeat, from the update screen
(`path` `ota`) or from a `.bin` picked off the card (`path` `sd`; a file
refused before the confirmation prompt counts, it is an install the user
could not have): `ok` is inferred from the version having moved, never from
what the install screen said, so an install that "succeeded" into the same
version reads as not ok with no error. That single event answers "how many
devices are on which version",
"how many use each app", "which version drains faster" and "who cannot
update" (the 6.25MB slots of a device flashed before v1.5.3 come back as
`too_large`).

A boot after a panic posts one more event, `{"event":"crash","level":"error",
"props":{"message":"<panic reason>","backtrace":"<first two stack lines>"}}`,
once, so a crash in the field opens a card by itself. Its `version` is the
one that crashed, written down at the boot after the panic: the record waits
for Wi-Fi, and an OTA can land in between.

On `x4pro` and `sticky` (ESP32-S3, Xtensa) only an assert or abort panic
carries a reason, and none carries a trace: `lib/hal/HalSystem.cpp` writes
the reason from `__wrap_panic_abort` alone and its backtrace wrap captures
the stack only on RISC-V. A CPU exception (LoadProhibited,
StoreProhibited, IllegalInstruction) therefore posts `"panic without a
recorded reason (reset: panic; last log: READER)"`: the `esp_reset_reason()`
name and the subsystem that logged last before the reset are what the
fingerprint has to tell two of them apart, and `backtrace` is empty. An
assert reads `"assert failed: ... (reset: panic)"`. The reason is used only
when the capture marker was still set at boot (`HalSystem::panicReasonRecorded()`,
read in `main.cpp` before `checkPanic()` clears it): the text in RTC memory
outlives the crash that wrote it, so an exception after an assert with no
clean boot between posts as "without a recorded reason", not as that assert.
Carrying the program counter and the exception cause is a card, not a
limitation of the pipe.

`device` is sha256(MAC + a 16-byte secret the device made once from its
hardware RNG and keeps in NVS, namespace `crossplay`, key `hbsecret`); the
MAC and the secret are never sent, and without the secret the id cannot be
matched to a MAC (a vendor prefix leaves 2^24 MACs, seconds of work against
a salt that is in this repository). When NVS gives no secret the device
falls back to sha256(MAC + that fixed salt) and logs it. A full flash erase
makes a new secret, so the device comes back as a new id. The address
and the public key come from the site's `/api/board-config`, fetched once and
cached on the card as `/.crosspoint/board.json`, fetched again when the board
answers 401 or 403 (a key rotation is a Vercel setting, not a release).
Between heartbeats the apps set, the OTA record and a pending crash live in
`/.crosspoint/heartbeat.json`, written whenever the state changes: a first
open, an OTA note (the attempt and the failure), a recorded panic, a failed
request and the answer that ends a run of them, a send, and the toggle
going back on.

The post runs inline in `loop()`, with input and the power button waiting
behind it, and the 5 s timeout is per wait, not per request. `SecureClient`
makes two connect attempts (a TLS 1.3-capable hello, then a TLS 1.2-only
one), each a blocking DNS lookup the timeout does not cover, then up to 5 s
for TCP and up to 5 s for the handshake. A silent :443 therefore costs about
10 s plus DNS (a dropped SYN spends the two TCP waits and never reaches the
handshake; a port that accepts and says nothing spends the two handshake
waits) and up to 20 s plus DNS when both run to the deadline. One request
per loop pass (the board config on one pass, the event on the next). The
real bound is the persisted backoff: a request that fails is not tried again
for 15 minutes, then not before the next UTC day, one try a day until one is
accepted. That wait (`retry`, `fails`) is in the state file, because deep
sleep is a boot and a device that sleeps often would otherwise pay the stall
at every boot.

Settings > System > "Send a daily heartbeat" (default on) turns all of it
off, and off records nothing: no app open, no OTA note, no panic is written
to the card while it is off, and switching it back on forgets whatever the
file still held from before, so there is never a backlog waiting to go out.
The site says so in one sentence beside the Install button. The rules
are `src/network/HeartbeatCore.{h,cpp}` and `host-tests/heartbeat` pins
them; `src/network/Heartbeat.cpp` is the clock, the card, the radio and the
TLS. The serial log says which decision was taken and why under `HEARTBEAT`.

## Reading the numbers

Signed-in users (the inbox page) read four views: `devices_by_version`
(last 7 days), `daily_active_devices` (30 days), `events_daily` (30 days,
by service and event), `service_users` (7 days). The inbox page shows them
under "Numbers", next to GitHub's own download counts per release.

## What each owner sends (the cards)

Each service has a card on the board naming the events it should post and
where in its code. The firmware heartbeat is `src/network`; Get Books, the
Anki bridge and the Instapaper bridge post from the pi; the site posts
`install` and `report`. The pipe is built; the sending is the owner's.

## The pulse: what looks from outside

A service that is down posts nothing, and on this table "no events" reads
as "no usage". So the board itself looks from outside: every 30 minutes its
scheduler (`pg_cron`) fires one HTTP request per row of `pulse_targets`
through `pg_net`, and three minutes later reads the answers
(`20260904001000_pulse_on_the_board.sql`). A host that answers with a
status its row allows is an `info` `pulse`/`probe` event; anything else, a
timeout included, is an `error` with a fixed fingerprint per host, so an
outage is one card however long it lasts, closed by the next ok probe and
reopened as a new card if it returns. The same pass checks the daily
upstream sync through GitHub's public compare API: upstream commits xteink
lacks, older than 30 hours, with no `sync/upstream-*` pull request open,
is an error on `tooling`.

It runs on the board and not on GitHub because this repository is a fork,
and GitHub does not run `schedule:` workflows in forks (five slots passed
in silence with the workflow "active"). `workflow_dispatch` was never
affected, but a pulse that has to be dispatched is not a pulse.

**When your service goes live, add its row:** `board pulse add <host>
<GET|POST> <url> <alive> <app>`, where `alive` is the statuses that mean
up (`200`, `2xx,401`, ...; a 401 from a Basic-auth root is what proves the
service is up) and `app` is whose card an outage becomes. `board pulse`
lists the rows; `board pulse remove <host>` drops one. A host added before
it exists opens a card every half hour. The board records status, not
latency, so `ms` in `pulse_hosts` is empty for these probes.

## The workflow's own numbers

The board watches itself through five views: `pulse_hosts` (up now, uptime
and median latency per host, 7 days), `workflow_weekly` (cards opened and
closed, asks to Mario and his answers, error cards, hook refusals,
releases, by week), `state_dwell` (hours a card sits in each state, 30
days), `inbox_latency` (what waits on Mario now, and how long an answer
takes), `open_cards_by_app`. The hooks post a `workflow`/`refusal` event on
every refusal and write the same line to `<workspace>/.board/refusals.log`;
that count is the one that says whether the rules teach or merely obstruct.

## Retention

Raw events are kept 90 days. A nightly job (`pg_cron`, 04:17 UTC, in
`20260903000600_observability.sql`) folds the days before today into
`events_rollup` (day, service, event, level, count, distinct devices) and
deletes the raw rows past 90 days. `error_fingerprints` keeps its counts
regardless. Anything that wants a number older than 90 days reads the
rollup.
