# Building an app for this device

What we learned building Chess. Read this before writing a second app; most of it
was paid for with a mistake.

Three companion docs: [LOCAL_SCOPE.md](../LOCAL_SCOPE.md) for why the fork is
shaped the way it is, [shelf.md](shelf.md) for the mechanical
add-an-app recipe and the navigation rules, [design-language.md](design-language.md) for how it should
look. This one is about **method**: how to work, how to verify, and what the
platform will do to you.

---

## 1. The loop

You have a full desktop simulator. Use it for everything.

```bash
./scripts/dev.sh          # Mario's window: rebuilds and restarts on every change
./scripts/sim-shot.sh '<input>' '<screenshots>'   # scripted, headless, for you
```

Two instances run at once with separate SD cards (`fs_mario/`, `fs_agent/` via
`CROSSPOINT_SIM_SD`), so agent test runs cannot disturb a game in progress. A
build is ~7 seconds. There is no excuse for guessing.

Input script actions are `<ms>:<action>` joined by `;`:
`TAP:x,y[,hold]`, `SWIPE:x1,y1,x2,y2[,ms]`, and the keys `BACK ENTER LEFT RIGHT
UP DOWN POWER SLEEP HOME QUIT`. Coordinates are logical pixels; the X4 Pro is
480x800 portrait. Screenshots are `<ms>:<path>`, auto-converted to PNG.

`sim-shot.sh` prints the activity trace, so a test is usually "drive some taps,
then grep for `Entering activity: X`". That is a real regression test and it
runs headless.

### Landscape, and the one thing it breaks

`renderer.setOrientation(LandscapeCounterClockwise)` gives 800x480 logical
coordinates, and `tapToLogical()` maps touches through the same transform, so
an app's own code needs no rotation arithmetic at all. Set it in `onEnter` and
set it back in `onExit`: the orientation is global, and leaving it turned
rotates whatever activity comes next.

What does not follow the rotation is `sim-shot.sh`. Its input script is
normalised **once at startup**, against the portrait logical size, and only
then does `tapToLogical` rotate it. So a script tap aimed at a landscape screen
has to be converted:

```bash
land() { python3 -c "print(f'{round($1*479/800)},{round($2*799/480)}')"; }
```

Screenshots come out correctly rotated, so only the input needs this. Half an
hour went into guessing rotations before reading `parseTouchSpec` in the
simulator's `HalGPIO.cpp`, which answers it in four lines.

### Seed the card to render a state honestly

A screenshot of an empty save file is not a screenshot of the design. The
Connections menu draws sixteen days of your record; on a fresh agent card all
sixteen cells are empty and the layout looks like it has a hole in it. Write a
plausible save to `fs_agent/.crosspoint/` before rendering, so what you judge is
what the user will actually see. This is not the same as faking a result: the
data is real input to real code, and the code is the thing under test.

### Look for it in the SDK before you build it

Three times in this fork, the thing that looked like it had to be written from
scratch was already there:

- `GfxRendererTarget::FONT_SLOTS = 3` reads like a three-font ceiling. It is a
  working set. `GfxRenderer` holds an uncapped `std::map<int, EpdFontFamily>`
  and rebinding a slot mid-render is one assignment.
- The first two FreeInkUI bugs here were both tokens `freeink::ui::Screen`
  would have supplied, skipped by calling components directly.
- Solitaire's suit pips were hand-assembled from circles and triangles, then
  hand-rasterised from implicit curves, across three rounds that still looked
  carved. `freeink-sdk/libs/assets/Icons` vendors Lucide **and** ships
  `tools/gen_icons.py`, whose entire job is turning SVGs into C structs at any
  size. Its output type carries `opticalCenterY`, measured from the real
  artwork -- which is exactly the "the pip is off by 7px and inconsistently
  between suits" defect that would otherwise be fixed by hand-tuning an offset
  per suit forever.

The tell is the same each time: you are about to write a generator, a format,
or a metadata field. Grep the SDK first. The payoff is not only the code you
skip -- once the suits were on the SDK's pipeline, swapping artwork three times
(Lucide, traditional, Noto) cost a manifest edit and a script run each.

### A reviewer needs a sequence, not three pretty frames

An unbiased critic was given three screenshots of Solitaire and found real
defects: clipped type, a 1px stroke against 4px pips, no spacing scale. It
found **none** of the four bugs Mario hit within minutes of playing, because
every one of them lives in a state no still frame contained: a _selected_ card
in a _fanned_ waste, a column deep enough to overflow, an empty column with a
card in hand. It got to the edge of one of them -- "unverifiable but likely,
worth testing with a deliberately deep column" -- and could go no further.

A screenshot captures geometry. Bugs like those are in the relationship between
two states. So when handing work to a reviewer (or reviewing your own), render
the awkward states on purpose: something selected, a list at its longest, a
container empty, a control disabled. Use the temporary-one-line-change trick
above to reach the ones a tap script cannot.

### Offer designs by rendering them, not by describing them

Prose about a layout is worth almost nothing, and a list of options in chat is
worth less. When a screen's shape is genuinely open, build two or three
_complete_ versions behind a temporary `#define`, render each through the device
path, compose them side by side, and hand over the image. Mario picks, you keep
one and delete the rest with the switch.

```cpp
#ifndef MENU_VARIANT
#define MENU_VARIANT 1
#endif
```

```bash
PLATFORMIO_BUILD_FLAGS="-DMENU_VARIANT=2" ./scripts/sim-shot.sh '<input>' '<shot>'
```

