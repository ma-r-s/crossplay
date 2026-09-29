#pragma once

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"

// Wordle. For now it draws one seeded game so the three proposed layouts can
// be rendered side by side; the game, the download and the record come once
// one layout is chosen.
class WordleActivity final : public Activity {
 public:
  WordleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Wordle", renderer, mappedInput) {}
  ~WordleActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  toybox::Interactions interactions_;
};
