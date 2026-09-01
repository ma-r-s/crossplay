# Instapaper on the reader: the queue, the sync, and what it deliberately is not

Status: v1 design, written 2026-08-30 alongside the build. The shape copies
the Anki bridge (`study-sync-bridge-plan.md`) because Mario asked for exactly
that: "in the same manner anki is being synced". Where this design departs
from that one, the departure is argued rather than assumed.

## What it is

An `INSTAPAPER` app on the shelf. It shows the account's unread queue, opens
an article as paged text, remembers how far the reading got, and archives an
article when the reader says so. A second service, `server/read-bridge/`,
holds the account and does everything the device cannot: OAuth signing, HTML
extraction, and deciding what changed.

One sync, in one sentence: the device posts what it already has and what it
did, the bridge asks Instapaper for the difference, converts whatever is new
into flat text, and the device downloads it.

## Why a bridge at all, given the API is public

Three reasons, and the first alone settles it.

1. **OAuth 1.0a with HMAC-SHA1 signatures on every request.** Signing needs
   percent-encoding, parameter sorting, a nonce, a clock the device does not
   reliably have, and an HMAC over a canonical string. That is a pile of
   crypto plumbing on a board whose free heap is measured against a TLS
   handshake. It is also the kind of code that fails silently and looks like
   a server problem.
2. **The consumer key is the application, not the user.** Instapaper issues
   one key per _application_, human-reviewed, and can suspend it (error
   1042). A key shipped inside firmware images is a key published. It belongs
   on a server, in an env var.
3. **`get_text` returns HTML.** Something has to turn that into the flat
   document the panel draws. That job is already server-side for Study
   (the deck converter) and for Hacker News (`r.jina.ai`), for the same
   reason: HTML parsing is not what a 240MHz board with an e-ink panel should
   spend its battery on.

So the device speaks a small JSON protocol we own, exactly as it does for
Anki, and the bridge is the only thing that ever holds an Instapaper token.

## The paragraph of honesty this design owes its users

The bridge holds, per user, an Instapaper OAuth access token. That token can
read every bookmark, add bookmarks, and **delete** them permanently. It does
not expire on its own. At sign-in the plaintext password transits the
bridge's memory once, is exchanged for the token, and is discarded -- but a
compromised bridge harvests passwords at login time, not merely tokens. The
bridge also caches the text of every article it has delivered.

Everything in Security below exists because of this paragraph. It is the
same paragraph the Anki bridge owes, with "collection" replaced by "reading
list", and it is shorter only because there is less to lose, not because the
custody is different.

## A separate service, not a second feature of the Anki bridge

Tempting, and wrong. `study-bridge` derives its user id from the AnkiWeb
username: joining the two would make an AnkiWeb account a prerequisite for
reading Instapaper on the device, and would put an Instapaper token and an
AnkiWeb collection in one blast radius for no user-visible gain.

The cost of separating is that `pairing.py`, `jobs.py` and half of
`store.py` exist twice. That is real -- see the `fix-the-twin-too` memory,
which is exactly this failure mode -- so each copied module says in its
docstring which file it is a twin of, and the twin is named in this document
too. What was NOT done, deliberately: hoisting the shared half into a
`bridgekit` package. That refactor touches a live service holding real
credentials, to save four hundred lines that have not changed since they
were written. If a third syncing app appears, do it then.

## The device API

All under the device bearer token except pairing start/poll/abandon and
healthz. Every refusal is a sentence the firmware shows verbatim; the device
never sees an error code.

- `POST /api/pair/start` -> `{code, pollToken}`. Codes are 8 characters from
  an unambiguous alphabet and live five minutes.
- `POST /api/pair/claim` (browser, session cookie, CSRF token).
- `GET /api/pair/poll?pollToken=` -> pending, or `{deviceToken, username}`.
  The device then shows "Paired to <username> -- confirm?" and stores nothing
  until a button is pressed.
- `POST /api/pair/abandon` -> kills a pending code, or revokes a registration
  the confirm screen declined.
- `POST /api/sync` -> `{job}`. Body:

      {"have": [{"id": 123, "hash": "OjMuzFp6", "progress": 0.42,
                 "progressAt": 1756500000}],
       "archive": [456, 789]}

- `GET /api/sync/status?job=` -> `running` | `done` + summary | `error`.
- `GET /api/article/{id}/{hash}` -> the flattened text, `text/plain`.
- `GET /healthz` -> static liveness, nothing per-user.

The summary is the whole answer to "what changed":

    {"articles": [{"id", "hash", "title", "url", "domain", "savedAt",
                   "progress", "bytes", "words", "renderable"}],
     "deleteIds": [...], "archived": [...], "failed": [{"id", "why"}],
     "unread": 42, "withheld": 0}