Two things this catches that discussion never does. First, the options that
sound best on paper are routinely the weakest on screen. Second, the version
that wins usually wins on one specific element, and once you can see all three
at once it is obvious which element that is: the chosen menu kept its layout and
threw away its centrepiece.

Delete the switch in the same commit. A variant macro that survives is a second
codepath nobody renders.

### Reaching states a script cannot play into

Checkmate, page 2 of a list, an error path. Temporarily change one line to start
from that state, verify, then revert. It is how the game-over screen, the
capture strips and list pagination all got checked. Always revert in the same
session, and say in the commit that you did.

---

## 2. The interaction model

### Chrome comes from FreeInkUI. Only the app's own surface is hand-drawn.

Anything shaped like standard chrome — headers, rows, buttons, dialogs, lists,
keyboards — is a FreeInkUI component, styled through
[`ToyboxTheme.h`](../src/apps_local/ui/ToyboxTheme.h). Do not hand-roll it. The
SDK is already linked into every env, and `fui::GfxRendererTarget` adapts it to
the renderer this firmware already has, so the cost of using it is one target
and one `Frame`.

The reason is not saved lines (the first screen ported came out slightly
longer). It is that the next two rules stop being rules you have to remember:

**Write a screen as a free function over a model, not as code inside an
Activity.** That is the SDK's own shape, and it buys two things that are not
optional:

```cpp
// ChessScreens.cpp -- freestanding: no renderer, no Activity, no storage.
void buildSettings(toybox::Screen& screen, const SettingsModel& model);

// ChessActivity.cpp -- fills the model, supplies the target, draws the board.
toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
toybox::Screen screen(frame, toybox::themeTokens());
chessui::buildSettings(screen, settingsModel());
toybox::reportOverflow(interactions, "Chess settings");
```

**Always go through `freeink::ui::Screen`, never the components directly.**
`Screen` substitutes theme tokens into every component it builds: title style,
header side padding, row height, row gap, side padding, selection style. Calling
`fui::header()` yourself opts out of all of it. Both bugs in this fork's first
port were tokens `Screen` would have supplied -- a header defaulting to 6px of
padding, and a title drawn black on a black band -- and they were "found" by
diffing screenshots when the layer that prevents them was sitting right there.
`Screen` needs only a `Frame` and tokens; `FreeInkApp` is a separate thing and we
do not use it, because CrossPoint's Activity already owns the loop and the
refresh.

Two exceptions worth knowing: `Screen::settingRow()` and `toggleRow()` do layout
only and skip the theming the other builders apply, so prefer `Screen::list()`
for label-and-value rows -- it themes, and it virtualizes. And `listInset` is
applied on top of the content rect, so leave it at 0 when the content margin
already carries the page margin, or the rows indent twice.

**Check `interactions.overflowed()` every paint.** The buffer is fixed-capacity
and silently drops controls past the limit: a control that draws normally and
cannot be tapped, with no log line. `toybox::reportOverflow()` is one call.

What stays hand-drawn is the app's own surface: the chess board, its pieces, the
capture strips. The SDK is explicit that app-specific rendering keeps its own
slot rect, and that boundary is the right one. Do not try to express a game
board as components.

Scope: **`src/apps_local/` only.** Porting upstream's reader activities would
trade this fork's near-zero merge conflicts for a rewrite upstream is already
doing themselves: 37 of their 85 source files touch FreeInkUI today. Ride that
migration, do not race it.

Worth knowing about the altitude difference. Upstream uses FreeInkUI as an
_interaction runtime_ -- `FreeInkApp<N,M>` owns the hit-test buffer and
dispatches actions to handlers -- while still drawing chrome with the older
`GUI.drawHeader`. We use the layer underneath it, `freeink::ui::Screen` and
`Frame`, via Toybox. That is deliberate: it is what keeps the screen builders
freestanding and therefore host-testable. Do not "upgrade" a screen to
`FreeInkApp` without understanding that it trades the tests away.

### One activation path

Touch and buttons must converge on a single function. In Chess both the tap
handler and Confirm call `handleSquareActivated()`. Two paths drift, and on a
device with both inputs the drift is invisible until someone uses the one you
did not test.

FreeInkUI gives this for free: touch, focus and Confirm all resolve to the same
semantic `ActionId`, so a row cannot behave differently depending on which input
reached it. Prefer that over another pair of hand-written branches.

### Hit-testing must share geometry with drawing

This is the rule that has caught the most bugs, in three separate places (the
Apps menu, the status capsule, the settings rows). **Never compute a tappable
region a second time.** Derive it from the same function that positioned the
pixels:

A component registering its own hit rect as it draws is this rule made
structural, which is the single best argument for the SDK. Where you still draw
by hand, derive the region from the same value that placed the pixels:

```cpp
const Rect gear = gearRect();
toybox::gear(renderer, gear, false);          // draw from it
frame.hit(grown(gear), ActionOpenSettings);   // and register from it
```

The rule earned its place: `pill()` once painted a capsule full width while
`pillRect()` hit-tested a narrower centred one, so the outer thirds of PLAY
AGAIN were dead. Two functions that are only wrong together.

Upstream's `MappedInputManager` bridge helpers violate this by design: they ask
the caller to re-supply `listTop`, `listHeight`, `rowHeight`. That is why a theme
change silently moves the hit targets. Do not copy that pattern.

### Anything drawn outside its own cell needs its own pass

