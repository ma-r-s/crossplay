#pragma once

// Routing that waits for the panel, for every screen in the fork rather than
// only the toybox ones.
//
// The gap this closes. Building a screen and SHOWING it are separated by a
// whole panel refresh -- 0.3-2s; HalDisplay::HALF_REFRESH alone is 1720ms.
// Every screen in this fork builds its table, publishes it, and only then
// calls displayBuffer(), so for the length of a refresh the loop task routes
// taps against a screen nobody can see yet. A finger still resting where the
// PREVIOUS screen's button was gets read against the NEW screen's table.
//
// The sharp version of the failure is not "the screen changed", it is a rect
// whose MEANING changed underneath a stationary finger. BattleshipScreens.cpp
// registers one capsule as `gameOver ? ActionPlayAgain : (canFire ? ActionFire
// : NO_ACTION)`: FIRE is tapped dozens of times a game and becomes PLAY AGAIN
// the instant the last shot lands, while the panel still reads FIRE.
// SeaSaltScreens.cpp and JaipurScreens.cpp share the shape and are worse,
// because their button was live and benign every round beforehand, so the
// player is trained onto that exact pixel.
//
// The rule, and why it is this rule. A tap routes unless what it would act on
// has changed since the last version the panel showed, and no paint has landed
// since that change. Three consequences, all deliberate:
//
//   * an UNCHANGED table always routes. That is what keeps touch-down feedback
//     working, and it is the difference between this and a device that reads
//     as frozen. Apps highlight a row on touch-DOWN, repaint, and act on the
//     RELEASE of that same contact; those repaints rebuild an identical table,
//     so nothing is gated. Suppressing the contact outright -- the obvious fix
//     -- would have been far worse than eating a release, because
//     InputManager::suppressTouchContact() also gates isTouchTapCandidate,
//     isTouchHeldAt and wasSwipe, so it would cancel Minesweeper's
//     hold-to-flag and Wavelength's hold-to-peek WHILE the finger is down.
//   * the gate is held by a CHANGE, not by a timer, and it opens on the very
//     next completed paint. There is no threshold to tune and no path where a
//     slow refresh leaves input dead longer than the refresh itself.
//   * before the first paint of all -- host tests, and the moment before the
//     boot splash -- nothing is gated, because there is no shown screen to
//     disagree with.
//
// This header holds the two shapes the fork actually has. Both live here, next
// to PaintClock.h, because the seam sits below every app: the toybox screens,
// the shared components (OptionPopup, KeyboardEntryActivity) and the games'
// geometry hit-tests all need it and none of them can reach the others.
//
//   * RevealedInteractions<N> -- a screen routed through an SDK
//     InteractionBuffer. The "what it would act on" is the table, and the
//     class computes the digest itself.
//   * SurfaceGate -- a play surface hit-tested against GEOMETRY. An 80-cell
//     board does not fit an interaction table, so those taps never reach
//     route() and no digest can see them. The meaning is instead the handful
//     of mode bits the hit-test reads, and only the app knows them.

#include <FreeInkUI.h>

#include <stddef.h>
#include <stdint.h>

#include "PaintClock.h"

namespace paintclock {

// The hit table, plus the one thing the SDK buffer cannot know: whether the
// panel has actually SHOWN the table being routed against.
//
// Capacity is a template parameter because the three users want three sizes:
// toybox screens 24, KeyboardEntryActivity 48 (a 5-row EN layout registers 41
// keys), OptionPopup 17. Sizing them all to the largest would cost every
// toybox screen the keyboard's table.
template <size_t Capacity>
class RevealedInteractions {
 public:
  using Buffer = freeink::ui::InteractionBuffer<Capacity>;

  // Lets a Frame (and anything else wanting the raw SDK buffer) bind to this
  // exactly as it did when this was an alias for it. Every existing call site
  // keeps compiling and keeps meaning what it meant.
  operator Buffer&() { return buffer_; }              // NOLINT(google-explicit-constructor)
  operator const Buffer&() const { return buffer_; }  // NOLINT(google-explicit-constructor)

