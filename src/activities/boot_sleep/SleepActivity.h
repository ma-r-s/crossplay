#pragma once
#include "activities/Activity.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;

 private:
  // De-ghost the panel before painting a sleep image: HALF/FULL are both
  // single-shot absolute paints with no flush, so a mostly-black inverted sleep
  // image ghosts the prior high-contrast screen (a reader page) through. One
  // paint to white resets the baseline so the image paint lands clean (#2588).
  void flushPanelWhite() const;
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap) const;
  void renderLastScreenSleepScreen() const;
  void renderBlankSleepScreen() const;

  bool fromTimeout = false;
};
