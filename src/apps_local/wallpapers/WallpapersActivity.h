#pragma once

// Wallpapers on the device. The thin layer: renderer, storage, input, shelf.
//
// The app is a face for the sleep system that already exists. It lists the
// BMPs in /wallpapers, and setting one copies it to /sleep.bmp -- the single
// image the sleep screen already checks first -- and switches the sleep mode to
// CUSTOM. So the whole feature is "give the existing wallpaper system a way to
// choose"; SleepActivity is untouched.

#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "WallpapersScreens.h"

class WallpapersActivity final : public Activity {
 public:
  WallpapersActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Wallpapers", renderer, mappedInput) {}
  ~WallpapersActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void scanLibrary();
  void loadActive();
  void computeWarning();
  // Copy /wallpapers/<name> to /sleep.bmp and switch the sleep mode to CUSTOM.
  bool setWallpaper(int index);

  std::vector<std::string> names_;  // library file names, sorted
  int selected_ = 0;
  int activeIndex_ = -1;  // which name is pinned as the sleep screen, or -1
  std::string rightLabel_;
  std::string warning_;  // free-space advisory; empty when there is room

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