The selection frame is drawn outside its square, so it overlaps the neighbours.
Drawn inside the per-square loop it was overdrawn by whichever adjacent square
rendered later, leaving a broken rectangle. Draw the board, then draw the
overlapping decoration.

### Guard on the condition, not on a proxy for it

Chess handed off to the engine on `if (!gameOver)`, which was a correct proxy
right up until two-player mode existed, at which point the engine happily
answered a human opponent. The guard had to be `engineToMove()`, the thing
actually being asked. Proxies are how a feature breaks a neighbour that was
written before it.

### Slow work goes off the render path, one pass later

The engine search is deferred by one loop pass after the human's move, so the
repaint showing the move and "THINKING" lands _before_ the search starts. Do the
same for anything slow. And bound it: the search takes a **node budget**, not a
time limit, because node counts are deterministic and a test can assert on them.

Upstream's font downloader is the counter-example: it does blocking HTTP on the
main loop and pins it for 40 seconds with no repaint and no input, which is
indistinguishable from a crash. Its log line is
`New max loop duration: 39864 ms`. If the UI ever looks frozen, read the log
before assuming a hang.

### e-ink timing, and what it costs

A partial refresh is ~0.3s; a full one 1-2s. **Every state change you show costs
a refresh.** Chess spends exactly two per move: one to acknowledge yours, one to
show the reply. That is the floor if you want your own move drawn immediately.

Battery is not the constraint people expect. A 60-move game is ~120 partial
refreshes plus about a minute of extra CPU against a 1100mAh cell: a fraction of
a percent. Ten minutes of reading costs more. Spend refreshes on meaning, not on
frames.

### Controls to test against

Arrows are the buttons, Return confirms, **Esc is Back and how you leave an
app**, H is the capacitive Home key, mouse is touch.

**Games in this fork are touch-only, and that is deliberate.** Upstream's rule
that every screen must also work with buttons exists because the X3 and X4 have
no touch panel; this fork targets the X4 Pro, which does. Building button
navigation anyway means a cursor, a focus model, and their bugs -- Connections
carried a cursor index that survived a month change and drew a black square
meaning nothing, which is the entire value that support delivered. Back leaves a
screen; everything else is a tap.

Chess still has a board cursor from before this was settled. Left alone rather
than ripped out mid-session, but it is the same call whenever it next comes up.

---

## 3. What the renderer will do to you

Each of these cost a debugging cycle.

**`getTextHeight()` returns the ASCENDER, not the ink height.** And `drawText()`
takes `y` as the top of the ascender box. Centring on it centres the _box_, so
capital ink sits at the bottom of it and the text visibly hangs low. Use
`toybox::drawCapsCentered()`, which measures real ink from the `H` glyph.

**The BW glyph path floods any antialiasing to solid black.**
`GfxRenderer.cpp:451` paints a pixel for _any_ coverage above zero. Stock fonts
are built `--2bit`, i.e. antialiased, so every vector font through that path
comes out bloated. Toybox ships a pixel font converted in **1-bit** mode
(`ToyboxFonts.cpp`); do not reach for the reader's body faces.

**There is no grey text.** `GfxRendererTarget::text()` decides ink with
`style.color != Color::White`, so every non-white colour draws solid black --
`DarkGray` type is black type. Dimming a label by colouring it cannot work, and
it fails silently, looking exactly like the code never ran. Dim with a
**dithered fill** behind the text instead, which is what `SUBMIT` and the spent
stepper arrows do.

**`drawIcon()` bakes in a portrait rotation** meant for the reader's themed
lists. It will turn a board on its side. Use `toybox::blit1bpp()`.

**`GfxRendererTarget` centres text on the font's line box**, not on cap height,
so all-caps labels drawn by a FreeInkUI component sit one pixel lower than
`drawCapsCentered()` puts them. Measured, not guessed: one logical pixel for the
Toybox UI cut, invisible at 220ppi, and not worth wrapping a `final` class to
recover. The error is `(lineHeight - capHeight)/2 - ascender + capTop`, so it
grows with the gap between line height and cap height. Re-measure before binding
a much larger font to a slot.

**Two component props do not inherit the theme when you call the component
directly** rather than through `Screen`: `header()` falls back to 6px of side
padding, and it uses `titleText` verbatim instead of resolving it against the
band's foreground, so a black-on-black title simply disappears. Both are set in
`ToyboxTheme.h`; the lesson is to diff a ported screen against a screenshot of
the old one rather than trusting that tokens reached it.

**A style set to `FONT_SLOT_SMALL` reads as no style at all.** The slot is
`0`, and `textStyleUnset()` calls a style unset when its font is 0 and every
other field is default -- so `Screen::list()` helpfully puts the theme's style
back and your small label returns at full size. It looks exactly like the
assignment never happened. Name one more field (the alignment the component is
going to apply anyway) and the style is treated as owned.

**`HeaderProps::rightLabel` is drawn with `subtitleText`, not `trailingText`.**
`trailingText` belongs to the trailing _button_. On Toybox's solid black band a
subtitle left at the theme default is black on black, so the label is invisible
and indistinguishable from never having been set: the Hacker News page
indicator was missing through two renders that way. Same defect as the
black-on-black header title, one prop over.

**A list row's title band is one line tall the moment it has a subtitle.** With
`labelText.maxLines = 2` and a subtitle, the second line of the label is drawn
straight through the subtitle underneath it. The component supports a wrapping
label _or_ a subtitle, never both; put the secondary value in the `value` slot
instead, which sits in the band beside the label.

