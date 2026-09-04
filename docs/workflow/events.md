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
| `device`  | A hash of the MAC with a fixed salt, the same on every post from one device. Never the MAC, never a name. Optional for services that have no device. |
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
          "ota":{"attempted":true,"ok":false,"error":"too_large"}}}
```

`apps` is the set opened since the last heartbeat (shelf titles, lowercased,
`HACKER NEWS` is `hackernews`). `uptime_h` is hours since boot, and deep
sleep is a boot. `heap_min_kb` is the lowest free heap since boot. `ota` is
the install attempted since the last heartbeat: `ok` is inferred from the
version having moved, never from what the install screen said, so an
install that "succeeded" into the same version reads as not ok with no
error. That single event answers "how many devices are on which version",
"how many use each app", "which version drains faster" and "who cannot
update" (the 6.25MB slots of a device flashed before v1.5.3 come back as
`too_large`).

A boot after a panic posts one more event, `{"event":"crash","level":"error",
"props":{"message":"<panic reason>","backtrace":"<first two stack lines>"}}`,
once, so a crash in the field opens a card by itself. Its `version` is the
one that crashed, written down at the boot after the panic: the record waits
for Wi-Fi, and an OTA can land in between.

`device` is sha256(MAC + a fixed salt); the MAC is never sent. The address
and the public key come from the site's `/api/board-config`, fetched once and
cached on the card as `/.crosspoint/board.json`, fetched again when the board
answers 401 or 403 (a key rotation is a Vercel setting, not a release).
Between heartbeats the apps set, the OTA record and a pending crash live in
`/.crosspoint/heartbeat.json`, written once per first open and once per send.

The post runs inline in `loop()`, so it is bounded: every network wait is
5s, one request per loop pass (the board config on one pass, the event on
the next), and a request that fails is not tried again for 15 minutes, then
not before the next UTC day, one try a day until one is accepted. That wait
(`retry`, `fails`) is in the state file, because deep sleep is a boot and a
device that sleeps often would otherwise pay the stall at every boot.

Settings > System > "Send a daily heartbeat" (default on) turns all of it
off; the site says so in one sentence beside the Install button. The rules
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
