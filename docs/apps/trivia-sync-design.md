# Trivia: report a question, filter what you get, sync the pack

Design for board card #257, child of #253. Nothing here is built yet. This is
the shape, the ranked alternatives, and what each one costs.

Mario's words on the card are the spec. Where this document departs from them
it says so and why.

## What Mario is being asked to confirm

Five things. Everything else here is a code-level call and does not need him.

1. **Reports go to one endpoint on the existing site, not a fourth service on
   the pi.** He guessed a new service; the answer is yes, but the smallest
   possible one, and the site already has four functions of exactly this shape.
2. **A report carries no free text.** One tap files it, an optional second tap
   picks a reason from a list. Nothing typed, because there is no keyboard and
   no way to review what was sent.
3. **The existing pseudonymous device header rides along**, as it does on every
   other request to his hosts, and the service uses it only to tell "one person
   reported this forty times" from "forty people reported it once", then throws
   it away. The alternative is losing that distinction.
4. **"Wrong difficulty" becomes two buttons, TOO HARD and TOO EASY.** Unsplit it
   is not actionable.
5. **The first cut makes updating possible, not fast.** Today a device that has
   a pack can never receive a new one. Fixing that comes before making it
   incremental, and incremental is a second piece of work.

Two things are cut that he asked about, both because the data does not exist:
**category filtering** (the record has no category field) and **language**
(Spanish barely exists in the corpus). Region-locked filtering beyond US-centric
needs a format change and is deferred rather than cut.

**And one thing he should probably push back on, which a cold reviewer raised
and I did not cut on my own authority.** The evidence says the service is
premature: `tools_local/trivia/verdicts.tsv` holds **one verdict in its entire
life**, `flags.txt` (which `docs/trivia-curation.md:165` says the device writes)
**does not exist anywhere in the tree**, and there is no tool that reads flags off
a card at all. So the curation loop this card wants to put on the internet has
never once run locally. A public, rate-limited, de-duplicating endpoint is
machinery for a volume of reports that has never existed.

The counter-argument is that CrossPlay is published and the loop has never run
*because* it needs a card reader and a person. Both are true. It is his call,
and it is only about **when**, not whether — nothing else in this design depends
on the service.

One thing needs doing whether or not any of this ships: **card #326**, the rated
corpus lives in one gitignored scratch directory that `wt.sh prune` will delete.

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

Checked live rather than read: an empty `POST` to
`https://crossplay.ma-r-s.com/api/report` answers **400** with the handler's own
sentence. That is the evidence that matters, because the very first thing the
handler does is return **503** when `SUPABASE_URL` or `SUPABASE_SERVICE_ROLE_KEY`
is missing. A 400 therefore proves the function is deployed, routed, **and**
holds working board credentials — so a second function beside it needs no new
environment, no new project and no new secret.

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
| B   | a new immutable id minted per question, carried in the record | yes | yes | format bump, +4 bytes x 50k (~6%). **The sticky column DOES exist** -- in the scratch corpus, and nowhere in git |
| C   | `(pack id, index)`, resolved to a corpus row **on the server** | only via a manifest **captured at build time** | yes | a manifest the builder must emit and something must keep forever |

### Recommendation: C

**The device names a question by where it sat: `(pack id, index)`. The server
resolves that to a corpus row.**

This works because of a fact the pack format already guarantees and #146 already
noticed: _index stability was preserved while id stability was not._ `pack.state`
is index-addressed, one byte per question at the question's own index. The index
is the only handle the device has that is cheap, already correct, and already
load-bearing.

**The thing C does NOT get for free, found by the critic round.** `corpus_id` is
not a stable id: `build_pack.py:341` mints it as `sha1(norm_key(clue))[:12]` —
option A's key, by another name. So "index → corpus_id" is "index → a hash of the
clue **as it read on build day**", and a later repair moves it.

