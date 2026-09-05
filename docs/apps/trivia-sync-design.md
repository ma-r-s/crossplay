# Trivia: report a question, filter what you get, sync the pack

Design for board card #257, child of #253. Nothing here is built yet. This is
the shape, the ranked alternatives, and what each one costs.

Mario's words on the card are the spec. Where this document departs from them
it says so and why.

## What already exists, so this is not re-derived

Five things are already in the tree and the design leans on all of them.

**A report button, under another name.** `ActionFlag` is drawn as `HIDE` and
sets `kFlagged` in `pack.state` (`TriviaScreens.cpp:210`,
`TriviaActivity.cpp:495`). The doc calls it "the whole curation loop". It
takes no reason, it exists only in QUIZMASTER, and the indices are read back
off the card **by hand** into `tools_local/trivia/verdicts.tsv`. So the local
half of reporting is done; what is missing is a reason, a route off the
device, and a presence in SOLO.

**A settings screen of Trivia's own.** `SettingRow` has exactly one row,
`US QUESTIONS`, since card #311 (`TriviaScreens.cpp:367`). That is the surface
new options go on. `TriviaScreens.h` already argues which kind of option
belongs there and which belongs on the front door; that argument is followed
below rather than reopened.

**A transport that already does everything a sync needs.**
`src/apps_local/bridge/BridgeHttp.h` is service-agnostic: `bridge::request`
does POST with a JSON body over verified TLS, `bridge::streamToFile` streams a
GET to a `.part`, and `identify()` attaches `User-Agent`,
`X-CrossPlay-Device`, `X-CrossPlay-Board` and `X-CrossPlay-Report` to any
request bound for one of our hosts. Instapaper uses it; Study has the older
copy. **A trivia sync is a new `Endpoint` and two calls, not a new stack.**

**A public unauthenticated report endpoint, already shipped.**
`site/api/report.js` takes a stranger's bug report with no account, caps every
size, runs a honeypot, rate-limits on a salted hash of the address so the
address is never stored, and writes to the board with the service key. It is
the precedent for the trivia endpoint and most of it is copyable.

**An events table and a device-identity convention.** `docs/workflow/events.md`
defines `events` on the board and the three device headers; `X-CrossPlay-Device`
is `sha256(MAC + a per-device secret that never leaves the device)`, so it is
pseudonymous by construction and already has an off switch in Settings > System.

## D1. What a question's identity is

**This is the decision the whole card hangs on, and getting it wrong makes the
feature self-defeating.** Card #146: pack ids are `sha1` of the clue text and
`pack_format.py` re-derives them. So if a report names a question by content
hash, then **repairing the reported question changes its id and orphans the
report that caused the repair.** The report survives pointing at nothing, and
the same question can be reported forever.

### The options

