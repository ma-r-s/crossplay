# Notes

Lists you tick with one hand. Card #516.

The prior art behind the shape is in [notes-research.md](notes-research.md); this
is what was built and why it is built that way.

## One kind of line

A list is a Markdown file in `/notes/`, and its NAME IS ITS FILENAME.
`/notes/Shopping.md` is the list called Shopping. One source of truth: renaming
is a file rename rather than a content rewrite, and a person who drops `.md`
files on the card over the reader's own file transfer gets exactly the lists
they expect with no import step and no database.

**THE KIND BELONGS TO THE NOTE, NEVER TO THE LINE.** A note is a LIST, where
every line has a tick box, or a PAGE of words, where none does. Within one note
there is no second kind of line. This is the rule the first version of this app
got wrong, and everything else that was wrong with it followed:

> A line the parser did not recognise as `- [ ] ` was "prose": not tickable, not
> deletable on the device, drawn at `toybox_10` beside `toybox_20`, and worth
> nothing in the deck's tally. And the way to produce one was to type a word on
> your phone, which is what the phone is for. To get a real item you typed the
> marker yourself: nine keyboard taps before the first letter on iOS, thirty for
> a shopping list. The page's own hint taught a string that does not even work,
> because `- [ ]Milk` with no space after the bracket is rejected by `classify`.

`notes::kindOf` INFERS the kind from the file: any `- [ ]`/`- [x]` makes it a
list, otherwise it is a page. Nothing is stored beside the file, so a note
dropped on the card from a computer works with no import step and nothing to
keep in sync.

**Inference can be wrong, so there is a way out rather than a cleverer guess.**
A shopping list typed as plain lines opens as a page with nothing to tick, which
is what Mario found. The note's menu therefore carries MAKE IT A LIST /
MAKE IT A NOTE (`coerceToList` / `stripMarkers`), one tap either way, and the
deck's footer is two buttons, `+ LIST` and `+ NOTE`, so a new note's kind is
chosen rather than deduced from an empty file.

The marker is still what the FILE holds, so a desktop editor sees ordinary
Markdown checkboxes. It is simply never something a person types:

- The phone surface coerces on the way IN (`notes::coerceToList`), but only
  into a file that is already a list: a page of words is not turned into tick
  boxes behind its author's back. Lines that already carry a marker are left
  byte for byte alone, so a save from the phone cannot disturb what was ticked
  on the device.
- On a list the device draws a box on every row, and ticking a line that has no
  marker writes one -- so a file authored on a computer joins the rule instead
  of sitting outside it forever.
- `counts()` counts lines, not markers.

**A tick flips exactly one byte.** `Line::markAt` is the offset of the `' '` or
`'x'` between the brackets, so everything else in the file is preserved by
construction rather than by a re-serialiser that can drift from the parser. That
is why the task syntax is strict (`- [] milk` is prose): being permissive would
mean a tick had to insert a byte.

## The three screens, and no settings

**The deck is a deck of CARDS.** Each one is a square badge, the name beside it,
and under the name a second line: for a list, a progress bar; for a note, its
first words. A list's badge is filled black and carries the tally knocked out of
it; a note's is outlined with three rules drawn inside it, the last one short
the way a paragraph's last line is. **The two kinds are told apart by shape,
before anything is read** -- the first version distinguished them with a tally
that only one of them had, which is a difference you have to go looking for.

Alphabetical, because that is the order a person can predict; recency would move
the row you are aiming at. `+ LIST` and `+ NOTE` split the footer bar: Mario
chose a pinned bar over the action-as-last-row alternative because the bar
anchors the bottom, and two buttons over one that opens a screen asking which
kind, because that screen cost a tap and a full repaint to say one word.

**A list.** A strip under the band saying `3 LEFT OF 4` with the same bar the
deck card uses, then tick boxes down the left, the text beside them, done lines
struck through in place. The rows answer *which*; the strip answers *how much*,
which is the question a list exists to answer and the one the rows cannot.

**The strip stands in even air, and it counts its own space once.** It began at
`kBodyTop`, which meant it carried the body's 36px inset above it, a gutter
below it, and the first row's own centring under that: a 14px bar inside 90px of
page, which Mario read as the bar hogging the screen. It now starts one gap
below the chrome, its rule closes the block, and the rows begin immediately
under that rule -- because a row is taller than its tick box and already centres
it, so the row's padding IS the air below. Measured on the render: 32px from the
chrome down to the bar, 36px from the rule down to the first box.

ADD on the left of the footer, the fork-wide home for a primary action; CLEAR
DONE only when there is something to clear, and on the RIGHT, so the control
that removes lines never occupies the pixels ADD had a moment ago.