## The insight the protocol is built on: `have` is the device's index

Instapaper's `bookmarks/list` takes a `have` parameter -- a comma-separated
list of `id:hash:progress:timestamp` -- and answers with only what the client
does not already have, plus a `delete_ids` list of what it should drop, and
**it takes the reading progress in that same string as an update**. That is
delta sync and progress push in one call, designed by Instapaper, and the
device's own index is exactly the string it wants.

So the bridge keeps no mirror of the reading list. It holds the token, a
cache of converted article text, and nothing else that could drift. Compare
the Anki bridge, which must hold a full collection because AnkiWeb's protocol
gives it no other way to answer "what changed".

Two traps in that gift, both handled:

- **`delete_ids` is limit-sensitive.** The docs are explicit: ids sent in
  `have` that "would not have appeared in the list within the given limit"
  come back as deletions. Ask for 25 while the device holds 100 and
  Instapaper correctly reports 75 deletions that are not deletions at all.
  The bridge always asks for the maximum (500) **and** refuses to pass
  `delete_ids` through when the answer came back at the limit, because at
  that point it did not see the whole folder. The device caps its own
  library at 120 articles for the same reason, far below 500.
- **Progress only moves forward in time.** Instapaper takes our progress only
  when its own timestamp is older. A device with a wrong clock would either
  never push progress or clobber the phone's. The device stamps progress with
  its RTC and the bridge refuses a timestamp more than a day in the future,
  which turns a bad clock into a stuck-but-safe article rather than a
  corrupted one.

## What runs on a sync, in order

1. Validate; enqueue; return the job id immediately (the pattern the Anki
   bridge uses because an e-ink HTTP client must not hold a request open).
2. Apply the device's intents: `bookmarks/archive` per queued id. These are
   at-least-once by design -- archiving an already-archived bookmark is a
   no-op -- so a response the device never receives costs one repeat and
   nothing else. **Intents run before the list**, or the article the user
   just archived comes straight back down.
3. `bookmarks/list` with the composed `have` string. This is also what
   pushes reading progress.
4. For each new or changed bookmark: `bookmarks/get_text`, convert, cache
   under `articles/<id>/<hash>.txt`. A conversion that fails is recorded in
   `failed` and skips that article; one bad article never costs the sync
   (the Anki bridge learned this the expensive way with one unbuildable
   deck aborting every deck).
5. Status becomes done with the summary above.

## The article, once converted

Flat UTF-8 text: paragraphs separated by a blank line, list items prefixed
`- `, `<pre>` keeping its own line breaks, headings on their own paragraph,
images and figures dropped. Typography is folded to ASCII on the bridge
(curly quotes, dashes, ellipses), because a glyph outside the reading cut's
subset draws as **nothing at all** -- a real Hacker News comment once read
"(Ive turned off" for exactly this reason.

That fold cannot save a script the cut has no glyphs for at all. So the
bridge measures: an article whose characters are mostly outside Latin-1 is
marked `renderable: false`, the device says so on the row instead of opening
a blank page, and the text is still downloaded so the verdict can change
later without a re-sync. This is the Hacker News readability gate's lesson --
one verdict, shown at the moment it is actually known -- applied to a
different failure.

Reading time is `words / 220`, computed on the bridge because it already has
the words.

## The device

`src/apps_local/instapaper/`, `/.crosspoint/instapaper/` on the card (a
device-managed cache, like the Hacker News library, rather than `/study/`
which holds decks a user copies on by hand).

    .bridge        pairing token, last sync, nothing else
    index.tsv      one versioned row per article
    a<id>.txt      the flattened text

Four screens: the queue, the reader, a notice, and pairing. Plus the sync
screen, which is a busy caption and a verdict -- **not** Study's stage ladder.
That ladder exists because a first Anki sync moves gigabytes and takes
minutes; an Instapaper sync moves a few hundred kilobytes and takes seconds.
Copying it would buy a second copy of a component to maintain in exchange for
animating a wait that is not there.

The reader is `textArea`-shaped, one document paged a screenful at a time,
like the Hacker News article view and unlike its comment view (no threading
to draw). `topLine / lineCount` at the moment of leaving IS Instapaper's
progress definition -- "the top edge of the user's current viewport,
expressed as a percentage of the article's total length" -- so progress is
read off the pager rather than modelled separately.

Writes from the device are **archive only**. Star, delete, folders and
highlights are cut from v1: each is a second write path with its own failure
mode, and none of them is what an e-reader is for. Archive is the one action
that follows from reading, and it is reversible from any other Instapaper
client, which is the property that makes it safe to do from a device with
two physical buttons.

