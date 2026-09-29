#include "WordleActivity.h"

#include <Memory.h>

#include "../Shelf.h"
#include "../ui/ToyboxTheme.h"
#include "WordleScreens.h"

namespace fui = freeink::ui;

std::unique_ptr<Activity> WordleActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<WordleActivity>(renderer, mappedInput);
}

void WordleActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void WordleActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
  }
}

namespace {

// A made-up game three guesses in, so every mark is on the screen at once:
// the answer is PLANT, and the fourth row is being typed.
wordleui::GameModel seededGame() {
  using wordleui::Mark;
  wordleui::GameModel model;
  model.rightLabel = "SEP 28";
  struct Guess {
    const char* word;
    Mark marks[wordleui::kLetters];
  };
  const Guess guesses[] = {
      {"CRANE", {Mark::Absent, Mark::Absent, Mark::Correct, Mark::Correct, Mark::Absent}},
      {"SLOTH", {Mark::Absent, Mark::Correct, Mark::Absent, Mark::Present, Mark::Absent}},
      {"TULIP", {Mark::Present, Mark::Absent, Mark::Present, Mark::Absent, Mark::Present}},
  };
  int row = 0;
  for (const Guess& guess : guesses) {
    for (int c = 0; c < wordleui::kLetters; ++c) {
      model.tiles[row][c] = {guess.word[c], guess.marks[c]};
      Mark& key = model.keys[guess.word[c] - 'A'];
      if (static_cast<int>(guess.marks[c]) > static_cast<int>(key)) key = guess.marks[c];
    }
    ++row;
  }
  const char* typing = "PLA";
  for (int c = 0; typing[c] != '\0'; ++c) model.tiles[row][c] = {typing[c], Mark::Typed};
  return model;
}

}  // namespace

void WordleActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);
  wordleui::buildGame(screen, seededGame());
  renderer.displayBuffer();
}
