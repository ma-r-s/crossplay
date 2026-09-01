# WAVELENGTH

The spectrum party game, one device passed round a bar table. A strip runs
between two opposing words, slot 1 at the bottom and slot 20 at the top. One
player sees a hidden target and says a single clue out loud that, to them, sits
exactly there. Everyone else argues, moves the marker, locks it in, and calls
which side of their guess the target was really on.

Three layers, as the shelf expects: `WavelengthCore` is the rules and nothing
else (freestanding, host-tested), `WavelengthScreens` draws them (freestanding
builders over plain models), `WavelengthActivity` is the only part that knows
about hardware.

## The rule everything else defers to

**The board is public and only the target is hidden.**

Two cold critics, briefed as players rather than reviewers, independently found
the same fatal flaw in the first design: a six-inch greyscale panel held in one
person's hands makes the board, the marker and the ends as private as the
secret, and that person then holds four powers at once, being the only one who
can see it, move it, lock it and referee it. In the physical game the dial sits
in the middle of the table and you can put your hand on it.

So from the clue onward the device **lies flat and anyone may touch it**. The
holder is not a role. Any change that quietly takes the board back into one pair
of hands is a regression, and the first revision made exactly that mistake by
passing the device back to the clue-giver for the reveal.

## The round

| #   | Screen          | What it is for                                                 |
| --- | --------------- | -------------------------------------------------------------- |
| 1   | MENU            | Front door. Headline is the hit target. Record and ornament.   |
| 2   | PASS LEFT       | No names, no seats, no turn order to remember.                 |
| 3   | PICK ONE        | Two spectra, chosen before the target is drawn.                |
| 4   | YOUR TARGET     | Hold to reveal. The band shows only while a thumb is down.     |
| 5   | SAY IT OUT LOUD | Both ends large, target confirmed hidden.                      |
| 6   | DIAL            | Flat on the table. Keys or taps step the marker. Hold to lock. |
| 7   | WHICH WAY       | The end-call, worth one point.                                 |
| 8   | REVEAL          | Full refresh. Band, guess, points, verdict.                    |
| 9   | SESSION         | Totals against a reference, and the ornament again.            |

Back unwinds to the menu, except from the peek onward, where it **abandons the
round and passes left**. That is deliberate: if backing out re-dealt for the
same person, a clue-giver could quietly hunt for an easy axis, and the deck's
strangest cards would never be played.

## Scoring

Exact 5, off by one 3, off by two 1, nothing beyond. A correct end-call adds 1.
Round one is a practice round and does not score, because every table's first
round is a zero and that is the worst place to land a discouraging number.

Two properties are asserted exhaustively in `host-tests/wavelength/`, because
both encode decisions that would be easy to reverse by accident:

- **No slot is dominated.** An earlier draft kept the target away from the ends
  so the five-wide band always fitted, which made slots 1, 2, 19 and 20 strictly
  worse guesses than 3 and 18. Targets are unrestricted.
- **A perfect round is unbeatable.** The end-call is made before the reveal, so a
  table that locked exactly cannot have called either way correctly; it counts
  as correct anyway. Without that, an off-by-one with a good call outscores a
  perfect round and nobody can explain why.

## The deck

252 pairs, the retail CMYK deck (designers Alex Hague, Justin Vickers, Wolfgang
Warsch), generated into `WavelengthPairs.h` by
`tools_local/wavelength/gen_pairs.py` from `pairs_en.txt`.

> **PERMISSION, on Mario's testimony.** On 2026-08-31 Mario stated that CMYK
> granted permission to use these pairs, given in a Reddit exchange. The
> exchange is not archived in this repository and no link is cited, because
> inventing a citation would be worse than recording the provenance honestly. If
> the thread is still reachable, put the link here and this paragraph can become
> a reference instead of a testimony.
>
> **The transcription is a different claim and is still unchecked.** Permission
> to use CMYK's cards is not evidence that these 252 lines ARE CMYK's cards.
> Twenty pairs spot-checked against the physical box would settle it; nobody has
> done that yet.

The transcription came via `github.com/heybenchen/wavelength`, which carries no
licence file and no provenance note, so treat `pairs_en.txt` as a working
transcription to be spot-checked against the physical cards rather than as an
authoritative copy.

**The generator's job is refusal, not formatting.** It measures every end word
against the real font tables and rejects anything that cannot fit, so an
overlong word cannot reach the device and be truncated into a word that simply
stops. `advanceX` in the generated cuts is 12.4 fixed point, sixteenths of a
pixel; reading it as pixels makes everything sixteen times too wide.

A character count is not a width. `LIVED IN` and `GROWN-UP` are both eight
characters and differ by 70px at the display cut.

## Traps, all of them paid for

**A slot is not a cut, and this app hit it three times.** The font slot decides
how big the ink is; `CutMetrics` only decides where it is centred. Passing a
mismatched pair is silent. It produced "NOT SCORED" rendered at 82px, a headline
truncated to "WAVELE", and a line drawn straight through the one under it.
`caps()` now derives the metrics from the slot so the two cannot disagree, and
`endWord()` walks the cut ladder down because 54 of the deck's 252 pairs do not
fit at the display cut.

**Do not pass a sentinel slot to mean "no marker".** It drew a phantom marker at
the foot of the strip. `drawScale` and `drawMarker` are separate for that
reason; a screen with no guess yet simply does not ask for one.

**The hold-to-reveal check must not return early.** A tap is a touch-down before
it is a release, so an early return ate every tap on the peek screen and the
game could be entered and never left. It surfaced only because six consecutive
screenshots came back byte-identical.

**`render()` must end with `displayBuffer()`.** Without it the activity enters,
draws into the buffer, and never publishes; the previous screen stays up and it
looks exactly as though the app never started. The tell is the absent
`[GFX] Time = N ms` log line.

**Auto-sleep is suppressed for the whole round**, released only at the menu.
There is a long stretch where the table argues and nobody touches the glass.

**The icon was spliced, not regenerated.** `tools_local/toybox/icons.txt` warns
that a straight regeneration drops `icon_yahtzee_32` and `icon_connectfour_32`,
whose SVG sources were never committed. Verified true before splicing: the
scratch generation contains neither. Icon count went 84 to 86.

## Not verified

**Ghosting on the peek, which is the one thing the simulator cannot answer.**
The target band is high-contrast, held, then hidden on a full refresh. If a
residue survives that refresh, the game leaks its only secret. Nothing in the
simulator models ghosting. This needs two minutes on a real X4 Pro: peek,
release, look at the strip.

**Whether the deck is fun**, which no test answers and which needs a table.
