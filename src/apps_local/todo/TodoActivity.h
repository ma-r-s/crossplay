#pragma once

// The starting point for a new app. Copy the directory, rename the class.
//
// Deliberately NOT registered in Shelf.cpp, so it compiles into the firmware
// but never appears on the device. Sixty-six lines of always-building template
// is worth the flash: a template that is excluded from the build rots silently,
// and the alternative starting point is copying a real app, the smallest of
// which is nineteen hundred lines.
//
// See docs/shelf.md for the three-way split to grow it into.

#include <memory>

#include "../../activities/Activity.h"

// Template for a fork-local app. Copy this directory, rename the class, and add
// one row to kGames or kApps in ../Shelf.cpp. Nothing else needs to change.
//
// Local apps run under the same resource protocol as the reader: allocate in
// onEnter(), free in onExit(), keep locals under 256 bytes, and never hardcode
// screen dimensions.
class SampleActivity final : public Activity {
 public:
  SampleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Sample", renderer, mappedInput) {}
  ~SampleActivity() override = default;

  // Factory referenced by the registry table.
  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