**A glyph the font does not have draws as nothing.** No box, no fallback.
`EpdFont::getGlyph` answers nullptr and the pen does not advance, so the
character is not merely wrong, it is absent. A real Hacker News comment
rendered as "(Ive turned off duplicate detection...)" and only looking at the
panel caught it.

The renderer does log it -- `No glyph for codepoint N`, from `renderCharImpl`
-- and `scripts_local/sim-shot.sh` fails a run that produces one. That gate is
worth knowing about and worth not trusting alone: it only sees the screens a
scripted run visits, with the data that run happens to have, so a headline
pulled live from someone else's server is exactly the case it never reaches.

**Fold external text with `utf8FoldTypography` (`lib/Utf8/Utf8.h`) at the point
it enters your app** -- the JSON field, the parsed index row, the file you just
read off the card -- and not at the point you draw it, so nothing downstream has
to remember. It turns curly quotes, the dash family, the ellipsis, the space
family and the f-ligatures into the ASCII every cut carries, and it is a
provable no-op on text that is already ASCII.

Which cut you are drawing in decides what is left, and the cuts differ more than
they look:

| cut | slot it usually fills | carries |
| --- | --- | --- |
| `toybox_14/20/30/44/64` | button, UI, display | U+0020..U+007E and nothing else |
| `reading_serif_*` | body, on the reading screens | ASCII plus Latin-1, and nothing above U+00FF |
| `toybox_10`, `instrument_*` | small / tile | most of Latin-1, most of Latin Extended-A, the curly quotes, U+2013 and U+2014, U+2026, U+20AC |
| `ubuntu_10/12` | the system UI, the OPDS browser, the Wi-Fi picker | the common punctuation, but no U+FFFD, so what it lacks is a hole like anywhere else |
| `notosans_*`, `notoserif_*` | the EPUB reader's page | the widest set, plus a U+FFFD box for the rest |

Read that third row carefully: "most of" is doing work. `toybox_10` is missing
twelve Latin-1 codepoints and 29 of the 128 Latin Extended-A ones, and its dash
family is U+2013 and U+2014 and nothing else. The exact answer is not in this
table, on purpose -- `host-tests/typefold/` derives all of it from the font data
on every run, and a number copied out of a font into prose is a number that
rots.

So an accented letter is safe in body copy and lost in a Jersey title band.
Three gaps stay open on purpose and are asserted in that suite: Latin
Extended-A (a Polish or Turkish name still loses a letter in the reading cut);
any non-ASCII in a Jersey cut; and the rarer marks in the system UI, where
`ubuntu_12` cannot draw 32 of the 67 folded codepoints and has no U+FFFD to
show a box with. The first two are not folded because folding a letter means
writing a DIFFERENT letter. The third is not folded because the OPDS title also
becomes a FILENAME (`src/util/OpdsFilename.cpp`), and rewriting a character
there renames a book on the card.

**A light shape must be knocked out before it is stroked.** Drawing only an
outline leaves the shape hollow and the surface beneath shows through. Fill with
the page colour, then stroke.

**Flash is the app slot, not the chip.** `pio` reports the app partition.
`partitions.csv` splits the X4 Pro's 16MB into two 7.94MB app slots (`app0`
running, `app1` the OTA landing pad) plus small nvs/otadata/coredump areas. Two
slots are not optional and are not about crash rollback: a running app cannot
erase the flash it is executing from, so an OTA needs somewhere else to land.

The slots used to be 6.25MB each, with 3.38MB held for a SPIFFS partition that
nothing in this firmware ever mounted (no `SPIFFS.begin`, no
`esp_vfs_spiffs_register`, and no release job built an image to put in it;
books, fonts, saves and packs are all files on the SD card). By 1.5.3 the x4pro
image was 6,427,744 bytes, **98.1% of a slot, 123KB from failing to link**. The
dead partition was split between the slots, which put the same image at 77.2%.

Where those bytes actually go, measured on the 1.5.3 x4pro image:

| | |
|---|---|
| code + libraries | 2,605KB |
| embedded fonts | 2,426KB (38% of the image) |
| hyphenation tries | 349KB (German alone is 201KB) |
| i18n strings | 307KB |
| icons | 131KB |
| web UI html/js | 86KB |

So the next two wins, when the new room runs out, are fonts and hyphenation,
not code. `SdCardFontSystem`, `FontInstaller` and the web UI's font upload
already exist, so moving all but a fallback family to the SD card is plumbing
that is mostly built.

**The table only changes on a full serial flash.** An OTA writes the app
partition and otadata, never offset 0x8000. A device already in the field keeps
the table it was installed with and stays capped at whatever that table said,
so repartitioning is free before a device ships and costs a USB reflash after.
`host-tests/release` asserts the table's arithmetic, because upstream still
ships the spiffs row and this file conflicts on every upstream merge.

**There are exactly two greys**: `LightGray` (25%) and `DarkGray` (50%). The
source still says `TODO: maybe find a better pattern?`. Extending that set is
the cheapest big win available to every app's appearance.

**Arduino.h turns some ordinary method names into something else.** It defines
`word(...)` as a function-like macro expanding to `makeWord(...)`, so a perfectly
reasonable `Round::word()` becomes `Round::makeWord()` in every translation unit
that reaches Arduino -- and _only_ there. Insider's rules compiled, its screens
compiled, every host test passed and the simulator ran the whole game; the
device build failed at link with `undefined reference to
insider::Round::makeWord()`, naming a method that appears in no source file.

