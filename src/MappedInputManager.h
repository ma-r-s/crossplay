#pragma once

#include <HalGPIO.h>

#include "util/ButtonReleaseGate.h"

class GfxRenderer;
namespace freeink {
namespace ui {
enum class ScreenEdge : uint8_t;
}
}  // namespace freeink

class MappedInputManager {
 public:
  enum class Button {
    Back,
    Confirm,
    Left,
    Right,
    Up,
    Down,
    Power,
    PageBack,
    PageForward,
    NavNext,
    NavPrevious,
    ScreenLeft,
    ScreenRight,
    ScreenUp,
    ScreenDown
  };
  enum class SwipeDir { None, Left, Right, Up, Down };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  MappedInputManager(HalGPIO& gpio, const GfxRenderer& renderer) : gpio(gpio), renderer(renderer) {}

  // The pump an activity calls only when it BLOCKED the loop (see the download
  // and sync flows). The gate settles here as well as in
  // ActivityManager::loop() because those flows are the second path to a
  // release read: they advance the latch themselves, so a gate settled only by
  // the activity stack would keep an arm standing for the whole of a blocking
  // download and swallow the Back that cancels it. settle() is idempotent for
  // a given frame's edges, so the two paths cannot disagree.
  void update() const;

  // ---- The press/release seam. See util/ButtonReleaseGate.h for the bug.
  //
  // An activity that finishes on a PRESS hands the matching RELEASE to
  // whatever is underneath, which never saw the press. ActivityManager owns
  // both calls: settleReleaseGate() once per frame before any activity reads
  // input, and swallowNextReleaseOfHeldButtons() at the moment the activity on
  // top changes. Apps need neither and must not call them -- an app that
  // guards its own release with a "did I see the press" flag is solving this
  // one caller at a time, which is what fifty-five files would otherwise have
  // to do.
  //
  // Power is deliberately NOT covered. Its release is consumed outside the
  // activity stack (sleep in main.cpp, the frontlight double-click window), so
  // an arm on it could swallow a sleep, and nothing in src/ finishes an
  // activity on a Power press.
  void settleReleaseGate() const;
  void swallowNextReleaseOfHeldButtons() const;
  // For logging and tests only.
  uint8_t heldReleaseGateMask() const { return releaseGate.armedMask(); }
#if FREEINK_CAP_TOUCH
  // X4 Pro delays a single power click until its frontlight double-click window
  // expires. The main loop supplies that one-frame event here.
  void setPowerConfirmClickFrame(const bool clicked) { powerConfirmClickFrame = clicked; }
#endif
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  // One-shot threshold event while the button is down; consumes its release.
  bool wasLongPressed(Button button, unsigned long thresholdMs) const;
  bool consumeSuppressedRelease() const;
  bool isPressed(Button button) const;
  bool hasTouch() const;
  bool wasScreenTapped(int& x, int& y) const;
  bool wasScreenTouchDown(int& x, int& y) const;
  // One-shot long-press from the SDK touch classifier, fired WHILE the finger
  // is still down (stationary contact held past the SDK threshold). Consuming
  // it suppresses the remainder of the contact — its continued hold and its
  // release edge — so the ensuing finger lift can't also tap-dismiss the popup
  // the long-press opened. The SDK owns that latch and self-clears it once the
  // contact ends.
  bool wasScreenLongPress(int& x, int& y) const;
  // A hold the SDK's classifier missed, delivered as an ordinary tap.
  //
  // InputManager::wasTouchTap has NO duration gate (InputManager.cpp:643): it
  // asks for a release edge, an unsuppressed single contact and no motion past
  // the release slop, and a stationary finger held for five seconds satisfies
  // all four. The long press is the WEAKER signal -- touchLongPressEvent is set
  // only when an update() happens to run while the finger is still down and
  // past TOUCH_LONG_PRESS_MS (:1220), and it is cleared at the top of every
  // update() (:507). An app that blocks its own loop -- an e-ink repaint, or a
  // page of BMP thumbnails decoded off the card -- can miss that window
  // outright, and then the lift arrives as a plain tap.
  //
  // Harmless while tap and hold mean the same thing. Not harmless on a grid
  // where a tap acts and a hold offers to delete: there the missed hold does
  // the one thing the user was reaching past, with no confirmation.
  //
  // Ask this AFTER wasScreenTapped() has reported a tap -- that call is what
  // latches the duration, through rememberTouchHeldTime(). True means route it
  // as a hold.
  bool tapWasHeldLong() const;
  // The fork's screen-hold threshold. Deliberately the same 500ms the SDK's
  // classifier uses for touchLongPressEvent, so the two agree about what a hold
  // is -- but held HERE because the SDK's own TOUCH_LONG_PRESS_MS is private
  // and this fork does not patch the submodule to read it.
  //
  // If the SDK ever retunes its value, this must move with it. The two drifting
  // apart is not a crash: it is holds classifying one way in the SDK and the
  // other way here, on exactly the screens where tap and hold do different
  // things. A gap, named so the next person sees it.
  static constexpr unsigned long SCREEN_HOLD_MS = 500;
  // fork-local: ignore the remainder of the in-progress contact -- its hold
  // and its release edge -- via the SDK's suppression latch. For apps that do
  // their own hold timing against geometry outside the interaction buffer
  // (Minesweeper's flag hold): after the hold fires, the finger lift must not
  // also arrive as a tap. The SDK self-clears once the contact ends.
  void swallowCurrentTouch() const;
  bool isScreenTouchHeld(int& x, int& y) const;
  // Raw release edge, also true when the contact ended in a swipe or drag-off
  // (which wasScreenTapped never reports). InputSnapshot builders forward it
  // off-target so FreeInkUI routing clears its pressed-element state.
  bool wasScreenTouchReleased() const;
  bool wasTapInRect(int x, int y, int width, int height) const;

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
  // Home-key boards use a short Home-key tap to exit; their bottom-edge swipe
  // is intentionally unused. Other boards retain the bottom-edge Home gesture.
  // The reader menu remains on its existing top-edge gesture and middle tap.
  bool wasHomeGesture() const;
  // A Home-key hold runs the configured long-press action in the reader.
  bool wasHomeKeyHold() const;
  bool wasMenuGesture() const;
  // Bottom-edge up-swipe as the reader-menu gesture (SHOW_READER_MENU's Swipe
  // Up option). Only meaningful on home-key boards, where Home lives on the
  // key and the bottom edge is free; elsewhere the same swipe is the Home
  // gesture and this returns false.
  bool wasReaderMenuSwipeUp() const;
  // Top-edge down-swipe opens the light panel when the active board actually
  // has a frontlight. ActivityManager consumes it before activity input.
  bool wasLightPanelGesture() const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  const GfxRenderer& getRenderer() const { return renderer; }
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Maps four screen-direction labels onto the two physical front-button roles
  // using the same live-orientation transform as ScreenLeft/Right/Up/Down.
  Labels mapDirectionalLabels(const char* back, const char* confirm, const char* left, const char* right,
                              const char* up, const char* down) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

