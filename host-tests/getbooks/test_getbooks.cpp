// Three Get Books reports from the device, pinned so they cannot come back.
//
//   1. "The books icon has a box around it now."  v1.12.12 gave the core theme
//      an outlined tokens.button so the download screen's Cancel would read as
//      a control. Screen::header() hands that same style to a header's LEADING
//      action and plainStyles() to its trailing one, so the catalog icon came
//      out boxed -- and the search icon at the other end of the same band
//      acknowledged a tap with nothing, because plainStyles carries no fill in
//      any state. Both ends are chrome; both now take defaultButtonStyles().
//
//   2. "Cannot go back until the book image has loaded."  The cover fetch runs
//      synchronously inside the activity loop, so loop()'s own Back check
//      cannot run while it blocks. The transfer's progress callback is the only
//      code that still executes, which makes it the only place a Back is seen.
//
//   3. The previous book's cover on this book's wait screen. The cover caches
//      at a fixed path per EXTENSION and is found again by probing four of
//      them, so a leftover .bmp outranked this book's .jpg.
//
// What this file reaches, and what it does not:
//
//   * The header checks drive the real SDK through the real uiButtonStyles()
//     and applyHeaderActionChrome(), so gutting either turns them red. They
//     CANNOT prove OpdsBookBrowserActivity::screenHeader calls the helper --
//     that activity needs a renderer, the card and Arduino. The screenshot
//     pair in the commit is what covers the call site.
//   * The cover-cache checks drive the real clearAll()/findCached() against a
//     fake card. They CANNOT prove OpdsDetailActivity::onEnter calls clearAll,
//     which is the same call-site gap; that call is what fixes report 3, and
//     it is one line in onEnter().
//   * pumpBlockingFetch is driven by a fake input. It cannot model the fact
//     that loop() is blocked, only the policy of what the pump must read.

#include <FreeInkApp.h>

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "../../src/components/BlockingFetchInput.h"
#include "../../src/components/UiControlChrome.h"
#include "../../src/util/OpdsCoverCache.h"

namespace fui = freeink::ui;

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL %s:%d  %s\n", "test_getbooks.cpp", line, what);
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// ------------------------------------------------- reports 1 (chrome)

// Records outlines AND grounds. Recording only strokes was how the whole
// "keeps the fill it inverts to" half of the fix went untested: swapping
// defaultButtonStyles() for plainStyles() changed no stroke anywhere, so a
// stroke-only target reported the same green either way.
class ChromeTarget final : public fui::DrawTarget {
 public:
  struct Stroke {
    fui::Rect rect;
    uint8_t width;
  };
  struct Fill {
    fui::Rect rect;
    fui::Paint paint;
  };
  std::vector<Stroke> strokes;
  std::vector<Fill> fills;

  int16_t lineH = 20;

  fui::Size measureText(const fui::FontId, const char* text, const fui::TextStyle) const override {
    return fui::Size{static_cast<int16_t>(text ? std::strlen(text) * 10 : 0), lineH};
  }
  int16_t lineHeight(const fui::FontId) const override { return lineH; }
  void fill(const fui::Rect rect, const fui::Paint paint, const uint8_t = 0, const uint8_t = 0xFF) override {
    if (paint.kind != fui::PaintKind::None) fills.push_back(Fill{rect, paint});
  }
  void stroke(const fui::Rect rect, const fui::Paint paint, const uint8_t width, const uint8_t = 0,
              const uint8_t = 0xFF) override {
    if (paint.kind != fui::PaintKind::None && width > 0) strokes.push_back(Stroke{rect, width});
  }
  void line(const fui::Point, const fui::Point, const uint8_t, const fui::Paint) override {}
  void triangle(const fui::Point, const fui::Point, const fui::Point, const fui::Paint) override {}
  void text(const fui::Rect, const char*, const fui::TextStyle) override {}
  void bitmap(const fui::Rect, const fui::BitmapRef, const fui::BitmapMode, const fui::Paint = {},
              const fui::Rotation = fui::Rotation::None) override {}
};

