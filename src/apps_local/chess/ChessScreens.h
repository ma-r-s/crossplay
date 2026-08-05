#pragma once

// The chess app's screens, as freestanding builders.
//
// No GfxRenderer, no Activity, no storage: a model in, a drawn frame out. That
// is what makes them host-testable (see host-tests/ui/), and the menu's row set
// is worth testing because it changes shape with the opponent, which is exactly
// where a screen bug hides.

#include "../ui/ToyboxScreen.h"

namespace chessui {

namespace fui = freeink::ui;

// Semantic actions. Every input path (touch, focus + Confirm) resolves to one
// of these, so a control cannot behave differently depending on which input
// reached it.
enum : fui::ActionId {
  ActionMenuRow = 1,
  ActionCloseSettings = 2,
  ActionOpenSettings = 3,
  ActionPlayAgain = 4,
  ActionStartRow = 5,
};

// The full catalogue. Which of these are on screen depends on the opponent:
// Level and PlayAs mean nothing when two people share the device, and a menu
// that shows inert rows is worse than one that shows fewer.
enum class MenuRow : uint8_t { NewGame, TakeBack, Opponent, Level, PlayAs, Hints, Count };

// Who you are playing *when you are playing alone or across one device*. This
// is a preference and it persists.
//
// Nearby is deliberately NOT one of these. Multiplayer is an action, not a
// setting: making it a saved value meant re-opening chess resumed a search
// nobody had asked for, which is the opposite of what a DS did. It lives on the
// start menu instead, where it can be seen and taken.
// PassAndPlay hands the device back and forth, so the board turns to face
// whoever is moving. FaceToFace lies flat between two people sitting opposite,
// so it never turns -- one of you reads it upside down, exactly as you would a
// real board. That is the case where only one of you owns a device.
enum class Opponent : uint8_t { Computer, PassAndPlay, FaceToFace };

struct SettingsModel {
  // Opened from the start menu rather than from the board's gear. It changes
  // what the screen offers, not just where closing it lands: NEW GAME and TAKE
  // BACK act on a game you are looking at, and from the menu there is no such
  // game -- offering them there is a row that teleports you somewhere you were
  // not going.
  bool fromMenu = false;
  Opponent opponent = Opponent::Computer;
  bool humanPlaysWhite = true;
  bool showHints = true;
  // 0 easy, 1 normal, 2 hard.
  uint8_t level = 1;
  bool canTakeBack = false;
  // A setting that cannot apply mid-game was changed while the menu was open,
  // so leaving will start a new one and the close button has to say so.
  bool restartPending = false;
  int selected = 0;
};

int visibleRows(const SettingsModel& model);
MenuRow rowAt(const SettingsModel& model, int visibleIndex);
const char* rowLabel(MenuRow row);
const char* rowValue(const SettingsModel& model, MenuRow row);

void buildSettings(toybox::Screen& screen, const SettingsModel& model);

// The start menu. Chess opens here rather than on a board, so that "click where
// it says multiplayer" is literally true: it is a row you can see, next to the
// game you were already playing, rather than something to hunt for in settings.
enum class StartRow : uint8_t { Continue, NewGame, PlayNearby, Settings, Count };

struct StartModel {
  // No saved game means no CONTINUE row, rather than one that does nothing.
  bool hasSavedGame = false;
  // Reads out the position you would be returning to, so the row is worth
  // looking at rather than being a word.
  const char* continueDetail = "";
  int selected = 0;
};

int startRows(const StartModel& model);
StartRow startRowAt(const StartModel& model, int visibleIndex);
const char* startRowLabel(StartRow row);

// Draws the menu and returns the rect left above the rows for the app's own
// artwork. The rows anchor to the bottom margin, so the composition reads as a
// page with a footer and the space above becomes one deliberate zone rather
// than slack -- the same shape the board screen uses.
//
// What goes in that zone is the position you would be returning to. That is the
// ornament rule taken literally: made of the app's own material (a board and
// the real piece art) and carrying the app's own data (your game, which changes
// every time you move). A picture of chess would be wallpaper by the third day;
// a picture of *your* chess is worth opening the app to see.
fui::Rect buildStartMenu(toybox::Screen& screen, const StartModel& model);

struct BoardModel {
  const char* status = "";
  // The status capsule is only a button when the game is over. While one is
  // running it says whose move it is and must not respond to a tap.
  bool gameOver = false;
  // Who you are playing, or null against the engine. Only the face is drawn
  // from it -- the capsule already says the name -- and only in a match, so
  // solo chess keeps the full-width capsule it always had.
  const char* theirName = nullptr;
};

// Draws the board screen's chrome and returns the rect left for the board
// itself. The play surface is app-drawn by design: FreeInkUI hands an app a
// slot and stays out of it.
fui::Rect buildBoardChrome(toybox::Screen& screen, const BoardModel& model);

}  // namespace chessui