C still works, but only for a reason that has to be built rather than assumed:
**the manifest is a snapshot, captured at build time, and kept.** The old pack's
manifest holds the old id; `corpus_repaired.jsonl` holds old-id-beside-new-text
(`assemble_pack.py:55-66`, and card #146 is exactly this). Resolution walks that
chain. Which means the first draft's objection to option B — "the corpus has no
such column today" — was **backwards**: the sticky column is the one thing that
does exist, in a scratch file, and nowhere in git.

So C's real cost is not "a manifest the builder already has the data for". It is
**a manifest that must be emitted and then kept forever**, and the section below
is why that is not a safe assumption today.

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

### The strongest argument against C, which is not weak

Option B carries an immutable id inside each record. It costs a format bump and
4 bytes x 50,000 = 200 KB on a 3.4 MB pack, about 6% — on the very download this
card is trying to shorten. That is why it was ranked second.

But B buys one thing C does not, and the corpus finding below makes it matter
more than it first appears: **a pack carrying its own ids is self-describing.**
`pack_format.py` already reads a pack back out precisely because "a published
pack.dat is the only surviving copy of its corpus" — and today what comes back
is a re-derived hash that its own module says is not a join key. With B, dumping
a pack recovers the real ids, and a lost corpus is recoverable from any published
pack. With C, a lost manifest is a lost join and nothing can rebuild it.

So the honest ranking is: **C if the manifests are kept somewhere durable, B if
they will not be.** C is recommended because the manifest is small, is published
in the same release as the pack it describes, and never has to be fetched by a
device — which is a lower bar than "remember to keep a scratch directory". But
that recommendation is conditional on the fix below actually being made, and if
it is not made, B is the safer design and worth its 6%.

Both are strictly better than A, which is what `verdicts.tsv` uses today.

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

This matters because de-duplication is the whole value of the queue: forty
reports of one question from one annoyed player must not read as forty players
agreeing, and only a stable id can tell those apart.

**But "use the id only to count distinct devices" does not survive being written
down.** To know whether this device already reported this question, something has
to remember that it did — and a table of (device, question) IS a device history,
whatever the column is called. Saying "we only use it for counting" is the kind
of rule that holds until someone writes a useful query.

So the id is never stored, and never even reaches the row. What is stored is

```
report_key = sha256(server_secret || device_id || pack_id || index)
```

...used as a uniqueness constraint and nothing else. This keeps exactly the
property wanted and loses exactly the one not wanted:

- A second report of the **same question** from the same device collides, so it
  is idempotent and the count stays honest.
- Two reports of **different questions** from the same device share nothing. The
  key is per-question, so the rows cannot be joined into a history.
- The secret is server-side, so the keys cannot be probed offline by anyone who
  guesses a device id — which, since the id is `sha256(MAC + a device secret)`,
  is already infeasible, but the salt means it does not have to be relied on.
- `site/api/report.js` already does this shape with `reporterHash`, a salted
  hash of the address kept "so the address itself is never stored". Same trick,
  same reason.

The alternative — **suppress the header on this path** — stays available and
costs only the de-duplication. It is worth doing if the hashing above is judged
not worth the complexity, but it should be a decision, not a default: with no
de-duplication at all, one determined player can drown the queue by tapping
`HIDE` repeatedly, and nothing downstream can tell.

Write the rule into the service's README beside the code, because this is
precisely the constraint that erodes quietly.

## D3. Freshness, versioning, and whose problem it is

The parent card's read is that versioning is shared machinery for #253. **That
is right for freshness and wrong for incrementality, and the two have been
running together.** Splitting them is the main correction this design makes.

### The starting position, stated plainly

**Nothing is shared between the three packs today.** Verified against the live
releases on 2026-09-05, not inferred: each is its own **prerelease tag** with its
own assets (`trivia-pack/pack.dat`, `xkcd-pack/{index,text,images}.dat`,
`wallpapers/wallpapers.dat`), each app fetches its own thing its own way, and
updating one touches nothing else. There is no shared manifest, no common
freshness check, no shared download path and no shared wording. A reader who
assumes some sharing already exists will misread this whole proposal: the
sharing is what is being *proposed*, and the current count of shared machinery
is zero.

### Every size, measured, and labelled with which one it is

Two different quantities have been travelling under one name in this document,
and each is defensible alone, which is worse than one wrong number. **Bytes over
the wire** size the sync-and-incremental argument this card exists for. **Bytes
resting on the card** size the free-space precondition and decide what happens
to someone whose card is nearly full. Measured from the live releases:

| Pack | Over the wire | On the card | Note |
| --- | --- | --- | --- |
| trivia | 6,624,675 B (6.6 MB) | 6,674,633 B | +`pack.state`, exactly `count` bytes; the live pack is **49,958** questions, so 49,958 B |
| xkcd | 140,179,380 B (140.2 MB) | the same | **not unpacked** -- the three `.dat` files are the card layout, and `runUpdate()` appends to `images.dat` in place |
| wallpapers | 1,009,302 B (1.0 MB) | the same, as 21 files of 48,062 B | the `.dat` is exactly the concatenation, so unpacking changes the shape, not the total |

**The 217 MB figure in `xkcd-pack-format.md:211` describes a pack that was never
published.** It is not a card-versus-wire confusion, which was the obvious
theory and is wrong: xkcd is not unpacked, so its two numbers are the same
number. The live `index.dat` settles it. Its records imply an `images.dat`
ending at exactly 139,590,525 bytes, which is exactly the published file's size,
so the released pack is internally complete and consistent — but it carries
**247** comics with a closer rendition where the doc's 217 MB build has **493**.
Same 3,279 comics, different rendition policy. The documented build exists on one
machine and the published one is a different, earlier artifact.

That is #253's central claim turned into a number: *"nothing regenerates the
pack, so the device can only ever be as fresh as the last time a person
remembered."* The published pack is not stale by a few comics; it is a different
build from the one the documentation describes.

### The host already supports Range, so incrementality is entirely our client

Worth correcting prominently, because the first draft treated resume as
structurally impossible. It is not. Verified against the live release:

```
$ curl -r 0-15 .../releases/download/trivia-pack/pack.dat
HTTP/2 206
accept-ranges: bytes
content-range: bytes 0-15/6624675
```

The CDN honours byte ranges and reports the full length. **So nothing on the
server side blocks resuming or fetching a difference** — the only obstacle is
that `HttpDownloader::downloadToFile` exposes no header parameter and removes the
destination before the first byte. `freeink::SecureHttpClient` already has
`addHeader` and `getHeader`, so this is a contained change to one function, not a
new stack.

It also hands the freshness check a cheaper shape than a sidecar manifest: a
16-byte Range request returns the pack's own header **and** `content-range` gives
the total size, so one tiny request answers "how many questions and how big" with
no new asset to publish. It does not answer "which build", because the header's
`flags` and `resv` bytes are zero and carry no id — which is the gap a manifest
or a format bump fills, and the reason the manifest is still recommended.

### The three packs are not the same shape

| Pack       | Bytes                                            | Container                                                                                       | How it changes                                    |
| ---------- | ------------------------------------------------ | ----------------------------------------------------------------------------------------------- | ------------------------------------------------- |
| trivia | **6,624,675 B** wire / **6,674,633 B** card (+ `pack.state`, one byte per question) | one blob, index + records | rows **removed and edited** in place |
| xkcd | **140,179,380 B** wire (3 assets) / same on card, it is not unpacked | three blobs | content appends; the **artifact is rebuilt and `--clobber`ed wholesale** |
| wallpapers | **1,009,302 B** wire / same, as 21 files of 48,062 B | **no format at all** -- a bare concatenation, no magic, no header, no count | a set: added and removed |

So _"can I tell whether I am stale, and what will it cost me to fix that"_ is one
question with one answer for all three. _"Can I fetch only the difference"_ is
three different mechanisms:

- **xkcd**'s _content_ appends, so its difference is usually a byte range at the
  end. Its _artifact_ does not: `xkcd-pack-format.md:38-51` says the hosted pack
  is rebuilt and `gh release upload --clobber`ed, and the closer-rendition change
  rewrote it. A ditherer tweak rewrites every byte of a 217 MB file — exactly
  when a byte-range delta is most wanted and exactly when it stops working. It
  does already have a live incremental path (`runUpdate()` asks xkcd.com what is
  new) separate from the bulk fetch.
- **wallpapers** skips images already on the card, but at **unpack** time
  (`WallpapersActivity.cpp:720`), after downloading the whole `wallpapers.dat`
  (`:504`). That saves SD writes, not bytes. The conclusion holds because the
  file is 1 MB, not for the reason the first draft gave.
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

**A queued report can outlive the pack it names, and that is the one way this
design can lie.** An index only means something against a pack id, and the queue
is drained on a schedule nobody controls: a player can report ten questions, go
a month without Wi-Fi, and sync. If the pack were updated first, or if the queue
carried one pack id for the whole batch, those ten indices would be sent
labelled with a pack they were never filed against and would resolve, silently,
to ten wrong questions. `trivia-pack-format.md`'s own residual makes this worse
rather than better: a replacement pack with the **same count** keeps the state
file, so nothing on the device can even see that the indices now mean something
else.

Three rules close it, and all three are cheap:

1. **The pack id lives in the queue's header, not in the request.** The queue is
   `pack.meta`'s id plus the entries filed under it.
2. **Sync sends reports before it fetches anything.** The upload is small and the
   download is minutes; doing it the other way round is what creates the window.
3. **A pack update with a non-empty unsent queue does not re-label it.** The
   queue is closed under its old pack id and a new one is started beside it. Two
   small files beat one wrong one, and the server can resolve both because D1
   keeps every manifest.

The queue is truncated only on a 2xx. A queue that reaches its cap drops the
**newest** entry rather than the oldest: the first report of a question is the
one worth keeping, and a cap reached at all means sync has not run in a very
long time.

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
- **`screens-overflow-silently`**: 24 interaction slots (`kMaxInteractions`, `src/apps_local/ui/ToyboxScreen.h:45`). Solo already spends four
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

Ten codes, at most nine ever shown at once, because two are conditional: `us`
only when US questions are off, `giveaway` only in solo.

**Whether nine rows fit one screen has to be rendered, not calculated.** The
numbers say it is close and probably fits: `kRowHeight` is 62 with a theme row
gap, `kChromeHeight` is 76 plus the rule, and a `kPillHeight` of 52 goes to the
way out, on the X4 Pro's 800px portrait height. Nine rows at that pitch lands
within a few tens of pixels of the available band — which is the range where
this fork's `screens-overflow-silently` failure lives, and a row that does not
fit is drawn nowhere and answers nothing rather than erroring. Subtitles would
spend the margin outright, so the reason rows carry labels only.

So: build it as one screen, `sim-shot.sh` it, and page it only if the render
says so. Do not ship the arithmetic. The fork's own rule (three arrangements
rendered side by side before any new screen) applies here more than usual,
because this is a list whose length is a design output rather than a given.

