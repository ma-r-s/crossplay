#pragma once

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "SolitaireCore.h"
#include "SolitaireScreens.h"

// Klondike solitaire, in landscape.
//
// The only app in the fork that rotates the screen. onEnter sets the renderer
// to landscape and onExit puts it back, because the orientation is global and
// leaving it turned would rotate whatever activity comes next.
class SolitaireActivity final : public Activity {
 public:
  SolitaireActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Solitaire", renderer, mappedInput) {}
  ~SolitaireActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { Menu, Board, Won };

  void settleWin();
  void routePile(int pile, int tapY);
  void routeButton(int button);
  void newDeal();

  // The save is the board itself rather than a seed and a move list: replaying
  // moves to rebuild a position is a second implementation of the rules that
  // has to agree with the first one forever.
  void saveGame() const;
  // Records that the board moved on. Writes only every kSaveEvery moves; see
  // the note on that constant for why the every-move version was wrong.
  void touchSave();
  // Writes now if anything is pending. Called on every way out of the board.
  void flushSave();
  bool loadGame();
  void clearSave() const;

  // One byte per finished game, appended: 1 abandoned, 2 won. Same shape as
  // Connections' results file and for the same reason -- it is the cheapest
  // thing that can answer "how have I been doing".
  void recordResult(bool won) const;
  void fillStats(solitaireui::MenuModel& model) const;

  solitaire::Game game;
  solitaireui::Layout layout;
  View view = View::Menu;
  // Consumed by the next render(): a full refresh instead of the usual fast
  // one. Only a win sets it.
  bool flashOnNextPaint = false;
  bool hasGame = false;
  bool drawThree = false;
  // Pile -1 means nothing is picked up.
  int selectedPile = -1;
  int selectedCard = 0;
  bool interactionsReady = false;
  // Set on the frame a game is won, so the result is recorded exactly once.
  bool recorded = false;
  // Moves made since the board was last written to the card.
  int unsaved = 0;
  toybox::Interactions interactions;
};