The shape is what to remember: the freestanding layers this fork is built around
never see `Arduino.h`, so a collision like this is invisible to everything green
and shows up once, at the end, as a linker error about a symbol you did not
write. `bit(b)` is the other one defined there today. Only function-like macros
are involved, so a _member_ called `word` is fine and a _method_ called `word()`
is not. Run `pio run -e x4pro` before believing an app is done, not just the
simulator.

**Never put a file with `main()` under `src/`.** PlatformIO's `[base]` filter is
`+<*>`, so a host test's `main()` links into the firmware and replaces the real
one. Host tests live in `host-tests/`.

**Fonts and generated headers**: `pio` regenerates `lib/I18n/I18n*` and
`src/network/html/**.generated.h` on every build. Exclude them from any file
watcher or you get an infinite rebuild loop.

---

### What CrossPoint's renderer does not have

Two things an app written against the fork's earlier base reached for that are
absent here, both found by the compiler during the move:

- **No one-shot refresh override.** There is no `requestNextFullRefresh()`; the
  only control is the mode argument on `displayBuffer(HalDisplay::RefreshMode)`.
  If an app wants the full blink for one frame, keep a flag and consume it at
  the paint. Solitaire's win screen does exactly that. Do not add the override
  upstream to save four lines.
- **Thirteen icons, and none of them are yours.** `UIIcon` is `None, Folder,
Text, Image, Book, File, Recent, Settings, Transfer, Library, Wifi, Hotspot,
Bookmark`. Every game currently draws `Book`, which is honestly wrong and is
  the first thing to fix. Adding enum variants means editing `BaseTheme.h` and
  `LyraTheme.cpp` per app, so whatever replaces this has to arrive as an asset
  the shelf resolves, not as thirteen-plus-N enum entries.

### The simulator can lag the branch

`scripts_local/sim_catchup.py` patches the fetched simulator library on every
build, because it tracks upstream's `develop` and this fork sits on
`feat-touch-ui`, which is ahead. Header stubs in `sim-stubs/` cover most of it;
two things they cannot, because a library's own include path beats the project's
`-I` and its headers always shadow ours.

The patch is idempotent and **reports when an anchor stops matching**. If a build
prints `[sim-catchup] ... no longer applies`, upstream has fixed it: delete that
patch rather than carrying it.

## 4. Verification, and why it is not optional

### Screens are testable too, and they are what changes

The rules had 2940 assertions and the screens had none, which was backwards: the
rules are settled, and the screens change every time someone asks for something.
Because the builders take a model and a `Screen` and touch no hardware, a host
test builds one against a fake `DrawTarget` and asks what it drew and what it
made tappable:

```bash
./host-tests/ui/run.sh
```

`run.sh` deliberately puts **only** the SDK on the include path -- no `src/`, no
`lib/`, no Arduino. If a screen builder ever reaches for `GfxRenderer` or the SD
card, that build breaks loudly instead of the screens quietly becoming
untestable again.

The assertions worth writing are the ones about the interaction table, because
they test drawing and hit-testing together: tap the far edge of a button and
assert the action; tap a running game's status capsule and assert `NO_ACTION`.
The two worst bugs this project shipped -- a dead-on-its-edges PLAY AGAIN and an
invisible header title -- are both pinned there now.

### Pure logic goes in a freestanding module

`ChessCore` and `ChessEngine` have no Arduino, no renderer, no heap. That is the
whole reason 2940 assertions can run on a laptop in a second:

```bash
./host-tests/chess/run.sh
```

Structure every app this way. Rules, state machines, generators, scoring: all of
it can be freestanding, and the part that cannot is only the drawing.

### Making a game multiplayer

Inherit `linkplay::LinkActivity` instead of `Activity`, declare a state that
fits one packet, and fill in what only your game can answer. Everything else --
discovery, pairing, who moves first, retransmits, disconnects, sleep
suppression, the searching screen, the "they left" screen, the rematch
conversation and the tick itself -- is already written and is the same in every
game:

```cpp
class MyActivity final : public linkplay::LinkActivity {
 protected:
  linkplay::PlayBase& linkState() override { return link; }
  const linkplay::PlayBase& linkState() const override { return link; }
  const char* linkGameTitle() const override { return "MY GAME"; }
  const char* linkHeadline() const override;      // "CHECKMATE", "YOU WIN"
  void onMatchStart(bool goesFirst) override;     // a fresh board, and your side
  bool takeOpponentState() override;              // adopt theirs into yours
  void onRematch() override;                      // both said yes
  void onLinkEnded() override;                    // back to solo
  bool matchGameOver() const override;
  void gameLoop() override;                       // what loop() used to be
  void gameRender() override;                     // what render() used to be

  linkplay::Play<Board> link;                     // trivially copyable, <= 192 bytes
};
```

`enterLink(GameId::MyGame)` when the player taps PLAY NEARBY, and that is the
whole integration.

The row is called **PLAY NEARBY** in every game, and it carries the mark below.
Do not invent another wording: "click where it says multiplayer" only works if
something says it, and NEARBY is what it is -- somebody in the room, not a
server and not a friends list. `loop()` and `render()` are `final` on purpose: the tick has
to run before every one of your early returns, and chess's first early return is
a settings overlay, so a tick written anywhere else died whenever somebody
opened one for ten seconds.

Battleship is the proof this is a layer rather than a pattern to copy. It writes
those methods and **zero screens**; the two hundred lines chess used to hold now
live in `LinkActivity` and neither game can drift from the other.

#### The mark