**One reason is sharper than it looks.** With US questions off, `Chooser::next`
already skips marked records (`TriviaCore.cpp:214`), so a player with the toggle
off can only ever meet a US-centric question the pack **failed to mark**. `us` is
therefore not a taste report at all — it repairs a bit, not a row, and it is the
only reason whose fix is one byte. Word the row as "THIS IS A US QUESTION"
rather than "TOO AMERICAN", which invites the taste report the toggle already
handles.

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
| Region-locked generally | SETTINGS | `regional` | **needs a pack flag that does not exist**, and the obstacle is not the one it looks like -- see under the table. Defer. |
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

## The critic round

A cold agent with no context fact-checked this document against the tree. Four
claims were wrong and are corrected above. What follows is what it broke that
needed more than an edit.

### The local half is NOT done, and that reorders the card

The first draft opened by saying the flag indices "are read back off the card by
hand into `verdicts.tsv`", as though only the route off the device were missing.
Checked, that is false in three ways:

- **`flags.txt` does not exist.** `docs/trivia-curation.md:165` says "the device
  appends the id to `flags.txt` on the SD card". Nothing in the tree writes,
  reads or mentions such a file outside that line and a comment in
  `verdicts.tsv`. It is a documented mechanism that was never built.
- **There is no tool to extract flags at all.** `pack.state` is `count` raw
  bytes; `verdicts.tsv` is keyed on content-hash ids; the only bridge is
  `pack_format.py dump()`, whose own header says its ids are re-derived and "not
  a join key".
