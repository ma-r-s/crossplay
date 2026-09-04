#pragma once

// Toy Battle on the device. The thin layer: renderer, input, shelf.
//
// Deliberately small for now. It exists so the board can be photographed at
// native size and a layout chosen; the link path, the how-to deck and saved
// games come after that decision, not before it.

#include <memory>

#include "../../MappedInputManager.h"
#include "../../activities/Activity.h"
#include "../link/LinkActivity.h"
#include "../ui/ToyboxScreen.h"
#include "ToyBattleBrain.h"
#include "ToyBattleCore.h"
#include "ToyBattleFlow.h"
#include "ToyBattleMenus.h"

class ToyBattleActivity final : public linkplay::LinkActivity {
 public:
  ToyBattleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : linkplay::LinkActivity("ToyBattle", renderer, mappedInput) {}
  ~ToyBattleActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 protected:
  // --- what the link layer asks of a game ----------------------------------
  linkplay::PlayBase& linkState() override { return link; }
  const linkplay::PlayBase& linkState() const override { return link; }
  const char* linkGameTitle() const override { return "TOY BATTLE"; }
  const char* linkHeadline() const override;
  void onMatchStart(bool goesFirst) override;
  bool takeOpponentState() override;
  void onRematch() override;
  void onLinkEnded() override;
  bool matchGameOver() const override;
  void gameLoop() override;
  void gameRender() override;
  void drawLinkArt(const Rect& area) override;

 private:
  void beginGame();
  tbui::MenuModel menuModel() const;
  void openMenu();
  void cycleSetupRow(tbui::SetupRow row);
  void refreshSaveLine();
  void takeOpponentTurn();
  void goTo(toybattle::Screen next);
  const char* promptText() const;

  // The line under the title is the app's one channel for saying anything, so
  // it carries two things: the question being asked, and -- until the next
  // action -- what just happened. `notice` is that second one, and it beats the
  // question while it stands.
  void say(const char* message);

  toybattle::Screen screen = toybattle::Screen::Menu;
  // What was chosen before the game started. Held here rather than read off the
  // Game, because two of the three are decided before a Game exists and the
  // difficulty is not part of a position at all.
  toybattle::Options options{};
  toybattle::Game game{};
  toybattle::Draft draft{};
  const char* notice = nullptr;
  int menuSelected = 0;
  int setupSelected = 0;
  int howToPage = 0;
  // Which of the three rules treatments is on screen. Scaffolding for the
  // side-by-side pick, cycled by the Up button on the HOW TO PLAY screen; it
  // goes with the two variants Mario does not choose.
  int mapPage = 0;
  uint8_t seat = 0;

  // What the front door offers to go back to. `preview` is the board the menu
  // draws: the saved position if there is one, an empty board otherwise, so the
  // ornament is always the map you are about to play.
  bool hasSave = false;
  char saveDetail[48] = {};
  toybattle::Game preview{};
  int played = 0;
  int won = 0;
  // A finish is recorded once, in one place. render() runs more than once per
  // move, so a screen builder that counted a win would count several.
  bool recorded = false;

  // A move is only sent when the rules AND the link both say it is your turn.
  // Asking whether they agree is not the same question and passes on the
  // opponent's turn, which is how another game in this fork fired on the wrong
  // side of the board.
  bool canAct() const;
  void commitMove();
  void requestNewGame();
  void saveGame();
  bool loadGame();

  // The shared state is `Game` itself, which is already the wire format: 148
  // bytes, trivially copyable, with an exact-size assert. Nothing is serialised
  // and nothing is described twice.
  linkplay::Play<toybattle::Game> link;
  // A zeroed Game has no legal move, so a follower that has not been dealt to
  // yet would read as a finished game -- and the layer latches the rematch
  // screen the moment it sees one, permanently. This says "nothing has been
  // dealt", which is a different thing from "the game is over".
  bool dealt = false;
  mutable char headline[48] = {};
};
