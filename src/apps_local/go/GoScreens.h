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
  // The square button on the end of the RESUME row. Its own action rather than
  // a second meaning for the row, because it throws a game away and the two
  // must never be one pixel apart in the table.
  ActionDiscard = 8,
};

enum class MenuRow : int { Play = 0, PlayNearby, Settings, Count };
enum class SettingsRow : int { Opponent = 0, Level, Handicap, PlayAs, Board, Count };

struct MenuModel {
  const char* nearbyName = nullptr;
  int selected = -1;
  // A game is part-played and PLAY will resume it rather than start one.
  bool inProgress = false;

  // The position the front door draws, one byte a point, and the board it is
  // on. The game IN PROGRESS when there is one, the last game finished when
  // there is not, and null when neither exists -- which is a device that has
  // never played. The front door sat empty until the first game was over,
  // which is exactly backwards: the thing a player most wants to see from here
  // is the game they are in the middle of.
  const uint8_t* boardPoints = nullptr;
  uint8_t boardSize = go::kSmallSize;
  // How far in the game in progress is. Only read when `inProgress`.
  int moveNumber = 0;

  // The record and the last finished game's margin, for the caption.
  bool hasHistory = false;
  bool lastWon = false;
  // The margin in half points, so "BY 5.5" is expressible.
  int lastMarginHalves = 0;
  int wins = 0;
  int losses = 0;
};

struct SettingsModel {
  int selected = -1;
  go::Opponent opponent = go::Opponent::Computer;
  // How hard the computer plays, and NOTHING else. The handicap and the colour
  // are their own rows below: a level that silently spotted stones made EASY
  // mean two things at once and neither of them was adjustable.
  go::Level level = go::Level::Medium;
  // Which colour the player takes against the computer. Black moves first and
  // gives away komi; White takes the komi and moves second. On these boards
  // that is a real choice rather than a preference.
  uint8_t playAs = go::kBlack;
  // Stones the player is spotted, 0 or 2..kMaxHandicap. Non-zero forces them to
  // Black, because a handicap is Black's by definition.
  int handicap = 0;
  // The board the next new game is played on.
  int boardSize = go::kSmallSize;
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
  // The machine refused the count and the game came back. Without a word on the
  // screen, a board reappearing on its own reads as a fault.
  bool disagreed = false;
  const char* opponentName = nullptr;
  // Two people sharing one device, so "YOUR MOVE" is the wrong words.
  bool sharedDevice = false;
  bool thinking = false;
  // You passed and the machine answered with a STONE rather than a pass, which
  // on a board that does not move reads as the machine ignoring you. It is not:
  // there are points belonging to nobody and it is taking them, which is correct
  // play and worth one point each. `freePoints` is how many are left, and it is
  // the same number the engine's own pass rule reads.
  bool itPlayedOn = false;
  // An int, not a byte: nineteen by nineteen has 361 points, and a byte showed
  // "74 FREE POINTS" with 330 free and dropped the line at exactly 256.
  int freePoints = 0;
};

struct CountModel {
  go::Game game{};
  uint8_t seat = go::kBlack;
  // Whose area each point counts as, with the dead stones already lifted.
  // Carried in rather than recomputed, so the number under the board and the
  // marks on it come from one pass.
  uint8_t owner[go::kMaxPoints] = {};
  int blackHalves = 0;
  int whiteHalves = 0;
  // A link match: both seats have to say yes, and this one already has.
  bool youAccepted = false;
  bool theyAccepted = false;
  // Counting has a turn too, in a match: a mark is a state change and the link
  // only takes one from the seat holding the turn. Without this the screen
  // offers controls whose taps are silently dropped.
  bool yourTurn = true;
  bool sharedDevice = false;
};

struct ResultModel {
  go::Game game{};
  uint8_t seat = go::kBlack;
  uint8_t owner[go::kMaxPoints] = {};
  int blackHalves = 0;
  int whiteHalves = 0;
  const char* opponentName = nullptr;
  bool sharedDevice = false;
};

// An intersection's centre, and the exact inverse. Up to a hundred and sixty
// nine points against a twenty-four slot interaction buffer, so the board is
// hit-tested arithmetically from the geometry that drew it rather than
// registered point by point -- the same discipline chess and checkers use, for
// the same reason.
//
// `size` rather than a constant: the two boards fill the SAME square, at 49px
// a line and 33px a line. Keeping the square fixed is what lets the seat bands,
// the buttons and the frame stay where they are on both.
//
// There is no seat argument and there must not be one: a go board has no near
// end. Turning it to face whoever is to move would move every stone on screen
// for no gain, because the position means the same thing from both sides.
void stoneCentre(const fui::DeviceContext& device, int size, int point, int16_t& cx, int16_t& cy);
bool pointAt(const fui::DeviceContext& device, int size, int x, int y, int& point);
int16_t stoneRadius(int size);

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildSettings(toybox::Screen& screen, const SettingsModel& model);
void buildBoard(toybox::Screen& screen, const BoardModel& model);
void buildCount(toybox::Screen& screen, const CountModel& model);
void buildResult(toybox::Screen& screen, const ResultModel& model);

// "B+5.5", "W+12.5". One function so the result screen, the front door's
// caption and the count all say it the same way.
void formatResult(char* out, int capacity, int blackHalves, int whiteHalves);

}  // namespace goui
