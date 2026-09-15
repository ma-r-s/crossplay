#pragma once

// The Notes screens.
//
// Freestanding builders in the InstapaperScreens mould: a model in, a drawn
// frame out, no renderer and no Activity, so host-tests/ui can assert what they
// drew and what they made tappable.
//
// ---------------------------------------------------------------------------
// Three rules these obey, all three paid for by a render
// ---------------------------------------------------------------------------
//
// **NOTHING IS EVER ELIDED.** Not by us and not by the list component either,
// which will happily truncate a label to "Packing for Lisbon an..." if it is
// handed one too long. So every variable string is measured here first and the
// row is given a cut it fits in, or two lines of one. `notes::pickCut` returns
// 0 when even the smallest cut cannot hold a string in the lines available,
// and the only caller that can happen to breaks the line instead.
//
// **PEERS SHARE A CUT.** The rows of a deck, and the lines of a list, mean
// "compare these with each other". Sized one by one, a long row comes out
// smaller than the rows beside it and reads as a different kind of thing. So
// the cut is chosen once, from the widest member, and every row is set in it.
// The Connections board paid for this on 58% of its archive.
//
// **ROWS FILL THE PAGE.** A screen that holds its image for hours cannot have a
// slab of nothing above its footer. The row height is derived from the band and
// the number of rows on the page, within a finger-sized floor and a ceiling, so
// a full page is full rather than top-aligned with 280px of air under it.
//
// There is no OFTEN row and no add screen. Both existed to make re-adding a
// frequent item one tap; Mario cut them, and the whole add screen went with
// them, because a list of frequent items was all that screen held. Adding is
// the keyboard, and the phone route lives on the menu sheet where it is one
// choice among four rather than a second way of looking at the note.

#include <cstdint>

#include "../ui/ToyboxScreen.h"

namespace notesui {

namespace fui = freeink::ui;

// Chess uses 1-4, the link layer the 200s, Hacker News the 300s, Instapaper the
// 320s. Notes takes the 340s.
enum : fui::ActionId {
  ActionOpenNote = 340,
  ActionNewNote = 341,
  ActionToggleTask = 342,
  ActionAddLine = 343,
  ActionMenu = 344,
  ActionClearDone = 345,
  ActionRename = 346,
  ActionDelete = 347,
  ActionUsePhone = 348,
  ActionDismiss = 349,
};

// --- Shared measuring ----------------------------------------------------

// The largest cut in which every one of `strings` fits `width` in at most
// `maxLines` lines, with nothing dropped. Returns 0 when even the smallest
// cannot, which is the caller's signal to break rather than to shrink.
fui::FontId pickCut(const fui::DrawTarget& target, const char* const* strings, int count, int16_t width, int maxLines,
                    const fui::TextStyle& probe);

// How many lines `text` needs at `style`'s cut, or 0 if more than `maxLines`.
int linesNeeded(const fui::DrawTarget& target, const char* text, int16_t width, int maxLines,
                const fui::TextStyle& style);

// --- The deck ------------------------------------------------------------

struct DeckItem {
  const char* title = "";
  const char* tally = nullptr;  // "4/9", or null for a note with no tasks
};

struct DeckModel {
  const DeckItem* items = nullptr;
  int count = 0;
  int firstVisible = 0;
};

// A: rows with a pinned NEW NOTE bar at the foot.
void buildDeckBar(toybox::Screen& screen, const DeckModel& model);

// B: the same rows with NEW NOTE as the last row of the column, so the page is
// one list of things and there is no bar under it to leave a gap above.
void buildDeckRow(toybox::Screen& screen, const DeckModel& model);

// --- A note, open --------------------------------------------------------

struct Task {
  const char* text = "";
  bool checked = false;
  bool isTask = true;
};

struct NoteModel {
  const char* title = "";
  const Task* tasks = nullptr;
  int count = 0;
  int firstVisible = 0;
  bool anyDone = false;
  // "1 / 2", set by the Activity only when the note does not fit one page. A
  // list that silently stops at the sixth of eight lines is the worst thing
  // this screen can do, and the gap above the footer is where it goes, because
  // that gap is the only space on the page that is otherwise doing nothing.
  const char* pageLabel = nullptr;
  // The menu control on the band. Owned by the Activity, which knows the
  // glyphs; a square icon button sits centred on the band where a text label
  // sits on the component's own baseline, low against the title.
  const freeink::Icon* menuIcon = nullptr;
};

// A: tick boxes, with ADD and CLEAR DONE on a footer bar.
void buildNoteBar(toybox::Screen& screen, const NoteModel& model);

// B: tick boxes, with ADD A LINE as the last row. Everything else is on the
// menu, so the page is the list and nothing else.
void buildNoteRow(toybox::Screen& screen, const NoteModel& model);

// --- The menu ------------------------------------------------------------

struct MenuModel {
  const char* title = "";
  const freeink::Icon* menuIcon = nullptr;
  bool anyDone = false;
  // Null when the reader is not on Wi-Fi. The row is still drawn, because a
  // control that appears and disappears teaches nobody where it lives; it says
  // what is missing instead.
  const char* phoneHint = nullptr;
};

void buildMenu(toybox::Screen& screen, const MenuModel& model);

}  // namespace notesui
