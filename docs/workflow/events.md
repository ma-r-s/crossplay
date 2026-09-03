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

Once a day, when the device has Wi-Fi, the firmware posts one event:
`{"service":"firmware","event":"heartbeat","device":"<hash>","version":"1.12.9","board":"x4pro","props":{"apps":["trivia","hackernews"],"uptime_h":31}}`.
`apps` is the set opened since the last heartbeat. That single event answers
"how many devices are on which version" and "how many use each app". A
switch in Settings turns it off, and the site says so in one sentence.

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