const uint8_t kIconBits[8] = {0};
fui::BitmapRef testIcon() {
  fui::BitmapRef ref;
  ref.data = kIconBits;
  ref.width = 8;
  ref.height = 8;
  return ref;
}

constexpr fui::ActionId ACTION_SETTINGS = 4;
constexpr fui::ActionId ACTION_SEARCH = 2;
constexpr int16_t kHeaderHeight = 44;
constexpr int16_t kScreenW = 480;

fui::DeviceContext device() {
  fui::DeviceContext ctx;
  ctx.width = kScreenW;
  ctx.height = 800;
  ctx.hasTouch = true;
  ctx.hasButtons = true;
  return ctx;
}

// The core theme, built from the SAME uiButtonStyles() the firmware installs
// into tokens.button. Not a copy of it: a fixture copied out of the fix agrees
// with the fix by construction and cannot fail when production changes.
fui::ThemeTokens coreTheme() {
  fui::ThemeTokens tokens;
  tokens.headerHeight = kHeaderHeight;
  tokens.button = uiButtonStyles();
  return tokens;
}

// header() lays both action buttons out as squares of (band height - 8), the
// leading one inset 4 from the left edge and the trailing one 4 from the
// right. Naming the two ends separately is what lets an assertion say "these
// two draw alike" rather than "the total happens to be zero".
constexpr int16_t kActionSide = static_cast<int16_t>(kHeaderHeight - 8);
bool isLeadingAction(const fui::Rect& r) { return r.width == kActionSide && r.height == kActionSide && r.x == 4; }
bool isTrailingAction(const fui::Rect& r) {
  return r.width == kActionSide && r.height == kActionSide && r.x == static_cast<int16_t>(kScreenW - 4 - kActionSide);
}

// One rendered band. Tokens, context and input are members: Frame and Screen
// hold references, so a temporary would dangle before the assertions ran.
struct RenderedHeader {
  ChromeTarget target;
  fui::ThemeTokens tokens = coreTheme();
  fui::DeviceContext ctx = device();
  fui::InputSnapshot input{};
  fui::InteractionBuffer<48> interactions;

  // `flash` is the action the reader currently has a finger on, or NO_ACTION.
  // `chrome` is the fix under test.
  void build(const bool chrome, const fui::ActionId flash = fui::NO_ACTION) {
    fui::Frame<48> frame(target, ctx, input, interactions);
    if (flash != fui::NO_ACTION) interactions.setFlash(flash, 0);
    fui::Screen<48> screen(frame, tokens);
    fui::HeaderProps header;
    header.title = "Get Books";
    header.borderEdges = fui::EdgeBottom;
    header.leadingIcon = testIcon();
    header.leadingAction = ACTION_SETTINGS;
    header.trailingIcon = testIcon();
    header.trailingAction = ACTION_SEARCH;
    if (chrome) applyHeaderActionChrome(header);
    screen.header(header);
  }

  int outlinesOn(bool (*where)(const fui::Rect&)) const {
    int n = 0;
    for (const auto& s : strokesOf()) {
      if (where(s.rect)) ++n;
    }
    return n;
  }
  int groundsOn(bool (*where)(const fui::Rect&)) const {
    int n = 0;
    for (const auto& f : target.fills) {
      if (where(f.rect)) ++n;
    }
    return n;
  }
  const std::vector<ChromeTarget::Stroke>& strokesOf() const { return target.strokes; }
};

// The report, at rest: no box on the catalog icon.
void theCatalogIconIsNotBoxed() {
  RenderedHeader out;
  out.build(true);
  CHECK(out.outlinesOn(isLeadingAction) == 0);
}