One symbol means "this connects to another device", and it appears in exactly
two kinds of place:

- the **PLAY NEARBY** row of a game's start menu, drawn with
  `toybox::iconAtRowRight(screen, band, row, linkui::nearbyMark(), selected)`
- the **shared link screen**, which `buildLink()` already draws for you

That is the whole rule, and the reason for it is recognition rather than
decoration. The DS never had to explain local play: you learned one mark and
then you knew, on every box and every menu, which games would talk to the
machine next to you. A mark only does that if it is never used for anything
else, so **do not reach for `nearbyMark()` for any other purpose**, and do not
give multiplayer a second symbol somewhere.

It is `linkui::nearbyMark()` rather than a Lucide name in your code, so there is
one place to change it and no game can drift to a different glyph. The glyph is
`radio`: symmetric arcs from a point, a thing here signalling both ways. `wifi`
was rejected for reading as the internet, `radio-tower` as one-to-many
broadcast, and `share-2`/`waypoints`/`network` as topology diagrams rather than
marks. See `tools_local/toybox/icons.txt`.

**Check what is behind it.** The mark is a 1-bpp mask painted in one colour, so
it is invisible on a background of that colour and nothing warns you. Drawing it
took three attempts: black on the black headline slab, then white after moving
it off the slab onto white paper, then finally right. Whenever a drawn element
moves, re-check its background.

Rules that are worth knowing:

- **Send the whole shared state, never the move.** Board games are tiny (chess
  ~90 bytes as FEN, battleship 50, connect four 11), and whole states make
  desync structurally impossible: a lost packet is a stale frame the next one
  corrects.
- **A secret inside a shared state is a drawing discipline, not a guarantee.**
  Battleship's state carries both fleets, because the alternative is not sending
  the whole state; what keeps yours secret is that the drawing code is never
  told where theirs is until the game ends. That is honest and it is enough for
  two devices in the same room running the same build. What it does NOT survive
  is keeping your own half of the state before you have sent it: adopting theirs
  is a whole-struct copy and would wipe it. Keep anything you have not handed
  over yet outside the shared state.
- **A phase that both sides play at once has to become two moves.** Battleship
  places fleets simultaneously and the wire only alternates, so whoever goes
  first sends their fleet as their first move and the other answers with theirs.
  No new primitive, and the wait is invisible because both players are arranging
  during it. Ask what the DS would do before reaching for a new mechanism.
- **Ask the session for the turn; never mirror it.** `linkPhase()` is live
  because taking delivery and sending both move the turn _immediately_, so a
  match can pass through two phases inside one loop. A copy taken at the top of
  the pass had battleship firing on the opponent's turn.
- **`YourTurn` already means their move is on your board.** The phase machine
  will not report it otherwise, so you cannot compute a move from a stale
  position. Forgetting `takeOpponent()` parks you visibly on `TheirTurn` and
  says so in the log, rather than desyncing quietly.
- **Bump the `GameId` value whenever the state layout changes.** Two builds only
  find each other when it matches, so a half-flashed pair fails to pair instead
  of misreading each other. Same discipline as the cache format version.
- **Do not surface any of it to the player.** No lobby, no host/join, no pairing
  code, no retry button, no "searching failed": they tap MULTIPLAYER and the next
  thing they see is a board. Searching never times out, because an error the
  player has to interpret is a bug and they can already see who is in the room.
- **The name is the only identity, and it is free to derive things from.** The
  device name (`player::name()`) already crosses the radio in Hello and Join, so
  anything computable from it costs nothing to add: the opponent's avatar is a
  pure function of `LinkModel::theirName` and no byte was added to the protocol
  for it. Before widening a packet, check whether the answer is already in the
  name.
- **Show the opponent's face during the match, not only while pairing.**
  `linkui::withOpponentFace()` takes the left of a status band, draws them, and
  returns what is left for the capsule. Both link games call it, so neither can
  place it differently. The first version showed faces only on the shared
  pairing screen, which meant the person you were playing disappeared the moment
  you started playing them.
- **A label and the thing it draws are not always the same string.** The link
  seats are labelled "YOU" and drawn from your _name_, and the first version
  derived the face from the label -- so every player saw a blank head in their
  own seat, because "YOU" parses to no words. Nothing failed and nothing logged.
  The test had passed a real name into the label, which is the shape of the
  mistake: a fixture more convenient than the real caller stops testing the real
  caller.
- **Put a name in a sentence with `player::shortName()`, never whole.** A name is
  three words and up to twenty characters. "SHAGGY SLEEPY GOATEE'S MOVE" ran past
  chess's status capsule and the component dropped _"MOVE"_ -- the word carrying
  the meaning -- with no ellipsis and no log line. Battleship's "%s SANK YOUR %s"
  had the same shape. Their first word always fits and reads better anyway.

Tests live in `host-tests/link/`, and `test_play.cpp` drives the layer through
the loop ordering a game author would plausibly get _wrong_, on a link that
drops, duplicates and reorders. `test_chesslink.cpp` and
`test_battleshiplink.cpp` play whole games of the real thing through it, on a
link that drops, duplicates and reorders. Add your game's state type to that
soak rather than trusting the device -- battleship's version pins the bug that
no divergence check can see, where your own fleet is wiped by their first packet
and both devices then agree perfectly about a game your ships were never in.

Then watch two of them actually find each other. With no arguments you get two
windows to play one against the other with the mouse:

```bash
./scripts/sim-link.sh
```

With a script it runs headless, which is the regression-test form:

