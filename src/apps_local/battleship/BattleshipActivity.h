#pragma once

#include <memory>

#include "../../components/UITheme.h"  // Rect
#include "../link/LinkActivity.h"
#include "../player/PlayerName.h"
#include "../ui/ToyboxScreen.h"
#include "BattleshipCore.h"
#include "BattleshipScreens.h"

// Battleship. Touch only, in the fork's usual three layers: BattleshipCore has
// the rules and the gunner, BattleshipScreens has the chrome, and this file is
// the only part that draws.
//
// Two grids, one screen. The big one is their water and it is the only thing
// you can tap; the small one is your own fleet and it only ever reports. That
// asymmetry is the whole interface: there is nothing to do on your own board
// once the game starts, so it stops being a control and becomes a readout.
class BattleshipActivity final : public linkplay::LinkActivity {
 public:
  BattleshipActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : linkplay::LinkActivity("Battleship", renderer, mappedInput) {}
  ~BattleshipActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 protected:
  // The whole multiplayer integration. No screens, no protocol, no notes, no
  // seat states, no tick: LinkActivity owns all of that and every game gets it
  // the same. What is left is only what battleship can answer.
  linkplay::PlayBase& linkState() override { return link; }
  const linkplay::PlayBase& linkState() const override { return link; }
  const char* linkGameTitle() const override { return "BATTLESHIP"; }
  const char* linkHeadline() const override;
  void onMatchStart(bool goesFirst) override;
  bool takeOpponentState() override;
  void onRematch() override;
  void onLinkEnded() override;
  bool matchGameOver() const override { return gameOver(); }
  // Battleship is the one game that already counted its matches: finishGame()
  // runs from fireAtAimed() when your shot sinks the last ship and from
  // takeOpponentState() when theirs does, and both of those run inside a match.
  // Calling it again here would double-count, because it has no once-only
  // guard. So this is deliberately empty. See link/LinkEndgame.h.
  void onMatchEnded() override {}
  void onLinkPhaseChanged() override { sendFleetIfReady(); }
  void drawLinkArt(const Rect& slot) override { drawMiniGrid(slot); }
  void gameLoop() override;
  void gameRender() override;

 private:
  // Which screen owns the pass. Menu is the front door, Place is setting up,
  // Board is the game.
  enum class View : uint8_t { Menu, Place, Board };

  // Where a grid landed. Derived once and used by both painting and hit
  // testing, so the drawn cells and the tappable cells cannot drift apart --
  // the rule that has caught more bugs in this fork than any other.
  struct GridGeometry {
    int originX = 0;
    int originY = 0;
    int cell = 0;
  };

  // Their water: the big grid, and the only surface that takes a tap.
  GridGeometry targetGrid() const;
  // Your own fleet, small, under it.
  GridGeometry fleetGrid() const;
  // Setting up: one grid, as big as the screen will give it.
  GridGeometry placeGrid() const;
  static int cellAt(const GridGeometry& grid, int x, int y);
  static Rect cellRect(const GridGeometry& grid, int cell);

  int boardTop() const;
  int statusPillY() const;

  void drawGridLattice(const GridGeometry& grid, bool heavyFrame) const;
  // Their water. Takes `reveal` rather than reading the game's state itself:
  // the opposing fleet is in memory the whole time and the only thing keeping
  // it secret is that this function is not told where it is. Passing the flag
  // in makes every caller say out loud whether it means to show it.
  void drawTargetGrid(bool reveal) const;
  void drawFleetGrid() const;
  void drawPlaceGrid() const;
  // Which of their ships have gone down. Names only, never damage: you are told
  // "you sank my cruiser" and nothing else, so a bar that filled up as a ship
  // took hits would hand you information the game does not owe you.
  void drawRoster(const Rect& box, bool own) const;
  // The band under the placement grid: the five ships by name and length, with
  // the one you are holding inverted. It fills what was slack, it teaches the
  // screen (the ships have names, and one of them is picked up), and tapping a
  // row selects that ship through the same function the grid uses.
  Rect placeRosterRect() const;
  void drawPlaceRoster() const;
  int placeRosterRowAt(int x, int y) const;
  // The menu's ornament: the game you would be going back to, or the last one
  // you finished. Made of the app's own material and carrying your own data,
  // which is the only kind of decoration this device allows.
  void drawMiniGrid(const Rect& slot) const;

  bshipui::StartModel startModel() const;
  bshipui::PlaceModel placeModel() const;
  bshipui::BoardModel boardModel();
  void drawStartMenu();
  void drawPlaceScreen();
  void drawBoardScreen();

