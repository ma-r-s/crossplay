# Notes

A deck of cards you tick with one hand. Card #516.

The prior art behind the shape is in [notes-research.md](notes-research.md); this
is what was built and why it is built that way.

## One data type

A note is a Markdown file in `/notes/`, and its NAME IS ITS FILENAME.
`/notes/Shopping.md` is the note called Shopping. One source of truth: a note
whose first line is a task still has a name, renaming is a file rename rather
than a content rewrite, and a person who drops `.md` files on the card over the
reader's own file transfer gets exactly the notes they expect with no import
step and no database.

A line beginning `- [ ]` or `- [x]` is tickable; everything else is prose. There
is no separate to-do list: a shopping list is a note whose lines are all tasks,
a recipe is a note with none. One parser, one file format, one screen, and the
only edit the device itself has to perform is a tick.

**A tick flips exactly one byte.** `Line::markAt` is the offset of the `' '` or
`'x'` between the brackets, so everything else in the file is preserved by
construction rather than by a re-serialiser that can drift from the parser. That
is why the task syntax is strict (`- [] milk` is prose): being permissive would
mean a tick had to insert a byte.

## The three screens, and no settings

**The deck.** Rows with the tally in a reserved right-hand gutter, alphabetical
because that is the order a person can predict; recency would move the row you
are aiming at. NEW NOTE is a bar pinned to the foot: Mario chose it over the
action-as-last-row alternative because the bar anchors the bottom, so a
three-note deck reads as a list that ended rather than a button floating in
space.

**A note.** Tick boxes down the left, the text beside them, done lines struck
through in place. ADD on the left of the footer, the fork-wide home for a
primary action; CLEAR DONE only when there is something to clear, and on the
RIGHT, so the control that removes lines never occupies the pixels ADD had a
moment ago.

**The menu**, behind the gear on the band: type on your phone, clear done,
rename, delete. The rare and the destructive, over the note they belong to.

**There is no settings screen**, deliberately. Nothing here has two defensible
values, and the one thing that looks like a setting -- "type on your phone" --
is a per-note action that must never become a mode.

## The layout rules, each paid for by a render

- **Nothing is ever elided.** Not by us and not by the list component, which
  truncated three of six deck titles the first time it was handed them.
  `pickCut` returns the largest cut in which EVERY string fits in the lines
  available, and 0 when none does, and the deck and the note fall to two lines
  rather than to a smaller cut. The header title is fitted before the band draws
  it for the same reason.
- **Peers share a cut.** The rows of a deck and the lines of a list are compared
  with each other, so the cut is chosen once from the widest member. Sized one
  by one, a long row comes out smaller than its neighbours and reads as a
  different kind of thing.
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

## Not built

- **The phone route.** The menu row is drawn and says "join Wi-Fi first"; the
  `Surface::NotesOnly` page behind it is the next slice. Everything else works
  with no second device, which is the point: the phone is one row on a menu, not
  a prerequisite.
- **OFTEN.** A row of one-tap pills of what this list has held before. Cut by
  Mario, and the whole add screen went with it, because a list of frequent items
  was all that screen held.
- Folders, tags, search, rich text, sync, accounts, handwriting.
