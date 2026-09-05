#pragma once

#include <memory>

#include "../../components/UITheme.h"  // Rect
#include "../link/LinkActivity.h"
#include "../player/PlayerName.h"
#include "../ui/ToyboxScreen.h"
#include "JaipurBrain.h"
#include "JaipurCore.h"
#include "JaipurLink.h"
#include "JaipurScreens.h"

// Jaipur. Touch only, in the fork's usual three layers: JaipurCore has the
// rules, JaipurScreens has the chrome, and this file is the only part that
// draws.
//
// The board is one screen and selling is not a separate view. You tap cards and
// the capsule at the bottom names the single action your selection currently
// means, live when it is legal and dithered when it is not. That falls out of
// the rules rather than being imposed on them: one market card is a take, two
// or more is an exchange, and a hand selection is a sale.
//
// Portrait, and the layout Mario picked from three rendered side by side. The
// two that lost: TABLE laid the market and the token rail out sideways, which
// left only 268px of body and cramped everything; PRICED put each pile's next
// token value on the card itself and dropped the pile zone entirely, which read
// well but threw away the depth pips -- and those pips are the round's clock,
// since three empty piles ends it.

class JaipurActivity final : public linkplay::LinkActivity {
 public:
  JaipurActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : linkplay::LinkActivity("Jaipur", renderer, mappedInput) {}
  ~JaipurActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 protected:
  linkplay::PlayBase& linkState() override { return link; }
  const linkplay::PlayBase& linkState() const override { return link; }
  const char* linkGameTitle() const override { return "JAIPUR"; }
  const char* linkHeadline() const override;
  void onMatchStart(bool goesFirst) override;
  bool takeOpponentState() override;
  void onRematch() override;
  void onLinkEnded() override;
  bool matchGameOver() const override { return game.currentPhase() == jaipur::Phase::GameOver; }
  // Counted here for a link match, and nowhere else in one: the link layer
  // stops running gameLoop() the moment the game ends (it puts up its own
  // endgame screen), so the solo path's GameOver check in gameLoop() never
  // fires for a match. See link/LinkEndgame.h.
  void onMatchEnded() override { recordResult(); }
  // The link screen's ornament, and after a match its result: it takes the
  // screen the moment the game ends, so this is where the final position is
  // reported.
  void drawLinkArt(const Rect& slot) override;
  void gameLoop() override;
  void gameRender() override;

 private:
  enum class View : uint8_t { Menu, Board, RoundOver, Rules };

  // Where every tappable thing landed. Derived once per paint and used by both
  // drawing and hit-testing, so the drawn cards and the tappable cards cannot
  // drift apart. That rule has caught more bugs in this fork than any other.
  struct Layout {
    Rect market[jaipur::kMarketSlots];
    Rect hand[jaipur::kGoodCount];
    Rect herd;                       // your camels, tappable to give in a trade
    Rect piles[jaipur::kGoodCount];  // the goods token piles, a readout
    Rect bonus[jaipur::kBonusStacks];
    Rect theirs;  // their hand size and herd, a readout
    Rect score;   // seals, both totals, the deck
    Rect body;
  };

  Layout layoutBoard(const Rect& body) const;

  // --- selection ----------------------------------------------------------
  //
  // One selection model with no modes. What is selected decides what the
  // capsule offers, and the capsule is the only thing that commits.
  void clearSelection();
  void tapMarket(int slot);
  void tapHand(int good);
  void tapHerd();
  // The move the current selection means, and whether it is legal. Both the
  // capsule's label and its action come from this one function, so the button
  // and the rules can never disagree.
  bool selectionMove(jaipur::Move& out) const;
  void capsuleLabel(char* buffer, size_t size) const;
  bool selectionEmpty() const;
  void commitSelection();

