# Games at scale: the plan

Drafted 2026-08-08, rewritten the same day after two cold reviews. Turns
"implement many more games" from a series of one-off efforts into a repeatable
cycle, and fixes the two things that break first when the count goes up: the
shelf runs out of screen, and the website starts lying.

## What is actually true today

Measured against the source, not assumed. Everything here was verified.

- **The GAMES folder fits 9 rows and holds 8.** Body band is 598px
  (800 - 16 margin - 112 header/gutters - 74 player footer), rows are 62px on a
  4px gap (`ShelfScreen.cpp:19-23`, `ToyboxMetrics.h:14-31`,
  `ToyboxTokens.h:155`). APPS has no footer, so it fits 10 and holds 3.
- **The five named games take GAMES to 13**, and Mario says many more follow.
- **No list of promised games exists in the repo.** Now in the `games-promised`
  memory.
- **`site/assets/shots/` is 28 PNGs**: 27 captured by hand from the simulator
  with tap scripts that were never saved, plus `og.png`, which is a Playwright
  composite built _from_ `games.png` (`site/make-og.py:54`). `site/assets/post/`
  holds four more.
- **`site/emulator/crossplay.{js,wasm,data}` is checked in**, last built
  2026-08-07. It is the real firmware, so when it is stale the page lets people
  _play_ an old version.
- **`.claude/skills/` is committed** and reaches every worktree (all 9 trees
  carry 6 files).
- **`crossplay-ci.yml` fires on push to `xteink` and on every pull request**, and
  checks out at the default shallow depth.

## What is already broken, before a single new game lands

Three defects the first review found. None of these are hypothetical, all three
were confirmed in the source, and **all three have to be fixed whichever shape
the shelf ends up with.** This is now the first work, ahead of any decision.

**1. Row icons misplace as soon as a list scrolls.** `iconAtRowRight`
(`ToyboxScreen.h:127-135`) computes `rowY = band.y + index * (rowHeight +
rowGap)` from the absolute item index and never subtracts `topIndex`.
`ShelfScreen.cpp:123-125` feeds it the raw loop counter. The function's own
comment admits the constraint: it is exact "for a band that does not scroll,
which is every list in this fork that carries icons." At 10 games the tenth
icon paints in black at y=721, on top of the black player footer that starts at 722. Once scrolled, every icon sits N rows away from its label. The three shelf
tests in `host-tests/ui/test_ui.cpp` never use a list that scrolls, so nothing
catches it.

So the earlier claim that "nothing breaks at 10, it just gets worse quietly" was
exactly wrong. It breaks at 10, silently.

**2. There is a hard cap of 16 items per folder, and it truncates in silence.**
`ShelfFolderActivity.h:36` sets `kMaxItems = 16` and sizes two fixed arrays by
it; `ShelfFolderActivity.cpp:20` clamps `itemCount` to it with no log and no
static_assert, unlike the icon check next door in `Shelf.cpp:56-64` which is
enforced at compile time. Register game 17 and it does not appear, and nothing
says so. Whatever the cap becomes, it needs the same compile-time treatment the
icon rule already gets.

**3. Touch cannot reach anything below the fold.** `ShelfFolderActivity.cpp:58`
handles exactly one gesture, `wasScreenTapped`. There is no swipe, no drag, and
the 3px overflow track is drawn but not tappable. So row 10 is reachable only by
the physical buttons, on a device where `docs/building-apps.md` states that
games in this fork are touch-only and deliberately so.

**This third one is the real argument for changing the shelf**, and it is
functional rather than aesthetic. The arguments the first draft made against
scrolling were both false: there are no partial repaints
(`ShelfFolderActivity.cpp:106,146` clears and blits the whole panel on every
render) and there are no half-drawn rows (`list.h:218` breaks before drawing one
that would overflow, and nine rows use 590 of 598px). Those are struck.

## Phase 0: rules of engagement

**0a. Voice input rule, into global `~/.claude/CLAUDE.md`.**

Mario dictates, and transcription mangles proper nouns. Four instances in one
conversation: "Widme files" (README), "this report" (this repo), "knucklebballs"
(Knucklebones), "yatzy" (Yahtzee). Two of them would have changed what got
built. Proposed section:

> ## Voice Input
>
> Mario dictates. Transcription mangles proper nouns, product names and jargon.
>
> - When a word looks like a name, a tool or a term and matches nothing in the
>   project, **ask**. Do not guess, and do not silently normalise it into
>   something plausible.
> - Only nouns that would change what gets built need confirming. Dropped words,
>   run-on sentences and grammar noise do not: read through them.
> - Seen so far: "Widme files" = README, "this report" = this repo,
>   "knucklebballs" = Knucklebones, "yatzy" = Yahtzee, "miesweeper" = Minesweeper.