  // Called BEFORE the SDK Frame clears the buffer -- the table still in it at
  // that moment is the one the panel has been showing. Only adopt it as
  // "shown" when a paint has actually landed since the last build; otherwise
  // the previous build never reached the panel and the table before IT is
  // still what the user is looking at.
  //
  // Digests the PUBLISHED generation, which is by definition the one the panel
  // has been showing. For a caller that never opts into the publish cycle,
  // published_ and building_ stay pinned at the same generation and this is
  // the same array as data(). For one that DOES publish -- OptionPopup and
  // KeyboardEntryActivity both do -- building_ has already been flipped by the
  // time this runs, and data() would silently be the wrong array: a rebuild
  // from two generations ago, compared against as though the panel had shown
  // it.
  void beginBuild() {
    const uint32_t painted = paintclock::painted();
    if (painted != builtAtPaint_) shownDigest_ = digestOf(buffer_.publishedData(), buffer_.publishedCount());
    builtAtPaint_ = painted;
  }

  freeink::ui::ActionEvent route(const freeink::ui::InputSnapshot& input) {
    if (!shown(buffer_.data(), buffer_.count())) return {};
    return buffer_.route(input);
  }

  freeink::ui::ActionEvent routePublished(const freeink::ui::InputSnapshot& input) {
    if (!shown(buffer_.publishedData(), buffer_.publishedCount())) return {};
    return buffer_.routePublished(input);
  }

  // Straight forwarders: none of them decide anything, so none of them gate.
  void publish() { buffer_.publish(); }
  void beginPublishCycle() { buffer_.beginPublishCycle(); }
  void clear() { buffer_.clear(); }
  size_t count() const { return buffer_.count(); }
  bool overflowed() const { return buffer_.overflowed(); }
  const freeink::ui::Interaction* data() const { return buffer_.data(); }
  size_t publishedCount() const { return buffer_.publishedCount(); }
  bool publishedOverflowed() const { return buffer_.publishedOverflowed(); }
  const freeink::ui::Interaction* publishedData() const { return buffer_.publishedData(); }
  int16_t activeIndex() const { return buffer_.activeIndex(); }
  int16_t focusedIndex() const { return buffer_.focusedIndex(); }
  void setFocusedIndex(const int16_t index) { buffer_.setFocusedIndex(index); }

  // For tests, for anyone reasoning about a dropped tap, and for a caller that
  // must know BEFORE routing. route() reports a suppressed tap as a
  // default-constructed event, which is the same thing it reports for a tap
  // that landed on nothing -- so any caller with a do-something-on-nothing
  // branch (OptionPopup dismisses, MurdleActivity strikes a clue) has to ask
  // here first or the gate turns a swallowed tap into a wrong action.
  bool routable() const { return shown(buffer_.data(), buffer_.count()); }
  bool publishedRoutable() const { return shown(buffer_.publishedData(), buffer_.publishedCount()); }

 private:
  static uint32_t digestOf(const freeink::ui::Interaction* slots, const size_t slotCount) {
    // FNV-1a. Cheap, and collisions cost one wrongly-allowed tap in the window
    // rather than anything durable.
    uint32_t hash = 2166136261u;
    const auto mix = [&hash](const uint32_t value) {
      for (int byte = 0; byte < 4; ++byte) {
        hash ^= (value >> (byte * 8)) & 0xFFu;
        hash *= 16777619u;
      }
    };
    mix(static_cast<uint32_t>(slotCount));
    for (size_t i = 0; i < slotCount; ++i) {
      const freeink::ui::Interaction& slot = slots[i];
      mix(static_cast<uint32_t>(static_cast<uint16_t>(slot.rect.x)) |
          (static_cast<uint32_t>(static_cast<uint16_t>(slot.rect.y)) << 16));
      mix(static_cast<uint32_t>(static_cast<uint16_t>(slot.rect.width)) |
          (static_cast<uint32_t>(static_cast<uint16_t>(slot.rect.height)) << 16));
      mix(static_cast<uint32_t>(slot.action));
      mix(static_cast<uint32_t>(static_cast<uint16_t>(slot.value)) |
          (static_cast<uint32_t>(slot.inputMask) << 16));
      // StateDisabled and ONLY StateDisabled. It looks like a cosmetic bit and
      // is not one: InteractionBuffer::findTouch skips disabled entries
      // outright, so flipping it changes what a tap DOES. DungeonScreens.cpp
      // registers its NEXT button with an identical rect, action, value and
      // inputMask and flips only this bit on model.moreToPlay, so a dead
      // control becoming live would otherwise slip through the digest
      // unchanged. The focus/active/flash bits stay excluded, because those
      // really are only how a row draws.
      mix(static_cast<uint32_t>(slot.state & freeink::ui::StateDisabled));
    }
    return hash;
  }

