# Toy Battle terrain editor

Trace a printed Toy Battle board into the topology the firmware needs, without
anybody hand-copying indices.

```bash
open tools_local/terrain-editor/index.html
```

## Why it exists

Reading a board off a photograph is the one part of this game that does not
survive being done by eye. Castle Field is regular and came out fine; La
Croisette is irregular and did not. So the person who can see the board does the
tracing, and the tooling does everything after that.

**Positions are a hint, not the deliverable.** The board is redrawn for a
480x800 panel, so what has to be right is the graph -- which bases exist, what
joins what, which bases fence each region and what it pays. Where anything sits
is a starting layout, nothing more.

## Doing it

1. Pick the terrain from the dropdown. That prefills the name, the special base
   kind it uses, and the objective where it is already known.
2. Load the board picture with **ref** and trace over it. Zoom to taste.
3. `1` drop bases. `2`/`3` the two H.Q. `4` click two bases to join or unjoin
   them. `5` stamp the special kind chosen on the right. `6` click every base
   that fences a region, set its medals, add it. `7` drag. `8` erase.
4. Watch the **Checks** panel. Every line must be OK.

**An H.Q. can fence a region.** Click it with the region tool like any other
slot when the region really does run up to it. It shapes the region and it is
what the medals get centred in, but it never enters the mask: the rulebook says
occupy every *base* surrounding a region, and an H.Q. is not a base, so a fence
of three bases and an H.Q. is taken by holding the three.
5. **copy JSON** and paste it back into the chat.

## The checks are the point

The list in the panel is the same one
`host-tests/toybattle/test_toybattle.cpp` runs against every terrain in the
firmware: both seats have an H.Q., no base is stranded, regions are fenced by
two or more real bases, the medals on the board can actually reach the
objective, gates admit something. A board that passes here loads there. Keep the
two in step when either changes.

`selftest.py` breaks a known-good board twelve different ways and requires the
checker to catch all twelve, because a validator nobody has tried to break has
never been tested. It runs as part of `host-tests/toybattle/run.sh`.

## From JSON to firmware

```bash
tools_local/terrain-editor/to_cpp.py boards/castle-field.json           # emit the terrain
tools_local/terrain-editor/to_cpp.py --check boards/castle-field.json   # validate only
```

`boards/castle-field.json` is the fixture that proves the whole path: the
Castle Field in `ToyBattleCore.cpp` **is** this file's generated output, and
regenerating it reproduces the committed board exactly. The suite re-checks the
fixture on every run, so a hand-edit to the generated terrain shows up as a
failure rather than as a slow divergence.
