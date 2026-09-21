#pragma once

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "HeartsBrain.h"
#include "HeartsCore.h"
#include "HeartsScreens.h"

// Hearts, in landscape.
//
// The second app in the fork to rotate the screen, for the same reason as the
// first: four seats and a thirteen-card fan want a wide panel far more than a
// page of text wants a tall one. onEnter sets the orientation and onExit puts
// it back, because it is global.
class HeartsActivity final : public Activity {
 public:
  HeartsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Hearts", renderer, mappedInput) {}
  ~HeartsActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { Menu, Board, Score, HowTo };

  void newGame();
  void routeHandCard(int index);
  void routeButton(int button);

  // Moves whatever is not waiting on the player. Returns true if anything
  // changed, which is what decides whether the panel is repainted.
  bool advance();
  void fillSeats(heartsui::SeatView* seats) const;
  void fillLegal(heartsui::BoardModel& model) const;
  const char* statusLine() const;
  const char* subStatusLine() const;

  void saveGame() const;
  bool loadGame();
  void clearSave() const;
  void recordResult(int place) const;
  void fillStats(heartsui::MenuModel& model) const;

  hearts::Game game;
  heartsui::Layout layout;
  View view = View::Menu;
  hearts::Skill skill = hearts::Skill::Sharp;
  bool hasGame = false;
  bool interactionsReady = false;
  bool flashOnNextPaint = false;
  int howToPage = 0;

  // The three cards the player has picked to pass, as hand indices.
  bool picked[hearts::kHandSize] = {};
  int pickedCount = 0;

  // A completed trick stays on the table for a beat before it is swept, so the
  // fourth card is actually seen. Zero means nothing is pending.
  uint32_t trickShownAt = 0;
  // Who took the last trick, for the line under the table.
  hearts::Seat lastWinner = hearts::Seat::South;
  bool hasLastWinner = false;

  uint32_t rng = 1;
  char statusBuffer[64] = {};
  char subStatusBuffer[48] = {};
  toybox::Interactions interactions;
};