### The index format, and why it is versioned from birth

`HackerNewsSaved` learned this at a cost worth not repeating: a field added
without a version bump makes a version-1 row hand back its second column as
its title, so every saved article displays as a number and the library saves
a duplicate on every tap. One unbumped integer, two bugs, neither of which
looks like a format problem from outside.

So: magic + version on line one, and a reader that skips a row it cannot
parse rather than rejecting the file. Columns carry the two pieces of local
state a sync needs and nothing else can hold -- whether progress has moved
since the last successful sync, and whether an archive is queued.

## Security

Inherited wholesale from the Anki bridge, because the threat model is the
same and that one has been through a critic round and a live deployment:
Fernet at rest with the key in the environment; device tokens random 32 bytes
stored only as hashes; per-IP and per-username rate limits on the login
endpoint, which is a credential-stuffing oracle by construction; an
an allowlist (now OPEN by Mario's decision -- see below); sessions as sealed
cookies, SameSite=Lax,
HttpOnly, CSRF on every state-changing form; its own container uid, its own
pinned subnet, no host ports, cloudflared-only ingress, and the firewall unit
that drops that subnet's traffic to every RFC1918 range.

One addition specific to this service: **the token can delete bookmarks, and
nothing in this design ever calls delete.** The endpoint is not wrapped, not
proxied, and not reachable from the device protocol at all.

### Opening registration, and what it required first

The plan this copies said registration stays behind an allowlist "until
aggressive per-IP and global caps plus per-username exponential lockout exist
AND HAVE BEEN EXERCISED". When Mario opened both bridges, one of the three
existed: a per-IP window. The global cap was on `/api/sync` and not on the
endpoint that is a credential-stuffing oracle, the per-username limiter was
flat rather than exponential, and none of it was tested.

All three now hold. `GLOBAL_LOGIN` is a ceiling nothing can route around by
having many addresses; `Lockout` (bridge/lockout.py) charges exponentially on
FAILURES after two free ones, so a typo costs nothing and a sustained attack on
one account costs time that doubles; `tests/test_lockout.py` and an end-to-end
case in `tests/test_api.py` exercise both.

Two things that fell out of building it and are worth keeping:

- **The flat per-username window had to GO, not sit beside the lockout.** Both
  keyed on the username, the flat one fired first, and its cruder message was
  the only one a locked account ever saw. Two limiters on one key with the
  weaker one winning is worse than either alone, because it hides which
  mechanism is acting.
- **A limiter test can pass on the wrong mechanism.** The end-to-end case only
  reaches the lockout from a fresh visitor IP; from the suite's own address the
  per-IP window answers first, and the assertion would have been green while
  testing nothing it claimed to.

**What none of this touches: distributed stuffing.** Per-IP and per-username
counters are both defeated by having many of each, and no in-process limiter
can help. That case needs a control in front of the service -- Cloudflare rate
limiting, which is Mario's hands on a dashboard. It was already an open item;
opening the allowlist is what turned it from prudent to load-bearing, and it is
now the largest remaining gap in this service's security.

## The one thing that cannot be verified from here

Everything in this document is built and tested. The suites run against
`tests/fake_instapaper.py`, which implements the five endpoints this bridge
uses, applies the `have` delta rules, and **verifies OAuth signatures** -- so
the signing, the delta, the conversion, the device protocol and the archive
round trip are all proven without an account and without a network.

What a fake cannot prove is that the real Instapaper behaves the way this fake
believes it does. Specifically, and these are the assumptions worth naming
because each one came from the documentation rather than from an observation:

- that `delete_ids` is limit-scoped exactly as documented, so the window guard
  in `engine.py` is guarding the real behaviour and not an imagined one;
- that a bookmark's `hash` really is computed from url + title + description +
  progress and NOT from the content, which is what lets the bridge reuse
  cached text when the hash moves;
- that progress in the `have` string is accepted only when its timestamp is
  newer, which is the whole conflict-resolution story;
- that `get_text` returns the shape of HTML the converter was written against,
  on real articles rather than fixtures.

**What is needed:** one sign-in with a real Instapaper account, through the
bridge's own page, plus an OAuth consumer token (a form on instapaper.com,
reviewed by a human) for the service to sign with.

**What it unblocks:** the four assumptions above become observations, and the
service can be deployed for real use.

**What stays unproven until then:** nothing about the code as written -- it is
green -- but every one of those four is a belief about somebody else's server.
If one of them is wrong, the failure is quiet: a stale row, a re-downloaded
article, or a reading position that does not move.

The other operational step, whenever it happens: a hostname for the service
(the firmware constant says `read.ma-r-s.com`) added to the tunnel, the way
`sync.ma-r-s.com` was.
