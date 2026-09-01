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
  ActionMenuRow = 1,  // the front door's list; ListItem carries which row
  ActionReveal = 2,   // quizmaster: turn the answer over
  ActionNext = 3,
  ActionFlag = 4,    // "this question is bad" -- the whole curation loop
  // Carries which of the four in the event VALUE. Frame::hit's value parameter
  // defaults to 0, so a caller that forgets it makes every option read as the
  // first one -- which shipped in v1.12.0 and made solo play a coin toss. This
  // comment used to say "ListItem carries which of the four", describing an
  // implementation that was not there.
  ActionOption = 5,
  ActionQuit = 6,
  ActionDifficulty = 7,
  ActionGetPack = 8,  // fetch the question pack over WiFi
};

enum class MenuRow : int { Quizmaster = 0, Solo, Difficulty, Count };

// A headline, a sentence, and at most one thing to do about it. Used for the
// empty card and for every download outcome.
struct NoticeModel {
  const char* headline = "";
  const char* body = "";
  const char* actionLabel = nullptr;
  fui::ActionId action = 0;
};

struct MenuModel {
  int selected = -1;
  int difficulty = 0;  // 0 = any
  uint32_t packCount = 0;
  uint32_t seenCount = 0;  // how many of packCount have been served
  bool packMissing = false;
};

// One question, mid-round. `answer` is null until it is turned over, which is
// what the screen keys on -- a separate "revealed" flag is a second fact that
// can disagree with the first.
struct QuestionModel {
  const char* clue = "";
  const char* answer = nullptr;
  const char* alternate = nullptr;
  // 0 means there is no question, so no difficulty to report. A real question
  // is always 1..kDifficulties. Defaulting to 1 made the empty screen claim a
  // level it did not have, and the meter is the one thing on that screen a
  // player could mistake for their own setting.
  int difficulty = 0;
  int asked = 0;
};

// Solo. The options are always drawn; `chosen` is -1 until one is tapped, and
// after that the correct one is marked and the wrong pick struck.
struct ChoiceModel {
  const char* clue = "";
  const char* option[trivia::kOptions] = {};
  int correct = 0;
  int chosen = -1;
  int difficulty = 0;  // 0 = no question; see QuestionModel
  int asked = 0;
  int right = 0;
};

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildNotice(toybox::Screen& screen, const NoticeModel& model);
void buildQuestion(toybox::Screen& screen, const QuestionModel& model);
void buildChoice(toybox::Screen& screen, const ChoiceModel& model);

}  // namespace triviaui
