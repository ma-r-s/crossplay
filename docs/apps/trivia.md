# TRIVIA

A pack of questions on the card, two ways to play them. The questions and how
they were chosen are in [../trivia-curation.md](../trivia-curation.md); the
on-card format is [trivia-pack-format.md](trivia-pack-format.md). This is the
app.

**No question count is written down here on purpose.** The shipped pack is
50,000 clues whose difficulty is Jeopardy's dollar value; the pack
`assemble_pack.py` builds holds exactly the questions a rating run has reached,
so it is a different and SMALLER number on every build. The front door reads the
count off the card.

## Two modes, because the pack is not uniform

**QUIZMASTER** puts one clue on the panel and nothing else. The device is the
person reading out; the room does the arguing; a tap turns the answer over. It
works alone too, the way a flashcard does.

**SOLO** adds four options and keeps score, because with nobody else in the room
something has to judge you.

They are not two skins on one mode. Not every question carries distractors, so
solo draws from part of the pack and the chooser is told `requireChoice` and
skips the rest. How large that part is depends on which builder made the pack:
**18,485 of the shipped 50,000** (under a third), against **76.6%** of a pack
assembled from the local rating run, which generates its own candidate options
rather than drawing them from questions of the same type. That share is flat
across the five levels (72.7% to 82.5%), so no level is short of solo material.

## The question screen

Chosen from three arrangements rendered side by side: this one, the clue set as
a left-ragged column, and the clue as the whole panel with no header at all. It
won on looking like the rest of the shelf, which matters more than the extra
type size the third one bought.

Two rules the screen has to keep:

**The clue is prose, so it is never `inkCentred`.** That helper solves for the
cap band and mixed-case text set with it hangs every descender below the box.
Only the all-caps chrome is ink-centred.

**`TextStyle::maxLines` defaults to ONE.** A clue drawn without raising it is a
single line truncated with U+2026, which Jersey has no glyph for -- so the
sentence stops mid-word and the screenshot looks fine. The line count is derived
from the box height rather than picked, so a taller box wraps further with no
second number to keep in step.

Faces are bound per view. The clue is a page of prose and takes the reading
serif; the front door is a menu and takes `proseMenuFaces()` so it reads like
every other menu in the fork. A menu subtitle at the 20px UI cut runs off a
480px panel, so the two on the front door are measured, not guessed.

## HIDE, and why it only appears with the answer

The answer state offers NEXT, HIDE and END, **in both modes since board #257**.
Solo had no report control at all before that, which was the wrong way round:
solo is the mode where "the options give it away" and "wrong answer" are the two
faults a player can actually see. The cost is honest and worth naming: solo's
footer goes from a pair to a trio, so NEXT is narrower and its centre moves the
first time a player meets it after updating. END does not move -- `drawAsideAction`
derives its x the same way for pair and trio.

HIDE sets the question's `FLAGGED` bit in `pack.state`, so the chooser never
serves it again, and queues a report in `/trivia/reports.dat`. **It is offered
only once the answer is showing**, because a question cannot be called bad until
you have seen what it claims.

### One tap reports; the reason is optional

The HIDDEN notice that follows offers WHY? and UNDO on **their own row above**
`NEXT QUESTION`, never beside it. Beside it would mean `drawActionPair`, which
shrinks the primary and moves its centre -- and the primary here is the control
that continues the round, so a player who had learned where it is would open a
list instead. See the `same-pixel-different-action` memory.

WHY? opens a list on its own screen. Skipping it leaves a report with no reason,
which is still a report: demanding a category is how you get no reports. Two
rows are conditional -- THE OPTIONS GIVE IT AWAY only in solo, and THIS IS A US
QUESTION only when US questions are off, because with the toggle off the chooser
already skips marked records, so a US question a player MEETS is one the pack
failed to mark. That reason repairs a bit, not a row.

UNDO is the undo HIDE never had: it clears the bit and withdraws the queued
report. Before it, a mis-tap hid a question permanently with nothing on screen
about it. TRIVIA > SETTINGS > HIDDEN QUESTIONS shows the total and offers to
show them all again.

### Reading them back

`python3 tools_local/trivia/collect_flags.py <card>/trivia/pack.dat --apply`
turns the flags and their reasons into `verdicts.tsv` lines, which the next pack
build applies.

**Not every reason is a deletion.** `bad` removes the question, and three
reasons do not ask for that: TOO EASY and TOO HARD move a level, and THIS IS A
US QUESTION sets bit 7 of a difficulty byte. Those are written with the verdict
`repair`, which no builder applies and which a person acts on -- emitting them
as `bad` would delete a good question for a one-byte defect. A report with no
reason at all is still a removal, which is the point of allowing one. That tool is new with #257: `docs/trivia-curation.md` used to
describe a `flags.txt` that nothing ever wrote, nothing read a card, and
`verdicts.tsv` had accumulated exactly one verdict in its lifetime.