// Without the fix it IS boxed. This is the assertion that names the bug rather
// than the fix, and it renders the SAME band both ways so the only difference
// is the chrome helper.
void withoutTheChromeTheCatalogIconIsBoxed() {
  RenderedHeader bare;
  bare.build(false);
  CHECK(bare.outlinesOn(isLeadingAction) == 1);
  CHECK(bare.outlinesOn(isTrailingAction) == 0);
}

// Both ends of one band, at rest and under a finger. The at-rest half is the
// box; the under-a-finger half is the tap acknowledgement plainStyles() does
// not have, and it is why the fix uses defaultButtonStyles(). Swap it for
// plainStyles and the flash checks go red.
void bothEndsOfTheBandDrawAlike() {
  RenderedHeader rest;
  rest.build(true);
  CHECK(rest.outlinesOn(isLeadingAction) == rest.outlinesOn(isTrailingAction));
  CHECK(rest.outlinesOn(isLeadingAction) == 0);

  RenderedHeader leadingHeld;
  leadingHeld.build(true, ACTION_SETTINGS);
  RenderedHeader trailingHeld;
  trailingHeld.build(true, ACTION_SEARCH);
  // A finger on either end paints that end's ground.
  CHECK(leadingHeld.groundsOn(isLeadingAction) == 1);
  CHECK(trailingHeld.groundsOn(isTrailingAction) == 1);
  // And the two acknowledge identically.
  CHECK(leadingHeld.groundsOn(isLeadingAction) == trailingHeld.groundsOn(isTrailingAction));
}

// The flash must be a DIFFERENT ground from the resting one, or "it draws a
// fill" is satisfied by a control that looks the same pressed and unpressed.
void aHeldIconLooksDifferentFromARestingOne() {
  RenderedHeader rest;
  rest.build(true);
  RenderedHeader held;
  held.build(true, ACTION_SETTINGS);
  fui::Paint restPaint{};
  fui::Paint heldPaint{};
  for (const auto& f : rest.target.fills) {
    if (isLeadingAction(f.rect)) restPaint = f.paint;
  }
  for (const auto& f : held.target.fills) {
    if (isLeadingAction(f.rect)) heldPaint = f.paint;
  }
  CHECK(restPaint.kind != fui::PaintKind::None);
  CHECK(heldPaint.kind != fui::PaintKind::None);
  CHECK(!(restPaint.kind == heldPaint.kind && restPaint.color == heldPaint.color));
}

// Narrowing the header must not have quietly reverted v1.12.12's actual fix.
// Driven through the real uiButtonStyles(), so deleting the border there turns
// this red -- which a hand-copied fixture could not do.
void aRealButtonKeepsItsOutline() {
  ChromeTarget target;
  const fui::ThemeTokens tokens = coreTheme();
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  fui::InteractionBuffer<48> interactions;
  fui::Frame<48> frame(target, ctx, noInput, interactions);
  fui::Screen<48> screen(frame, tokens);
  fui::ButtonProps cancel;
  cancel.label = "CANCEL";
  cancel.action = 3;
  screen.button(cancel, fui::Rect{100, 400, 280, 44});
  CHECK(target.strokes.size() == 1);
}

// ------------------------------------------------- report 3 (cover cache)

// A card that is just the set of paths on it.
struct FakeCard {
  std::set<std::string> files;
  int removes = 0;
  bool exists(const char* path) const { return files.count(path) != 0; }
  void remove(const char* path) {
    ++removes;
    files.erase(path);
  }
};

// The stale-cover bug itself: the previous book's file under a DIFFERENT
// extension outranks this book's, because findCached probes in order.
void aLeftoverUnderAnotherExtensionWouldBeFound() {
  FakeCard card;
  card.files.insert(opdscover::pathFor(".bmp"));
  CHECK(opdscover::findCached(card) == opdscover::pathFor(".bmp"));
}

