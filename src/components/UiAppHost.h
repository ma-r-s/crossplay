#pragma once
#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <PaintClock.h>

#include <atomic>
#include <cstdint>

class GfxRenderer;
class MappedInputManager;

// Owns the FreeInkApp hosting protocol every FUI screen shares, list-shaped or
// not: the font-bound render target, the app, and the uiReady handshake that
// lets the loop task route touch snapshots against the interaction table the
// render task rebuilds. FreeInkApp keeps the last complete interaction table
// published while it builds the next one, so the gate closes only when the
// old table is semantically invalid (screen reset/explicit close), not around
// an ordinary repaint. The handshake lives here exactly once so no screen
// re-implements (and mis-orders) it.
//
// UiListActivity layers list navigation on top of this; screens that are not a
// single list (sliders, prompts, state machines) inherit or hold this directly
// and keep their own input/loop logic.
class UiAppHost {
 public:
  // One shared instantiation for every FUI screen (the largest of the sizes
  // the screens used to pick individually): FreeInkApp, Screen and Frame are
  // capacity-templated, so per-screen capacities each minted a fresh copy of
  // that code in flash. The wider interaction buffer costs ~300 bytes of RAM
  // per live host, bounded by the activity stack depth.
  using UiApp = freeink::ui::FreeInkApp<24, 6>;
  using UiScreen = UiApp::ScreenType;

  explicit UiAppHost(const GfxRenderer& renderer);

  // Screen-entry reset: close the routing gate and rebind the shared theme
  // tokens (refreshed for the active UITheme + this target's fonts). Call from
  // onEnter/loadOnce before registering actions and the screen fn.
  //
  // This is also the fork's only reliable "the screen's meaning changed"
  // signal on this stack, so it now closes routing until the panel has
  // actually SHOWN the new screen, not merely until the table is rebuilt.
  // See revealed().
  void resetUi();

  // Re-derives the device context, renders into FreeInkApp's non-published
  // interaction-table generation, then opens uiReady after the first publish.
  // Once open, routing stays available during later renders through the last
  // complete published generation. Call from the render task wherever the app
  // should paint; chrome before, hints after.
  void renderUi();

  // What loop-task routing saw this pass. `routed` is true when the gate was
  // open and the snapshot carried relevant touch input — the invalidated()
  // repaint check belongs behind it, so a pending render requested elsewhere
  // is not re-requested on every idle pass.
  struct TouchRoute {
    freeink::ui::ActionEvent event{};
    freeink::ui::InputSnapshot snap{};
    bool routed = false;
    explicit operator bool() const { return static_cast<bool>(event); }
  };

  // Gated snapshot-build + route: the common loop head. withLongPress forwards
  // the SDK long-press (rows must carry InputLongPress); routeHeld forwards
  // held frames for InputDrag elements (sliders, drag-select fields).
  TouchRoute routeTouch(const MappedInputManager& input, bool withLongPress = false, bool routeHeld = false);

  // Gated route of a caller-built snapshot, for flows that need the snapshot
  // before dispatch (e.g. a handler that reads "was this a release" state).
  freeink::ui::ActionEvent route(const freeink::ui::InputSnapshot& snap);

  // Close the routing gate outside a render, e.g. when the data the
  // interaction table indexes is released mid-state. Reopens on renderUi().
  void closeRouting() { uiReady = false; }
  bool routingReady() const { return uiReady.load() && revealed(); }

  freeink::ui::GfxRendererTarget uiTarget;  // must precede `app`: the app holds a reference to it
  UiApp app;

 private:
  // Has the panel shown the screen that resetUi() announced?
  //
  // Publishing the table is not the same moment as revealing it: the caller
  // paints AFTER renderUi() returns, and displayBuffer() blocks 0.3-2s
  // (HalDisplay::HALF_REFRESH is 1720ms). Without this, a finger already
  // resting where the PREVIOUS screen's control was is routed against the new
  // screen's table for the length of a refresh.
  //
  // Keyed on resetUi() rather than on every render on purpose, and that is the
  // whole reason this is safe. An ordinary repaint -- a row highlight while a
  // finger is down, a value ticking -- does not reset, so it is never gated,
  // and the press/release pair that FreeInkApp routes across such a repaint
  // still completes. Only a screen ENTRY waits, and it waits for exactly one
  // paint from any source, so there is no threshold to tune and no path where
  // a slow refresh leaves input dead longer than the refresh itself.
  bool revealed() const { return reveal_.revealed(); }

  // Opened by the render task after publication and closed on lifecycle/state
  // resets; read by the loop task (route*).
  std::atomic<bool> uiReady{false};
  // Armed by resetUi() (screen entry), stamped by renderUi(), released by the
  // first paint that lands afterwards. The decision itself lives in
  // paintclock::RevealGate so that it can be host-tested; UiAppHost cannot be
  // built on the host at all.
  paintclock::RevealGate reveal_;
};
