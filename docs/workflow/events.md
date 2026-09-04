# Events: one table for every number, and every error

Every service posts what happens, its own and what the devices tell it, to one table on the board
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
| `service` | `firmware`, `getbooks`, `anki`, `instapaper`, `site`, `release`, `pulse`, `upstream-sync`, `workflow`. One word, lowercase, the same word every time.                                    |
| `event`   | What happened: `download`, `search`, `sync`, `install`, `report`, `crash`, `update`, `probe`, `run`. Same rule.                                          |
| `level`   | `info` (default) or `error`.                                                                                                                         |
| `device`  | The id from the device's `X-CrossPlay-Device` header (below): pseudonymous, the same on every request from one device, and not matchable to a MAC without that device's secret. Never the MAC, never a name. A service whose request carried no id may use its own salted hash of an account or token instead, or leave it out. |
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

## What a device sends

A device never makes a request just to report: it never brings the radio up
for the board, and it never spends a request on it. Instead every request
the firmware makes to one of CrossPlay's own services (Get Books, the Anki
bridge, the Instapaper bridge) carries three headers, and the service posts
what they say alongside the event it was going to post anyway:

```
User-Agent: CrossPlay-ESP32-1.12.13
X-CrossPlay-Device: 9f2c...64 hex...
X-CrossPlay-Board: x4pro
X-CrossPlay-Report: {"battery_pct":84,"heap_min_kb":112,"uptime_h":31,
                     "crash":{"message":"assert failed: ... (reset: panic)","version":"1.12.12","backtrace":""},
                     "ota":{"attempted":true,"ok":false,"error":"too_large","path":"ota"}}
```

| Header               | What is in it                                                                                                                                                                                                                                      |
| -------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `User-Agent`         | `CrossPlay-ESP32-<version>`, as it always was. The version is read from here and nowhere else.                                                                                                                                                     |
| `X-CrossPlay-Device` | 64 hex: sha256(MAC + a 16-byte secret the device made once from its hardware RNG and keeps in NVS, namespace `crossplay`, key `hbsecret`). The MAC and the secret are never sent; without the secret the id cannot be matched to a MAC. A full flash erase makes a new secret, so the device comes back as a new id. |
| `X-CrossPlay-Board`  | `x4pro` or `sticky`.                                                                                                                                                                                                                               |
| `X-CrossPlay-Report` | Compact JSON, at most 600 bytes. Always `battery_pct`, `heap_min_kb` (lowest free heap since boot) and `uptime_h` (hours since boot; deep sleep is a boot). While one is pending, `crash` and/or `ota`, below.                                        |

`crash` is the last panic, recorded at the boot after it and carried until a
request delivers it: `message` is the panic reason, `version` the firmware
that crashed (an OTA can land between the panic and the request, so it is
written down at that boot, not read from the User-Agent), `backtrace` the
first stack lines when the chip records any. On `x4pro` and `sticky`
(ESP32-S3, Xtensa) only an assert or abort panic carries a reason, and none
carries a trace: `lib/hal/HalSystem.cpp` writes the reason from
`__wrap_panic_abort` alone. A CPU exception (LoadProhibited,
StoreProhibited, IllegalInstruction) therefore reads `"panic without a
recorded reason (reset: panic; last log: READER)"`, and the reset name plus
the subsystem that logged last are what the fingerprint has to tell two of
them apart. An assert reads `"assert failed: ... (reset: panic)"`.

`ota` is the install attempted since the last report, from the update
screen (`path` `ota`) or from a `.bin` picked off the card (`path` `sd`; a
file refused before the confirmation prompt counts). `ok` is inferred from
the version having moved, never from what the install screen said, so an
install that "succeeded" into the same version reads as not ok with no
error; the 6.25MB slots of a device flashed before v1.5.3 come back as
`too_large`.

A request that carries none of this (a browser, a curl, an older firmware)
is served exactly as before and counted under whatever the service already
knew: the version alone for Get Books, a salted hash of the token or the
account for the bridges. Settings > System on the device turns the headers
off; off sends nothing and records nothing.

## What each service posts

The three services share one reader for the headers (`bridge/events.py` in
`server/study-bridge` and `server/read-bridge`, byte-identical twins that a
test keeps identical; `getbooks/events.py` in the Get Books repository) and
trust none of it: an id that is not 64 hex is no id, a board that is not a
short word is no board, a report over 1000 bytes or not a JSON object is
ignored with one debug line and the request served all the same, and only
numbers are copied out of the health fields.

On every request the service accepts (a 2xx or 3xx; a 401 the device will
retry posts nothing, so a crash is not counted twice), whatever the
endpoint:

- **The usage event it already posted** (`getbooks`/`search`,
  `getbooks`/`download`, `anki`/`sync`, `instapaper`/`sync`, and their
  `error` twins) gets `device` from the header (over the service's own hash
  when both exist), `board` from the header, `version` from the User-Agent,
  and `battery_pct`, `heap_min_kb`, `uptime_h` copied into `props` when the
  report carried them.
- **A report with `crash`** posts one more event, `{"service":"firmware",
  "event":"crash","level":"error","device":..,"version":<crash.version>,
  "board":..,"props":{"message":<crash.message>,"backtrace":<crash.backtrace>,
  "app":"firmware","via":"getbooks"|"anki"|"instapaper"}}`, so a crash in the
  field opens a card on the firmware's owner by itself, once per distinct
  reason, whichever service happened to hear it.
- **A report with `ota`** posts `{"service":"firmware","event":"update",
  "device":..,"version":..,"board":..,"props":{...the ota object...,
  "app":"firmware"}}` at level `info`, or at level `error` with
  `props.message = "update failed: <error> (<path>)"` when `ok` is false and
  an error is named, so "who cannot update" is a card with a count.

Each service says `device report via <service>: crash on <version>` or
`update <level>` in its log when it posts one. The service that heard it is
`props.via`; the card lands on the firmware.

## Reading the numbers

Signed-in users (the inbox page) read five views over the events that carry
a device: `devices_by_version` (distinct devices per board and version, over
every event with a device and a version in the last 7 days),
`daily_active_devices` (distinct devices per day, 30 days),
`battery_by_version` (board, version, the average `battery_pct` of the
events a device carried, averaged per device first so a reader that syncs
ten times a day weighs the same as one that syncs once, with the device and
report counts beside it, 7 days), `events_daily` (30 days, by service and
event), `service_users` (7 days). The inbox page shows them under "Numbers",
next to GitHub's own download counts per release.

A device is counted when it uses a service. One that only plays games and
never searches a book or syncs a deck is not on the board at all, and that
is the design, not a gap: "devices heard from" means devices that used
something. Downloads per release, from GitHub, is the number for the rest.
The views are in `20260903000200_events.sql` as written and
`20260904001100_devices_from_usage.sql` as they read now.

## What each owner sends (the cards)

Each service has a card on the board naming the events it should post and
where in its code. The firmware puts its headers on every request it makes
(`src/network`); Get Books, the Anki bridge and the Instapaper bridge post
from the pi, their own events and the firmware's; the site posts `install`
and `report`. The pipe is built; the sending is the owner's.

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
