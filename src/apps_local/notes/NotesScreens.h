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
  ActionNewList = 350,
  ActionNewPage = 351,
  ActionOpenNote = 340,
  ActionToggleTask = 342,
  ActionAddLine = 343,
  ActionMenu = 344,
  ActionClearDone = 345,
  ActionRename = 346,
  ActionDelete = 347,
  ActionUsePhone = 348,
  ActionSwitchKind = 352,
  ActionDismiss = 349,
};

// --- Shared measuring ----------------------------------------------------

// The largest cut in which every one of `strings` fits `width` in at most
// `maxLines` lines, with nothing dropped. Returns 0 when even the smallest
// cannot, which is the caller's signal to break rather than to shrink.
// `onlyProbeCut` asks for the probe's own cut or nothing, for a caller that
// wants to try a wrapping arrangement at this size before dropping a size.
fui::FontId pickCut(const fui::DrawTarget& target, const char* const* strings, int count, int16_t width, int maxLines,
                    const fui::TextStyle& probe, bool onlyProbeCut = false);

// How many lines `text` needs at `style`'s cut, or 0 if more than `maxLines`.
int linesNeeded(const fui::DrawTarget& target, const char* text, int16_t width, int maxLines,
                const fui::TextStyle& style);

// --- The deck ------------------------------------------------------------

struct DeckItem {
  const char* title = "";
  const char* tally = nullptr;    // "4/9" for a list; null for a note
  const char* preview = nullptr;  // the note's first words; null for a list
  int done = 0;
  int total = 0;  // above zero means a list, and draws the bar
};

struct DeckModel {
  const DeckItem* items = nullptr;
  int count = 0;
  int firstVisible = 0;
  // "1 / 2" when the deck does not fit one page. A list that silently stops at
  // the fifth of six is the worst thing either of these screens can do.
  const char* pageLabel = nullptr;
};

// The NEW NOTE bar is PINNED to the foot. Mario chose it over the alternative
// (the action as the last row of the column) because the bar anchors the bottom
// of the page: a three-note deck then reads as a list that ended, rather than as
// a button floating in the middle of nothing.
void buildDeck(toybox::Screen& screen, const DeckModel& model);

// How many rows a page of this deck holds. Asked of the same layout the drawing
// uses, so the page label, the physical keys and the drawn rows cannot
// disagree; two functions that must agree are two functions that can differ.
int deckCapacity(const fui::DrawTarget& target, const fui::DeviceContext& device, const DeckModel& model);

// --- A note, open --------------------------------------------------------

// A line of a list. There is no second kind: every non-empty line is an item
// with a box, drawn at the same cut as its neighbours. The app can no longer
// author anything else, and a file written elsewhere that does is shown as
// items too -- ticking one writes the marker.
struct Task {
  const char* text = "";
  bool checked = false;
};

struct NoteModel {
  const char* title = "";
  // A page is the same rows without tick boxes: text you keep, not things to
  // do. The kind belongs to the note, so one flag decides the whole screen and
  // no line can disagree with its neighbours.
  bool page = false;
  const Task* tasks = nullptr;
  int count = 0;
  int firstVisible = 0;
  bool anyDone = false;
  // The strip under the band, on a list only: what is left, and a bar. Zero
  // total draws nothing, which is what an empty list and every note want.
  int done = 0;
  int total = 0;
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

void buildNote(toybox::Screen& screen, const NoteModel& model);
int noteCapacity(const fui::DrawTarget& target, const fui::DeviceContext& device, const NoteModel& model);

// --- The menu ------------------------------------------------------------

struct MenuModel {
  const char* title = "";
  const freeink::Icon* menuIcon = nullptr;
  bool anyDone = false;
  // The address, once the page is up; null otherwise. Never a reason the row is
  // unavailable, because it never is: tapping it with no Wi-Fi offers to join
  // one.
  const char* phoneHint = nullptr;
  // What the note is NOW. The kind row offers the other one, so this picks its
  // wording rather than adding a second action.
  bool isList = false;
};

void buildMenu(toybox::Screen& screen, const MenuModel& model);

// --- Typing from a phone -------------------------------------------------

struct PhoneModel {
  const char* title = "";
  const freeink::Icon* menuIcon = nullptr;
  // What the QR carries: the device's own address. Generated from the live IP
  // at the moment of drawing, so the only way it can be wrong is DHCP moving
  // this reader between the paint and the scan.
  const char* url = "";
  // What a person reads and can type or bookmark. The mDNS name when the
  // responder started, the dotted address when it did not -- never a name that
  // cannot resolve, because the prose would then blame their Wi-Fi.
  const char* readable = "";
  bool saved = false;
};

// Returns the square the caller draws the code into: QrUtils needs a renderer,
// which this layer does not have.
fui::Rect buildPhone(toybox::Screen& screen, const PhoneModel& model);

// --- The delete confirm --------------------------------------------------

struct ConfirmModel {
  const char* title = "";
  const freeink::Icon* menuIcon = nullptr;
  const char* prose = "";
};

// KEEP occupies EXACTLY the pixels DELETE NOTE had on the menu, so a repeat of
// the press that opened this -- a double tap, an impatient second jab during a
// 0.3-2s repaint, a finger that never moved -- cancels. DELETE sits where no
// menu control was. See same-pixel-different-action.
void buildConfirm(toybox::Screen& screen, const ConfirmModel& model);

// The same page with one way off it. Used for every refusal the card can hand
// back, which are the only failures this app has: the message is shown verbatim
// rather than summarised, because "the card is nearly full" and "the card would
// not take the change" want different things from the person reading them.
void buildNotice(toybox::Screen& screen, const ConfirmModel& model);

// The rect the menu's last row occupies, so the confirm and the menu agree by

}  // namespace notesui
