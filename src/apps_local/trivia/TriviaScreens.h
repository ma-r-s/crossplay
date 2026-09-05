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
  ActionFlag = 4,  // "this question is bad" -- the whole curation loop
  // Carries which of the four in the event VALUE. Frame::hit's value parameter
  // defaults to 0, so a caller that forgets it makes every option read as the
  // first one -- which shipped in v1.12.0 and made solo play a coin toss. This
  // comment used to say "ListItem carries which of the four", describing an
  // implementation that was not there.
  ActionOption = 5,
  ActionQuit = 6,
  ActionGetPack = 8,  // fetch the question pack over WiFi
  // TRIVIA's own settings screen. Carries the row in the event VALUE, like
  // every other settings list in this fork (chess, forehead, toybattle).
  ActionSettingsRow = 9,
  ActionCloseSettings = 10,
  // The HIDDEN notice's second row. WHY? opens the reason list; UNDO takes the
  // report back. Both live ABOVE the primary bar rather than beside it: adding
  // a second control to drawAction's row would shrink NEXT QUESTION and move
  // its centre, and a player who has learned where that bar is would open a
  // list instead of continuing. See the same-pixel-different-action memory.
  ActionWhy = 11,
  ActionUnhide = 12,
  // The reason list. Carries the reason's own wire value, so the row order on
  // screen can change without changing what a report means.
  ActionReasonRow = 13,
  ActionCloseReason = 14,
  ActionSync = 15,
  ActionUnhideAll = 16,
};

enum class MenuRow : int { Quizmaster = 0, Solo, Difficulty, Settings, Count };

// The rows of TRIVIA's own SETTINGS screen.
//
// DIFFICULTY is deliberately NOT one of these, and it is worth saying why here
// rather than rediscovering it. The two options are different KINDS. Difficulty
// is a per-session mood -- easy tonight, hard tomorrow, changed as often as the
// mode is -- so it belongs beside QUIZMASTER and SOLO on the front door, where
// it costs no taps. US QUESTIONS is a persistent preference about which
// questions exist for you at all: set once, near enough to never changed again.
// One surface each is not two option surfaces; a chess LEVEL is set-and-forget
// and that is why chess keeps its on this screen instead.
// SYNC and HIDDEN join US QUESTIONS by the same test the comment above applies:
// both are set-and-forget rather than per-session. SYNC is the one door to the
// network once a pack exists -- before this, ActionGetPack was reachable ONLY
// from the empty-card and failure notices, so a device that already had a pack
// could never receive a newer one (docs/open-items.md).
enum class SettingRow : int { UsCentric = 0, Sync, Hidden, Count };

// What the SETTINGS screen shows. The activity owns the value; this is a
// picture of it, and the screen writes nothing.
struct SettingsModel {
  bool usCentric = false;
  // How many questions this card is hiding, and how many of those have not yet
  // been sent. Shown because HIDE had no visible total anywhere and no way back:
  // a mis-tap was permanent and silent.
  uint32_t hidden = 0;
  uint32_t pending = 0;
  // What the card knows about its own build, already rendered into a sentence
  // by the activity -- the screen writes nothing.
  const char* packLine = "";
};

// A headline, a sentence, and at most one thing to do about it. Used for the
// empty card and for every download outcome.
struct NoticeModel {
  const char* headline = "";
  const char* body = "";
  const char* actionLabel = nullptr;
  fui::ActionId action = 0;
  // Up to two extra controls, drawn as their own row ABOVE the primary. The
  // primary's rect never changes whether these are present or not, which is the
  // whole point of putting them on a second row.
  const char* secondLabel = nullptr;
  fui::ActionId secondAction = 0;
  const char* thirdLabel = nullptr;
  fui::ActionId thirdAction = 0;
};

// The WHY? list. Labels and their wire values are supplied by the activity, so
// this screen has no opinion about what a reason means.
struct ReasonModel {
  static constexpr int kMax = 10;
  int count = 0;
  const char* label[kMax] = {};
  int value[kMax] = {};
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
void buildSettings(toybox::Screen& screen, const SettingsModel& model);
void buildReasons(toybox::Screen& screen, const ReasonModel& model);
void buildNotice(toybox::Screen& screen, const NoticeModel& model);
void buildQuestion(toybox::Screen& screen, const QuestionModel& model);
void buildChoice(toybox::Screen& screen, const ChoiceModel& model);

}  // namespace triviaui