  bool shown(const freeink::ui::Interaction* slots, const size_t slotCount) const {
    // Nothing has ever been painted: no shown table to disagree with.
    if (paintclock::painted() == 0) return true;
    // A paint landed since this table was built, so the panel is showing it.
    if (paintclock::painted() != builtAtPaint_) return true;
    // Still mid-paint. Safe only if the meaning did not move under the finger.
    return digestOf(slots, slotCount) == shownDigest_;
  }

  Buffer buffer_;
  uint32_t shownDigest_ = 0;
  uint32_t builtAtPaint_ = 0;
};

// The same rule for a play surface that is hit-tested against GEOMETRY.
//
// Eight apps compute a cell from raw x/y rather than routing -- an 80-cell
// board does not fit an interaction table -- so those taps never reach
// route() and RevealedInteractions cannot see them. What a tap on the board
// MEANS is not in the table either: Minesweeper's FLAG capsule is registered
// with an identical rect, action, value and inputMask and flips only
// StateSelected, which the digest deliberately ignores as paint. The mode bit
// it sets is what decides whether a grid tap digs or flags.
//
// So the app supplies the meaning, as one number. Both calls must derive it
// from the SAME expression -- an app that hashes flagMode in render() and a
// stale copy of it in loop() has a gate that is silently always open, which
// looks exactly like a gate that works. Activity::surfaceMeaning() exists so
// there is one definition to call twice rather than two to keep in step.
//
// Note what is deliberately NOT in a meaning: anything that only changes how
// the board draws. A cell outline that follows a held finger, a flashing
// cursor, a redrawn score. Folding those in would gate the release of the very
// contact that caused the repaint, which is the failure this whole mechanism
// is shaped to avoid.
//
// Two tasks touch it: the render task stamps (via ActivityManager, around the
// render dispatch) and the loop task asks. The fields are plain uint32_t, the
// same choice RevealedInteractions makes above and for the same reason -- a
// 32-bit aligned load or store is single-instruction on both targets, so a
// torn read cannot happen. A stale read can, and it resolves as "a paint has
// landed since the build", which routes. That is the fail-OPEN direction: the
// cost is a missed gate for one pass, never a surface that stops answering.
class SurfaceGate {
 public:
  // The screen was (re)built just now, and `meaning` is what a tap on the play
  // surface means in the frame being built. Call from render(), before the
  // paint -- the same place and the same moment a Frame's constructor calls
  // RevealedInteractions::beginBuild().
  void noteBuilt(const uint32_t meaning) {
    const uint32_t painted = paintclock::painted();
    // A paint landed since the last build, so THAT build is what the panel is
    // showing now. Adopt its meaning as the shown one. Without this check a
    // second build before a single paint would measure from the intermediate
    // frame the user never saw.
    if (painted != builtAtPaint_) shownMeaning_ = builtMeaning_;
    builtAtPaint_ = painted;
    builtMeaning_ = meaning;
  }

  // Is a tap on the play surface safe to act on, given what it would mean now?
  bool routable(const uint32_t meaning) const {
    // Nothing has ever been painted: no shown frame to disagree with.
    if (paintclock::painted() == 0) return true;
    // A paint landed since the build, so the panel is showing it.
    if (paintclock::painted() != builtAtPaint_) return true;
    // Still mid-paint. Safe only if the meaning did not move under the finger.
    return meaning == shownMeaning_;
  }

 private:
  uint32_t builtAtPaint_ = 0;
  uint32_t builtMeaning_ = 0;
  uint32_t shownMeaning_ = 0;
};

// Fold one more value into a meaning. Order matters and that is the point: a
// board is usually two or three small bits (a mode flag, a selected square, a
// phase) and mixing them positionally keeps "selected e2, white to move" from
// colliding with "selected d4, black to move".
inline uint32_t mixMeaning(const uint32_t accumulated, const uint32_t value) {
  uint32_t hash = accumulated;
  for (int byte = 0; byte < 4; ++byte) {
    hash ^= (value >> (byte * 8)) & 0xFFu;
    hash *= 16777619u;
  }
  return hash;
}

// Seed for mixMeaning chains. FNV-1a's offset basis, same as the table digest.
constexpr uint32_t kMeaningSeed = 2166136261u;

}  // namespace paintclock
