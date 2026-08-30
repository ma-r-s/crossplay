# Study sync flow UI rework

Mario's ask, verbatim: "the whole UI of this sync process is horrible.
Lets rework it after done." The door (deck screen) shipped in v1.5.2 and
is NOT part of this; this is everything after the SYNC door is pressed.
This is v1 of the brief, with a cold critic's round folded in (the round
found two missed states, the safety taxonomy, two mechanical couplings,
the Jersey/username trap, and the Sticky power-key aliasing).

## The flow as it exists (v1.5.2)

One generic view (`View::SyncMsg`: centered title + wrapped body, Back
hint) carries every state. Pairing has two views of its own (PairQr,
PairConfirm). All in StudyActivity.cpp, blocking flow on the loop task,
screens painted via showSync(). The WiFi picker that precedes everything
is stock chrome and is OUT of scope; its silent-cancel path stays.

## States, with their safety truth

Every terminal state carries one of three truths, and any post-ack
failure verdict LEADS with it, above the transport text:
NOTHING-SENT / REVIEWS-SAFE / REVIEWS-SAFE + DECKS PARTLY UPDATED
(renames + setBuild commit per deck inside the apply loop, so earlier
decks persist when a later one fails).

| #   | Today                                                                         | Kind        | Safety                               |
| --- | ----------------------------------------------------------------------------- | ----------- | ------------------------------------ |
| 1   | SYNC / Connecting to the bridge.                                              | busy        | -                                    |
| 2   | SYNC / Getting a pairing code.                                                | busy        | -                                    |
| 3   | SYNCING / Packing this card's reviews.                                        | busy        | -                                    |
| 4   | SYNCING / Sending your reviews.                                               | busy        | -                                    |
| 5   | PREPARING / This first time can take a while...                               | busy (>30s) | -                                    |
| 6   | SYNCING / Fetching (slug) (N files in). [missed in round 0]                   | busy        | -                                    |
| 7   | SYNCED / This card and your Anki are up to date.                              | success     | safe                                 |
| 8   | (bridge/transport text; EMPTY on 5 transport blips)                           | error       | pre-ack nothing-sent / post-ack safe |
| 9   | This reader was unpaired on the bridge. Press SYNC to pair it again.          | error       | nothing-sent                         |
| 10  | Could not read the card. Nothing was sent.                                    | error       | nothing-sent                         |
| 11  | Pairing stopped/cancelled. Nothing was stored.                                | neutral     | nothing-sent                         |
| 12  | Your reviews are safely sent. The bridge keeps working... (Back in poll loop) | early leave | safe                                 |
| 13  | Stopped. The decks on the card are unchanged... (Back in download) [missed]   | early leave | safe (+ per-deck partial)            |
| 14  | Could not save the pairing to the card.                                       | error       | nothing-sent                         |

The empty-body blip terminal (row 8) must be impossible in the rework:
the verdict renders from a structured model, and a missing message falls
back to a stock sentence.

## Constraints and mechanical couplings (requirements, not notes)

- E-ink: no spinners. Progress is structural. Refresh discipline:
  PARTIAL refresh per ladder tick, one FULL refresh on the verdict (the
  flash as punctuation, per docs/design-language.md).
- The blocking-on-loop-task model stays; the rework changes what is
  painted, EXCEPT: any new view enum values must be added to BOTH the
  loop() Back handler (today keyed to View::SyncMsg; a miss falls
  through to shelf::leave and quits the app) and syncGuardsSleep()
  (today syncBusy_ || PairQr || PairConfirm; the verdict may sleep).
- Fonts: chrome in toybox cuts, but the username on PairConfirm and any
  server-originated text keep a NotoSerif global face. Jersey is
  ASCII-subset; a CJK username in a toybox cut renders as replacement
  glyphs and blinds the anti-hijack gate.
