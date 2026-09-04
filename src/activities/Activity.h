#pragma once
#include <Logging.h>

#include <cassert>
#include <memory>
#include <string>
#include <utility>

#include "ActivityManager.h"  // for using the ActivityManager singleton
#include "ActivityResult.h"
#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "RenderLock.h"
#include "RevealedInteractions.h"
#include "util/ScreenshotInfo.h"

class Activity {
  friend class ActivityManager;

 protected:
  std::string name;
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;

  ActivityResultHandler resultHandler;
  ActivityResult result;

  paintclock::SurfaceGate surfaceGate;

 public:
  explicit Activity(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : name(std::move(name)), renderer(renderer), mappedInput(mappedInput) {}
  virtual ~Activity() = default;
  virtual void onEnter();
  virtual void onExit();
  virtual void loop() {}

  virtual void render(RenderLock&&) {}

  // If immediate is true, the update will be triggered immediately.
  // Otherwise, it will be deferred until the end of the current loop iteration.
  virtual void requestUpdate(bool immediate = false);

  // Request an immediate render and block until it completes.
  virtual void requestUpdateAndWait();

  virtual bool skipLoopDelay() { return false; }
  virtual bool preventAutoSleep() { return false; }
  // Exclusive storage activities suspend global controls and normal activity
  // transitions so no filesystem code races a raw SD-card owner.
  virtual bool requiresExclusiveStorageLoop() const { return false; }
  virtual bool isReaderActivity() const { return false; }
  // Returns true when the activity schedules its own forced refresh.
  virtual bool handleForcedRefresh() { return false; }
  virtual bool isHomeActivity() const { return false; }
  virtual bool handleHomeGesture() { return false; }
  virtual ScreenshotInfo getScreenshotInfo() const { return {}; }

  // ---- The play-surface reveal gate. See lib/GfxRenderer/RevealedInteractions.h.
  //
  // An activity that hit-tests a play surface against GEOMETRY (an 80-cell
  // board does not fit a 24-slot interaction table) never reaches route(), so
  // toybox::Interactions cannot see those taps. What such a tap MEANS is not
  // in the table either: Minesweeper's FLAG capsule is registered with an
  // identical rect, action, value and inputMask and flips only StateSelected,
  // which the digest ignores as paint -- while the mode bit it sets is what
  // decides whether a grid tap digs or flags.
  //
  // An activity opts in by overriding surfaceMeaning() and guarding its
  // hit-test with surfaceRevealed(). Both call surfaceMeaning(), so there is
  // ONE definition of what a tap means rather than two expressions to keep in
  // step -- a gate whose two halves drift apart is silently always open, which
  // looks exactly like a gate that works.
  //
  // The build-side stamp is NOT the app's job: ActivityManager takes it around
  // the single render dispatch, so a new screen cannot forget it, and it is
  // taken on EVERY render including the ones that draw no play surface -- skip
  // those and builtAtPaint_ goes stale and the gate is silently always open.
  // The default meaning is 0 for every activity that does not override, and
  // 0 == 0 always routes, so this is a no-op for everything not opted in.
  //
  // THE TRAP, and it fails silently in the open direction: surfaceMeaning() is
  // evaluated just BEFORE render() runs, so it must be derived only from state
  // that loop() has already settled -- a mode flag, a screen enum, a seat, a
  // turn. Never from something render() itself writes. Four apps cache the
  // pixel-to-cell layout in a member during render (MurdleActivity::gridLayout,
  // murdleui's clue layout, DungeonActivity::layout and pickerLayout,
  // solitaire's): hashing one of those reads the PREVIOUS frame's layout at
  // stamp time and the new one at tap time, so the gate compares against a
  // frame that was never on the panel. Hash what DETERMINES the layout instead
  // -- the view, the puzzle index -- which loop() owns and render() only reads.

  // What a tap on this activity's play surface means right now. Fold several
  // values together with paintclock::mixMeaning starting from kMeaningSeed.
  //
  // Exactly three things belong in one, and the temptation to add a fourth is
  // what makes this gate worse than the bug:
  //
  //   1. Surface identity and whether it is live at all -- the screen or view
  //      enum, plus the dead/live bit (over, yourTurn, generating,
  //      computerThinking). This is where the changes nobody's finger caused
  //      show up: an opponent's move arriving over the link, a generator
  //      finishing, an auto-transition to a result screen.
  //   2. The pixel-to-cell map -- anything the coordinate arithmetic ITSELF
  //      reads. Chess's whiteAtBottom() turns the board 180 degrees on every
  //      half-move in Pass-and-Play; checkers' seat flips it; battleship's two
  //      grids have different cell sizes.
  //   3. Mode bits that reinterpret EVERY cell at once. Minesweeper's flagMode
  //      is the whole worked example.
  //
  // Leave OUT the surface's contents and the user's transient selection, and
  // leave them out for the same reason rather than by a causality argument:
  // neither moves which cell a pixel is, and the handler consulting them is
  // the category the table digest already ignores. The separating test is
  // uniform versus local. flagMode reinterprets all eighty cells under a
  // finger resting anywhere; a selected square reinterprets exactly the one
  // cell the player just deliberately touched. Fold a selection in and you
  // gate the second half of select-then-move, which is every move in chess and
  // checkers, and aim-then-fire, which is how battleship shoots. Fold the
  // contents in and you gate every consecutive dig, entry and mark.
  virtual uint32_t surfaceMeaning() const { return 0; }

  // Is a tap on the play surface safe to act on? Guard every geometry
  // hit-test with this.
  bool surfaceRevealed() const { return surfaceGate.routable(surfaceMeaning()); }

  // Called by ActivityManager immediately before render(). Not for apps.
  void noteSurfaceBuilt() { surfaceGate.noteBuilt(surfaceMeaning()); }

  // Start a new activity without destroying the current one
  // Note: requestUpdate() will be invoked automatically once resultHandler finishes
  void startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler);

  // Set the result to be passed back to the previous activity when this activity finishes
  void setResult(ActivityResult&& result);

  // Finish this activity and return to the previous one on the stack (if any)
  void finish();

  // Convenience method to facilitate API transition to ActivityManager
  // TODO: remove this in near future
  void onGoHome(HomeMenuItem item = HomeMenuItem::NONE);
  void onSelectBook(const std::string& path);
};
