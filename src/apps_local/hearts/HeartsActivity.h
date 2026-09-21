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
  // Deals done; skip a pass this hand does not have, and clear per-hand state.
  void startHand();
  void routeHandCard(int index);
  void routeButton(int button);

  // Moves whatever is not waiting on the player. Returns true if anything
  // changed, which is what decides whether the panel is repainted.
  bool advance();
  void fillSeats(heartsui::SeatView* seats) const;
  void fillLegal(heartsui::BoardModel& model) const;
  // NOT const. They format into the buffers below, and the const versions of
  // them reached those buffers through a const_cast, which is a lie about what
  // the function does written in the one place a reader checks.
  const char* statusLine();
  const char* subStatusLine();

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
  // GHOSTING IS A RESIDUE OF CHANGE, and this app changes a lot of dithered
  // area: the hand re-dithers whenever the led suit moves, the four places
  // redraw every trick, and the on-turn plaque is solid black that moves four
  // times a trick. A partial refresh leaves a little of each behind, and
  // sixty-five of them in a hand is how a panel turns grey.
  //
  // Two answers, both from docs/design-language.md's "the refresh flash as
  // punctuation": flash whenever the SCREEN changes, where a blink reads as a
  // page turn rather than an apology, and flash periodically inside a hand so
  // the residue never accumulates for thirteen tricks.
  uint8_t lastPaintedScreen = 0xFF;
  uint8_t tricksSinceFlash = 0;
  int howToPage = 0;
  // NEW GAME asks once before it throws a saved game away.
  bool confirmingNew = false;
  // Why the last tap was refused, shown for one paint. A dimmed card that does
  // nothing when tapped is correct and still unsatisfying: the dither says
  // "not this one" and nothing says why.
  const char* rejected = nullptr;
  // Where the turn indicator goes for the one paint a refusal owns the status.
  const char* turnNote = nullptr;
  char turnNoteBuffer[40] = {};

  // The three cards the player has picked to pass, as hand indices.
  bool picked[hearts::kHandSize] = {};
  int pickedCount = 0;

  // A completed trick stays on the table for a beat before it is swept, so the
  // fourth card is actually seen. Zero means nothing is pending.
  uint32_t trickShownAt = 0;
  // When the table last moved. A MEMBER, not the function-local static it was:
  // a static inside loop() outlives the activity, so leaving Hearts and coming
  // back carried the old timestamp into a new game and the first bot either
  // played instantly or waited for a beat that had already passed.
  uint32_t lastStep = 0;
  // Who took the last trick, for the line under the table.
  hearts::Seat lastWinner = hearts::Seat::South;
  bool hasLastWinner = false;

  uint32_t rng = 1;
  char statusBuffer[64] = {};
  char subStatusBuffer[48] = {};
  toybox::Interactions interactions;
};