- The confirm gate keeps its exact mechanics: rect set during render,
  drainInput() after the confirm screen paints (the latched-Confirm
  walk-through is the bug the gate exists to prevent), tap-only on the
  X4 Pro (front buttons are unassigned pins). Sticky power-key aliasing
  (click = Confirm, so a habitual screen-off click confirms) is an
  ACCEPTED RISK for v1: the gate still requires the phone-side login
  first, and the confirm screen names the account. Documented here so
  the acceptance is a decision, not an oversight.
- Back honesty: before the ack, Back is NOT advertised (it is not live
  in long stretches anyway); from the ack on, the footer promise and the
  Back hint appear together. The home gesture is honored anywhere Back
  is.
- Heap: sync runs with the deck closed; no deck fonts. Toybox + the
  NotoSerif globals only.
- Facts require data the flow does not carry today: the flow-to-render
  contract is a struct (stage states, per-stage facts, safety flag,
  verdict, counts), not two strings. Review count comes from the
  payloads, decks-updated count from applyManifests.
- DOWNLOAD honesty: on a routine sync every buildId matches and no
  download runs; the stage completes with the fact UP TO DATE rather
  than pretending to run. The ladder's value concentrates in the
  minutes-long first sync, the post-ack leave story, and failure
  placement; the routine sync just ticks through.

## Design direction

One SYNC surface with three faces sharing one visual language: the
STUDY header band, the 16px unit, lucide glyphs (check / x / minus)
through the manifest pipeline.

1. **The ladder (busy face).** Four stages: CONNECT, SEND REVIEWS,
   BUILD DECKS, DOWNLOAD. Done stages show a filled marker and a fact
   where one exists (142 SENT, UP TO DATE); the active stage an
   outlined marker; pending stages dimmed. The >30s preparing note is a
   caption under the active stage, not a screen replacement. Per-file
   download progress is the DOWNLOAD stage's live fact (N OF M). The
   safety footer appears from the ack on.
2. **The verdict (terminal face).** One glyph (check / x / minus), a
   verdict line, the safety line first on post-ack failures, one
   sentence of body, a "what now" line when relevant. Success carries
   facts: reviews sent, decks updated, the LAST SYNC time the door will
   show. One full refresh.
3. **Pairing keeps its two screens**, restyled to the chrome; QR
   arrangement bones stay (they won their own round), confirm mechanics
   untouched.

## The three variants to render (SETTLED: V2 won, 2026-08-27)

