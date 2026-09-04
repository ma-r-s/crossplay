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

| Field     | What goes in it                                                                                                                                                                                                                                              |
| --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `service` | `firmware`, `getbooks`, `anki`, `instapaper`, `site`, `release`. One word, lowercase, the same word every time.                                                                                                                                              |
| `event`   | What happened: `download`, `search`, `sync`, `install`, `report`, `update`, `crash`, `error`. Same rule.                                                                                                                                                     |
| `level`   | `info` (default) or `error`.                                                                                                                                                                                                                                 |
| `device`  | sha256 of the MAC and a secret the device made once and keeps in NVS: pseudonymous, the same on every post from one device, and not matchable to a MAC without that device's secret. Never the MAC, never a name. Optional for services that have no device. |
| `version` | Firmware version as printed, `1.12.9`. Optional.                                                                                                                                                                                                             |
| `board`   | `x4pro` or `sticky`. Optional.                                                                                                                                                                                                                               |
| `props`   | Anything else, small: a format, a byte count, a duration, a book id. For errors, `message` is required.                                                                                                                                                      |

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

## Device reports: headers on the requests a device makes anyway

The device never makes a request of its own to report. When the firmware
talks to one of CrossPlay's own services for some other reason (Get Books at
`books.ma-r-s.com`, the Anki bridge at `sync.ma-r-s.com`, the Instapaper
bridge at `read.ma-r-s.com`: any host in `ma-r-s.com`, and no other host, not
Hacker News, not xkcd, not GitHub), that request carries three headers
beside the `User-Agent` it already had:

```
User-Agent: CrossPlay-ESP32-1.12.14
X-CrossPlay-Device: 9f1c...64 hex...
X-CrossPlay-Board: x4pro
X-CrossPlay-Report: {"battery_pct":84,"heap_min_kb":112,"uptime_h":31}
```

`X-CrossPlay-Device` is the pseudonymous id (below). `X-CrossPlay-Board` is
`x4pro` or `sticky`. The version is in the `User-Agent`, where it always
was. `X-CrossPlay-Report` is compact JSON and never more than 600 bytes:
always `battery_pct`, `heap_min_kb` (the lowest free heap since boot) and
`uptime_h` (hours since boot; deep sleep is a boot), and, only while
something waits to be delivered, one or both of

```
"crash":{"message":"assert failed: ... (reset: panic)","version":"1.12.13","backtrace":"0x3FCE...|0x3FCE..."}
"ota":{"attempted":true,"ok":false,"error":"too_large","path":"ota"}
```

A 2xx from one of our hosts clears both: the service has them. Any other
answer (a refusal, no answer) leaves them for the next request. A crash whose
full record would not fit loses its backtrace first, then has its message
cut; it never disappears for being long.

### What the services post

The device posts nothing. A service that sees the headers posts, with the
same public key as everything else on this page:

- `firmware`/`crash`, `level: error`, with `device` and `board` from the
  headers, `version` from `crash.version` (the version that crashed, which is
  not always the one making the request: the record survives an update in
  between), `props.message` and `props.backtrace`. The fingerprint rule
  above makes one card of it.
- `firmware`/`update` with `device`, `board`, `version` (from the
  `User-Agent`) and `props` as the `ota` object: `attempted`, `ok`, `error`,
  `path`. `ok` is inferred by the device from the version having moved, never
  from what its install screen said, so an install that "succeeded" into the
  same version arrives as not ok with no error; a 6.25MB slot on a device
  flashed before v1.5.3 arrives as `too_large`.
- `device`, `board` and `version` on its own usage events
  (`getbooks`/`download`, `anki`/`sync`, `instapaper`/`sync`) whenever the
  headers are present. That is what "how many devices are on which version"
  and "which board" read from. A device that never touches a service is never
  counted, and that is the design: the count is of devices using CrossPlay's
  services, not of devices that exist.

The headers are input from the internet: parse the JSON defensively (a
device can be old, anyone can send anything), cap what is stored, and never
fail the request over them.

### The id

`device` is sha256(MAC + a 16-byte secret the device made once from its
hardware RNG and keeps in NVS, namespace `crossplay`, key `hbsecret`); the
MAC and the secret are never sent, and without the secret the id cannot be
matched to a MAC (a vendor prefix leaves 2^24 MACs, seconds of work against
a salt that is in this repository). When NVS gives no secret the device
falls back to sha256(MAC + that fixed salt) and logs it once. A full flash
erase makes a new secret, so the device comes back as a new id.

### The crash record

A boot after a panic writes the record down, once, at that boot: the
version running is the one that crashed, and the record waits for the next
request to one of our hosts, an OTA in between notwithstanding.

