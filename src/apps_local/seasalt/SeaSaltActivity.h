#pragma once

#include <memory>

#include "../../components/UITheme.h"  // Rect
#include "../link/LinkActivity.h"
#include "../ui/ToyboxScreen.h"
#include "SeaSaltBrain.h"
#include "SeaSaltCore.h"
#include "SeaSaltLink.h"
#include "SeaSaltScreens.h"

// Sea Salt & Paper. Touch only, in the fork's usual three layers: SeaSaltCore
// has the rules, SeaSaltScreens has the chrome and the card tile, and this
// file is the only part that touches hardware.
//
// The board is one screen with three tabs -- your hand, your table, theirs --
// and a dashed hint box that answers "what does this card do" at the moment
// you select it, so nobody has to remember fourteen card rules. The layout is
// the one Mario picked from the design round: constant card height to twelve
// cards, the census on every card, the piles and deck always visible.
//
// A turn's sub-decisions (keep which of two, discard to which pile, dig which
// card) are their own full screens rather than popovers: an e-ink panel wants
// one clear question per refresh.

class SeaSaltActivity final : public linkplay::LinkActivity {
 public:
  SeaSaltActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : linkplay::LinkActivity("SeaSalt", renderer, mappedInput) {}
  ~SeaSaltActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 protected:
  linkplay::PlayBase& linkState() override { return link; }
  const linkplay::PlayBase& linkState() const override { return link; }
  const char* linkGameTitle() const override { return "SEA SALT"; }
  const char* linkHeadline() const override;
  void onMatchStart(bool goesFirst) override;
  bool takeOpponentState() override;
  void onRematch() override;
  void onLinkEnded() override;
  bool matchGameOver() const override { return game.currentPhase() == seasalt::Phase::GameOver; }
  // Sea Salt already counts a match from both takeOpponentState() and
  // afterHumanAction(), so this is insurance rather than the fix -- and free
  // insurance, because countMatchEnd() is guarded by statsCounted and a second
  // call does nothing. See link/LinkEndgame.h.
  void onMatchEnded() override { countMatchEnd(); }
  void drawLinkArt(const Rect& slot) override;
  void gameLoop() override;
  void gameRender() override;

 private:
  enum class View : uint8_t { Menu, Board, Call, RoundOver, Rules };

  // --- state to screens ----------------------------------------------------
  seasaltui::StartModel startModel() const;
  seasaltui::BoardModel boardModel();
  seasaltui::CardTile tileFor(uint8_t card, int seat, bool selected) const;
  seasaltui::PileTile pileTileFor(int pile) const;
  seasaltui::RoundModel roundModel() const;
  // What the kind-group of `card` currently scores for `seat`; the corner
  // number on every tile.
  int groupPoints(uint8_t card, int seat) const;

  void drawStartMenu();
  void drawBoard();
  void drawCall();
  void drawRoundOver();
  void drawTutorial();

  // --- input ---------------------------------------------------------------
  void routeStartMenu();
  void routeBoard();
  void routeCall();
  void routeRoundOver();
  void routeTutorial();
  void activateStartRow(seasaltui::StartRow row);
  void handleCardTap(int tileIndex);

  // The hint line for the current selection, and the primary pill's label and
  // liveness. One function feeds both, so the button and the hint cannot
  // disagree about what a selection means.
  void composeHint();

  // --- flow ----------------------------------------------------------------
  void goToMenu();
  View viewForStep() const;  // where the game's current Step belongs
  void startNewGame();
  void clearSelection();
  // Plays every decision the brain owes, then returns the view to the human.
  void playOpponentTurn();
  void afterHumanAction();  // shared exit: re-derive view, queue the brain
  bool myTurn() const;
  // Whether this device may commit a move right now. In a match that is not
  // the same question as myTurn(): the transport turn must agree. Battleship
  // taught that checking they MATCH is not enough -- both must say yes.
  bool mayAct() const;
  void countMatchEnd();

  uint32_t nextSeed();
  bool loadGame();
  void saveGame() const;
  void loadStats();
  void saveStats() const;
  void refreshContinueDetail();

  seasalt::Game game;
  linkplay::Play<seasalt::Game> link;

  View view = View::Menu;
  int seat = 0;
  int menuSelected = 0;
  int tutorialPage = 0;
  int tab = 0;
  int page = 0;
  bool hasSavedGame = false;
  uint32_t seed = 1;
  int played = 0;
  int won = 0;
  bool statsCounted = false;  // this game already added to played/won

  bool opponentPending = false;

  // Selection: up to two cards of your hand, by card id.
  uint8_t sel[2] = {seasalt::kNoCard, seasalt::kNoCard};

  // What the last opponent turn did, narrated as it happened. Solo only: the
  // brain runs on this device, so the narration needs no wire format.
  char report[96] = "";

  // Where the chrome left the card grid on the last paint, per view that has
  // one. The tap router hit-tests against exactly what was drawn.
  Rect gridSlot{};

  char hint[120] = "";
  char primaryLabel[32] = "";
  bool primaryEnabled = false;
  char continueDetail[32] = "";
  mutable char headline[40] = "";
};
