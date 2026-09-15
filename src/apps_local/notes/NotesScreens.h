#pragma once

// The Notes screens, in three proposals each, for Mario to choose between.
//
// Freestanding builders in the InstapaperScreens mould: a model in, a drawn
// frame out, no renderer and no Activity, so host-tests/ui can assert what they
// drew and what they made tappable.
//
// ---------------------------------------------------------------------------
// What every variant here has to survive
// ---------------------------------------------------------------------------
//
// These are drawn with hostile content on purpose, because the interesting
// question is not what a shopping list looks like with "Milk" in it. Every
// model below carries at least one item longer than its box, one empty state,
// and a count wide enough to crowd the title. Toybox's rule is that NOTHING IS
// EVER ELIDED, and the Toybox cuts above toybox_10 do not even carry an
// ellipsis glyph -- an overflow there draws as a sentence that simply stops.
// So every string that can vary is either fitted down a cut (`toybox::fitted*`)
// or wrapped, never trusted.
//
// The other constant: a phone is OPTIONAL. Nothing in the deck or the note
// screen mentions one, every variant of the add screen can add an item with no
// second device, and the phone is one route among several rather than the way
// in.

#include <cstdint>

#include "../ui/ToyboxScreen.h"

namespace notesui {

namespace fui = freeink::ui;

// Chess uses 1-4, the link layer the 200s, Hacker News the 300s, Instapaper the
// 320s. Notes takes the 340s.
enum : fui::ActionId {
  ActionOpenNote = 340,
  ActionNewNote = 341,
  ActionToggleTask = 342,  // actionValue carries the row index
  ActionAdd = 343,
  ActionAddOften = 344,  // actionValue carries the pill index
  ActionTypeHere = 345,
  ActionUsePhone = 346,
  ActionClearDone = 347,
  ActionNoteMenu = 348,
};

// --- The deck ------------------------------------------------------------

struct DeckModel {
  const fui::ListItem* items = nullptr;  // label = title, value = "4/9" or null
  int count = 0;
  int selected = 0;
  int topIndex = 0;
};

// A: a list with the count in the value slot, and NEW NOTE on the footer bar.
void buildDeckList(toybox::Screen& screen, const DeckModel& model);

// B: the same rows with the count drawn as a bracketed tally in the row's own
// right-hand gutter, and no footer: NEW NOTE is a row at the end of the list,
// so the deck is one column of things and the page never has a dead bar.
void buildDeckTally(toybox::Screen& screen, const DeckModel& model);

// C: two-up cards. A note is a card, so it is drawn as one: a framed box with
// the title inside it and the tally in the corner marks.
void buildDeckCards(toybox::Screen& screen, const DeckModel& model);

// --- A note, open --------------------------------------------------------

struct Task {
  const char* text = "";
  bool checked = false;
  bool isTask = true;  // false draws the line as prose, with no box
};

struct NoteModel {
  const char* title = "";
  const Task* tasks = nullptr;
  int count = 0;
  int firstVisible = 0;
  // The OFTEN row: what this note has held before and is not holding now.
  const char* const* often = nullptr;
  int oftenCount = 0;
  bool anyDone = false;
};

// A: box on the left, text beside it, OFTEN pills along the bottom.
void buildNoteBoxes(toybox::Screen& screen, const NoteModel& model);

// B: no boxes. A done line is struck and carries a heavy left bar, so the ink
// that marks it is in the margin rather than in a column of empty squares.
void buildNoteBars(toybox::Screen& screen, const NoteModel& model);

// C: boxes on the left, and the bottom bar is two buttons (ADD, CLEAR DONE)
// rather than pills, so the OFTEN list lives one tap away on the add screen and
// this screen is nothing but the list.
void buildNoteQuiet(toybox::Screen& screen, const NoteModel& model);

// The row band and row height, shared with the Activity so its hit-testing and
// the drawn rows come from one function rather than two that can disagree.
fui::Rect noteBand(const fui::DeviceContext& device, bool withPills);
int16_t noteRowHeight(const fui::DrawTarget& target);

// --- Adding, without a phone ---------------------------------------------

struct AddModel {
  const char* noteTitle = "";
  const char* const* often = nullptr;
  int oftenCount = 0;
  // Drawn only by buildAddSplit, and only when the Activity has an address to
  // put in it. Null means the device is not on Wi-Fi, and that variant then
  // draws its bottom half as the reason rather than as a broken code.
  const char* phoneUrl = nullptr;
};

// A: a full page of OFTEN pills in two columns, with TYPE and PHONE on the
// footer. The things you have bought before are the fastest way to add one.
void buildAddPills(toybox::Screen& screen, const AddModel& model);

// B: a plain list of the same items, one per row, with TYPE IT as the first
// row. Rows hold a long item that a pill cannot.
void buildAddList(toybox::Screen& screen, const AddModel& model);

// C: split page -- pills on top, the phone route drawn underneath with its
// address, so both ways are visible at once and neither is a mode.
void buildAddSplit(toybox::Screen& screen, const AddModel& model);

// The QR square buildAddSplit reserves. The Activity draws the code into it
// with QrUtils, which needs a renderer this layer does not have.
fui::Rect addQrBox(const fui::DeviceContext& device);

}  // namespace notesui