```bash
./scripts/sim-link.sh '1800:TAP:120,635;3600:TAP:120,411;5400:TAP:120,144;20000:QUIT' '12000:./qa-artifacts/pair.bmp'
```

Two simulators, their own SD cards (`fs_link_a`, `fs_link_b`), launched together
because discovery is the thing under test. Screenshots get `-a`/`-b` suffixes;
`SIM_LINK_INPUT_B` drives the second device differently, which is how you check
what one shows when the other walks away.

**Save at every position, not at the door.** `onExit()` is more reliable than it
looks -- the Home gesture and deep sleep both replace the activity, and
`replaceActivity()` runs the outgoing `onExit()` first -- but writing only there
still loses everything to a panic, a watchdog reset or a flat battery, and it
makes what survives depend on which fields that one function happens to
serialise. WAVELENGTH shipped an `onExit()` that dutifully wrote the all-time
record and nothing about the round in progress, so a cold tester pressed Home
one key from Back and lost the round, the hidden number and the session score.
Fixing `onExit()` would not have helped; the round had never been written down.

The shape that works, and it is worth copying:

- **A freestanding `pack()` / `unpack()` module beside the rules**, taking no
  renderer and no storage, so the round-trip is host-testable. See
  `src/apps_local/wavelength/WavelengthSave.{h,cpp}` and its tests in
  `host-tests/wavelength/`. Version the file and make `unpack` accept the older
  version rather than rejecting it, or an upgrade throws away a year of record.
- **Write on every state change**, not on the way out: the screen change and the
  cursor move both. It is around a hundred bytes beside a panel repaint that
  costs a hundred times more.
- **Write to a temp file and rename**, and do it BEFORE you raise the write
  frequency. `Storage.openFileForWrite()` carries `O_TRUNC`, so writing in
  place empties the file at open: power lost in that window leaves nothing at
  all, and going from one write a game to fifteen a round multiplies that
  window by fifteen. Write to `<name>.tmp`, `flush()`, release the handle,
  check the byte count, then `remove` + `rename`. `ConnectionsActivity::
  saveResult()` is the reference. Without this the frequent-write rule above
  trades a small loss mode for a total one, which is worse than what it fixed.
- **Refuse to resume what the file cannot support.** A screen number is only
  meaningful with the state behind it, so validate before restoring and fall
  back to your front door with the session intact rather than to a half-drawn
  board.
- **Ask what a resumed screen SHOWS.** If your app has a secret, the resume must
  not land on the screen that displays it.
- **Clear the in-progress state when the game genuinely ends.** State that
  outlives the thing it belonged to is its own bug, and it is the one this
  mechanism introduces.

Chess reached the first half of this on its own and says so at the call site: it
saves on the completed move rather than the completed game. TOY BATTLE and
JAIPUR still write only in `onExit()` and on the way out to their own menus.

**A match is not your saved game.** This is the one that bit hardest: three
separate defects, all the same shape. Two rules, and each wants a different
mechanism:

- _Never write your save during a match._ The position on screen is the shared
  game, and your save file is what you restore from when the match ends. No
  caller ever wants the other behaviour, so put the check inside your save
  function, not at each call site. Chess had this wrong in `Back` and again in
  `onExit` -- and `onExit` is the one that matters, because it runs on sleep,
  which the player triggers by doing nothing.
- _Never restart a shared board alone._ This one is NOT a guard in your reset
  function, because an agreed rematch legitimately resets during a match. The
  distinction is authorised versus unilateral, and only the caller's intent
  knows which. So give the intent one home -- chess funnels every "new game"
  door through `requestNewGame()`, which asks in a match and resets when solo.
  A new door cannot choose wrong because there is no choice left at the call
  site.

The general lesson: when the same defect keeps arriving through new doors, a
true invariant belongs in the callee, and an intent that means two things
belongs in one funnel. Guards at call sites are what let it recur.

One trap the layer cannot save you from: **gate your board input on the turn.**
It refuses an out-of-turn `play()`, but nothing stops your app from mutating its
own state first and only then discovering it cannot send it. Chess had exactly
that bug in `handleSquareActivated`, and two simulators are what found it.

Battleship then found the subtler version of the same thing, which is worth
stating because it looks like a correct guard: it asked whether the rules and
the link **agreed** about the turn. They agree perfectly when it is the
opponent's turn, so the tap went through, the board mutated, and the link
refused to send it. The check has to be that both say yes, not that they say the
same thing. `[BSHIP] the link refused a shot the board allowed` is what that
reads like in the log, one line before the desync.

### Verify the behaviour, not the label

Face to face shipped broken and the mode never worked once. The switch that
implements it was in a scripted edit that hit an assertion partway through, so
the file was never written -- and the old two-way line it should have replaced
is perfectly valid code, so the build stayed green. Then I "verified" it by
screenshotting the settings row, which said FACE TO FACE, and shipped.