The second bullet is load-bearing. Without it every dictated sentence turns into
an interrogation.

## Phase 1a: fix the three defects

Independent of any decision about shape, and worth doing first so the variants
in 1b are compared on a shelf that works:

- Derive icon row positions from the list's own row rects, or subtract
  `topIndex`. Add a host test with a list that actually scrolls, and mutate it to
  confirm the test can fail.
- Replace the silent 16-item clamp with a compile-time check, matching
  `everyItemHasAnIcon()`.
- Give the folder a touch route past the fold. What that route is depends on 1b,
  so this lands with the winner.
- ~~Persist `lastItem` / `lastFolder`.~~ Done: they live in
  `/.crosspoint/shelf.cfg`, and the item that was open at sleep is persisted
  beside them so wake reopens the game rather than landing on Home. See
  [shelf.md](shelf.md), "Wake comes back into the app, not to Home".

## Phase 1b: the shape

**Decided 2026-08-08: one flat GAMES folder that pages (option A below).**

**The first draft decided this and the decision was wrong.** It rejected
category folders for breaking shelf rule 1, but rule 1 is a _depth_ rule ("a
folder holds items, never folders", `shelf.md:59-62`), and sibling folders on
Home add no depth and no third tap. `shelf.md:195-204` documents adding a folder
as a supported change. Meanwhile paging does break the tap budget it claimed to
protect: 40 games over 9 rows is five pages, so the last page is open plus four
page taps plus open. `shelf.md:18` promises "uniformly two taps to anything."

So both options are live, and the choice is about what the device _is_ rather
than about code. Two candidates, to be built and looked at:

**A. One flat GAMES folder, paged.** Keeps a single place where games live. Costs
a page control, a page indicator, and a rule for where new games land. Browsing
is a walk: there is no search and no keyboard anywhere in the fork, registry
order encodes ship date which the user cannot know, and alphabetical requires
guessing the registered string ("D&DIAGRAMS" files under D). At 40 games,
finding one you did not just play means paging until you see it.

**B. Genre folders on Home, each fitting one screen.** Something like BOARD /
CARDS / PUZZLES beside APPS. Two taps to everything, permanently. No paging, no
page indicator, no "where does a new game land" question because it lands where
you would look for it. The 16-item cap and the scrolling icon bug both stop
mattering because no folder ever exceeds 9. Cost is per folder and documented at
`shelf.md:195-199`: a `UIIcon` value in `BaseTheme.h`, a case in
`LyraTheme.cpp`, a line in `icons.txt`. The doc calls that "affordable at two
folders and would not be at ten"; four is affordable by its own accounting.
Open questions it raises: whether Home has room for four appended rows, whether
all three game folders carry the player footer, and what happens to the games
whose genre is arguable (Insider, D&Diagrams).

Note `ShelfFolderActivity.h:36`'s own comment, which is an argument nobody in
the first draft engaged with: "A folder that outgrows it is a sign it wants
splitting, not a bigger number."

**How it gets settled.** Both built behind a temporary `#define` against a
realistic registry of about 25 games, rendered with `sim-shot.sh`, composed side
by side, Mario picks, the loser is deleted in the same commit. This is
`building-apps.md`'s "Offer designs by rendering them, not by describing them"
(line 106), and it exists because options described in prose are consistently
judged wrong.

**The dummy registry must exceed 16 carefully**: with the cap still in place the
comparison silently renders the wrong screen, which is why Phase 1a comes first.

Also to be settled by looking, not prose: whether the restored position is
visible at all. `ShelfFolderActivity.cpp:41` sets `cursorShown = false` and
line 136 passes `selected = -1`, so today nothing is highlighted on arrival. At
8 items that is fine because the whole list is on screen. At 40 it means landing
on page 3 of 5 with no row marked and no sign the other pages exist.

Rejected outright, with the corrected reason: **a 3-column grid.**
`CONNECTIONS`, `HACKER NEWS` and `D&DIAGRAMS` do not fit a 141px tile
(448 content width, two 12px gutters, over three). The first draft also claimed
Jersey has no ellipsis to truncate with; that is wrong, three ASCII periods work
fine, and the clause is struck. The tile-width argument stands on its own.

## Phase 2: making the website unable to lie

This is where the real work is, and the first draft badly under-scoped it.

### Measured, not assumed

Run before committing to any of this, because the whole design rests on the
first line:

- **Two identical captures are byte-identical.** Same tap script, same card,
  same md5. Pixel comparison is therefore a usable gate, which is the load
  bearing assumption of everything below.
- **The simulator emits 960x1600 and the site ships 480x800.** A downsample sits
  between them, so a manifest entry that pins only the tap script still permits
  two different files from one render. The scaler and its settings are part of
  the recipe.
- **The SD card decides the picture, and it is gitignored** (`fs_`, `fs_*/`).
  The device's own three-word name is drawn into the footer of every shelf shot
  and is generated per card, so `games.png` has already named three different
  devices across three captures; the alt text in `index.html` named the first of
  them long after the file had moved on, because nothing ties prose to pixels.
  The recent-books list is the same hazard one level up: this tree's card has no
  book, so its Home screen reads "No open book" where the shipped `home.png`
  shows Alice. No tap script alone can reproduce these shots, so a **committed
  seed card** is a third prerequisite that the first version of this plan missed
  entirely.

So an entry needs three pinned inputs, not one: the script, the downsample, and
the card.

**2a. The bootstrap is the main cost, and it was unfunded.** The draft noted
that the 27 tap scripts were never saved and then asked for an `input` field for
every manifest entry without costing it. Reconstructing those 27 scripts _is_
Phase 2. Until an entry is verified to reproduce its own shot, its recorded
state is a guess and any staleness report is confident noise.

**2b. Determinism is a clock problem more than a dice problem.** The draft named
six games and was wrong about half of them:

- **The dice half is small.** Five call sites, all already seeded xorshift cores
  taking entropy from `millis()`: `SolitaireActivity.cpp:78`,
  `BattleshipActivity.cpp:52`, `InsiderActivity.cpp:68`,
  `MurdleActivity.cpp:159`, `JaipurActivity.cpp:198`. Pinning these for sim and
  host builds is an afternoon.
- **D&Diagrams has no randomness at all.** Its puzzles are precomputed in
  `DungeonPuzzles.h`. It should not have been on the list.
- **Connections is already deterministic** by design: its board is seeded from
  the puzzle id (`ConnectionsActivity.cpp:146`). Its nondeterminism is the
  calendar, via `time(nullptr)` at line 83.
- **Study was missing and is the worst offender.** Three `time(nullptr)` calls
  (`StudyActivity.cpp:138,172,400`), and its shot's alt text on the site quotes
  FSRS intervals that are a function of today's date. `study.png` and
  `connections.png` rot on the calendar with zero commits.

So the work is a clock the simulator can pin, not just a seed. That reaches
`lib/hal/` and the two apps that call `time(nullptr)` directly.

**2c. Staleness is decided by pixels, with git as a cheap pre-filter.** The
draft argued the opposite and the second review dismantled it:

- Provenance false-positives on **every game landing**, because
  `gen_toybox_icons.sh` rewrites `src/apps_local/ui/ToyboxIcons.h`, which is a
  dependency of nearly every shot. Adding an icon is mandatory per game, so the
  "one dither change fails all 29" failure the draft used to reject pixel diffs
  is the _normal case_ for provenance, not the edge.
- Provenance is blind to the two largest real drift sources: the wall clock
  above, and the SD card, which is gitignored (`fs_*/`) and which
  `building-apps.md` insists on seeding precisely because it determines the
  picture.

So: `--stale` narrows candidates by git, `--regen` re-renders and compares
bytes, and **only a byte difference is a finding**. Identical pixels write no
file, so there is genuinely no binary churn, and the shared-token false positive
disappears because the render is unchanged. It costs a simulator build per
candidate, which decides where it runs.

**2d. The gate runs at landing, in the tree. Not in CI, and not in `check.sh`.**
The draft put it in `crossplay-ci.yml` and the second review showed it can
neither run nor prevent:

- `actions/checkout@v6` there takes the default shallow clone, so a git-history
  check silently returns empty and reports green, which is this project's own
  recorded anti-pattern.
- Vercel deploys off the push, in parallel with the workflow. A red job is a
  smoke alarm, not a lock.
- The workflow also fires on every pull request, which reintroduces exactly the
  mid-development noise that was the reason for staying out of `check.sh`.

`--regen` cannot run on a runner anyway: `sim-shot.sh:20` refuses outside its own
tree. So the gate is a step in the landing script, run in the worktree where the
simulator lives.

**2e. The manifest.** `site/shots.toml`, one entry per image, holding the tap
script, capture time, size, and the byte hash of the accepted render.
`og.png` needs a dependency on `games.png` rather than on source paths, since it
is composited from it. The manifest covers `assets/post/` and the emulator wasm
too.

**2f. Alt text.** The draft proposed grepping `index.html` for a manifest
string. Three of the seventeen alt values contain HTML entities today
(`D&amp;DIAGRAMS` at line 104, and two more), so a manifest holding the true
string never matches, and holding the escaped string means the manifest is a
copy of an encoding rather than the truth. Worse, presence is not accuracy: the
check goes green when nobody edits either, which is the actual failure mode.

Honest resolution: the alt text is prose about a picture, so it is reviewed by
whoever regenerates the picture. `--regen` prints the current alt text beside
the changed image and requires an explicit acknowledgement. That is a prompt,
not a proof, and the plan should not pretend otherwise.

**2g. "No build step for the site" was already false** and is struck from the
refusal list. `site/README.md` documents two mandatory pre-deploy steps that
fail silently if skipped.

## Phase 3: the game cycle

A skill at `.claude/skills/new-game/SKILL.md`, but **short and about judgement**,
not a 12-step spine. The five existing skills are 54 to 65 lines, and
`.claude/skills/README.md` says they are "principle- and decision-focused on
purpose" and "do not restate CLAUDE.md". The draft's 12 steps mostly restated
`docs/building-apps.md` and `docs/shelf.md`, and the step most likely to be
skipped, the landing, was step 11 of 12: after the part that feels like the work.

Split accordingly:

- **The skill carries the judgement.** Does this game suit a slow black and
  white panel and touch (anything real-time does not)? Rules layer first and
  freestanding, screens host-testable, activity thin. Does it want two players,
  and if so it goes through `src/apps_local/link/` and sees a match with a turn
  and an opponent, never a radio or a packet. When to stop.
- **A script carries the mechanics.** `scripts_local/land.sh`: regenerate the
  affected shots, rebuild the emulator wasm, print the docs and README and memory
  files that this game's change touches, refuse to pass while any is stale.
  Mechanical steps belong somewhere that cannot forget them.

## Phase 4: the critic loop

Three cold critics, each starting with no builder context, because the failure
modes differ and one agent judging all three does none well:

- **Rules critic**: the freestanding core plus a driver, thousands of headless
  games, hunting illegal states, unreachable wins, unhandled draws, an
  exploitable opponent, save and restore round trips.
- **Look critic**: rendered screenshots plus `docs/design-language.md`. Toybox
  fit, ink budget, legibility at panel size, whether decoration carries data. It
  looks, it does not play.
- **Play critic**: scripted walkthroughs only, bounded to a named set of
  scenarios. `sim-shot.sh` is headless and takes the per-tree build lock, so
  every scenario is a build and a replay from boot.

The draft said "a round ends when it reports no confirmed findings" and gave the
critic veto. That is an unbounded loop whose exit is controlled by the party
holding the veto, and a critic that returns nothing is indistinguishable from one
that did not look. Three fixes:

- **Calibrate before believing.** Plant a known defect once per critic and
  confirm it is found. This project already has the lesson written down twice:
  prove the probe can say yes, and mutate the test to check it notices.
- **Cap the rounds.** Three. A fourth means something structural is wrong and it
  goes to Mario rather than looping.
- **Order the critics.** Rules beats look beats play when they conflict, because
  a correct game that looks wrong is fixable and a beautiful broken one is not.
  Critic-versus-critic deadlock is likelier than implementer-versus-critic, and
  the draft had no answer for it.

**The critic keeps its veto over the implementer.** Disagreements go to Mario.

Written into the skill as an honest limit: a green report means the rules were
fuzzed and the screens were looked at. It does not mean an agent played the game
and enjoyed it.

## Phase 5: one game through the whole thing

The draft picked Connect Four for being small. Smallness is the wrong criterion:
it has no randomness, so it never crosses Phase 2, which the plan itself calls
the real work; it is game 9, so it fits inside current capacity and never
exercises the shelf; and the link layer it would test is already proven by
Battleship.

**Knucklebones instead.** Dice, so it crosses the determinism work. Inherently
two-player, so it exercises the link layer on a game that was not built for it.
Rules that still fit on a napkin, so a failure points at the process rather than
at the game. It also lands after the shelf work, so it is the first game to go
onto whatever shape wins.

## What this plan refuses to do

- **No sub-folders.** Structurally impossible, and correctly so.
- **No pixel-diff gate in CI, and no staleness check in `check.sh`.**
- **No new multiplayer abstraction.** `link/` exists.

Removed from the draft's refusal list: "no collection folder" (now live option B)
and "no build step for the site" (the site already has one).

## Open for Mario

1. ~~Shape~~. Decided: option A, one flat GAMES folder that pages.
2. **Knucklebones as the pilot**, or another of the five.
3. **Where a new game lands in the registry.** Ship order puts every new game on
   the last page, which at five pages is the least visible place on the device.
   Alphabetical fixes that and reorders the list under the user every time. The
   cheapest answer is neither: the registry is a handwritten table, so a new game
   gets _inserted_ where it belongs rather than appended, and no code decides it.

## What the reviews changed

Recorded because the first draft was confidently wrong in ways worth remembering:
the shelf already breaks at 10 rather than degrading, the tap-rule argument ran
backwards, both stated reasons to dislike scrolling were false while the real one
(touch cannot reach past the fold) went unmentioned, provenance-based staleness
fails on exactly the case it was designed to avoid, the determinism work is about
a clock rather than dice, and the CI gate could neither run nor block.
