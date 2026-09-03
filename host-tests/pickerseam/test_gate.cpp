// The press/release seam, frame by frame. See run.sh for why this suite exists
// and what it is the only check for.
//
// The device produces edges from LEVELS: InputManager::applyStateChange does
// `pressedEvents = state & ~currentState; releasedEvents = currentState &
// ~state;` and then commits the state. So this file writes button levels on a
// timeline and derives the edges the same way the hardware does, rather than
// hand-writing edge masks -- a test that hand-feeds the masks it expects
// cannot falsify the arithmetic it is checking.
//
// The frame loop below is also the WIRING under test, not just the gate: it
// runs settle() before the screen reads input and arms at the moment the
// screen on top changes, which is the order ActivityManager::loop() uses. Get
// that order wrong in either place and cases 1 and 3 go red.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "ButtonReleaseGate.h"

namespace {

int checks = 0;
int failed = 0;

void ok() { checks++; }
void bad(const std::string& what) {
  checks++;
  failed++;
  std::printf("FAIL pickerseam  %s\n", what.c_str());
}

void expect(const bool got, const bool want, const std::string& what) {
  if (got == want) {
    ok();
    return;
  }
  bad(what + ": got " + (got ? "true" : "false") + ", wanted " + (want ? "true" : "false"));
}

// Button indices, matching HalGPIO's. Not included from there: this suite
// builds with nothing but the standard library on purpose.
constexpr uint8_t BTN_BACK = 0;
constexpr uint8_t BTN_CONFIRM = 1;
constexpr uint8_t BTN_DOWN = 5;

constexpr uint8_t bit(const uint8_t index) { return static_cast<uint8_t>(1u << index); }

// InputManager's edge derivation, and nothing else. One instance per test.
struct Latch {
  uint8_t level = 0;
  uint8_t pressEdges = 0;
  uint8_t releaseEdges = 0;

  void poll(const uint8_t nextLevel) {
    pressEdges = static_cast<uint8_t>(nextLevel & ~level);
    releaseEdges = static_cast<uint8_t>(level & ~nextLevel);
    level = nextLevel;
  }
};

// One main-loop pass, in ActivityManager's order.
//
//   gpio.update()                  -> latch.poll()
//   mappedInput.settleReleaseGate()
//   currentActivity->loop()        -> the reads below
//   ...the pending push/pop        -> arm, with the buttons down right then
struct Rig {
  Latch latch;
  ButtonReleaseGate gate;
  // What the screen on top saw this frame.
  uint8_t sawPress = 0;
  uint8_t sawRelease = 0;

