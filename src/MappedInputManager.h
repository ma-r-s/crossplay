#pragma once

#include <HalGPIO.h>

class GfxRenderer;

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward, NavNext, NavPrevious };
  enum class SwipeDir { None, Left, Right, Up, Down };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  MappedInputManager(HalGPIO& gpio, const GfxRenderer& renderer) : gpio(gpio), renderer(renderer) {}

  void update() const { gpio.update(); }
  // Advances the swallowCurrentTouch() latch. Call once per frame from the main
  // loop (where gpio.update() is guaranteed); it holds through the swallowed
  // contact's release-edge frame, then clears on the following idle frame so a
  // fresh touch is delivered normally. Not folded into update() because some
  // activities never call update() yet still read touch (ConfirmationActivity).
  void advanceTouchSwallow() const;
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool hasTouch() const;
  // True on boards with a capacitive home key (X4 Pro), where the bottom-edge
  // up-swipe is the reader-menu gesture rather than the exit-to-home gesture.
  bool hasHomeKey() const { return gpio.hasHomeKey(); }
  bool wasScreenTapped(int& x, int& y) const;
  bool wasScreenTouchDown(int& x, int& y) const;
  // Overload reporting the contact's current held duration (finger still down),
  // so a hold can be detected and fired WHILE pressed rather than on release.
  bool wasScreenTouchDown(int& x, int& y, unsigned long& heldMs) const;
  // Ignore the remainder of the in-progress touch contact — its continued hold
  // and its release edge. Used after a long-press fires while the finger is
  // still down, so the ensuing finger lift can't also tap-dismiss the popup the
  // long-press opened. Auto-clears once the contact ends and a new one begins.
  void swallowCurrentTouch() const;
  bool isScreenTouchHeld(int& x, int& y) const;
  // Raw release edge, also true when the contact ended in a swipe or drag-off
  // (which wasScreenTapped never reports). InputSnapshot builders forward it
  // off-target so FreeInkUI routing clears its pressed-element state.
  bool wasScreenTouchReleased() const;
  bool wasTapInRect(int x, int y, int width, int height) const;
  bool wasListItemTapped(int& index, int itemCount, int selectedIndex, int listTop, int listHeight,
                         bool hasSubtitle) const;
  bool wasListItemTouchedDown(int& index, int itemCount, int selectedIndex, int listTop, int listHeight,
                              bool hasSubtitle) const;

  // Combined touch interaction for a band of equal rows with caller-supplied
  // geometry — the shared hit-test for lists the theme helpers above do not
  // cover (custom row heights, option prompts, menus). Down = a held
  // tap-candidate is on a row (update the selection highlight); Tap = a tap
  // released on one (activate). rowHeight limits the hit to the top rowHeight
  // px of each step (0 = the full step, no gap band).
  enum class RowTouch : uint8_t { None, Down, Tap };
  RowTouch rowTouch(int& row, int top, int rowStep, int rowCount, int xStart = 0, int xEnd = INT32_MAX,
                    int rowHeight = 0) const;
  // Horizontal variant for side-by-side button pairs (confirmation prompts).
  RowTouch colTouch(int& col, int left, int colStep, int colCount, int yStart, int yEnd, int colWidth = 0) const;

  SwipeDir wasSwipe() const;
  // Back = left-to-right swipe anchored at the left edge. Public so swipe-mode
  // page turns (reader) can exclude it from a plain SwipeDir::Right.
  bool wasBackGesture() const;
  // Exit-to-home intent. Boards with a capacitive home key (X4 Pro) use the
  // key's press edge; everywhere else it's the bottom-edge up-swipe.
  bool wasHomeGesture() const;
  // Contextual menu intent (the reader menu). Home-key boards move this to the
  // bottom-edge up-swipe (freed by the home key); others keep the top-edge
  // down-swipe.
  bool wasMenuGesture() const;
  // Frontlight quick panel: top-edge down-swipe, only on home-key boards where
  // that edge is no longer the menu gesture.
  bool wasLightPanelGesture() const;
  // Reader-menu shortcut: a long press of the capacitive home key (home-key
  // boards only). A short tap goes home; the hold opens the reader menu.
  bool wasReaderMenuHold() const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  const GfxRenderer& getRenderer() const { return renderer; }
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

  // True when the control axis is flipped relative to the physical buttons: the user opted into
  // orientation-following front buttons AND the screen is *currently rendered* rotated (INVERTED /
  // LANDSCAPE_CCW). Keyed on the live renderer orientation rather than the persisted reader setting,
  // so portrait UI (home, settings) never swaps while the reader and its menus do.
  [[nodiscard]] bool isNavDirectionSwapped() const;

 private:
  HalGPIO& gpio;
  // Logical-to-physical button mapping depends on what the user is actually looking at: when the
  // screen is rendered rotated, the directional buttons must flip to match. The renderer is the only
  // authority on the *live* orientation (the reader rotates it and restores portrait on exit), so we
  // read it here instead of CrossPointSettings.orientation, which is just the persisted reader
  // preference and stays "rotated" even while portrait UI like home/settings is on screen.
  const GfxRenderer& renderer;

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
  bool wasTopEdgeDownSwipe() const;
  bool wasBottomEdgeUpSwipe() const;
  // Fetch the pending swipe (if any) and map both endpoints to logical screen coords
  bool decodeSwipe(int& sx, int& sy, int& ex, int& ey) const;
  bool listItemFromPoint(int x, int y, int& index, int itemCount, int selectedIndex, int listTop, int listHeight,
                         bool hasSubtitle) const;
  void rememberTouchHeldTime() const;

  mutable bool touchHeldOverrideValid = false;
  mutable unsigned long touchHeldOverrideMs = 0;
  mutable unsigned long touchHeldOverrideAt = 0;
  // swallowCurrentTouch() state: while active, all touch-edge readers report
  // nothing; touchSwallowWasDown tracks the prior frame's contact so the latch
  // clears only after the release-edge frame has passed.
  mutable bool touchSwallowActive = false;
  mutable bool touchSwallowWasDown = false;
};