The screenshot proved the label. The label was never the feature. What needed
checking was one move later: does the board stay put? Two more defects in the
same header came from the same habit -- a chevron drawn black on a black band
(`fillRect`'s last argument is a `bool`, so a `Color` silently became `true`),
and then a chevron pointing the wrong way. Both invisible to the compiler, both
obvious in a screenshot nobody took.

So: after a scripted edit, grep for the new code rather than trusting the exit
status, and reach the state where the feature would actually differ before
believing it works. "It builds and the setting says the right thing" is not
evidence.

### Perft, or the equivalent

Move generation is verified against published node counts, not spot-checks,
because the bugs live in _combinations_ (an en passant capture that is illegal
only because it uncovers a rank check). Find the equivalent for your domain: an
exhaustive property that a whole class of bugs cannot survive.

### Mutation-check your tests

Perft ran suspiciously fast, so I fed it deliberately wrong expectations and
confirmed it failed. Do this whenever a suite passes more easily than expected.
A green suite that cannot go red is worse than no suite.

Two of the avatar's mutations are worth keeping as examples, because both
survived a test written specifically to catch them.

**One survivor was a weak assertion.** `compose()` was made to write past the
caller's buffer, and the test checked the byte immediately after the limit --
which the mutation never touched, because it ran a cursor _past_ the end and
wrote further along. Checking every byte to the end of the array killed it.

**One survivor was a weak data structure, and no assertion could have fixed
it.** The artwork table was two hand-kept columns, `{"SPIKY", &icon_hair_spiky}`,
and the test compared the word column against the vocabulary. Swapping two icon
pointers left the word column correct, so the test passed while every device
quietly grew different hair. The fix was to delete the second column: one macro
token now spells both the word and the bitmap names, so there is nothing left to
desynchronise. **When a mutation survives, ask whether the answer is a better
assertion or one less thing to assert** -- the second is worth looking for first.

### Look at the output, do not reason about it

Every visual bug in this app was found by rendering and looking, and every one
was missed by reading the code. The pieces got three iterations because I looked
three times; the type got one iteration and shipped broken. **The log tells you
what happened, the screenshot tells you whether it was right.**

Crop and zoom when unsure. The hollow-piece bug was invisible at full size.

---

## 5. Process failures, and their cures

These are mine, from this project. They will recur.

**Do not mock up in a medium the target cannot reach.** An SVG mockup with
browser-hinted type and arbitrary letter-spacing produced something beautiful
that the device could not render, and the build looked disappointing by
comparison. Mock up _in the simulator_. Six seconds a build.

**A symptom that survives every variant is not a variant problem.** White pieces
looked wrong in all four visual treatments. That was the diagnosis, and I went
shopping for different piece shapes instead of reading it. The shapes were never
the bug.

**Assert every scripted edit.** Two separate incidents: a `str.replace` silently
did not match after clang-format reflowed the target, and shipped a screen
reading both "TAP TO PLAY AGAIN" and "PLAY AGAIN". And a slice built from two
`index()` calls ran backwards, producing an empty match, and `replace("")`
inserted text between every character in the file. Anchor on exact text, assert
the match, and check the result.

**The formatter reflows whole files.** A one-line change to an upstream markdown
file became a 30-line diff, which quietly wrecks the low-conflict fork strategy.
Apply those edits through a script instead of the editing tools.

**Run `./bin/clang-format-fix` after generating code.** The piece generator emits
12 bytes per line and clang-format rewraps them, so a regeneration otherwise
shows hundreds of lines of diff over byte-identical arrays. That noise hides real
diffs.

**A silent probe is not a negative result.** `sim-shot.sh` printed only
`Entering activity` and `[ERR]` lines, so a `LOG_INF` added to check whether a
tap was routed produced nothing, three times, and each time the honest-looking
reading was "the code never ran". The code ran fine; the harness ate the
evidence. Before believing a probe said no, prove the probe can say yes. The
script now takes `SIM_LOG_GREP` for this.

**Verify the claim, not the code path.** "It builds" is not "it works", "the log
says it happened" is not "it looks right", and "the test passes" is not "the test
can fail".

---

## 6. Where things live

| What                          | Where                                               |
| ----------------------------- | --------------------------------------------------- |
| Apps and games, registry      | `src/apps_local/`, one row in `Shelf.cpp`           |
| Navigation rules              | `docs/shelf.md`                                     |
| Visual language               | `src/apps_local/ui/Toybox.h`, `ToyboxFonts.cpp`     |
| Toybox numbers                | `src/apps_local/ui/ToyboxMetrics.h`                 |
| Toybox as FreeInkUI tokens    | `src/apps_local/ui/ToyboxTokens.h` (freestanding)   |
| Screen types                  | `src/apps_local/ui/ToyboxScreen.h` (freestanding)   |
| Renderer-side theme binding   | `src/apps_local/ui/ToyboxTheme.h`                   |
| Screen builders               | `<app>/…Screens.cpp`, freestanding                  |
| The SDK's own UI docs         | `freeink-sdk/docs/freeink-ui.md`                    |
| Who this device is            | `src/apps_local/player/`, freestanding              |
| Avatar artwork + generator    | `assets_local/avatar/`, `tools_local/avatar/gen_avatar.sh` |
| Local multiplayer             | `src/apps_local/link/`, freestanding                |
| Multiplayer, the game's half  | inherit `link/LinkActivity.h` (not freestanding)    |
| Host tests: rules             | `host-tests/chess/run.sh`                           |
| Host tests: multiplayer       | `host-tests/link/run.sh`                            |
| Host tests: battleship        | `host-tests/battleship/run.sh`                      |
| Host tests: screens           | `host-tests/ui/run.sh`                              |
| Asset generators              | `tools_local/`                                      |
| Vendored assets + licences    | `assets_local/`                                     |
| Simulator scripts             | `scripts_local/`, symlinked from `../scripts/`      |
| Verify everything             | `./scripts/check.sh`                                |
| Pull CrossPoint forward       | `./scripts/sync.sh`                                 |
| Upstream-owned files we touch | listed in `LOCAL_SCOPE.md`, keep it short           |