  // Runs a frame at the given button levels. `finishesOnPress` is the screen
  // acting on a press edge and handing control back, the way
  // WifiSelectionActivity cancels on wasPressed(Back).
  void frame(const uint8_t level, const uint8_t finishesOnPress = 0) {
    latch.poll(level);
    gate.settle(latch.pressEdges, latch.releaseEdges, latch.level);

    sawPress = latch.pressEdges;
    sawRelease = 0;
    for (const uint8_t index : {BTN_BACK, BTN_CONFIRM, BTN_DOWN}) {
      if ((latch.releaseEdges & bit(index)) && !gate.swallowsRelease(index)) {
        sawRelease = static_cast<uint8_t>(sawRelease | bit(index));
      }
    }

    if (finishesOnPress && (latch.pressEdges & finishesOnPress)) {
      uint8_t held = 0;
      for (const uint8_t index : {BTN_BACK, BTN_CONFIRM, BTN_DOWN}) {
        if ((latch.level | latch.pressEdges) & bit(index)) held = static_cast<uint8_t>(held | bit(index));
      }
      gate.arm(held);
    }
  }
};

// -- 1. The measured bug ----------------------------------------------------
//
// From commit 168587fb, probing every loop pass: the picker cancels on the
// press at 5607ms, five HackerNews passes go by with released=0, and the
// release lands at 5684ms and shuts the app. Five clean passes is the point --
// it is one physical press producing two logical events, not a latch read
// twice.
void oneBackPressIsOneEvent() {
  Rig rig;
  rig.frame(0);
  rig.frame(bit(BTN_BACK), /*finishesOnPress=*/bit(BTN_BACK));  // the picker cancels here
  expect(rig.sawPress == bit(BTN_BACK), true, "the picker sees the Back press");

  for (int pass = 0; pass < 5; pass++) {
    rig.frame(bit(BTN_BACK));  // still held, the five clean passes
    expect(rig.sawRelease == 0, true, "no release while Back is still down");
  }

  rig.frame(0);  // the release, 77ms later, at the caller
  expect(rig.sawRelease == 0, true, "the caller does not act on the picker's release");
  expect(rig.gate.swallowsRelease(BTN_BACK), true, "the arm is still up on the frame that carries the release");
}

// -- 2. Back still works, which is the failure mode to fear -----------------
//
// A guard that swallows too much leaves Back dead, which reads as a frozen
// device and is worse than the bug. The second press must land.
void theNextBackIsHonoured() {
  Rig rig;
  rig.frame(bit(BTN_BACK), bit(BTN_BACK));
  rig.frame(0);
  expect(rig.sawRelease == 0, true, "first release swallowed");

  rig.frame(0);
  expect(rig.gate.armedMask() == 0, true, "the arm is dropped once nothing is pending");

  rig.frame(bit(BTN_BACK));
  rig.frame(0);
  expect(rig.sawRelease == bit(BTN_BACK), true, "the user's own second Back is acted on");
}

// -- 3. A press clears the arm unconditionally ------------------------------
//
// The anti-freeze invariant. However an arm was set, and whatever else the
// firmware does or fails to do, ONE press takes it back off -- so a button
// cannot stay dead for more than a single press/release cycle. Driven here
// through the pathological path: the release edge is never delivered because
// the button is pressed again in the very same frame the gate would have seen
// it settle.
void aPressAlwaysClearsTheArm() {
  ButtonReleaseGate gate;
  gate.arm(bit(BTN_BACK));
  // Press edge and level both set, no release edge: the arm must not survive.
  gate.settle(bit(BTN_BACK), 0, bit(BTN_BACK));
  expect(gate.swallowsRelease(BTN_BACK), false, "a press edge clears the arm even while the button is down");

  gate.settle(0, bit(BTN_BACK), 0);
  expect(gate.swallowsRelease(BTN_BACK), false, "and that release is the user's");
}

// -- 4. A release that never arrives here does not wedge the gate ----------
//
// The activity stack does not run on every main-loop pass (sleep guards, a
// blocking fetch that pumps input itself). If the release edge goes by
// unseen, the arm has to clear on its own rather than wait for a press.
void aLostReleaseClearsOnItsOwn() {
  ButtonReleaseGate gate;
  gate.arm(bit(BTN_BACK));
  gate.settle(0, 0, 0);  // button already up, no edge to be had
  expect(gate.armedMask() == 0, true, "an arm with nothing pending is dropped");
}

// -- 5. The arm is per button ----------------------------------------------
void otherButtonsAreUntouched() {
  Rig rig;
  // Confirm is held (the screen was opened with it); Back is not involved.
  rig.frame(bit(BTN_CONFIRM), bit(BTN_CONFIRM));
  expect(rig.gate.swallowsRelease(BTN_CONFIRM), true, "Confirm is armed");
  expect(rig.gate.swallowsRelease(BTN_BACK), false, "Back is not armed by a Confirm press");

  // A Back press/release entirely inside the new screen, while Confirm is
  // still down, is the new screen's to act on.
  rig.frame(bit(BTN_CONFIRM) | bit(BTN_BACK));
  rig.frame(bit(BTN_CONFIRM));
  expect(rig.sawRelease == bit(BTN_BACK), true, "an unrelated button's release is not swallowed");

  rig.frame(0);
  expect(rig.sawRelease == 0, true, "and Confirm's own release still is");
}

// -- 6. A button already down at the swap, that the swap was not about ------
//
// The user is holding Down to scroll when something else (a timeout, a link
// message, a touch) changes the screen. The press went to the old screen, so
// the release is not the new screen's either.
void aHeldButtonAtAnUnrelatedSwapIsArmed() {
  Latch latch;
  ButtonReleaseGate gate;
  latch.poll(bit(BTN_DOWN));
  gate.settle(latch.pressEdges, latch.releaseEdges, latch.level);
  // ...the screen changes for a reason that is not this button.
  gate.arm(static_cast<uint8_t>(latch.level | latch.pressEdges));

  latch.poll(0);
  gate.settle(latch.pressEdges, latch.releaseEdges, latch.level);
  expect(gate.swallowsRelease(BTN_DOWN), true, "a button held across any swap is armed");
}

// -- 7. Two swaps in a row --------------------------------------------------
//
// The picker opens the keyboard on a Confirm press and the keyboard hands back
// on a Back press. Arming twice must not leave anything standing.
void armingTwiceStillClears() {
  Rig rig;
  rig.frame(bit(BTN_CONFIRM), bit(BTN_CONFIRM));
  rig.frame(0);
  expect(rig.sawRelease == 0, true, "the pushed screen ignores the Confirm release");

  rig.frame(bit(BTN_BACK), bit(BTN_BACK));
  rig.frame(0);
  expect(rig.sawRelease == 0, true, "the popped-to screen ignores the Back release");

  rig.frame(0);
  expect(rig.gate.armedMask() == 0, true, "nothing left armed");

  rig.frame(bit(BTN_BACK));
  rig.frame(0);
  expect(rig.sawRelease == bit(BTN_BACK), true, "Back works after two swaps");
}

// -- 8. A tap-fast press whose release lands the very next frame -------------
void aFastReleaseIsStillSwallowed() {
  Rig rig;
  rig.frame(bit(BTN_BACK), bit(BTN_BACK));
  rig.frame(0);  // released on the very next poll
  expect(rig.sawRelease == 0, true, "a fast press/release pair still leaves nothing for the caller");
}

// -- 9. The gate is inert until something arms it ---------------------------
void anUnarmedGateChangesNothing() {
  Rig rig;
  for (int pass = 0; pass < 3; pass++) {
    rig.frame(bit(BTN_BACK));
    rig.frame(0);
    expect(rig.sawRelease == bit(BTN_BACK), true, "an unarmed gate passes every release through");
  }
}

// -- 10. Two settles in one frame agree -------------------------------------
//
// There are two paths to a release read and both settle: ActivityManager::loop
// on a normal frame, and MappedInputManager::update() on the pump an activity
// runs while it has BLOCKED the loop (a download, the sync flow). They can both
// happen against one frame's edges, so settle has to be idempotent or the two
// paths disagree about the same press.
void twoSettlesInOneFrameAgree() {
  ButtonReleaseGate once;
  ButtonReleaseGate twice;
  once.arm(bit(BTN_BACK));
  twice.arm(bit(BTN_BACK));

  const uint8_t frames[][3] = {
      {0, 0, bit(BTN_BACK)}, {0, bit(BTN_BACK), 0}, {0, 0, 0}, {bit(BTN_BACK), 0, bit(BTN_BACK)}};
  for (const auto& f : frames) {
    once.settle(f[0], f[1], f[2]);
    twice.settle(f[0], f[1], f[2]);
    twice.settle(f[0], f[1], f[2]);
    expect(once.armedMask() == twice.armedMask(), true, "settling twice in one frame lands where settling once does");
  }
}

// -- 11. A blocking download can still be cancelled -------------------------
//
// The shape that made this worth closing: the picker hands back on a Back
// press and the caller goes straight into a flow that blocks the loop and
// pumps input itself (OpdsBookBrowser, Xkcd and Trivia all read
// wasReleased(Back) to cancel a download that way). If the arm outlived the
// handover, the Back that cancels the download would be swallowed too.
void aBlockingFlowCanStillCancel() {
  ButtonReleaseGate gate;
  gate.arm(bit(BTN_BACK));  // the picker cancelled on the press; Back still down

  // The blocking flow's own pump, frame by frame.
  gate.settle(0, 0, bit(BTN_BACK));  // still held
  expect(gate.swallowsRelease(BTN_BACK), true, "the picker's press is still owed a release");

  gate.settle(0, bit(BTN_BACK), 0);  // the picker's release, swallowed here
  expect(gate.swallowsRelease(BTN_BACK), true, "and the pump swallows it");

  gate.settle(0, 0, 0);
  expect(gate.swallowsRelease(BTN_BACK), false, "the arm is gone before the user can press again");

  // The user now presses Back to cancel the download.
  gate.settle(bit(BTN_BACK), 0, bit(BTN_BACK));
  gate.settle(0, bit(BTN_BACK), 0);
  expect(gate.swallowsRelease(BTN_BACK), false, "the cancel reaches the download");
}

}  // namespace

int main() {
  oneBackPressIsOneEvent();
  theNextBackIsHonoured();
  aPressAlwaysClearsTheArm();
  aLostReleaseClearsOnItsOwn();
  otherButtonsAreUntouched();
  aHeldButtonAtAnUnrelatedSwapIsArmed();
  armingTwiceStillClears();
  aFastReleaseIsStillSwallowed();
  anUnarmedGateChangesNothing();
  twoSettlesInOneFrameAgree();
  aBlockingFlowCanStillCancel();

  std::printf("%d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}
