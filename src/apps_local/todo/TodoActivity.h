#pragma once

#include <memory>

#include "../../activities/Activity.h"

class TodoActivity final : public Activity {
 public:
  TodoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Todo", renderer, mappedInput) {}

  ~TodoActivity() override = default;

  static std::unique_ptr<Activity> create(
      GfxRenderer& renderer,
      MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