  void goToMenu();
  void activateStartRow(bshipui::StartRow row);
  void routeStartMenu();
  void routePlacement();
  void routeBoard();

  // Setting up.
  void beginPlacement();
  void shuffleFleet();
  void selectShip(int shipIndex);
  void rotateSelected();
  void moveSelected(int cell);
  void handlePlaceTap(int cell);
  // The one door out of placement, so the single-player and the multiplayer
  // paths cannot diverge on what "ready" means.
  void commitFleet();
  // The one door onto the board, for the same reason. Three paths reach it in a
  // match (you sent last, they sent last, and the rematch) and the first two
  // left the narration line still saying WAITING FOR MARIO on a screen where
  // the waiting was over.
  void enterBoardView();

  // Playing.
  void handleTargetTap(int cell);
  // The single activation path for firing: the capsule and a second tap on the
  // aimed cell both land here. Two paths drift, and on a device with two ways
  // to do everything the drift is invisible until somebody uses the one that
  // was not tested.
  void fireAtAimed();
  void playComputerShot();
  void finishGame();
  void startNewGame();

  int opponentSide() const { return mySide ^ 1; }
  bool myTurn() const { return (game.turn & 1) == mySide; }
  bool gameOver() const { return bship::over(game); }
  // Whether the board may be touched at all. In a match this asks both the
  // rules and the link, which must agree -- and says so in the log when they do
  // not, because two turn counters that drift is the one desync this design can
  // still produce.
  bool canAct() const;
  // Hands the fleet over the moment the turn allows it. Placing is simultaneous
  // in the box and strictly alternating on the link, so setting up is two
  // moves: whoever goes first sends their fleet, the other answers with theirs,
  // and then the first player fires. The wait is invisible when both players
  // are still arranging, which is nearly always.
  void sendFleetIfReady();
  // The single door for "the player asked for a new game": solo it starts one,
  // in a match it asks the other device. A new door cannot pick the wrong
  // branch because there is no branch left at the call site.
  void requestNewGame();

  void reportShot(bool theirs);
  // Fills `capsule` and returns it. The label is derived at paint time from the
  // state rather than written at each transition, because there are five doors
  // into this screen and only one of them would have remembered.
  const char* capsuleLabel();
  // Not const: it fills `them` the way capsuleLabel() fills `capsule`, so the
  // returned pointer stays valid past the call.
  const char* themWord();

  void saveGame() const;
  bool loadGame();
  void saveStats() const;
  void loadStats();

  bship::Game game;
  // Your fleet while you are still arranging it, before it is handed to the
  // game. It has to live outside `game`: adopting a state from the other device
  // overwrites the whole struct, so a fleet parked in there would be wiped by
  // the opponent's first packet. Committing it is what makes it real.
  bship::Fleet myFleet;
  // Which half of `game` is yours. Zero when playing the computer; in a match
  // it is whichever side the coin toss gave you, and every drawing routine
  // reads it, so single player and multiplayer share one set of eyes.
  uint8_t mySide = 0;
  uint32_t seed = 1;

  // What a tap on the play surface means. See Activity::surfaceMeaning().
  uint32_t surfaceMeaning() const override;

  View view = View::Menu;
  int startIndex = 0;
  bool hasSavedGame = false;

  // Placement.
  int selectedShip = -1;
  bool fleetCommitted = false;

  // Playing.
  int aimCell = -1;
  // The computer owes a shot. Deferred by one loop pass so the repaint showing
  // your own shot lands before it thinks, exactly as the chess engine is.
  bool computerThinking = false;
  // What just happened, in the player's words. One line, and it is the only
  // narration this game has. It doubles as the instruction line while you are
  // setting up, because both answer the same question: what now.
  char report[48] = {};
  char capsule[24] = {};
  // What to call the opponent inside a sentence: their first word. Held rather
  // than returned by value so themWord() can hand back a pointer.
  char them[player::kMaxShortNameLength + 1] = {};

  // What the chrome left for the app's own surface, taken from the last paint.
  // The grids are derived from this rather than from the screen size, so the
  // rect that positioned the pixels is the rect that hit-tests them.
  Rect bodySlot{};

  int played = 0;
  int won = 0;
  int streak = 0;

  // ~2KB: the radio's receive ring plus the session. A member rather than a
  // local because it lives for the whole activity, and activities are
  // heap-allocated.
  linkplay::Play<bship::Game> link;
};
