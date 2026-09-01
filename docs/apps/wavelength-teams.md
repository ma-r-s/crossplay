# WAVELENGTH: teams mode, and why the end-call is shelved

Mario confirmed on 2026-09-01 that **teams mode should be implemented**. This
records what that decision already settles, what it leaves open, and the one
constraint any answer has to satisfy. It is a handover, not a design: the
design is the next session's to make, and **the game design is theirs to decide
rather than Mario's to approve** -- he decides what the product is, and he has;
how the game works is the designer's job.

## Why the end-call is shelved and not deleted

**The end-call is the OPPOSING TEAM'S bet.** In the physical game one team's
psychic gives the clue, that team places the dial, and the _other_ team bets
which side of it the target sits on. It is their chance to profit from the first
team's imprecision.

Co-op has no opposing team, so **it has nobody to make that bet**. The game was
asking the players who had just settled on a number whether they thought the
answer was somewhere else -- and if they had an opinion they would have moved
the marker. Mario spotted it cold: _"I have no idea how it makes sense to ask a
whole table that just settled on an answer if they thought it was higher or
lower."_

The design note (`wavelength-design-agreed`) debated the call's LABELS at
length -- spectrum end words rather than higher/lower, because the player
opposite reads the strip inverted -- and never once examined whether it belonged
in a co-op game at all. **A good decision about a thing whose place was never
questioned.**

Deleting it would mean building it again when teams lands, so `Mode::CoOp` /
`Mode::Teams` lives in `WavelengthCore.h`, both configurations are exercised by
the suite, and only co-op's route to the call screen is removed.

**Co-op does not BYPASS the call screen, it does not route to it.** A path that
is reachable-but-skipped is the same hazard as an input path bounded rather than
removed: it looks handled and stays live. `testCoOpNeverConsultsTheCall` proves
the absence by scoring every position both ways and requiring the same answer,
rather than by anyone reading the code and believing it.

**Co-op's ceiling is 5**, which is what every screen already said.

## The four questions, and what is already answered

**1. Who holds the panel, and when?**

Partly answered, and this is the hard constraint rather than an open question.
**The board is public and only the target is hidden.** Two cold critics,
briefed as players, independently killed the first design because a six-inch
panel in one person's hands makes the board as private as the secret, and that
person then holds four powers at once: the only one who can see it, move it,
lock it and referee it. Four more strangers have since tried to break the
secrecy and could not.

So: **any two-team design that creates a screen only one team may look at is a
regression**, and it will not feel like one while you are building it. It will
feel like the natural way to give each team its turn. The moment the board goes
back into one pair of hands, the game the critics rejected is back.

**2. Does the side call return as the opposing team's bet?**

That is the only reading under which it coheres, and it is why it is shelved
rather than deleted. The open half is **how the device knows which team is
answering** without asking a question it cannot verify. The co-op flow's answer
to every question of this shape has been to make the instruction social rather
than enforced -- `GUESSERS DECIDE. NOT THE GIVER.`, `THEN HANDS OFF THE
DEVICE.` -- and to make violations _visible_ rather than impossible, which is
how abandons ended up counted and shown. That pattern is available here and has
survived four rounds of strangers.

**3. How does a game end?**

Unanswered. The physical game runs to a target score rather than a round count.
Co-op has no end at all: the session runs until somebody presses END SESSION,
and a stranger asked for "something between 'you're done' and nothing". A target
score would answer both.

**4. What happens to the existing all-time record?**

Unanswered, and it is now a real problem rather than a hypothetical: **records
already on cards were scored on a ceiling of 6**, before an exact lock stopped
paying the end-call bonus it was never entitled to. A second mode that scores
differently makes one number the average of three different scoring systems.

The decision already taken: **old records are not silently rescaled.** Nobody's
night gets rewritten to make a statistic tidy. Whether the answer is a separate
per-mode record, a version stamp in `wavelength.sav`, or a reset with a note, it
should be chosen rather than inherited.

## What a newcomer gets wrong about the pause

- **Back inside a round opens the pause; it does not abandon.** Abandoning is a
  deliberate press on a screen that states its cost. Back out of the pause
  RESUMES: the safe direction is the default.
- **Abandons are counted and shown**, on the next PASS LEFT and on the end
  screen. That is not bookkeeping -- it is the anti-cheat. Abandoning was free
  and invisible, and the screen before it tells everyone else to look away, so
  the clue-giver could re-deal until they liked their target with the game
  itself clearing the room. Making it social is what closed it.
- **The pause carries the scoring table** because "how many points is one off
  again?" is asked constantly and used to cost the round to answer.
- **A paused round suppresses sleep.** The table is arguing over it, which is
  exactly when nobody is touching the glass.

## Where the constraints are written down

`docs/apps/wavelength.md` is the co-op flow and its traps. The memories
`wavelength-design-agreed` and `wavelength-app-state` carry the decisions and
what building it taught. Read all three before changing a rule: several of them
record ideas that were killed and why, and the reasons are not obvious from the
code that survived.
