#pragma once

// Knucklebones on the device. The thin layer: the renderer, input and the
// shelf. Everything it knows about the game itself lives in the three
// freestanding headers beside it, which is what lets those be tested without a
// panel.
//
// It derives from LinkActivity rather than Activity, so the two-device path is
// the same code as the solo one plus a different opponent. The game never sees
// a radio, an address or a packet: it sees a match with a turn.
//
// It owns four things and no fifth: which screen is showing, the game, the
// how-to page, and which seat this device plays. Every other question -- where
// Back goes, whose turn it is, whether a tap is allowed -- is answered by
// KnucklebonesFlow.h, so there is nowhere here for a second opinion to form.

#include <memory>

#include "../link/LinkActivity.h"
#include "../ui/ToyboxScreen.h"
#include "KnucklebonesCore.h"
#include "KnucklebonesFlow.h"

class KnucklebonesActivity final : public linkplay::LinkActivity {
 public:
  KnucklebonesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : linkplay::LinkActivity("Knucklebones", renderer, mappedInput) {}
  ~KnucklebonesActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  // --- what LinkActivity asks of a game ------------------------------------
  linkplay::PlayBase& linkState() override { return play; }
  const linkplay::PlayBase& linkState() const override { return play; }
  const char* linkGameTitle() const override { return "KNUCKLEBONES"; }
  const char* linkHeadline() const override;
  void onMatchStart(bool goesFirst) override;
  bool takeOpponentState() override;
  void onRematch() override;
  void onLinkEnded() override;
  bool matchGameOver() const override { return knucklebones::over(game); }
  // Counted here and nowhere else in a match. It used to be counted in an
  // else-if arm of gameLoop() that multiplayer returns before reaching, so no
  // link game was ever recorded and no final board was ever shown. See
  // link/LinkEndgame.h.
  void onMatchEnded() override;
  void gameLoop() override;
  void gameRender() override;

 private:
  void beginSoloMatch();
  void takeOpponentTurn();
  void goTo(knucklebones::Screen next);
  // The last finished board and the running tally, for the menu's ornament.
  // Written when a match ends, read once on entry.
  void recordResult();
  void loadHistory();

  knucklebones::Screen screen = knucklebones::Screen::Menu;
  knucklebones::Game game{};
  int howToPage = 0;
  int menuSelected = -1;
  // The seat this device plays. Always 0 against the built-in opponent, and set
  // from the coin toss in a match. Kept as a field rather than assumed so no
  // screen and no rule has to learn that seats exist.
  int seat = 0;

  // The whole shared state is the Game: 24 bytes, trivially copyable, and the
  // same description both devices already play by. There is no second wire
  // format to drift from the first.
  linkplay::Play<knucklebones::Game> play;

  // What the menu draws instead of being three rows and white space. Kept here
  // rather than in the core: the rules do not have a history, a device does.
  bool hasHistory = false;
  knucklebones::Grid lastYours{};
  knucklebones::Grid lastTheirs{};
  int wins = 0;
  int losses = 0;
  int draws = 0;
  // A match contributes to the tally exactly once, however many times the
  // result screen is drawn or backed into.
  bool resultRecorded = false;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};