// ...and clearing removes every extension findCached can reach. Derived from
// kExtensions rather than from a repeated literal: a fifth format added to the
// list is covered here the day it is added, and a clear that walked its own
// shorter list would fail on it.
void clearingReachesEveryExtensionThatCanBeFound() {
  for (const char* extension : opdscover::kExtensions) {
    FakeCard card;
    card.files.insert(opdscover::pathFor(extension));
    CHECK(!opdscover::findCached(card).empty());
    opdscover::clearAll(card);
    CHECK(opdscover::findCached(card).empty());
  }
}

void clearingAnEmptyCardRemovesNothing() {
  FakeCard card;
  opdscover::clearAll(card);
  CHECK(card.removes == 0);
  CHECK(opdscover::findCached(card).empty());
}

void clearingRemovesAllOfThemAtOnce() {
  FakeCard card;
  for (const char* extension : opdscover::kExtensions) card.files.insert(opdscover::pathFor(extension));
  opdscover::clearAll(card);
  CHECK(card.files.empty());
}

// ------------------------------------------------- report 2 (blocking fetch)

// Stands in for MappedInputManager: same const-callable shape, same one-shot
// semantics (an edge is reported once, and update() is what consumes it).
struct FakeInput {
  enum class Button { Back, Confirm };

  mutable int updates = 0;
  mutable bool backRelease = false;
  mutable bool homeGesture = false;

  void update() const { ++updates; }
  bool wasReleased(const Button button) const {
    if (button != Button::Back) return false;
    const bool edge = backRelease;
    backRelease = false;
    return edge;
  }
  bool wasHomeGesture() const {
    const bool edge = homeGesture;
    homeGesture = false;
    return edge;
  }
};

void aBackDuringTheFetchCancelsIt() {
  FakeInput input;
  input.backRelease = true;
  bool cancelled = false;
  bool goHome = false;
  pumpBlockingFetch(input, cancelled, goHome);
  CHECK(input.updates == 1);
  CHECK(cancelled);
  // Back steps back one screen; it is not a request to leave the app.
  CHECK(!goHome);
}

void aHomeGestureDuringTheFetchIsNotSwallowed() {
  FakeInput input;
  input.homeGesture = true;
  bool cancelled = false;
  bool goHome = false;
  pumpBlockingFetch(input, cancelled, goHome);
  // update() consumed the one-shot event, so nothing downstream will ever see
  // it: read here or lost.
  CHECK(cancelled);
  CHECK(goHome);
}

void anUneventfulPumpCancelsNothing() {
  FakeInput input;
  bool cancelled = false;
  bool goHome = false;
  pumpBlockingFetch(input, cancelled, goHome);
  CHECK(input.updates == 1);
  CHECK(!cancelled);
  CHECK(!goHome);
}

// The flag is sticky across the rest of the transfer. HttpDownloader reads it
// between chunks and the pump runs many times before it gets there -- a cancel
// that survived one call only would be the press arriving and nothing
// happening.
void theCancelSurvivesLaterPumps() {
  FakeInput input;
  bool cancelled = false;
  bool goHome = false;
  input.backRelease = true;
  pumpBlockingFetch(input, cancelled, goHome);
  pumpBlockingFetch(input, cancelled, goHome);
  pumpBlockingFetch(input, cancelled, goHome);
  CHECK(cancelled);
}

}  // namespace

int main() {
  theCatalogIconIsNotBoxed();
  withoutTheChromeTheCatalogIconIsBoxed();
  bothEndsOfTheBandDrawAlike();
  aHeldIconLooksDifferentFromARestingOne();
  aRealButtonKeepsItsOutline();
  aLeftoverUnderAnotherExtensionWouldBeFound();
  clearingReachesEveryExtensionThatCanBeFound();
  clearingAnEmptyCardRemovesNothing();
  clearingRemovesAllOfThemAtOnce();
  aBackDuringTheFetchCancelsIt();
  aHomeGestureDuringTheFetchIsNotSwallowed();
  anUneventfulPumpCancelsNothing();
  theCancelSurvivesLaterPumps();
  std::printf("getbooks: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