- **`verdicts.tsv` holds exactly one verdict** in its entire life, under four
  comment lines.

So the curation loop the pack-format doc describes has never run. **The smallest
useful thing this card can ship is not a service and not a sync button: it is a
small tool that reads `pack.state` beside `pack.dat` and emits `id<TAB>bad`
lines, plus deleting the stale `flags.txt` paragraph.** That closes the loop for
the population that exists today, and it makes everything else here optional
rather than prerequisite. It is now step 0.5.

**What I did not change, and why.** The critic's headline recommendation was to
cut the service from the first cut outright, on the evidence above. That is a
product decision — Mario asked for the sync-and-report service by name — so it
is his to make, not mine to cut. It is surfaced in "What Mario is being asked to
confirm" instead, with the evidence attached. The ordering, which is a code-level
call, I have changed: the service moved behind the extraction tool.

### Traps that would each have cost a session

- **`bridge::request` sends a POST body with no `Content-Type`.**
  `SecureHttpClient::writeRequest` adds Host, User-Agent, Connection, optional
  auth, caller headers and Content-Length, and nothing in `BridgeHttp.cpp` adds
  one — while the **simulator** path does (`BridgeHttp.cpp:223`, `-H
  'Content-Type: application/json'`). Device and sim send materially different
  requests, so this is a bug the simulator cannot show you.
  `site/api/report.js`'s `readBody()` already survives it by falling back to the
  raw stream; the trivia handler must copy that, not assume a parsed body.
