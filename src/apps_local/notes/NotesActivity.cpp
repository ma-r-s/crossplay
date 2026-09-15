#include "NotesActivity.h"

#include <Memory.h>

#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxTheme.h"
#include "NotesScreens.h"

namespace fui = freeink::ui;

namespace {

// The gallery's content is deliberately hostile. A note name longer than the
// band, a task longer than its row, a tally of 11/34, and a prose line among
// the tasks -- because a proposal that only survives "Milk" has not been judged.

const notesui::DeckItem kDeckItems[] = {
    {"Shopping", "4/9"},     {"Packing for Lisbon and the wedding", "0/12"},
    {"Bread", nullptr},      {"Bike service before the Pyrenees trip", "2/5"},
    {"Guest wifi", nullptr}, {"Things to ask the landlord about the flat", "11/34"},
};

const notesui::Task kTasks[] = {
    {"Milk", true, true},         {"Eggs", true, true},
    {"Bread flour", false, true}, {"Ask Nuria about the ferry tickets for Sunday", false, true},
    {"Olive oil", true, true},    {"the good one, not the cooking one", false, false},
    {"Lemons", false, true},      {"Coffee beans", false, true},
};

notesui::DeckModel deck(const int count) {
  notesui::DeckModel model;
  model.items = kDeckItems;
  model.count = count;
  return model;
}

notesui::NoteModel note(const int count, const char* pageLabel = nullptr) {
  notesui::NoteModel model;
  model.title = "SHOPPING";
  model.tasks = kTasks;
  model.count = count;
  model.anyDone = true;
  model.pageLabel = pageLabel;
  model.menuIcon = &icon_go_settings_32;
  return model;
}

notesui::MenuModel menu(const char* phoneHint) {
  notesui::MenuModel model;
  model.title = "SHOPPING";
  model.anyDone = true;
  model.phoneHint = phoneHint;
  model.menuIcon = &icon_go_settings_32;
  return model;
}

}  // namespace

std::unique_ptr<Activity> NotesActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<NotesActivity>(renderer, mappedInput);
}

void NotesActivity::onEnter() {
  Activity::onEnter();
  // The Toybox cuts are registered by the app that wants them, not by main.cpp,
  // so they cost no upstream surface. Without this every string on every screen
  // asks for a face the renderer does not have and draws nothing at all.
  toybox::ensureFonts(renderer);
  requestUpdate();
}

void NotesActivity::loop() {
  // Back is read on the per-frame path, above any "return unless a tap
  // arrived" guard, because the global back-swipe arrives as Button::Back and a
  // swipe is not a tap. host-tests/backgesture enforces this fork-wide.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
    return;
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    const uint8_t next = static_cast<uint8_t>(proposal_) + 1;
    proposal_ = static_cast<Proposal>(next >= static_cast<uint8_t>(Proposal::kCount) ? 0 : next);
    requestUpdate();
  }
}

void NotesActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);

  switch (proposal_) {
    case Proposal::DeckBar:
      notesui::buildDeckBar(screen, deck(6));
      break;
    case Proposal::DeckRow:
      notesui::buildDeckRow(screen, deck(6));
      break;
    case Proposal::NoteBar:
      notesui::buildNoteBar(screen, note(8, "1 / 2"));
      break;
    case Proposal::NoteRow:
      notesui::buildNoteRow(screen, note(8, "1 / 2"));
      break;
    case Proposal::Menu:
      notesui::buildMenu(screen, menu("192.168.1.42"));
      break;
    case Proposal::MenuNoWifi:
      notesui::buildMenu(screen, menu(nullptr));
      break;
    case Proposal::DeckShort:
      notesui::buildDeckRow(screen, deck(3));
      break;
    case Proposal::NoteShort:
      notesui::buildNoteRow(screen, note(3));
      break;
    case Proposal::DeckEmpty:
      notesui::buildDeckBar(screen, deck(0));
      break;
    case Proposal::kCount:
      break;
  }

  // The buffer records overflow and this reports it; a screen that silently
  // drops its last three controls is how Connections lost all its buttons.
  toybox::reportOverflow(interactions_, "Notes");
  renderer.displayBuffer();
}
