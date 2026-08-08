---
name: new-game
description: Building a new game or app for this fork's shelf. Use when adding a game, porting a board or card game, starting an app in src/apps_local/, or when asked to make something playable on the device. Covers the layer split, the two state machines to write before any screen, when a game earns multiplayer and how it gets it, the critic loop and its termination condition, and the landing step that gets skipped.
---

# Building a Game

The method is in `docs/building-apps.md`, the registration recipe in
`docs/shelf.md`. Read those. This is the judgment layer: what to decide, in what
order, and where the last nine games lost time. Work in a worktree.

## The gate, before any code

**Does it suit a slow black-and-white panel and a thumb?** Real-time does not.
Anything needing a keyboard does not: there is none. Anything whose fun is
colour or sound does not. Say so and stop rather than building it badly.

**Does it want two players?** If two people in a room would want it, it goes
through `src/apps_local/link/` and nothing else: the game sees a match with a
turn and an opponent, never a radio or a packet. No lobby, no host choice, no
code to read out. Resolve any multiplayer question by asking what the DS did.

## The split, in this order

| Layer      | Knows about                           |
| ---------- | ------------------------------------- |
| Rules      | nothing -- freestanding C++17         |
| Opponent   | the rules, nothing else               |
| Navigation | nothing -- two enums and a table      |
| Screens    | FreeInkUI and Toybox tokens           |
| Activity   | renderer, storage, input, shelf, link |

The first four are host-tested with no device. A screen builder that reaches for
`GfxRenderer` fails to compile in `host-tests/ui/`, which is the point.

**Write the navigation before any screen exists**, as two separate machines: one
for the shell (menu, how-to, settings, board, result), one for the phases of
play. Mixing them is how "Back went to the wrong screen" happens -- if "am I in
the how-to" and "is it my turn" share flags, Back has to know about turns. Make
`back()` an exhaustive switch with no default, and test properties rather than a
transcript: every screen reaches the top, no pair can Back into each other,
exactly one leaves the app.

**If the state is also the wire format, validate it.** A `turn` field indexed
into a seat array is memory corruption when a packet is corrupt; the link layer
checks payload length and nothing else.

## The critic loop

After the game works, not instead of making it work. Each critic starts cold.

- **Rules critic**: the freestanding core and a driver. It should compile
  experiments, not read code -- every finding that has mattered came with a
  mutant or a number.
- **Look critic**: rendered screenshots and `docs/design-language.md`.
- **Play critic**: scripted walkthroughs, bounded to named scenarios.

**Calibrate before believing**: plant a known defect and confirm it is found. A
critic reporting nothing is indistinguishable from one that did not look.

A round ends when it reports nothing you agree with; the loop ends after three.
A fourth means something structural is wrong and it goes to Mario. Where critics
conflict, rules beats look beats play. Green means fuzzed and looked at, not
played and enjoyed, and the report should not imply otherwise.

## Two traps already paid for

**A test asserting something was drawn cannot tell you it was visible.** Ink on
the black header band passes every host test and shows nothing.

**Layout arithmetic that overflows the panel is silent.** Pin the extremes of a
drawn board to the screen, or find it by looking, once.

## Before you call it done

`./scripts_local/check.sh --committed`, not just `check.sh`: uncommitted work
masks a broken commit.

Then land it in the same effort, because this is the step that comes after the
part that felt like the work: `docs/<game>.md`, a site card with a regenerated
screenshot and honest alt text, README rows, trademark attribution if the game
is somebody's, a `<game>-app-state` memory with its `MEMORY.md` line, and the
browser emulator rebuilt so the demo is not a version behind the page.

**Save the tap script that produced your screenshot, beside the shot.** Every
one of the site's existing images was captured by hand and its recipe thrown
away, so not one of them can be reproduced today. One recipe per game costs a
line; back-filling twenty-seven costs a week. Do not add to that pile.

## Self-review

- Does anything outside the activity know the renderer, storage or the radio?
- Is `back()` total, and does every screen reach the top?
- Did a mutant of each new invariant actually fail the suite?
- Did you look at every screen you changed, at native size?
- Is the shelf row registered with an icon, and does the folder still fit?
- Did the landing happen, or is it a promise in the summary?