  // True when the control axis is flipped relative to the physical buttons: always on touch boards,
  // or when button-only boards opt in, while the screen is currently INVERTED / LANDSCAPE_CCW.
  [[nodiscard]] bool isNavDirectionSwapped() const;

 private:
  HalGPIO& gpio;
  // Logical-to-physical button mapping depends on what the user is actually looking at: when the
  // screen is rendered rotated, the directional buttons must flip to match. The renderer is the only
  // authority on the *live* orientation (the reader rotates it and restores portrait on exit), so we
  // read it here instead of CrossPointSettings.orientation, which is just the persisted reader
  // preference and stays "rotated" even while portrait UI like home/settings is on screen.
  const GfxRenderer& renderer;

  Button mapScreenDirection(Button button) const;
  Labels mapFrontLabels(const char* back, const char* confirm, const char* left, const char* right) const;
  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
  // The ONE place a physical button index is read. Every logical button in
  // mapButton() -- including the recursive Nav*/Screen* forms and the
  // settings-driven front-button remap -- bottoms out here, so the release
  // gate covers all of them by construction rather than by a list that has to
  // be kept in step.
  bool readButton(uint8_t buttonIndex, bool (HalGPIO::*fn)(uint8_t) const) const;
  // The buttons the gate may arm: the six the activity stack navigates with.
  static constexpr uint8_t GATED_BUTTONS[] = {HalGPIO::BTN_BACK,  HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT,
                                              HalGPIO::BTN_RIGHT, HalGPIO::BTN_UP,      HalGPIO::BTN_DOWN};
  // SDK edge classification (fui::edgeSwipe) + the shared decode/held-time
  // bookkeeping; the wrappers below give each edge its board meaning.
  bool wasEdgeSwipe(freeink::ui::ScreenEdge edge) const;
  bool wasTopEdgeDownSwipe() const;
  bool wasBottomEdgeUpSwipe() const;
  // Fetch the pending swipe (if any) and map both endpoints to logical screen coords
  bool decodeSwipe(int& sx, int& sy, int& ex, int& ey) const;
#if FREEINK_CAP_TOUCH
  bool wasPowerConfirmClick() const;
#endif
  void rememberTouchHeldTime() const;
  void suppressNextRelease(Button button) const;

  // Mutable for the same reason touchHeldOverride* below are: every read
  // accessor on this class is const and the bookkeeping rides along with them.
  mutable ButtonReleaseGate releaseGate;

  mutable bool touchHeldOverrideValid = false;
  mutable unsigned long touchHeldOverrideMs = 0;
  mutable unsigned long touchHeldOverrideAt = 0;
  mutable uint16_t longPressFiredButtons = 0;
  mutable uint16_t suppressedReleaseButtons = 0;
#if FREEINK_CAP_TOUCH
  bool powerConfirmClickFrame = false;
#endif
};