On `x4pro` and `sticky` (ESP32-S3, Xtensa) only an assert or abort panic
carries a reason, and none carries a trace: `lib/hal/HalSystem.cpp` writes
the reason from `__wrap_panic_abort` alone and its backtrace wrap captures
the stack only on RISC-V. A CPU exception (LoadProhibited,
StoreProhibited, IllegalInstruction) therefore arrives as `"panic without a
recorded reason (reset: panic; last log: READER)"`: the `esp_reset_reason()`
name and the subsystem that logged last before the reset are what the
fingerprint has to tell two of them apart, and `backtrace` is empty. An
assert reads `"assert failed: ... (reset: panic)"`. The reason is used only
when the capture marker was still set at boot (`HalSystem::panicReasonRecorded()`,
read in `main.cpp` before `checkPanic()` clears it): the text in RTC memory
outlives the crash that wrote it, so an exception after an assert with no
clean boot between arrives as "without a recorded reason", not as that
assert. Carrying the program counter and the exception cause is a card, not
a limitation of the pipe.

### The install record

`ota` is the install attempted since the last delivery, from the update
screen (`path` `ota`) or from a `.bin` picked off the card (`path` `sd`; a
file refused before the confirmation prompt counts, it is an install the
user could not have). The attempt is written before the flash, because a
flash that works reboots the device; the failure is written when the screen
learns of it.

### The toggle and the file

Settings > System > "Include anonymous device info when using CrossPlay
services" (default on) turns all of it off. Off adds nothing to any request
and records nothing: no panic and no install note is written to the card
while it is off, and switching it back on forgets whatever the file still
held from before, so there is never a backlog waiting to go out. The site
says so in one sentence beside the Install button.

Between requests the pending crash and install record live in
`/.crosspoint/devreport.json`, written whenever the state changes: a
recorded panic, an install attempt or failure, the 2xx that delivered them,
the toggle going back on. A device coming from the heartbeat firmware
(v1.12.13 and before) reads what `/.crosspoint/heartbeat.json` held into the
new file once and deletes it, and deletes the `/.crosspoint/board.json` the
heartbeat cached, which nothing reads any more.

The rules are `src/network/DeviceReportCore.{h,cpp}` and
`host-tests/devreport` pins them: the header content and its 600-byte cap,
the crash and the install present only while pending, our hosts and the
look-alikes that are not, the toggle, and the clearing on a 2xx.
`src/network/DeviceReport.cpp` is the card and the settings;
`src/network/HttpDownloader.cpp`, `src/apps_local/bridge/BridgeHttp.cpp` and
`src/apps_local/study/StudySync.cpp` are where the headers go onto a request
and where the answer is read back. The serial log says what rode on which
request under `DEVREPORT`.

What the heartbeat had and this does not, on purpose: the daily post, the
board address and key fetched from the site and cached on the card, the TLS
client the firmware opened for itself, the backoff and the stall it put in
`loop()`, and the set of apps opened. Nothing in the firmware calls home.

## Reading the numbers

Signed-in users (the inbox page) read four views: `devices_by_version`
(last 7 days), `daily_active_devices` (30 days), `events_daily` (30 days,
by service and event), `service_users` (7 days). The inbox page shows them
under "Numbers", next to GitHub's own download counts per release.

## What each owner sends (the cards)

Each service has a card on the board naming the events it should post and
where in its code. The firmware posts nothing (see "Device reports" above);
Get Books, the Anki bridge and the Instapaper bridge post from the pi, and
they are the ones that turn the device's headers into `firmware` events; the
site posts `install` and `report`. The pipe is built; the sending is the
owner's.

## The pulse: what looks from outside

A service that is down posts nothing, and on this table "no events" reads
as "no usage". So one thing looks from outside: `server/pulse/pulse.sh`,
run by `.github/workflows/crossplay-pulse.yml` every 30 minutes on GitHub's
runners, probes every line of `server/pulse/hosts.txt` and posts one
`pulse`/`probe` event per host, `info` when the host answers with a status
its line allows, `error` otherwise. The error carries a fixed fingerprint
per host, so an outage is one card however long it lasts, and a fresh card
when it comes back after the first was closed. The same run checks the
daily upstream sync: upstream commits xteink lacks, older than 30 hours,
with no `sync/upstream-*` pull request open, is an error.

**When your service goes live, add its line to `hosts.txt`** with the
method, the path, the status it really answers there, and your app's name
(the card for an outage lands on that app's owner) (a 401 from a
Basic-auth root is fine, and is what proves the service is up). A host
listed before it exists opens a card every half hour.

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
