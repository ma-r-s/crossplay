#pragma once

// Bezel-margin ruler. The enclosure glass overlaps the panel's active area by
// a per-unit amount (community-measured 5-11 rows on top; see upstream
// discussion #618), and BoardConfig::ViewableInsets carries values that were
// never measured on the X4 Pro. This screen draws a numbered 1px tick ruler on
// each edge: looking straight on, the smallest visible number on an edge IS
// the count of pixels the bezel hides there. Feed the four numbers back into
// the board profile's viewableInsets.

#include <memory>

#include "../../activities/Activity.h"

class BezelActivity final : public Activity {
 public:
  BezelActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Bezel", renderer, mappedInput) {}
  ~BezelActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