- **`streamToFile` adds `Authorization: Bearer <token>` unconditionally**
  (`BridgeHttp.cpp:172`) with no empty-token guard, unlike `request` (`:122`). A
  tokenless pack fetch would send `Bearer `.
- **`events` has no `reporter_hash` column.** "Rate-limit exactly as
  `report.js` does it" does not port: `report.js` counts `cards.reporter_hash`,
  and `events` is id/at/service/event/level/device/version/board/props/
  fingerprint/card_id, indexed on at, (service,event,at), device and
  fingerprint. Counting a `props->>'ip_hash'` means scanning the table every
  heartbeat and download also lands in. So either the limiter gets its own small
  table with its own index, or the first cut has no per-address limit and says
  so. "No new table" and the rate-limit design contradicted each other.

### `pack.meta` is not bound to `pack.dat`, and the failure is silent

Saying "no format bump is needed, the manifest carries the id and the device
stores it beside the pack" recreates the pack-format doc's own residual in a
**weaker** form: `pack.state` at least has the count check, `pack.meta` has
none. Two concrete breaks:

- **Hand-copy**, which the format doc documents as a real case. Replace
  `pack.dat` and not `pack.meta`, and every report names the wrong pack id and
  joins to the wrong corpus rows — undetectable server-side, invisible to the
  player, and the outcome is good questions deleted on the strength of reports
  about other questions. **That is strictly worse than today**, where a bad
  report is a no-op.
- **Ordering.** `TriviaActivity.cpp:329-334` removes then renames; any
  `pack.meta` write is a separate unordered step, so a power loss between them
  leaves a new pack wearing an old id.

