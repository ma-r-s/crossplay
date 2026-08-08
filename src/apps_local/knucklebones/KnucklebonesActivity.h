#pragma once

// Knucklebones on the device. The thin layer: the renderer, storage, input and
// the shelf. Everything it knows about the game itself lives in the three
// freestanding headers beside it, which is what lets those be tested without a
// panel.
//
// It owns exactly three things, and no fourth: which screen is showing, the
// game, and the how-to page. Every other question -- where Back goes, whose
// turn it is, whether a tap is allowed -- is answered by KnucklebonesFlow.h, so
// there is nowhere here for a second opinion to form.

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "KnucklebonesCore.h"
#include "KnucklebonesFlow.h"

class KnucklebonesActivity final : public Activity {
 public:
  KnucklebonesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Knucklebones", renderer, mappedInput) {}
  ~KnucklebonesActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void beginMatch();
  void takeOpponentTurn();
  void goTo(knucklebones::Screen next);

  knucklebones::Screen screen = knucklebones::Screen::Menu;
  knucklebones::Game game{};
  int howToPage = 0;
  int menuSelected = -1;
  // The seat this device plays. Always 0 against the built-in opponent; kept as
  // a field rather than assumed so the two-device path can set it to 1 without
  // any screen or rule learning that seats exist.
  int seat = 0;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};
