#pragma once

// The PLAYER screen: read the stored name, draw the face it describes, roll a
// word when one is tapped.
//
// Deliberately thin, the way ShelfFolderActivity is. Everything worth testing
// is somewhere else: the words and the rolling in PlayerName (freestanding),
// the face in PlayerAvatar (freestanding), the layout in PlayerScreen
// (freestanding). What is left here is storage, the render lock and one switch.

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"

class PlayerActivity final : public Activity {
 public:
  PlayerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Player", renderer, mappedInput) {}
  ~PlayerActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  toybox::Interactions interactions;
  bool interactionsReady = false;
};
