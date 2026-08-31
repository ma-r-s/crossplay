#pragma once

// TRIVIA on screen. Freestanding builders over plain models.
//
// The app has two modes and they want different screens. QUIZMASTER puts one
// clue on the panel and nothing else: the device is the person reading out, the
// room does the arguing, and a tap turns the answer over. SOLO adds four
// options and keeps score, because with nobody else in the room something has
// to judge you.
//
// Everything here is prose rather than labels, which is the one thing that
// changes how it is drawn: toybox::inkCentred centres the CAP band, so setting
// mixed-case text with it hangs the descenders below the box (ToyboxTokens.h
// says so, and the clue is the longest mixed-case run in the fork). Clue text
// is therefore placed by its box, never ink-centred; only the all-caps chrome
// uses inkCentred.

#include "../ui/ToyboxScreen.h"
#include "TriviaCore.h"

namespace triviaui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionMenuRow = 1,   // the front door's list; ListItem carries which row
  ActionReveal = 2,    // quizmaster: turn the answer over
  ActionNext = 3,
  ActionFlag = 4,      // "this question is bad" -- the whole curation loop
  ActionOption = 5,    // solo: ListItem carries which of the four
  ActionQuit = 6,
  ActionDifficulty = 7,
};

// Three arrangements of the one screen that matters, rendered side by side so
// the choice is made by looking rather than by describing. See
// docs/apps/trivia.md; the chosen one stays and the others go.
enum class QuestionLayout : uint8_t { Card = 0, Page = 1, Stage = 2 };

enum class MenuRow : int { Quizmaster = 0, Solo, Difficulty, Count };

struct MenuModel {
  int selected = -1;
  int difficulty = 0;          // 0 = any
  uint32_t packCount = 0;
  bool packMissing = false;
};

// One question, mid-round. `answer` is null until it is turned over, which is
// what the screen keys on -- a separate "revealed" flag is a second fact that
// can disagree with the first.
struct QuestionModel {
  const char* clue = "";
  const char* answer = nullptr;
  const char* alternate = nullptr;
  int difficulty = 1;
  int asked = 0;
  QuestionLayout layout = QuestionLayout::Card;
};

// Solo. The options are always drawn; `chosen` is -1 until one is tapped, and
// after that the correct one is marked and the wrong pick struck.
struct ChoiceModel {
  const char* clue = "";
  const char* option[trivia::kOptions] = {};
  int correct = 0;
  int chosen = -1;
  int difficulty = 1;
  int asked = 0;
  int right = 0;
};

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildQuestion(toybox::Screen& screen, const QuestionModel& model);
void buildChoice(toybox::Screen& screen, const ChoiceModel& model);

}  // namespace triviaui
