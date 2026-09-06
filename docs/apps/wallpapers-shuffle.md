# Wallpapers: choosing a set, and letting it take turns

Card #305. Mario asked twice: *"didn't we agree to have a way to select multiple
ones at the same time and for them to cycle?"*

This is the design, written before the code, revised by a cold critic before a
line was written, and corrected afterwards where the critic proved it wrong.
Vocabulary note (#313): **rotation here means cycling through wallpapers**,
never screen orientation.

## What the platform already does

Nothing here schedules anything. `SleepActivity::renderCustomSleepScreen`
(UPSTREAM, `src/activities/boot_sleep/SleepActivity.cpp:552`, not ours to
change) already:

1. draws `/sleep.bmp` if its headers parse -- **this wins outright**;
2. otherwise picks a random valid image from `/.sleep`;
3. otherwise picks a random valid image from `/sleep`;
4. otherwise draws the default screen.

The sleep screen is drawn during a wake from deep sleep, which is a full chip
reset. Everything the feature needs therefore lives on the card and nothing in
RAM: `/sleep.bmp`, `/.sleep/*.bmp`, `/wallpapers/.active`. No new persistence.

### It takes turns; it does not shuffle, and the word matters

`selectRandomSleepFile` excludes the last `min(recentFill, N - 1)` images
(`SleepActivity.cpp:430`), and `APP_STATE.recentSleepFill` climbs to 16 and is
never reset. So for any set up to seventeen wallpapers **every member but one is
excluded**, and the order is a strict cycle: a two-set alternates, a three-set
round-robins. Calling that "shuffle" or "random" on screen would be a promise
the platform does not keep, so no user-facing string in this app says either
word. `host-tests/wallpapers` asserts that as a property of every sentence
rather than trusting anyone to remember it.

Two upstream behaviours we inherit and do not fix, both worth knowing when a
report arrives:

- the recent-window ring stores **directory indices, not names**, and it
  persists across the deep-sleep reset. Remove a member and add another and the
  new file can land on the freed index and arrive already marked "recently
  shown". "The one I just added never comes up" is that.
- the same ring is shared with `/sleep` (both are `SleepRecentKind::Standard`).

## The invariant, and why it is a convergence rather than a guarantee

> `/sleep.bmp` holds a file **iff** exactly one wallpaper is chosen.
> `/.sleep` holds files **iff** two or more are chosen.
> Never both.

`wallpapers::cardShapeFor(n)` is that rule as a function, and
`WallpapersActivity::commitSelection` is its only enforcer -- the single writer
of all three paths.

| chosen | `/sleep.bmp` | `/.sleep` | what the glass shows |
| --- | --- | --- | --- |
| 0 | deleted | emptied | the user's own `/sleep`, if they have one; else the default screen |
| 1 | that wallpaper | emptied | that wallpaper, every sleep |
| N >= 2 | deleted | exactly those N | a different one each sleep, in a cycle |

An emptied `/.sleep` does not shadow: `selectRandomSleepFile` returns false on a
directory with no valid image (`fileCount == 0`) and upstream falls through. The
directory itself may stay.

**Rows 1 and N >= 2 shadow the user's own `/sleep` folder**, which is upstream's
own documented rotation directory. That is the same silent precedence this
design exists to prevent, one level down, and it is unavoidable: `/sleep.bmp`
and `/.sleep` are both checked before `/sleep` by code we do not own. The app
does not manage `/sleep` and does not delete it; choosing nothing restores it.

**This app is not the only writer of these paths.** `BmpViewerActivity` copies
any browsed BMP straight to `/sleep.bmp` (`src/activities/util/BmpViewerActivity.cpp:200`)
and the File Transfer page can drop one at the card root. A power cut in the
middle of a one-to-many transition leaves one behind too. So "never both" is
what the app **converges to**, not something it can promise about a card it did
not have to itself. `loadSelection()` therefore reconciles on every entry rather
than assuming:

- `/sleep.bmp` present is reported as the chosen single, because that is what
  the glass shows;
- files in `/.sleep` behind it are **not deleted** -- they are the user's, and
  the hint strip says `One wallpaper is hiding a set.` instead;
- the next commit normalises, which is the user's own act and by then an
  announced one.

The commit order inside each branch is chosen so a power cut leaves a picture
the user CHOSE on the glass rather than nothing: the winning slot is written
before the losing one is cleared.

### Where the truth lives, and what `.active` becomes

The picker never reports a set from a hint file. It reports what is **on the
card**, because a second source is a second thing to be wrong (#354 was exactly
that shape):

- **N >= 2**: membership is `/.sleep` listed directly. There is no hint to
  disagree with, because the directory the picker reads *is* the directory the
  sleep screen reads.
- **exactly 1**: `/sleep.bmp` is a copy and carries no provenance, so which
  library file it came from is unknowable from the file itself. That is what
  `/wallpapers/.active` is for, and it keeps exactly today's meaning.
- **`.active` is deleted whenever the selection is a set.** A stale name beside
  a set is the disagreeing second source; deleting it is cheaper than
  reconciling it.

Names are matched case-insensitively (`wallpapers::sameFileName`). The card is
FAT: long names keep their case, the filesystem matches without it, and a card
that has been on a PC can hand a name back in a different case. A
case-sensitive test would draw no marker for a wallpaper that is in the set.

### Two members the picker counts and cannot show

- **A wallpaper deleted from the library while it is in the set** keeps showing.
  The files in `/.sleep` are copies, exactly as `/sleep.bmp` is a copy, and the
  app already promises that. It has one fewer tile to mark, and the count still
  counts it, because the count has to describe what the sleep screen does.
- **Files the user put in `/.sleep` themselves** are adopted for the same
  reason: they are what the sleep screen shows. They are never deleted to
  satisfy the invariant -- only by the user unchoosing them, or by their
  choosing a single wallpaper, which is what "set your sleep screen" means.

The gap this leaves: a member whose BMP headers do not parse is skipped by
`findNextValidSleepImage` and still counted here. Everything the app itself
writes is size-checked at `kWallpaperFileBytes` on the way in, so that can only
be a file somebody put on the card by hand.

## The interaction

### No gesture is a hold

`InputManager::wasTouchTap` has **no duration gate**, and an app may only pump
`mappedInput.update()` when it blocked the loop, so a hold on a screen that
repaints slowly arrives as an ordinary tap. A feature whose entry gesture is a
hold is a feature that intermittently does not exist. Nothing here uses one.

### Two grid modes, and the chip that switches them

`choosing_` is a plain bool, RAM only, reset on `onEnter`.

**Normal (`choosing_ == false`)**

- header: `WALLPAPERS`, the page or count label, and an outline chip **`CHOOSE`**;
- 0 or 1 chosen: a tap on a tile pins that one -- today's behaviour, unchanged;
- **2 or more chosen: a tap on a tile opens the set for editing with that tile
  toggled.** It does not collapse the set. That was the first design and it was
  wrong: one stray tap undid six taps of work, silently, under the very pixel
  that had meant "add to the set" one chip-press earlier. That is
  same-pixel-different-action with a destructive outcome. Getting back to one
  wallpaper is unchoosing the rest, which normalises to a plain pin at the last
  one.

**Choosing (`choosing_ == true`)**

- header: `CHOOSE A SET`, `N CHOSEN`, and the chip reading **`DONE`**;
- a tap on a tile toggles membership, **committed to the card immediately**;
- Back leaves choosing and stays in the app.

`DONE` and `Back` do the same thing, which is the point: there is no commit/
cancel ambiguity to get wrong, because there is nothing uncommitted.

**The count is in the header, not in the marks.** Four tiles fit a page and the
built-in library is six pages, so at most four marks can ever be on screen and
usually zero or one. Someone building a five-wallpaper set would otherwise be
paging blind.

**The chrome tiles keep their meaning in both modes.** `+ Add a wallpaper` and
`GET THE N BUILT-INS` still do what they say, and they leave choosing mode
because they leave the grid -- nothing is lost, since the set is already on the
card. A Notice raised by a failed toggle does **not** leave choosing mode: the
notice was about one file, and the user was in the middle of building a set.
The chip is drawn by `buildGridChrome` alone, so Offer, Fetching, Notice and
Help never carry it.

**The mode costs one tap on entry and one on exit.** `surfaceMeaning()` now
mixes in `choosing_`, because the mode changes what a *cell* does, so a tile tap
that arrives before the new frame is on the glass is refused -- 0.3 to 2s on
this panel. That is the correct answer (the tap was aimed at the previous
screen) and it is a real cost, named here rather than discovered on hardware.
`activeIndex_` stays out of the meaning, for the reason it was taken out.

### Why every toggle commits immediately

The alternative -- hold the set in RAM and write it all on `DONE` -- buys a
cancel and costs the two things a cancel cannot pay for:

- **A card that fills at wallpaper 4 of 5** leaves `/.sleep` half written and
  the sleep screen cycling a partial set with nothing saying so. Committing a
  tap at a time means the adds happen one file ahead of the removes, so a copy
  that fails leaves the set exactly as it was.
- **Two exits that mean different things.** On a panel this slow, `DONE` and
  `Back` disagreeing about whether the last twenty seconds counted is a bug
  factory. With nothing uncommitted they cannot disagree.

The cost is one ~48KB copy per tap, which is what a pin costs today, and small
beside the repaint the tap already causes. Two transitions cost more than one
copy and it is worth saying so plainly: 1 -> 2 copies both wallpapers into
`/.sleep` and then deletes the pin, and 2 -> 1 copies the survivor to
`/sleep.bmp` and then empties `/.sleep`. Neither intermediate state shows
nothing, and neither shows a picture the user did not choose; both are the
"pin plus set" state that `loadSelection()` already reconciles and reports.

### The free-space precondition

`HalStorage::freeBytes()` walks the FAT cluster chain -- seconds on a large
card, cached 20s -- and running it from a tap is how this app got its ten
seconds of blank screen. So the walk stays in `loop()`, after the paint, on the
existing `warningPending_` machinery; it now keeps its **raw** answer, and each
add applies `wallpapers::roomFor` at `kAddFloorBytes` (the 12MB floor that
protects Study's review log, plus the one file this tap writes) with no second
walk. Every add re-arms the walk so the number refreshes behind the next paint.

`Unknown` **proceeds** here, unlike the download's precondition, and the
difference is the reason rather than an exception to it: that doctrine says
proceeding on Unknown costs the user whose card is already in trouble, and this
write is transactional -- `.part`, size-checked, renamed -- so a card that
cannot take it loses nothing and says so on screen. Refusing on a card whose
free-space walk merely failed would make the picker unusable on it.

The `.part` discipline is not optional and the reason is the opposite of the
obvious one: `findNextValidSleepImage` **accepts** a truncated BMP.
`Bitmap::parseHeaders` seeks to `bfOffBits` and never checks that the pixel data
is complete, so a half-written 480x800 image is a *valid* sleep image and gets
drawn, half-rendered, on every sleep. The rename is the only thing between a
failed copy and a permanently broken sleep screen. `sweepPartFiles` now clears
them from all three places this app writes one: the library, `/.sleep`, and the
card root beside the pin -- it only ever swept the library, so
`/sleep.bmp.part` has been an orphan on any power cut mid-pin since the app
shipped.

### Honesty: a count is the last thing the strip says

`wallpapers::reachOfPinnedSleep` decides whether the settings let the sleep
system draw what this app wrote. Verified line by line against
`SleepActivity::onEnter`: it is **the same predicate for a set**, because every
gate applies before upstream ever chooses between `/sleep.bmp` and `/.sleep`.
Quick resume and the timeout flag short-circuit above the mode switch;
`TRANSPARENT_CUSTOM` returns earlier and draws overlays, never `/.sleep`;
`CUSTOM` and `COVER_CUSTOM` both land in `renderCustomSleepScreen`; and
`COVER_CUSTOM` from inside a book keeps its conservative `false` for the same
reason it does for a single file.

But reach is a pure function of two SETTINGS values, and a set has one more way
to be blocked that no setting can see -- a `/sleep.bmp` shadowing it. So the
strip is ordered, and `host-tests/wallpapers` asserts the ordering by coverage
over every combination rather than by matching words:

1. a settings caveat (`reachHint`) -- nothing of the user's can appear at all;
2. what the last selection changed behind their back
   (`stripLineAfterSelection`, which carries any caveat with it);
3. `One wallpaper is hiding a set.`;
4. the mode's own line, with a count only when nothing above applied.

**A number never appears while anything is stopping those files reaching the
glass.** That is the assertion, and it is what "shuffling 5 wallpapers" would
otherwise be a lie about.

## Fixed in passing

`pageCount()` counted **one** chrome tile while `drawGrid` and the tap handler
both counted `specialTiles()`, which is two while the built-in set is
incomplete. Three wallpapers plus two chrome tiles is five tiles, which is two
pages and was reported as one, so `clampPage()` forbade the page holding the
last wallpaper and it could not be reached. That is the "two halves disagree
about what is in a cell" failure the comment above `specialTiles()` names as
this fork's most repeated bug, and this design adds paging pressure to it. The
arithmetic is now `wallpapersui::pageCountFor`, freestanding and walked over
every combination in `host-tests/wallcaption`.

## Composing with PR #147 (`app/wallhold`, hold a tile to preview or delete)

Deliberately built on `xteink` rather than merged into #147, so each lands on
its own. They do not fight for a gesture: #147 owns the **hold**, this owns the
**tap** and one chip. The resolution when the second of the two merges:

1. In choosing mode a hold must **not** open the sheet. The sheet offers a
   delete, and a delete behind a gesture whose classifier misses
   (`tapWasHeldLong`) must never be reachable from a mode whose taps mean
   something else.
2. `saysOnSleepScreen(isMarked, reach)` from #147 answers for a set too, with
   `isMarked` meaning "in the chosen set" -- but it must also take the
   shadowed-set fact, for the same reason `shuffleStripLine` does: the sheet
   asserts "This one is on your sleep screen now.", and behind a stray
   `/sleep.bmp` that is false for every member but one.
3. Both edit the tap branch of `WallpapersActivity::loop()` and the chrome
   model; the conflicts are additive and named here so the resolver does not
   have to rediscover them.
