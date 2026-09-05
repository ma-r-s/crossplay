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

## FLAG, and why it only appears with the answer

The answer state offers NEXT and FLAG. FLAG sets the question's `FLAGGED` bit in
`pack.state`; the chooser never serves it again, and the indices are read back
off the card into `tools_local/trivia/verdicts.tsv`, which the next pack build
applies.

That is the whole curation loop, and it is the only layer that scales: 50,000
questions cannot be reviewed, but the few hundred anyone actually sees can be
judged as they are seen. **It is offered only once the answer is showing**,
because a question cannot be called bad until you have seen what it claims.

## Getting the pack

Nobody copies files to a card. A rolling `trivia-pack` GitHub **prerelease**, so
the OTA's `releases/latest` can never see it and a 6MB question pack never rides
in a firmware update. The first run offers GET THE QUESTIONS; the file lands as
`pack.dat.part` and is renamed only when whole, so a torn download leaves the
card exactly as it was and the app simply finds no pack.

`pack.state` is never downloaded. It is the device's own record of what it has
served and what someone rejected, and it is rewritten locally when a new pack's
question count no longer matches it.

## What the device never does

No scoring history, no streaks, no leaderboard. A bar game's score belongs to the
evening. `Score` is reset on entering solo and is not persisted anywhere.