**One action bar, one height, on every screen.** Filled on the left is the thing
that makes something (ADD, KEEP IT, `+ LIST`); outlined on the right is the
thing that takes something away (CLEAR DONE, DELETE IT). DELETE NOTE sits alone
on that bar on the menu, a page below the rows, so it is never under a thumb
that came for something else.

**Rows and cards share the page when they already fit on one** (`fittedPitch`,
capped at a card rather than a slab). A three-note deck was three text lines at
the top of an empty page, which is not minimal, it is unfinished. They never
grow while anything is paged: a row that changes size because the note got
longer is a row that moves under the finger.

**ADD keeps the keyboard up.** Type, done, type, done, Back. One visit per item
cost two activity transitions and two full-screen repaints EACH -- six repaints
to write three lines.

**There is no per-line delete, and that is not a hole.** Tick the wrong line and
press CLEAR DONE: two taps, with controls that already exist and already say
what they do.

**The menu**, behind the gear on the band: type on your phone, make it a
list/note, rename. Three rows, with DELETE NOTE alone on the action bar at the
foot. CLEAR DONE is not among them -- it lives in the footer of the list where
it is needed, and a control in two places is two places to keep in step.

**There is no settings screen**, deliberately. Nothing here has two defensible
values, and the one thing that looks like a setting -- "type on your phone" --
is a per-note action that must never become a mode.

## The layout rules, each paid for by a render

- **Nothing is ever elided, with exactly one exception.** Not by us and not by
  the list component, which truncated three of six deck titles the first time it
  was handed them. The exception is a deck card's preview of a note's first
  words, which is a glimpse by definition; it is set at `toybox_10`, the only
  cut in the family carrying an ellipsis glyph, and it is cut on a word
  boundary with three real periods rather than stopping wherever the width ran
  out.
  `pickCut` returns the largest cut in which EVERY string fits in the lines
  available, and 0 when none does.
- **Peers share a cut.** The rows of a deck and the lines of a list are compared
  with each other, so the cut is chosen once from the widest member. Sized one
  by one, a long row comes out smaller than its neighbours and reads as a
  different kind of thing.
- **Wrapping beats shrinking, and the ladder used to have it backwards.** The
  order is body at one line, body at two, and the small cut only for a single
  word too wide to break. It ran TITLE -> BODY -> SMALL before, so ONE long item
  halved every row on the screen -- and bought nothing, because `typeRowHeight`
  floors at a finger: small-on-one-line and body-on-two-lines produce the
  identical 72px row and the same eight rows per page.
- **THE BAND IS CHROME AND A FILENAME MUST NEVER RESIZE IT.** It ran through
  `fittedTitle`, so a list called "Packing for Lisbon" dropped the app's own
  title bar a whole cut and a longer name dropped it two. It is fixed now, and a
  name that will not fit is refused at the keyboard rather than silently
  shrinking the chrome later.
- **Row height comes from the type, and from the count only between a floor and
  a CAP.** Dividing the band by the number of rows fills a short page, but taken
  literally it redraws the same note with a different rhythm every time a line
  is added, and a two-item list becomes two slabs. `fittedPitch` divides only
  down to a cap (108 for a list row, 132 for a deck card) and only while nothing
  is paged, which pins the size flat across the counts people actually have: on
  a 601px band a list is 108 from one item to five, and a deck card is 132 from
  one note to four. Past that it steps down twice and paging takes over. The
  rule it replaced left three notes as three text lines at the top of an empty
  page, which is not minimal, it is unfinished.
- **A PRECONDITION IS WORTH ONLY WHAT ITS FAILURE COSTS.** Saving a note asked
  `Storage.freeBytes()` first, to refuse politely on a full card. Measured on
  device 1: that walk is **5317ms**, and it landed between OK and the next
  keystroke on the first add of every session (every later one was 19ms, since
  the walk is cached by the driver). The write already fails on a full card and
  already reports it, so the probe bought nothing but a better sentence -- and
  it is now asked only AFTER a write has failed, where it buys that sentence
  for free. Ask what the check costs and what its absence would actually
  produce; here the answer was five seconds against one word.
- **The card is written once per change, never read back.** `doc_` is the bytes
  that were just written, so re-reading the file to refresh the rows is a
  second trip for something already in RAM.
- **A list that does not fit says so.** `"1 / 2"` in the strip above the footer,
  which is the only part of the page otherwise doing nothing.