- V1 "Ladder": vertical checklist, markers in a left gutter, facts
  right-aligned per row (the door's label/value pattern). Verdict as a
  left-aligned block on the same column grid.
- V2 "Stage band": a horizontal four-segment band under the header
  (segments fill as they complete), the current stage as a large
  reading-cut headline beneath it, caption under, safety as footer.
  Verdict centered under the same band, which shows where it ended.
- V3 "Line + log": one large current-stage line, a quiet tile-cut log
  of completed stages accumulating beneath with check glyphs; closest
  to today's feel. Verdict is today's layout plus glyph and facts row.

Render each variant in three states: mid-sync (CONNECT and SEND done
with 142 SENT, BUILD active with the preparing caption, DOWNLOAD
pending, safety footer on), success verdict (142 REVIEWS SENT, 2 DECKS
UPDATED), and a post-ack transport error verdict (safety line leading).
Nine shots, composed per variant.

## Verification record

2026-08-27: full live end-to-end run in the simulator against a local
bridge + local sync server (tests/sim_stack.sh, throwaway account):
fresh pairing (QR, phone-side claim, confirm tap through the real gate
rect), send, build, download of a deck the card had never seen, SYNCED
verdict with real facts (1 DECK UPDATED, LAST SYNC stamp). Under 42s
end to end. The render matrix (band busy face, success and post-ack
error verdicts, both pairing screens) is preview-harness verified.

2026-08-27, user-test agent round (fresh eyes, three journeys): the
endpoints were judged ready for strangers; two defects found and fixed
the same night. (1) The verdict band invented progress -- four filled
segments over NOTHING WAS SENT on a cancelled pairing; the band now
draws stages exactly as the flow left them, and only Success fills the
strip. (2) An abandoned pairing code stayed claimable until TTL, and a
confirm-screen cancel left a ghost device registration (poll() registers
before the human confirms); /api/pair/abandon takes a pollToken or
deviceToken (possession is the authorization) and the firmware calls it
best-effort on every pairing walk-away. Both verified live against the
local stack; the bridge suite carries four new checks (24 total).
Wording: "This card" became "This reader" (in a flashcard app, card
reads as flashcard first). Deferred, recorded here so they are
decisions: the WiFi picker recurring on every sync is unverified on
hardware (the sim HAL saves no credentials); mid-flow screens cannot be
photographed by sim-shot while the flow blocks the loop (preview
harness covers them); the verdict screen's tap-anywhere exit is
unlabeled, matching the Image view's house precedent.

## What the done-done pass changed (2026-08-29)

Mario's ask: work until any new user can use this, spawning a cold
reviewer each round who reads the feature as a stranger, repeating until
nothing worth fixing comes back. Fifteen rounds. Most of what they found
was behaviour rather than layout, and lives in the commit messages on
`app/studyradio`; what follows is only what changes a claim made above.

**A fourth verdict kind: PART WAY.** The table above has success,
error, neutral and early-leave. One deck the service cannot build no
longer fails the whole sync, so a run can now end genuinely in between:
reviews away, most decks current, one deck named as unbuilt. It renders
Neutral with the ReviewsSafePartialDecks safety, and its what-now line
points at re-choosing. Cloze decks are the ordinary cause and nothing
warns beforehand -- the .apkg installer page discloses it, the pairing
page does not.

**The success footer is composed inside endSyncSession, not passed to
it.** The brief says the contract is a struct rather than two strings,
and it is, but the "what is waiting to study" line was still built at
the call site -- evaluated before endSyncSession reopens the deck the
flow closed for heap, so it always read zero counts. A user whose first
deck had just landed was told "Nothing is due right now" with two new
cards in it. Anything that reads deck counts belongs after the reopen.

**The deck picker is part of this surface now.** It was reachable only
on a first sync when the brief was written. It gained: rows that cannot
be chosen drawn with no checkbox at all (greying is not enough at 1bpp,
DarkGray dithers and reads as black at hairline weight), a caption when
the account holds more decks than the list carried, and a 200-deck list
where it was 32.

**The empty-card screen carries two doors.** GET MY DECKS re-downloads
what was chosen; CHOOSE OTHER DECKS re-opens the question. With one door
a failed build was a closed room: the only screen a deckless card can
show set the re-choose flag false whenever decks had been chosen, which
the failing pass sets true before it fails.

**Warnings share the scope line.** At four doors the deck screen has no
spare row -- above the ornament the brackets start five pixels below the
line before it, and below the caption is the top door. A first attempt
drew the warning straight through that door; the render caught it and no
test would have.

### Verification record, this pass

The journey a new user actually walks, run in the simulator against
tests/sim_stack.sh: an empty card, pairing, the QR, the browser claim,
the on-device confirm, the first sync that mirrors the collection, the
picker, the build, the download, and a card graded on the delivered
deck. Separately proven by scripted run: a poisoned ack checksum resends
the whole log and the sync still completes; an ack past EOF does the
same; a log truncated mid-record snaps back onto the record grid; a
32-character deck directory is found and opened; a matching checksum
sends nothing extra; the over-cap warning renders without collision on a
card holding ten decks; the escape door reaches the picker before any
build runs.

Known and open: the device parses each download's sha256 and never
checks it. Verification is by length, with TLS covering the wire, so
what is open is a file landing the right length with the wrong bytes.
StudySync.h says so plainly rather than letting the field imply
otherwise.