  // --- drawing ------------------------------------------------------------
  void drawBoardSurface(const Layout& layout);
  void drawMarketCard(const Rect& box, uint8_t card, bool selected, bool dim) const;
  void drawHandCounter(const Rect& box, int good, int held, int picked) const;
  void drawPile(const Rect& box, int good) const;
  void drawBonusStack(const Rect& box, int stack) const;
  void drawHerdBox(const Rect& box, int camels, int picked, bool tappable) const;
  void drawTheirSide(const Rect& box) const;
  void drawScoreStrip(const Rect& box) const;
  // The end of a round, scored line by line. Their bonus tokens were face
  // down all round and this is the screen that turns them over.
  void drawRoundSurface(const Rect& body) const;
  // The menu's ornament: the market of the game you would be going back to.
  // Made of the app's own material and carrying your own data.
  void drawMarketStrip(const Rect& slot) const;
  // How a finished match reads on the link screen: both totals and the seals.
  void drawResultArt(const Rect& slot) const;
  // The menu's price list: every good and its whole ladder of tokens. Static.
  void drawPriceList(const Rect& slot) const;

  jaipurui::StartModel startModel() const;
  jaipurui::BoardModel boardModel();
  jaipurui::RoundModel roundModel() const;
  void drawStartMenu();
  void drawBoard();
  void drawRoundOver();
  void drawTutorial();

  void goToMenu();
  // Which screen a game in this phase belongs on. CONTINUE used to send every
  // saved game to the board, including one whose round had already ended, and
  // that board has no legal move and no way forward -- the stuck state Mario
  // hit by pressing Back on the scores.
  View viewForPhase() const;
  void startNewGame();
  void routeStartMenu();
  void routeBoard();
  void routeRoundOver();
  void routeTutorial();
  void activateStartRow(jaipurui::StartRow row);

  // Which seat this device plays. Always 0 in single player; in a match it is
  // whichever side the coin toss gave us.
  int mySeat() const { return seat; }
  bool myTurn() const;
  // Whether this device may commit a move right now. In a match that is not the
  // same question as myTurn(): see JaipurLink.h.
  bool canAct() const;

  uint32_t nextSeed();

  // True when nobody is on the other end of a link, so the seat opposite is
  // played by the brain. Guards on the thing being asked rather than on a proxy
  // for it: chess once handed the engine a human opponent because the guard was
  // "the game is not over".
  bool opponentIsBrain() const { return !inMatch(); }
  // Runs the opponent's turn. Deferred by one loop pass after your move, so the
  // repaint showing what you did lands before the reply is worked out.
  void playOpponentTurn();

  // The game you left, so CONTINUE goes back to it. Debounced to the doors out
  // (leaving the board, leaving the app, falling asleep) rather than written on
  // every tap: a Jaipur round is dozens of moves and SPIFFS sectors wear out.
  bool loadGame();
  void saveGame() const;
  void refreshContinueDetail();

  // The record line under the front-door title. Counted once per finished match
  // (recordResult latches on `recorded`) and kept in jaipur.stats, its own
  // file: jaipur.sav is removed at GameOver, so a finished game is never
  // reloaded and the count cannot live in it. The pattern is battleship's.
  void recordResult();
  void saveStats() const;
  void loadStats();

  jaipur::Game game;
  linkplay::Play<jaipur::Game> link;

  View view = View::Menu;
  int seat = 0;
  int menuSelected = 0;
  int tutorialPage = 0;
  bool hasSavedGame = false;
  uint32_t seed = 1;

  // The front-door tally. Loaded once on entry, incremented when a match ends.
  int played = 0;
  int won = 0;
  // One finish is counted once. A finished match sits on screen for a while
  // (the round-over view, or the link endgame hold), so the loop sees GameOver
  // on many passes; this latches the count to the first of them.
  bool recorded = false;

  // Set when the opponent owes a move, consumed on the next loop pass.
  bool opponentPending = false;

  uint8_t selMarket = 0;                     // bitmask of market slots
  uint8_t selHand[jaipur::kGoodCount] = {};  // goods picked out of your hand
  uint8_t selCamels = 0;                     // camels picked out of your herd
  bool selCamelTake = false;                 // the take-all-camels tap

  // Where the chrome left the play surface on the last paint. Stored so the
  // tap router lays out against exactly the geometry that was drawn.
  Rect bodySlot{};

  char report[64] = "";
  // Filled by linkHeadline(), which the link layer calls while drawing and so
  // has to be const. The buffer is the only thing about it that changes.
  mutable char headline[40] = "";
  // Their first word, for the sentences that carry a name. Filled with the
  // models, and mutable for the same reason headline is.
  mutable char rivalShort[24] = "";
  char capsule[48] = "";
  char continueDetail[32] = "";
};