**The guard is free and the design already had it and never used it: the
manifest carries `count`.** The device sends the count it actually opened
alongside the pack id, and the service rejects any batch whose `(pack id, count)`
disagrees with its own manifest. That closes the hand-copy case and most of the
ordering one, and costs one integer.

### Report UI corrections

- **`WHY?` does not fit the notice, and my own draft committed the mis-tap it
  cited.** `NoticeModel` carries exactly one action, drawn by `drawAction` as a
  full-width bar (`TriviaScreens.cpp:232`). Adding a second means
  `drawActionPair`, which shrinks `NEXT QUESTION` to `full - kAsideWidth - gap`
  and **moves its centre** — so a player who learned to tap the right of that bar
  to continue now opens the reason list. That is `same-pixel-different-action`,
  on the one screen the design invoked the rule to protect. Fix: `NEXT QUESTION`
  keeps the full-width bar exactly where it is, and `WHY?` goes in the aside,
  which is empty on this notice today.
- **How the reason list pages is unspecified, and paging is the binding limit,
  not the 24-slot ceiling.** This fork has a recorded failure where swipe, page
  dots and RIGHT were all dead on a list (`the-shelf-opens-the-wrong-game`). Nine
  reasons over two pages with no working pager is the most likely way to get
  zero reasons — the exact failure Mario's own "demanding a category is how you
  get no reports" warns about. Either it fits one screen or the list is cut to
  what fits. Render it before choosing.
- **There is no un-report.** "Un-hide them all" does not retract a flushed
  report, and it throws away every deliberate hide to fix one mis-tap. A
  per-question undo on the `HIDDEN` notice ("that was a mistake") is the cheap
  version and should be in the first cut, because the notice is already on screen
  and already knows the index.
- **Truncate-on-2xx is lossy.** Reports appended between building the request and
  truncating the file are destroyed. Truncate to the offset that was sent, not to
  zero.

### Publishing is unbound

The pack is fetched from a GitHub release; a manifest served from the site is a
git commit under `site/` plus a deploy (`site/vercel.json` gates on
`git diff --quiet HEAD^ HEAD ./`), while the asset is `--clobber`ed
independently. Nothing keeps the two in step, and the design never said who
writes the manifest or what happens when they disagree — for the mechanism that
is the entire point of the card. **Publish the manifest as a release asset beside
`pack.dat`, in the same upload**, so they cannot skew; the site function reads it
rather than owning it.

## Order of work


0. **Card #326 first**, because it is the only step whose cost rises with delay:
   put the rated corpus somewhere durable. Nothing else here is safe to build on
   top of a join table that one `wt.sh prune` deletes, and step 2 needs it.
0.5. **DONE on this branch.** The extraction tool, and the stale doc:
   `tools_local/trivia/collect_flags.py` + `reports.py`, gated by
   `test_collect_flags.py`, and `docs/trivia-curation.md` no longer describes a
   `flags.txt` nothing writes. Original entry: A small tool that reads
   `pack.state` beside `pack.dat` and emits `id<TAB>bad` lines into
   `verdicts.tsv`, and delete the `flags.txt` paragraph at
   `docs/trivia-curation.md:165` describing a file nothing writes. This is the
   whole curation loop for the people who exist today, it is the smallest thing
   on this list, and every step below it is optional once it is done.
1. **#253 next, for the shared half**: a manifest per pack, a `PackMeta`
   sibling file, and one honest sync screen. Trivia consumes it. This is also
   the step that makes a device with a pack able to receive a new one at all,
   which is a fix on its own even if nothing below it ever ships.
2. Builder emits the `index -> corpus_id` map per build, and a pack id.
3. `site/api/trivia.js`: manifest `GET`, report `POST`, events rows.
4. Device: the report queue, `WHY?` on the `HIDDEN` notice, the reason screen,
   `HIDE` in solo, and the un-hide row in SETTINGS.
5. `verdicts.tsv` generated from the queue rather than typed.
6. Only then, D3b's overlay.