- **What is measured must be what is drawn.** `notePageSize` built its own
  `NoteModel` with only the rows in it, so a page of words was measured with a
  list's tick-box width and every list was measured against a band the progress
  strip was not standing in. The capacity came out one row larger than the
  screen, the page label counted that row and `noteRows` stopped before it, so
  an item fell between two pages. One `noteModel()` now feeds both.
- **A done line is struck, not greyed.** Grey is a dither here and a dithered
  flat field ghosts; a rule is one crisp row of pixels.
- **A ticked item never moves.** Sinking completed items is a full-screen reflow
  on every tap, and it shifts the row the finger is next to.
- **The whole row is the tap target**, not the 40px box: a miss costs two
  refreshes, the wrong one and the undo.

## Writing, and what happens when the card says no

Every write lands beside itself and is renamed (`<name>.md.part`), because
opening the real path truncates it first and a power cut mid-write would leave a
note that parses as empty -- which reads exactly like a note somebody deleted. A
`.part` left by a torn write is swept on the next scan.

A tick is written IMMEDIATELY, not on the way out. A tick a person saw and the
card did not is the failure mode of every app that saves on exit, and this one is
used one-handed in a shop with the power button under a thumb. When the card
refuses, the model is put back beside the file and the refusal is shown verbatim:
"the card is nearly full" and "the card would not take the change" want different
things from the person reading them.

The free-space floor is 12MB and is NOT sized to this app's own write, which is a
few hundred bytes. It is sized by who pays when the card fills.

## The delete confirm

KEEP IT occupies exactly the pixels DELETE NOTE had on the menu, so a repeat of
the press that opened the confirm -- a double tap, an impatient second jab during
a repaint, a finger that never moved -- cancels. DELETE IT sits where no menu
control was. The sheets share `kSheetRow` and one footer bar rather than
dividing the page by however many rows they happen to have, because a row added to the menu without
changing it would put KEEP somewhere DELETE NOTE never was.

## Typing from a phone

The list's menu opens a screen with a QR and the address under it. Scanning it
opens one page, served by the reader itself over your own network, holding THAT
list; saving writes it back and the panel redraws.

**The page is one row per item -- a real checkbox and a real text field -- not a
textarea.** A textarea showed a person their own list as source, `- [x] Milk`
and all, and made the marker something they had to type. The empty row at the
foot grows a fresh one as soon as you type in it, so a list is written without
reaching for a button between items.

**It is built from `site/styles.css`, not from memory of it.** Warm paper
`#f5f2ea` and ink `#111110`; the display stack at weight 400 in sentence case,
never bold; ALL CAPS only for the mono eyebrow; square corners; 1/2/3px borders;
the black band with its 3px rule; disabled at `opacity: .42`; and the dark theme
the first version had none of, which matters for a page read in a shop at night.
The previous one was a cold `#faf9f7`, bold everywhere and uppercase prose: not
a near-miss, a different look.

**The QR is capped so the address under it can be read at the body cut.** It
used to grow into every spare pixel, which pushed the one string somebody may
have to type into a browser to the bottom in the smallest type on the screen.

`Surface::NotesOnly` exists for the reason `WallpapersOnly` does, one step
further: what is behind a code printed on a screen is one note, not the card.
The app sets the path before `begin()` and the client can never name it, so
there is nothing to validate because nothing is accepted. No dev routes, no file
manager, no WebDAV.

**The menu row is always enabled, even with no Wi-Fi**, because tapping it is
what offers to join one. It was drawn disabled saying "join Wi-Fi first", which
sends a person to Settings to do by hand the job the row is holding the tools
for.

**The code carries the address, always.** It is generated from `WiFi.localIP()`
at the moment of drawing and depends on no service, so the only way it can be
wrong is DHCP moving the reader between the paint and the scan. The mDNS name
goes where a human reads it, and only when the responder actually started: an
address that cannot resolve is worse than one line fewer, because the prose then
blames their Wi-Fi.

**Nothing in the app needs it.** Every screen works with no second device, which
is the point.

The one thing to know when reading the page's source: a top-level
`var name = document.getElementById('name')` assigns to `window.name`, a string
property of Window, so the element is coerced to `"[object HTMLElement]"` and
every write is silently dropped. The script is an IIFE and nothing in it is
called `name`.

## Not built

- **OFTEN.** A row of one-tap pills of what this list has held before. Cut by
  Mario, and the whole add screen went with it, because a list of frequent items
  was all that screen held.
- **Prose.** Deleted in the rework, with `stripHeading`, `Task::isTask` and the
  prose branch in `noteRows`. "A note is text and some of it happens to be
  tickable" is a data model; "a list you tick" is a product, and it is the one
  that was asked for.
- Folders, tags, search, rich text, sync, accounts, handwriting.
