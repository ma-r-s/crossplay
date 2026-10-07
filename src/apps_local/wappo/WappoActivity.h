#pragma once

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "WappoCore.h"
#include "WappoFlow.h"
#include "WappoProgress.h"
#include "WappoScreens.h"

class WappoActivity final : public Activity {
 public:
  WappoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Wappo", renderer, mappedInput) {}
  ~WappoActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void beginGame(int level);
  void goTo(wappo::Screen next);
  void saveState();
  void loadState();
  // The level the menu offers: the one after a level just cleared.
  int resumeLevel() const;
  void onLevelWon();

  uint32_t surfaceMeaning() const override;

  wappo::Screen screen = wappo::Screen::Menu;
  wappo::Screen returnTo = wappo::Screen::Menu;  // where Levels / How To go back to
  wappo::Game game{};
  int howToPage = 0;
  int levelsPage = 0;
  wappo::Progress progress{};
  int menuSelected = -1;

  wappoui::Layout boardLayout{};
  wappoui::LevelsLayout levelsLayout{};

  toybox::Interactions interactions;
  bool interactionsReady = false;
};
