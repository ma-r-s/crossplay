#pragma once

// Go on screen. Freestanding builders over plain models: no renderer, no
// storage, no Activity, so host-tests/ui can build every one of them against a
// fake target and ask what was drawn and what was made tappable.

#include "../ui/ToyboxScreen.h"
#include "GoCore.h"
#include "GoFlow.h"

namespace goui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionMenuRow = 1,
  ActionSettingsRow = 2,
  ActionPass = 3,
  ActionAgain = 4,
  ActionDone = 5,
  ActionResume = 6,
  ActionAccept = 7,
};

enum class MenuRow : int { Play = 0, PlayNearby, Settings, Count };
enum class SettingsRow : int { Opponent = 0, Level, PlayAs, Count };

struct MenuModel {
  const char* nearbyName = nullptr;
  int selected = -1;
  // A game is part-played and PLAY will resume it rather than start one.
  bool inProgress = false;

  // The record and the last game's final position, for the front door's
  // ornament. Null until a game has been finished on this device.
  bool hasHistory = false;
  const uint8_t* lastPoints = nullptr;
  bool lastWon = false;
  // The margin in half points, so "BY 5.5" is expressible.
  int lastMarginHalves = 0;
  int wins = 0;
  int losses = 0;
};

struct SettingsModel {
  int selected = -1;
  go::Opponent opponent = go::Opponent::Computer;
  go::Level level = go::Level::Medium;
  // Which colour the player takes against the computer. Black moves first and
  // gives away komi; White takes the komi and moves second. On a nine by nine
  // that is a real choice rather than a preference.
  uint8_t playAs = go::kBlack;
  // Stones the chosen level spots the player. Non-zero forces them to Black,
  // because a handicap is Black's by definition.
  int handicap = 0;
};

struct BoardModel {
  go::Game game{};
  // The point a finger has chosen but not committed, or kNothingAimed. Owned by
  // the flow; the screen draws it and never decides it.
  int aimed = go::kNothingAimed;
  // What is wrong with the aimed point, if anything. Computed by the flow from
  // the rules, so the warning and the move that triggers it cannot disagree.
  go::Caution caution = go::Caution::None;
  // Which colour this seat plays. Black unless the player chose otherwise or
  // the coin toss did.
  uint8_t seat = go::kBlack;
  bool yourTurn = true;
  // The opponent just passed, which is the one event in Go that is invisible on
  // the board and decides whether the game is about to end.
  bool theyPassed = false;
  // No legal move that is not filling your own eye: passing is the only sane
  // act and the board should say so.
  bool nothingLeft = false;
  const char* opponentName = nullptr;
  // Two people sharing one device, so "YOUR MOVE" is the wrong words.
  bool sharedDevice = false;
  bool thinking = false;
};

struct CountModel {
  go::Game game{};
  uint8_t seat = go::kBlack;
  // Whose area each point counts as, with the dead stones already lifted.
  // Carried in rather than recomputed, so the number under the board and the
  // marks on it come from one pass.
  uint8_t owner[go::kPoints] = {};
  int blackHalves = 0;
  int whiteHalves = 0;
  // A link match: both seats have to say yes, and this one already has.
  bool youAccepted = false;
  bool theyAccepted = false;
  bool sharedDevice = false;
};

struct ResultModel {
  go::Game game{};
  uint8_t seat = go::kBlack;
  uint8_t owner[go::kPoints] = {};
  int blackHalves = 0;
  int whiteHalves = 0;
  const char* opponentName = nullptr;
  bool sharedDevice = false;
};

// An intersection's centre, and the exact inverse. Eighty-one points against a
// twenty-four slot interaction buffer, so the board is hit-tested
// arithmetically from the geometry that drew it rather than registered point by
// point -- the same discipline chess and checkers use, for the same reason.
//
// There is no seat argument and there must not be one: a go board has no near
// end. Turning it to face whoever is to move would move every stone on screen
// for no gain, because the position means the same thing from both sides.
void stoneCentre(const fui::DeviceContext& device, int point, int16_t& cx, int16_t& cy);
bool pointAt(const fui::DeviceContext& device, int x, int y, int& point);
int16_t stoneRadius();

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildSettings(toybox::Screen& screen, const SettingsModel& model);
void buildBoard(toybox::Screen& screen, const BoardModel& model);
void buildCount(toybox::Screen& screen, const CountModel& model);
void buildResult(toybox::Screen& screen, const ResultModel& model);

// "B+5.5", "W+12.5". One function so the result screen, the front door's
// caption and the count all say it the same way.
void formatResult(char* out, int capacity, int blackHalves, int whiteHalves);

}  // namespace goui