That is the whole curation loop, and it is the only layer that scales: 50,000
questions cannot be reviewed, but the few hundred anyone actually sees can be
judged as they are seen.

## SYNC

TRIVIA > SETTINGS > SYNC. **Reports go up before anything is fetched**, and the
order is the design rather than a preference: a pack update landing while
reports about the previous build were still on the card would leave those
indices naming different questions, and the pack format's own residual (a
same-count replacement keeps `pack.state`) means nothing on the device could see
it. Uploading first closes that window.

The reports are sent under the pack id their **own queue header** carries, not
the card's current one, so a queue filed against an older build is still
resolvable. The endpoint is `/api/trivia` on the site; `site/api/trivia.js`
describes what it does and does not learn about the reader.

Then it fetches `pack.json` from the same release as `pack.dat` and says what it
found -- `UP TO DATE`, a `NEWER PACK` notice naming the size **before** spending
it, or `DIFFERENT PACK` when this card's own build is unknown. Those last two
are deliberately not the same sentence: "a newer pack is ready" is a claim the
device cannot make about a build it cannot name, and while they shared a branch
SYNC offered a download that changed nothing and left the build unknown
afterwards, forever.

**Sync is also what turns reporting on.** If the card holds a pack whose build
it does not know, and the published manifest describes exactly that pack -- same
question count, same byte size -- the id is adopted and `pack.meta` written.
That is the only path by which a device that already had a pack starts queueing
reports; it is also why nothing is adopted when the sizes disagree, since an id
the card does not actually hold would resolve every later report through the
wrong build's index map. Before this, `ActionGetPack` was reachable only from the empty-card
and failure notices, so a device that already held a pack could never receive a
newer one at all.

`/trivia/pack.meta` records which build the card holds. It carries the count and
the byte size as well as the id, because a record of what you hold is only worth
having if it can be caught lying: a hand-copied pack that replaces `pack.dat`
and leaves `pack.meta` describes a build the card no longer has. Either check
failing DISCARDS the meta, and an unknown build stops reports being **queued**
while play continues -- a report that cannot name its pack can never be
resolved, and one naming the wrong pack is worse than none.

## Getting the pack

Nobody copies files to a card. A rolling `trivia-pack` GitHub **prerelease**, so
the OTA's `releases/latest` can never see it and a 6MB question pack never rides
in a firmware update. The first run offers GET THE QUESTIONS; the file lands as
`pack.dat.part` and is renamed only when whole, so a torn download leaves the
card exactly as it was and the app simply finds no pack.

`pack.state` is never downloaded. It is the device's own record of what it has
served and what someone rejected, and it is rewritten locally when a new pack's
question count no longer matches it.

## International by default

The pack ships every question, but US-centric ones are marked and the chooser
hides them unless TRIVIA > SETTINGS > US QUESTIONS is on (off by default). The
difficulty levels are calibrated for an international table, where the
US-centric questions are the hardest, so turning the toggle on adds them mostly
to level 5. The flag rides bit 7 of each question's difficulty byte; see
[trivia-pack-format.md](trivia-pack-format.md) and board #191/#223. Nothing is
deleted from the data, so the toggle needs no re-download and no re-rating.

## The app owns its own settings

TRIVIA has a SETTINGS screen, reached from the last row of its front door. It
holds US QUESTIONS, which shipped in v1.12.29 in the DEVICE's Settings > System
list instead, one row below Developer Mode. That was wrong and card #311 moved
it: CrossPoint owns the reader, the keyboard and the system, and a CrossPlay
app's own options belong inside that app.

**DIFFICULTY stays on the front door, and that is not an oversight.** The two
options are different kinds. Difficulty is a per-session mood -- easy tonight,
hard tomorrow, changed about as often as the mode is -- so it sits beside
QUIZMASTER and SOLO where it costs no taps and reads without opening anything.
US QUESTIONS is a persistent preference about which questions exist for you at
all: set once, near enough to never changed again. One surface each is not two
option surfaces. Chess's LEVEL is set-and-forget, which is why chess keeps its
on the settings screen and Trivia does not.

**What did not move is the storage.** The value is still
`CrossPointSettings::triviaShowUsCentric` under the key `"triviaShowUsCentric"`,
which is what devices updating from v1.12.29 already have saved, so nobody's
choice was reset. The `SettingInfo` entry is still in `SettingsList.h` too --
without a category, the shape `SettingsList.h` already used for the OPDS and
frontlight values, which persists it and keeps it in the web settings API while
keeping it off the device's Settings screen. `host-tests/appsettings` is the
guard: it traces who reads each categorised setting and fails if every reader
lives in one app.

DIFFICULTY is untouched by all of this: still the byte in `/trivia/prefs`, still
set from the front door.

## What the device never does

No scoring history, no streaks, no leaderboard. A bar game's score belongs to the
evening. `Score` is reset on entering solo and is not persisted anywhere.
