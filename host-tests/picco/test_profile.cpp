// The Picco's screen and inputs are ones every CrossPlay app already meets.
//
// No app in src/apps_local reads a board: they lay out from the renderer's
// size, BoardConfig::ACTIVE.viewableInsets and the theme's uiScale, and they
// read Up/Down, a tap, a swipe and the left-edge Back gesture. So a board is
// "an app works there" when those few facts match a board the apps were
// already built and photographed on. For the Picco that board is the Sticky:
// the same 800x480 panel at the same uiScale and the same (default, unmeasured)
// bezel insets, Up and Down wired, no Back/Left/Right key, and no capacitive
// Home key, so Home is the bottom-edge swipe on both. docs/picco.md says so in
// prose; this holds the SDK profile to it, so a sync that measures the Picco's
// insets or changes its pins fails here and sends someone back to the screens.
#include <BoardConfig.h>

#include <cstdio>

static int failures = 0;
#define EXPECT(cond, what)                     \
  do {                                         \
    if (!(cond)) {                             \
      std::printf("FAIL picco  %s\n", (what)); \
      failures++;                              \
    }                                          \
  } while (0)

int main() {
  using namespace BoardConfig;
  const BoardProfile& p = PICCO;
  const BoardProfile& s = STICKY;
  const BoardProfile& x = XTEINK_X4_PRO;

  EXPECT(p.displayWidth == 800 && p.displayHeight == 480, "the Picco panel is 800x480 (480x800 in the apps' portrait)");
  EXPECT(p.displayWidth == s.displayWidth && p.displayHeight == s.displayHeight,
         "the Picco panel is the Sticky's size");
  EXPECT(p.uiScale == s.uiScale && p.uiScale == x.uiScale, "the Picco uiScale is the Sticky's and the X4 Pro's");
  EXPECT(p.viewableInsets.top == s.viewableInsets.top && p.viewableInsets.right == s.viewableInsets.right &&
             p.viewableInsets.bottom == s.viewableInsets.bottom && p.viewableInsets.left == s.viewableInsets.left,
         "the Picco bezel insets are the Sticky's (both the SDK's unmeasured default)");

  EXPECT(p.input.up >= 0 && p.input.down >= 0, "the Picco has the two page keys, Up and Down");
  EXPECT(p.input.back < 0 && p.input.left < 0 && p.input.right < 0,
         "the Picco has no Back, Left or Right key (Back is the left-edge swipe)");
  EXPECT(p.input.power >= 0, "the Picco power key is wired");
  EXPECT(p.input.toggleSwitch >= 0, "the Picco slide switch is wired");
  EXPECT(!p.touch.hasHomeKey && !s.touch.hasHomeKey, "neither the Picco nor the Sticky has a capacitive Home key");
  EXPECT(p.touch.controller == TouchController::Tma525c, "the Picco touch controller is the TMA525C");

  if (failures == 0) std::printf("picco profile: ok\n");
  return failures == 0 ? 0 : 1;
}
