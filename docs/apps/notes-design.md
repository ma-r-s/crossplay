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

**EVERY NON-EMPTY LINE IS AN ITEM.** There is no second kind. This is the rule
the first version of this app got wrong, and everything else that was wrong with
it followed:

> A line the parser did not recognise as `- [ ] ` was "prose": not tickable, not
> deletable on the device, drawn at `toybox_10` beside `toybox_20`, and worth
> nothing in the deck's tally. And the way to produce one was to type a word on
> your phone, which is what the phone is for. To get a real item you typed the
> marker yourself: nine keyboard taps before the first letter on iOS, thirty for
> a shopping list. The page's own hint taught a string that does not even work,
> because `- [ ]Milk` with no space after the bracket is rejected by `classify`.

The marker is still what the FILE holds, so a desktop editor sees ordinary
Markdown checkboxes. It is simply never something a person types:

- The phone surface coerces on the way IN (`notes::coerceToList`). Lines that
  already carry a marker are left byte for byte alone, so a save from the phone
  cannot disturb what was ticked on the device.
- The device draws a box on every row, and ticking a line that has no marker
  writes one -- so a file authored on a computer joins the rule instead of
  sitting outside it forever.
- `counts()` counts lines, not markers.

**A tick flips exactly one byte.** `Line::markAt` is the offset of the `' '` or
`'x'` between the brackets, so everything else in the file is preserved by
construction rather than by a re-serialiser that can drift from the parser. That
is why the task syntax is strict (`- [] milk` is prose): being permissive would
mean a tick had to insert a byte.

## The three screens, and no settings

**The deck.** Rows with the tally in a reserved right-hand gutter, at the SAME
cut as the name beside it -- at `toybox_10` it read as a superscript rather than
as this list's progress. Alphabetical, because that is the order a person can
predict; recency would move the row you are aiming at. NEW LIST is a bar pinned
to the foot: Mario chose it over the action-as-last-row alternative because the
bar anchors the bottom, so a three-list deck reads as a list that ended rather
than a button floating in space.

**A list.** Tick boxes down the left, the text beside them, done lines struck
through in place. ADD on the left of the footer, the fork-wide home for a
primary action; CLEAR DONE only when there is something to clear, and on the
RIGHT, so the control that removes lines never occupies the pixels ADD had a
moment ago.

**ADD keeps the keyboard up.** Type, done, type, done, Back. One visit per item
cost two activity transitions and two full-screen repaints EACH -- six repaints
to write three lines.

**There is no per-line delete, and that is not a hole.** Tick the wrong line and
press CLEAR DONE: two taps, with controls that already exist and already say
what they do.

**The menu**, behind the gear on the band: type on your phone, rename, delete.
Three rows. CLEAR DONE is not among them -- it lives in the footer where it is
needed, and a control in two places is two places to keep in step.

**There is no settings screen**, deliberately. Nothing here has two defensible
values, and the one thing that looks like a setting -- "type on your phone" --
is a per-note action that must never become a mode.

## The layout rules, each paid for by a render

- **Nothing is ever elided.** Not by us and not by the list component, which
  truncated three of six deck titles the first time it was handed them.
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
- **Row height comes from the type, never from the count.** Dividing the band by
  the number of rows fills a short page, but it also redraws the same note with
  a different rhythm after one line is added.
- **A list that does not fit says so.** `"1 / 2"` in the strip above the footer,
  which is the only part of the page otherwise doing nothing.
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
control was. A `static_assert` in `buildMenu` holds the menu's row table to the
row count `menuRowRect` divides by, because a row added to the menu without
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
