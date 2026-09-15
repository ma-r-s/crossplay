#include "NotesActivity.h"

#include <Memory.h>

#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "NotesScreens.h"

namespace fui = freeink::ui;

namespace {

// The gallery's content is deliberately hostile. Every list here carries an
// item longer than its box, a name longer than the band, a tally wide enough
// to crowd a title, and a prose line among the tasks -- because a proposal
// that only survives "Milk" has not been judged at all.

const fui::ListItem kDeckItems[] = {
    {"Shopping", nullptr, "4/9"},
    {"Packing for Lisbon and the wedding", nullptr, "0/12"},
    {"Bread", nullptr, nullptr},
    {"Bike service before the Pyrenees trip", nullptr, "2/5"},
    {"Guest wifi", nullptr, nullptr},
    {"Things to ask the landlord about the flat", nullptr, "11/34"},
};

const notesui::Task kTasks[] = {
    {"Milk", true, true},
    {"Eggs", true, true},
    {"Bread flour", false, true},
    {"Ask Nuria about the ferry tickets for Sunday", false, true},
    {"Olive oil", true, true},
    {"the good one, not the cooking one", false, false},
    {"Lemons", false, true},
    {"Coffee beans", false, true},
};

const char* const kOften[] = {"milk", "bananas", "butter", "tinned tomatoes", "rice"};

notesui::DeckModel deck(const int count) {
  notesui::DeckModel model;
  model.items = kDeckItems;
  model.count = count;
  model.selected = 1;
  return model;
}

notesui::NoteModel note() {
  notesui::NoteModel model;
  model.title = "SHOPPING";
  model.tasks = kTasks;
  model.count = static_cast<int>(sizeof(kTasks) / sizeof(kTasks[0]));
  model.often = kOften;
  model.oftenCount = static_cast<int>(sizeof(kOften) / sizeof(kOften[0]));
  model.anyDone = true;
  return model;
}

notesui::AddModel add(const char* url) {
  notesui::AddModel model;
  model.noteTitle = "SHOPPING";
  model.often = kOften;
  model.oftenCount = static_cast<int>(sizeof(kOften) / sizeof(kOften[0]));
  model.phoneUrl = url;
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
    case Proposal::DeckList:
      notesui::buildDeckList(screen, deck(6));
      break;
    case Proposal::DeckTally:
      notesui::buildDeckTally(screen, deck(6));
      break;
    case Proposal::DeckCards:
      notesui::buildDeckCards(screen, deck(6));
      break;
    case Proposal::NoteBoxes:
      notesui::buildNoteBoxes(screen, note());
      break;
    case Proposal::NoteBars:
      notesui::buildNoteBars(screen, note());
      break;
    case Proposal::NoteQuiet:
      notesui::buildNoteQuiet(screen, note());
      break;
    case Proposal::AddPills:
      notesui::buildAddPills(screen, add("http://192.168.1.42"));
      break;
    case Proposal::AddList:
      notesui::buildAddList(screen, add("http://192.168.1.42"));
      break;
    case Proposal::AddSplit:
      notesui::buildAddSplit(screen, add("http://192.168.1.42"));
      break;
    case Proposal::DeckEmpty:
      notesui::buildDeckList(screen, deck(0));
      break;
    case Proposal::AddNoWifi:
      notesui::buildAddSplit(screen, add(nullptr));
      break;
    case Proposal::kCount:
      break;
  }

  // The buffer records overflow and this reports it; a screen that silently
  // drops its last three controls is how Connections lost all its buttons.
  toybox::reportOverflow(interactions_, "Notes");
  renderer.displayBuffer();
}