|     | Identity                                                       | Survives a text repair | Survives a rebuild       | Cost                                                                 |
| --- | -------------------------------------------------------------- | ---------------------- | ------------------------ | -------------------------------------------------------------------- |
| A   | sha1 of the clue (today's `verdicts.tsv` key)                  | **no**                 | yes                      | zero, and it is the bug                                              |
| B   | a new immutable id minted per question, carried in the record  | yes                    | yes                      | format bump, +8 bytes x 50k, and the corpus has no such column today |
| C   | `(pack id, index)`, resolved to a corpus row **on the server** | yes                    | yes, via a per-build map | a manifest the builder already has the data for                      |

### Recommendation: C

**The device names a question by where it sat: `(pack id, index)`. The server
resolves that to a corpus row.**

This works because of a fact the pack format already guarantees and #146 already
noticed: _index stability was preserved while id stability was not._ `pack.state`
is index-addressed, one byte per question at the question's own index. The index
is the only handle the device has that is cheap, already correct, and already
load-bearing.

Three consequences worth stating plainly:

- **The device never needs a stable global id.** It cannot compute one anyway
  without carrying one in the record. Pushing resolution to the server is what
  makes B's format bump unnecessary.
- **The builder must emit a map.** For each build, `index<TAB>corpus_id`, published
  beside `pack.dat` as a release asset the device never fetches. `build_pack.py`
  and `assemble_pack.py` both have the corpus row in hand when they write the
  record; this is a `print` in an existing loop, not new machinery.
- **A pack id must exist at all.** Today the header carries `flags` and `resv`,
  both reserved zero, and no build stamp — `trivia-pack-format.md` names this as
  "the residual: length is a proxy, not an identity". See D3; the manifest carries
  the id and the device stores it beside the pack, so **no format bump is needed
  for this either.**

### The precondition, and it is not safe today

D1 resolves reports server-side against the corpus. **So the corpus has to
exist.** Checked rather than assumed, and the answer is uncomfortable:

- `docs/apps/trivia-pack-format.md` says the season TSVs `build_pack.py --src`
  wants "are not in this repo and not on this machine", and that a published
  `pack.dat` is "the only surviving copy of its corpus". Card #225 says the same.
- The rated corpus **does** exist — `wt/localrate/.rate/corpus_repaired.jsonl`,
  50,000 rows, each carrying the `id` this design wants to join on.
- It lives in **one gitignored scratch directory inside one worktree**
  (`.rate/` is ignored at `.gitignore:78`), and `./scripts/wt.sh prune` drops
  every merged, clean, idle tree.

So the single copy of the join table this feature depends on is one prune away
from being gone, and nothing would report an error — the reports would simply
stop resolving, months later, with no way to reconstruct why.

Filed as card **#326**, because the risk is real whether or not this design
ships: `corpus_repaired.jsonl` is also the one file card #146 names as holding
pre-repair ids beside post-repair text, so losing it re-opens #146 permanently.

**This is a blocker on collecting reports at all, and it is cheap to clear:**
publish the `index -> corpus_id` map for each pack as a release asset beside
`pack.dat` at build time. That asset is small, is versioned by the release it
sits in, and moves the dependency from a scratch directory to the same place the
pack itself already lives. Do it before the first report is accepted, not after.

### What happens to a report for a question that no longer exists

Four cases, all resolved server-side, none of them an error the player sees:

1. **The corpus row still exists.** The report joins the queue. Normal case.
2. **The row was already removed** (an earlier report won). The report is
   recorded as a _confirmation_ and closed. It still counts: three people
   independently confirming a removal is evidence the removal was right.
3. **The row was repaired since.** The report is attached, and marked
   `stale_since=<repair build>` so a human reviewing it knows the text they are
   being asked to judge is not the text the reporter saw. It is not dropped:
   "we fixed it and it is still wrong" is exactly the signal that matters.
4. **The pack id is unknown** (older than the retained manifests, or garbage).
   Rejected with a counted reason. Manifests are small; keep every one, and this
   case is only reachable by a forged request.

## D2. What a report record contains

```json
{
  "pack": "2026-09-05a",
  "reports": [{ "i": 4711, "r": "wrong" }, { "i": 812 }]
}
```

That is the whole thing. A pack id, and a list of indices with an optional
reason code.

**Nothing identifies a person, and nothing new identifies a device.** No
timestamp (the server's arrival time is close enough and cannot be used to
correlate a device across syncs), no score, no session, no counter, no
free text. Free text is the one omission worth defending: it would be the most
useful field and it is also the only one that can carry something a player
should not be posting from a device with no keyboard and no way to review what
they sent.

**One caveat that must be said out loud rather than assumed away.** The
transport attaches `X-CrossPlay-Device` to every request to a `ma-r-s.com`
host, by the card #125 convention, unless the owner turned the headers off. So
a report _is_ associated with a pseudonymous device id at the moment it
arrives, whatever the body says.

That is not an accident to route around, and there are two honest positions:

- **Keep it** (recommended). Without it, forty reports of one question from one
  annoyed player is indistinguishable from forty players agreeing, and telling
  those apart is the entire value of the queue. The id is already documented,
  already pseudonymous, already toggleable, and the alternative is inventing a
  _second_ identifier, which is strictly worse. The service should use it **only**
  to de-duplicate — count distinct devices per question, store the count, and
  never store the id on the report row itself.
- **Suppress it** on the report path specifically. Cheap to do; costs the
  de-duplication and buys nothing the existing toggle does not already buy.

Recommend keep-and-discard-after-counting, and write that rule into the service's
README so it cannot quietly become a device history later.

## D3. Freshness, versioning, and whose problem it is

The parent card's read is that versioning is shared machinery for #253. **That
is right for freshness and wrong for incrementality, and the two have been
running together.** Splitting them is the main correction this design makes.

### The three packs are not the same shape

| Pack       | Bytes                                            | Container                                                                                       | How it changes                                    |
| ---------- | ------------------------------------------------ | ----------------------------------------------------------------------------------------------- | ------------------------------------------------- |
| trivia     | `pack.dat`, 3.4-5.4 MB                           | one blob, index + records                                                                       | rows **removed and edited** in place              |
| xkcd       | `index.dat` + `text.dat` + `images.dat`, ~140 MB | three blobs                                                                                     | **append only** — comics are added, never revised |
| wallpapers | `wallpapers.dat`, 1,009,302 bytes                | **no format at all** — a bare concatenation of 48062-byte images, no magic, no header, no count | a set: added and removed                          |

So _"can I tell whether I am stale, and what will it cost me to fix that"_ is one
question with one answer for all three. _"Can I fetch only the difference"_ is
three different mechanisms:

- **xkcd** is append-only, so its difference is a byte range at the end — or a
  second asset holding comics since N. It also already has a live incremental
  path (`runUpdate()` asks xkcd.com what is new) entirely separate from its bulk
  pack fetch.
- **wallpapers** is a set of independent items and already skips items present
  on the card.
- **trivia** is the only one that gets _edited_, and editing is what a blob
  cannot do incrementally.

**Recommendation: the shared thing that goes to #253 is a manifest and a sync
UX. The incremental mechanism stays with each pack, and trivia's is D3b below.**

**A sidecar manifest is the only shape that fits all three**, and that is forced
rather than chosen: wallpapers has nowhere to put a version, because it has no
header. Two in-tree precedents to copy rather than invent, both already
shipping:

- `src/activities/settings/FontDownloadActivity.h` — a schema-versioned JSON
  manifest (`FONTS_MANIFEST_VERSION`, rejected on mismatch) whose per-file
  entries carry `name`, `size` and **`crc32`**. It downloads the manifest to a
  temp file first, specifically so the TLS buffers are freed before parsing.
- `src/apps_local/study/StudySync.h`'s `DeckManifest`, which already has the
  **`buildId`** concept this design calls a pack id. Read its warning first: the
  download is verified against the manifest's **length**, not its `sha256`, so
  that field is not proof of anything today.

Concretely, #253 should own:

```
GET https://<host>/packs/<name>.json
{ "id": "2026-09-05a", "built": "2026-09-05T04:00:02Z",
  "bytes": 3552104, "count": 25866, "parts": [...] }
```

...a `PackMeta` helper that stores the id the card holds in a sibling file
(`/trivia/pack.meta`, `/xkcd/pack.meta`), and one sync screen shape that can say
_"you are current"_ or _"a newer pack is ready, 3.4 MB, a few minutes"_ **before**
spending the radio. That is the piece all three want, and it is the piece that
makes a sync button honest rather than a spinner.

Two things that must ride along, both already known:

- **Take xkcd's paint-then-fetch pattern** (`XkcdActivity.cpp:759`), do not
  re-derive it. #253 says so; card #244 needs the same fix.
- **Take the input pump too.** `src/components/BlockingFetchInput.h` already
  encodes the pump-Back-during-a-blocking-fetch policy as
  `pumpBlockingFetch(input, cancelled, goHome)`, and **only the two OPDS flows
  use it** — trivia, xkcd and wallpapers each open-code the same five lines. Three
  copies of a rule about cancelling a multi-minute download is exactly the
  `fix-the-twin-too` shape.
- **No transport can resume today, and it is structural.**
  `HttpDownloader::downloadToFile` takes no header parameter, so a caller cannot
  inject `Range` even if it wanted to — and it calls `Storage.remove()` on the
  destination **before the first byte**, and again on any failure, so there is no
  partial file to resume from. `bridge::streamToFile` likewise. **But the
  capability exists one layer down**: `freeink::SecureHttpClient` already has
  `addHeader`, `sendRequest(method, ...)` and `getHeader`, so resume is an
  optional `resumeFrom` on `HttpDownloader` plus a conditional truncate plus
  reading `Content-Range` — a contained change, not a new stack. Nothing anywhere
  in the tree currently sends `Range`, `If-None-Match` or a `HEAD`.

### D3b. Trivia's own incremental step, and why it comes second

The natural fit for an edited blob, and it composes with D1:

**`pack.dat` becomes immutable and gains an overlay.** A small `pack.patch`
beside it carries, against a named base pack id: indices to hide, and
replacement records for indices whose text was repaired. Appended questions get
a second blob. The reader consults the overlay before the base.

Why this is the right shape and not just a clever one:

- **It preserves index stability by construction**, which is what keeps
  `pack.state` valid across an update — the exact failure
  `trivia-pack-format.md` warns about ("a stale FLAGGED byte landing on an
  arbitrary question hides it from every draw with nothing on screen").
- **It makes D1's identity permanently true.** The base pack never moves, so
  `(pack id, index)` never stops meaning what it meant.
- **The common case is kilobytes.** Removals and text repairs are exactly what a
  report queue generates, and they are tiny.
- The cost is a reader change in `TriviaCore` and a rebase step when the base
  pack is eventually replaced wholesale.

**But it is phase two.** Phase one is a manifest and a sync button that can say
what it will cost, because today `ActionGetPack` is reachable **only** from the
empty-card and error notices — there is no way to update a pack once one exists
at all. Fixing "cannot update" beats optimising "update is slow", and #253 says
the same in its own order of work.

## D4. The report UI

**Reports are queued on the card and flushed on sync.** Not a choice worth
ranking: the device is usually off Wi-Fi, and a report that needs a radio at the
moment of annoyance is a report that never happens. The queue is a small file,
`/trivia/reports.dat`, fixed-width like everything else here: `uint32 index`,
`uint8 reason`, appended, capped, and truncated on a 2xx.

`pack.state`'s `FLAGGED` bit stays and keeps its job (never serve me this
again). The queue is the outbound copy. Two files because they answer different
questions and have different lifetimes — the flag is local and permanent, the
queue is drained.

### Where the button goes

Today: `NEXT` / `HIDE` / `END` in QUIZMASTER once the answer is showing, and in
SOLO **no report control at all**. That gap matters more than the reason list:
solo is the mode where _the options give it away_ and _wrong answer_ are
visible, and it is the mode a single player uses.

The rules this fork already learned apply directly:

- **`same-pixel-different-action`**: never insert a row where a remembered tap
  lands. The four solo options are anchored to the bottom at 70px pitch and
  `drawAsideAction` owns the footer aside. A report control must go in the
  **aside**, where `END` and `HIDE` already live, and must not push the option
  stack.
- **`screens-overflow-silently`**: 24 interaction slots. Solo already spends four
  on options plus the footer. A reason list drawn _on the question screen_ is how
  you find that ceiling.
- **`a-tap-is-a-touch-down`** and the `HIDDEN` notice precedent: the action must
  say it happened. `ActionFlag` already learned this once.

So: **the report control is the existing aside, offered in both modes, and only
once the answer is showing** — the existing rule, and it is right: you cannot
judge a question until you have seen what it claims.

### One tap files it; the reason is an optional second

Mario's card already reaches this conclusion and it is correct: _"a report with
no reason is still a report, and demanding a category is how you get no
reports."_

So `HIDE` keeps its current behaviour exactly — one tap, question gone, `HIDDEN`
notice — and the `HIDDEN` notice gains one extra control: `WHY?`. Tapping it
opens a **separate reason screen** (a list, its own view, its own interaction
budget) which amends the queued report in place. Ignoring it leaves a reasonless
report, which is still useful.

This costs nothing on the play screens, adds no mis-tappable neighbour, and the
reason list gets a whole screen to be legible on, which eight rows on e-ink
needs.

### The reasons

Mario's five kept as filed, plus the additions the card argues for. Ordered by
how often a player will reach for them, not alphabetically:

| Code            | Label                     | Note                                                                  |
| --------------- | ------------------------- | --------------------------------------------------------------------- |
| `wrong`         | WRONG ANSWER              | the one that makes a question unplayable rather than annoying         |
| `nonsense`      | MAKES NO SENSE            | Mario's                                                               |
| `giveaway`      | THE OPTIONS GIVE IT AWAY  | **solo only**; a shipped regression that only a player detects        |
| `ambiguous`     | MORE THAN ONE ANSWER FITS |                                                                       |
| `outdated`      | OUT OF DATE               | true when written; a different fix from `wrong`                       |
| `broken`        | BROKEN TEXT               | turns card #147's scavenger hunt into a queue                         |
| `regional`      | ONLY A LOCAL COULD KNOW   | Mario's "regionally impossible"                                       |
| `us`            | TOO AMERICAN              | **only when US questions are OFF**, as Mario specified                |
| `hard` / `easy` | TOO HARD / TOO EASY       | Mario's "wrong difficulty", split — the signal is one-sided otherwise |

Ten codes, at most nine ever shown at once, and two of those are conditional.
That is a two-page list on this panel, which is a real cost — but it is a cost
paid on a screen nobody has to visit.

**One departure from the card worth flagging:** Mario listed "wrong difficulty"
as one reason. Split into TOO HARD and TOO EASY it is actionable (it moves the
level in a known direction); unsplit it is not. TOO HARD is also already on his
list separately, which is the tell that the split is what he meant.

## D5. Filtering

Only what the report reasons imply, per the brief. Everything here maps to a
reason a player can file:

| Filter                    | Where                    | Implied by    | Verdict                                                                                                                                                             |
| ------------------------- | ------------------------ | ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| US questions              | SETTINGS (**shipped**)   | `us`          | done, build on it                                                                                                                                                   |
| Hide questions I reported | nothing — already true   | all           | `FLAGGED` already does this. Say so on the screen; do not build it twice.                                                                                           |
| Region-locked generally   | SETTINGS                 | `regional`    | **needs a pack flag that does not exist.** The difficulty byte's bit 7 is spent on US-centric; there is no second free bit. Costs a format bump. Defer, and say so. |
| Difficulty                | front door (**shipped**) | `hard`/`easy` | done                                                                                                                                                                |
| Category on/off           | —                        | nothing       | **the record has no category field.** `build_pack.py:300-316` reads the category to drop wordplay and category-dependent clues, then does not store it. Not a filter that can exist without a format change. Cut.                                                                  |
| Language                  | —                        | nothing       | `trivia-sources` records Spanish barely exists. One honest setting is not a filter. Cut.                                                                            |

So the honest answer to "what does filtering mean beyond the toggle that
exists" is: **almost nothing new, today.** One row is already there, one is
already true and merely unsaid, and the two genuinely new ones are blocked on
data the pack does not carry. That is a better answer than four new rows that
filter nothing.

The one row worth adding now is a **statement, not a filter**: SETTINGS should
say how many questions this card is hiding and offer to un-hide them all. There
is no way to undo a mis-tapped `HIDE` today, and `flaggedCount()` is already
computed and already on screen elsewhere.

## D6. The service

### Ranked

**Option 1 — one endpoint on the existing site (Vercel), recommended.**

`site/api/trivia.js`, beside `firmware.js`, `report.js`, `board-config.js`,
`inbox.js`. `GET` returns the manifest; `POST` takes the report batch and writes
`events` rows on the board. `site/api/report.js` is a working template for
almost all of it.

- **Costs nothing new.** No container, no tunnel route, no `firewall.sh`, no
  `isolation_test.sh`, no pi uptime in the path of a game.
- **The threat model is genuinely empty**: no credentials, no account, no
  session, no upstream, nothing to steal. It is the easiest of the four services
  to get right, and the only one where that is true.
- **The pi is a single ARM box behind one tunnel.** Adding a fourth service to it
  puts a game's sync behind the same box that syncs Anki and Instapaper, for no
  benefit — trivia has no long-lived state, no credentials, and no reason to be
  stateful at all.
- **One real drawback, and it must not be glossed:** `crossplay.ma-r-s.com`
  resolves to `76.76.21.21` — Vercel directly, **not proxied through
  Cloudflare**, unlike `read.` and `sync.` which resolve to Cloudflare
  addresses. So the zone's WAF rate-limiting rules **do not apply to it**, and
  `site/api/report.js` is protected only by its own in-function counter. Either
  proxy the hostname through Cloudflare (a dashboard change Mario makes, which
  also fixes `report.js`) or accept that the in-function limit is the only
  limit. Given `free-plan-pins-the-rate-limit` — a 10s window is all the free
  plan sells, so the edge was never the binding constraint anyway — accepting it
  is defensible. Say it rather than imply an edge that is not there.

**Option 2 — a fourth bridge on the pi (`trivia.ma-r-s.com`).** The card's own
suggestion, and the house pattern. It is correct and it is much more machinery
than this job needs. The pattern is not "a FastAPI app": it is a free uid
(10004) and a pinned /24 (`172.31.86.0/24`), a `Dockerfile`, a `compose.yaml`
with its own `cloudflared` sidecar, a `firewall.sh` with a matching systemd
unit, an `isolation_test.sh` probing four targets from two vantage points, a
`Service` profile added to `server/attacks.py` with a fake upstream and a victim
sentinel, a `verify_attacks.sh` round, a port offset in `check.sh`, a
byte-identity test for the copied `events.py` and `chrome.py`, and a hostname
that must be exactly one label below the apex. All of that exists because those
two services hold credentials. **This one holds none**, so nearly every line of
it would be ceremony. Choose option 2 only if trivia later needs state the board
cannot keep.

Worth noting in option 1's favour rather than as an aside: the bridges' rate
limiters are **in-memory and die with the process**, so a restart resets every
counter. `report.js` counts against the database instead, which survives. On a
function there is no process to hold a counter in, so the durable form is the
only one available — the constraint and the better answer coincide.

**Option 3 — the manifest from the GitHub API, no new endpoint at all.**
`src/network/OtaUpdater.cpp` already streams `api.github.com` release JSON
through `ReleaseJsonParser`, and an asset's `size` and `updated_at` would tell a
device that something changed. Rejected, for two reasons rather than one: the
packs are **prereleases**, so it would need `/releases/tags/<tag>` rather than
`/latest`; and more importantly the API can say _that_ the asset changed but not
**what version it now is**, and D1 needs a pack id the server can join on. A
200-byte JSON asset published beside `pack.dat` answers both and costs one more
`gh release upload`.

**Option 4 — no service: reports stay on the card.** The manifest half genuinely
works this way and should be built that way under option 1 regardless. The
report half does not: without a route off the device, reporting stays what it is
today — a thing that needs a card reader and a person.

### Rate limiting and abuse

The endpoint is public, unauthenticated, and writes to a database. Precedent is
`REPORT_IP` / `GLOBAL_REPORT` (30 per 5 min, 240 per min) and `report.js`'s
10 per hour.

- **Per address, 20 batches per hour**, counted against a salted hash of the
  address, exactly as `report.js` does it — the address itself is never stored.
- **Batch caps**: at most 64 reports per request, at most 8 KB of body,
  `index < count` of the named pack, reason must be a known code. Anything else
  is a 400 with a sentence.
- **Per pack, per index, per device: one.** Re-reporting the same question is
  idempotent, which removes the incentive to loop.
- **A global ceiling** so a distributed flood cannot fill the table.
- **No CAPTCHA and no honeypot.** `report.js` needs a honeypot because a form
  bot fills every input; there is no form here.
- **The reports are advisory.** Nothing auto-removes a question. A report opens
  a queue a human reads before the next build — which is also the real abuse
  answer: poisoning the queue costs an attacker effort and buys them a row a
  person will ignore.

### Where reports land

`events`, with `service: "trivia"`, `event: "report"`,
`props: {pack, index, reason, corpus_id}`. No new table for the first cut:
the table exists, the Numbers page already reads it, and the build tool wants a
query rather than a schema. A dedicated table earns its place only when
`verdicts.tsv` starts being generated from it, and that is the same commit that
would create it.

## What this design does not do

- **It does not fix the fact that nobody can receive a new pack today.** This is
  not a hypothetical: `docs/open-items.md:52` already records it — _"a device
  that already has `/trivia/pack.dat` never re-downloads, so publishing does not
  reach an existing install; the file has to be deleted."_ Phase one of this
  design is what closes that item, and it is worth saying that the sync button's
  first job is not speed, it is **existing**.
- **It does not make the pack rebuild itself.** #253 names this as the thing
  that makes a sync button honest, and it is true: nothing under
  `.github/workflows/` builds either pack today. A sync that faithfully fetches
  a months-old asset is honest and useless. **This is the dependency, and it is
  #253's, not this card's.**
- It does not add the overlay (D3b) in the first cut.
- It does not add region-locked filtering, which needs a format bump.
- It does not give the pack a content hash in its header. The manifest carries
  the id and the device stores it beside the pack; the "length is a proxy"
  residual in `trivia-pack-format.md` stays open, and this design does not close
  it.

## Order of work

1. **#253 first, for the shared half**: a manifest per pack, a `PackMeta`
   sibling file, and one honest sync screen. Trivia consumes it.
2. Builder emits the `index -> corpus_id` map per build, and a pack id.
3. `site/api/trivia.js`: manifest `GET`, report `POST`, events rows.
4. Device: the report queue, `WHY?` on the `HIDDEN` notice, the reason screen,
   `HIDE` in solo, and the un-hide row in SETTINGS.
5. `verdicts.tsv` generated from the queue rather than typed.
6. Only then, D3b's overlay.
