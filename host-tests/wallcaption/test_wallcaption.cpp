// Proves the wallpaper picker's selection marker never collides with the
// artwork or with a caption -- for EVERY built-in name and the "+ Add" tile, in
// EVERY grid position, in the selected state.
//
//   host-tests/wallcaption/run.sh
//
// Unlike host-tests/ui, this one links lib/EpdFont and the real toybox cuts.
// The ui suite's draw target answers ten pixels a character, and a caption that
// overflows its box by a real face's widths is invisible to it: the whole point
// here is that the widths are the panel's own. See the "unwrapped strings have
// a pixel budget" and "tests that share the bug" notes.
//
// The vertical argument is a property of the LAYOUT, not of any one string:
// the brackets stop at markerBottomExtent() and the caption's line box starts
// after it, so no string can reach them as long as the real line height fits
// the caption row. Both halves are asserted below, which is what makes this a
// proof for the 21 names rather than a spot check of the one I looked at.
#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <FreeInkUI.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/apps_local/live/LiveCore.h"
#include "../../src/apps_local/ui/ToyboxText.h"
#include "../../src/apps_local/ui/fonts/reading_serif_14.h"
#include "../../src/apps_local/ui/fonts/toybox_10.h"
#include "../../src/apps_local/ui/fonts/toybox_14.h"
#include "../../src/apps_local/ui/fonts/toybox_20.h"
#include "../../src/apps_local/ui/fonts/toybox_30.h"
#include "../../src/apps_local/ui/fonts/toybox_64.h"
#include "../../src/apps_local/wallpapers/WallpapersCore.h"
#include "../../src/apps_local/wallpapers/WallpapersScreens.h"
#include "service_refusals.generated.h"

namespace fui = freeink::ui;

namespace {
int checks = 0;
int failed = 0;
// Reported at the end: how much of the 24-slot interaction table the Live
// screen spends at its fullest. Printed rather than only bounded, because a
// budget nobody sees is a budget the next screen quietly overruns.
int slotsAtFullList = 0;
// And the same for the "Your phone" destination, which is two controls and
// must stay two: it is the screen every route through this app now passes.
int phoneSlots = 0;
int addSlots = 0;
std::vector<std::string> firstFailures;

void check(const bool ok, const std::string& what) {
  ++checks;
  if (ok) return;
  ++failed;
  if (firstFailures.size() < 10) firstFailures.push_back(what);
}

// The faces the picker really binds: the caption asks for FONT_SLOT_SMALL and
// the Toybox theme answers with toybox_10 (WallpapersActivity::drawGrid).
EpdFont small10(&toybox_10);
EpdFont button14(&toybox_14);
EpdFont ui20(&toybox_20);
EpdFont display30(&toybox_30);
// The Live screen's two face sets. Unpaired it binds toybox::pairingCodeFaces
// (the 82px cut in SMALL, where only the code asks for it); paired it binds
// readingChromeFaces like the offer and the sheet. Both are here because the
// screen is built twice and the cuts are what decide whether a string fits.
EpdFont huge64(&toybox_64);
EpdFont reading14(&reading_serif_14);
EpdFontFamily smallFamily(&small10);
EpdFontFamily buttonFamily(&button14);
EpdFontFamily uiFamily(&ui20);
EpdFontFamily displayFamily(&display30);
EpdFontFamily hugeFamily(&huge64);
EpdFontFamily readingFamily(&reading14);

class FontTarget final : public fui::DrawTarget {
 public:
  const EpdFontFamily* familyFor(const fui::FontId font) const {
    if (font == fui::FONT_SLOT_SMALL) return &smallFamily;
    if (font == fui::FONT_SLOT_BODY) return &uiFamily;
    return &displayFamily;
  }
  int widthOf(const fui::FontId font, const std::string& text) const {
    if (text.empty()) return 0;
    int w = 0;
    int h = 0;
    familyFor(font)->getTextDimensions(text.c_str(), &w, &h);
    return w;
  }
  fui::Size measureText(const fui::FontId font, const char* text, const fui::TextStyle) const override {
    return fui::Size{static_cast<int16_t>(widthOf(font, text == nullptr ? "" : text)), lineHeight(font)};
  }
  int16_t lineHeight(const fui::FontId font) const override {
    return static_cast<int16_t>(familyFor(font)->getData(EpdFontFamily::REGULAR)->advanceY);
  }
  void fill(fui::Rect, fui::Paint, uint8_t = 0, uint8_t = 0xFF) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t = 0, uint8_t = 0xFF) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect, const char*, const fui::TextStyle) override {}
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint = {},
              fui::Rotation = fui::Rotation::None) override {}
};

// The grid CHROME is a different face set from the grid's captions, and the
// difference is the whole reason this block exists.
//
// drawGrid() builds its caption target with toybox::makeTarget(renderer) and
// the DEFAULT Faces, so FONT_SLOT_SMALL is kTileFontId = toybox_10 -- what
// FontTarget above models. render() builds the CHROME's target with
// toybox::proseMenuFaces(), where the same slot is kButtonFontId = toybox_14.
// So the hint strip draws ~40% wider than the captions do, and measuring it in
// toybox_10 said every sentence fitted while the panel cut one mid-word. The
// simulator screenshot is what caught it; this target is what stops it coming
// back ("drawn size is a claim").
//
// fittedTitle can rescue nothing here: it steps DOWN through the bound slots,
// and in this face set TITLE (30) and BODY (20) are both taller than SMALL
// (14), so there is no rung below and the only move left is the ellipsis.
class ChromeFontTarget final : public fui::DrawTarget {
 public:
  const EpdFontFamily* familyFor(const fui::FontId font) const {
    if (font == fui::FONT_SLOT_SMALL) return &buttonFamily;
    if (font == fui::FONT_SLOT_BODY) return &uiFamily;
    return &displayFamily;
  }
  int widthOf(const fui::FontId font, const std::string& text) const {
    if (text.empty()) return 0;
    int w = 0;
    int h = 0;
    familyFor(font)->getTextDimensions(text.c_str(), &w, &h);
    return w;
  }
  fui::Size measureText(const fui::FontId font, const char* text, const fui::TextStyle) const override {
    return fui::Size{static_cast<int16_t>(widthOf(font, text == nullptr ? "" : text)), lineHeight(font)};
  }
  int16_t lineHeight(const fui::FontId font) const override {
    return static_cast<int16_t>(familyFor(font)->getData(EpdFontFamily::REGULAR)->advanceY);
  }
  void fill(fui::Rect, fui::Paint, uint8_t = 0, uint8_t = 0xFF) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t = 0, uint8_t = 0xFF) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect, const char*, const fui::TextStyle) override {}
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint = {},
              fui::Rotation = fui::Rotation::None) override {}
};

// ---------------------------------------------------------------------------
// THE LIVE SCREEN, recorded rather than measured from the outside.
//
// This one is built through a real toybox::Screen and every string it hands to
// text() is kept, because the defect this screen can have is not a rectangle
// that overlaps -- it is a SENTENCE THAT STOPS. The Toybox cuts above toybox_10
// carry no U+2026, so an overflowing line at reading_serif_14 or toybox_64
// arrives neither clipped nor ellipsised: it ends at a plausible-looking place
// and the screenshot looks fine. Three of those shipped into this screen's
// first three renders (the prose, the footer, and a sender's name) and every
// one of them was found by a human opening the PNG.
//
// So the assertion is the layout itself: lay each recorded run out in the box
// it was given, in the face that will draw it, and it must come back WHOLE.
class LiveTarget final : public fui::DrawTarget {
 public:
  explicit LiveTarget(const bool paired) : paired_(paired) {}

  struct Run {
    fui::Rect box;
    std::string text;
    fui::TextStyle style;
  };
  std::vector<Run> runs;

  // The MARKS, recorded for the same reason the text is. Since the screen lost
  // its headings the affordances are icons: three in the control row and one at
  // the end of every sender row, and "is the mark there" is now the assertion
  // that "TAP TO REMOVE is on the screen" used to be. A no-op bitmap() would
  // have let the whole redesign draw nothing and stay green.
  //
  // `data` is the icon's bits pointer (fui::bitmapFromIcon), which is how a
  // mark is told from another mark without the test holding its own copy of
  // ToyboxIcons.h -- every icon there is `static const`, so an included copy
  // would be a different object with a different pointer.
  struct Mark {
    fui::Rect box;
    const uint8_t* data = nullptr;
  };
  std::vector<Mark> marks;

  // THE FRAMES, and they are recorded because one of them is the only thing on
  // a whole state of this screen. With no code there is no QR, and the square
  // that says where the QR will be is a stroked outline and nothing else -- so
  // a target that threw strokes away could not see the element that ran off
  // the bottom of the panel, and did not: the first version of the no-code
  // layout put it at y=702 on an 800px panel, the simulator's renderer logged
  // a hundred out-of-range lines, and every assertion here stayed green
  // because all of them were about text.
  std::vector<fui::Rect> frames;

  const EpdFontFamily* familyFor(const fui::FontId font) const {
    // FONT_SLOT_SMALL is the whole difference between the two states: the huge
    // cut while there is a code on the screen, the button cut once there is not.
    if (font == fui::FONT_SLOT_SMALL) return paired_ ? &buttonFamily : &hugeFamily;
    if (font == fui::FONT_SLOT_BODY) return &readingFamily;
    return &displayFamily;
  }
  // BOLD IS MEASURED, not ignored. The unpaired screen's address is the reading
  // cut in bold, and a target that measures every style at REGULAR would report
  // the narrower width for the one string on this screen whose whole job is to
  // be copied off the glass -- a test sharing the bug it exists to catch.
  fui::Size measureText(const fui::FontId font, const char* text, const fui::TextStyle style) const override {
    int w = 0;
    int h = 0;
    if (text != nullptr && text[0] != '\0') {
      familyFor(font)->getTextDimensions(text, &w, &h, style.bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    }
    return fui::Size{static_cast<int16_t>(w), lineHeight(font)};
  }
  int16_t lineHeight(const fui::FontId font) const override {
    return static_cast<int16_t>(familyFor(font)->getData(EpdFontFamily::REGULAR)->advanceY);
  }
  void fill(fui::Rect, fui::Paint, uint8_t = 0, uint8_t = 0xFF) override {}
  void stroke(const fui::Rect box, fui::Paint, uint8_t, uint8_t = 0, uint8_t = 0xFF) override { frames.push_back(box); }
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(const fui::Rect box, const char* text, const fui::TextStyle style) override {
    if (text == nullptr || text[0] == '\0') return;
    runs.push_back(Run{box, std::string(text), style});
  }
  void bitmap(const fui::Rect box, const fui::BitmapRef ref, fui::BitmapMode, fui::Paint = {},
              fui::Rotation = fui::Rotation::None) override {
    marks.push_back(Mark{box, ref.data});
  }

 private:
  bool paired_ = false;
};

// EVERY PAIR OF LINES THE DEVICE CAN PUT AT THE TOP OF THE PAIRED SCREEN,
// composed by the code that really composes them.
//
// The suite used to hand this screen "Tomorrow, 6:00" and "Once a day", which
// are strings the product cannot emit in any state. They fitted, so everything
// passed, and the headline's actual failure mode went unmeasured: fittedTitle
// steps a too-wide title DOWN a cut instead of refusing it, and at the display
// cut "In about 45 minutes" is 464px against a 448px body. A screen that loses
// its hierarchy looks fine in every assertion that asks whether text fits.
//
// So: every interval the service may ask for, at several points inside each
// one, both toggle positions and the backoff, through live::nextCheckPhrase and
// live::scheduleNote -- and then measured in the face that will draw them.
struct LivePhrases {
  std::string headline;
  std::string note;
};

std::vector<LivePhrases> liveHeadlines() {
  std::vector<LivePhrases> out;
  const int64_t base = live::kPlausibleEpochFloor + 1000000;
  const uint32_t intervals[] = {live::kMinIntervalSeconds, 1800, 3299, 3600, 5400, 21600, 43200, 86400, 172800, 604740,
                                live::kMaxIntervalSeconds};
  for (const uint32_t interval : intervals) {
    for (int on = 0; on < 2; ++on) {
      for (const int fails : {0, 1, 4, 99}) {
        // Several moments inside the interval, so every band of the countdown
        // is reached rather than only the one a single elapsed time lands in.
        for (const uint32_t part : {0u, 1u, 2u, 4u, 8u, 64u}) {
          live::Schedule schedule;
          schedule.on = on == 1;
          schedule.paired = true;
          schedule.intervalSeconds = interval;
          schedule.consecutiveFailures = fails;
          schedule.lastAttemptEpoch = base;
          const int64_t now = base + static_cast<int64_t>(part == 0 ? 0 : interval - interval / part);
          out.push_back(LivePhrases{live::nextCheckPhrase(schedule, now), live::scheduleNote(schedule)});
        }
      }
    }
  }
  return out;
}

// The one example pair, for the blocks that only need A headline rather than
// every headline. Composed like the rest: nothing in this file types a phrase
// the device would have to be able to produce.
LivePhrases liveSample() {
  live::Schedule schedule;
  schedule.on = true;
  schedule.paired = true;
  schedule.intervalSeconds = 21600;
  schedule.lastAttemptEpoch = live::kPlausibleEpochFloor + 1000000;
  return LivePhrases{live::nextCheckPhrase(schedule, schedule.lastAttemptEpoch + 600), live::scheduleNote(schedule)};
}

fui::DeviceContext device() {
  fui::DeviceContext ctx;
  ctx.width = 480;
  ctx.height = 800;
  ctx.hasTouch = true;
  ctx.hasButtons = true;
  return ctx;
}

bool overlaps(const fui::Rect& a, const fui::Rect& b) {
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

// Is `inner` wholly inside `outer`? Used to say which control a mark belongs
// to: a glyph that merely overlaps a button could be the neighbour's, and a
// button that owns two marks is a button that has been drawn on twice.
bool contains(const fui::Rect& outer, const fui::Rect& inner) {
  return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}

// What the caption actually draws, by the Activity's own rule: the long name
// when it fits, otherwise the short one, and an ellipsis only if both overflow.
std::string drawnCaption(const FontTarget& t, const wallpapers::DisplayName& name, const int16_t width,
                         const fui::TextStyle& style) {
  std::string fitted = toybox::fitLines(t, name.full.c_str(), width, 1, style);
  if (fitted != name.full) fitted = toybox::fitLines(t, name.brief.c_str(), width, 1, style);
  return fitted;
}
}  // namespace

int main() {
  const FontTarget target;
  const wallpapersui::GridGeom g = wallpapersui::gridGeom(device());
  const fui::Rect panel = fui::makeRect(0, 0, 480, 800);

  fui::TextStyle caption;
  caption.font = fui::FONT_SLOT_SMALL;
  caption.align = fui::TextAlign::Center;
  caption.maxLines = 1;

  // 1. The marker is a mark on the CELL: clear of the picture and clear of the
  //    label, in every grid position. A bracket near the panel edge has less
  //    padding to live in, which is why every slot is walked and not just one.
  for (int slot = 0; slot < g.perPage; ++slot) {
    const fui::Rect thumb = wallpapersui::thumbRect(g, slot);
    const fui::Rect cap = wallpapersui::captionRect(g, slot);
    const fui::Rect cell = wallpapersui::cellRect(g, slot);
    const wallpapersui::MarkerRects m = wallpapersui::markerRects(thumb);
    const std::string at = " (slot " + std::to_string(slot) + ")";
    for (int i = 0; i < wallpapersui::MarkerRects::kCount; ++i) {
      check(!overlaps(m.r[i], thumb), "bracket " + std::to_string(i) + " sits on the artwork" + at);
      check(!overlaps(m.r[i], cap), "bracket " + std::to_string(i) + " sits on the caption box" + at);
      check(m.r[i].x >= panel.x && m.r[i].y >= panel.y, "bracket " + std::to_string(i) + " runs off the panel" + at);
      check(m.r[i].x + m.r[i].width <= panel.width && m.r[i].y + m.r[i].height <= panel.height,
            "bracket " + std::to_string(i) + " runs past the panel edge" + at);
    }
    // The layout invariant the strings then ride on.
    check(wallpapersui::markerBottomExtent(thumb) < cap.y, "brackets reach into the caption row" + at);
    check(cap.y >= thumb.y + thumb.height, "caption starts above the artwork edge" + at);
    check(cap.y + cap.height <= cell.y + cell.height, "caption spills out of the cell" + at);
    // The caption's line box is reserved in every cell, so selecting a tile
    // adds a mark and never re-flows what is underneath it.
    check(cap.height >= target.lineHeight(fui::FONT_SLOT_SMALL),
          "caption row is shorter than the real line height" + at);
  }

  // 1b. A bracket must not reach into a NEIGHBOURING cell. It extends 9px
  //     outside the artwork into a 24px gap, so the clearance is real but thin,
  //     and a bracket bleeding sideways would read as the wrong tile being
  //     selected -- the picker's one job is saying which wallpaper is chosen.
  for (int slot = 0; slot < g.perPage; ++slot) {
    const wallpapersui::MarkerRects m = wallpapersui::markerRects(wallpapersui::thumbRect(g, slot));
    for (int other = 0; other < g.perPage; ++other) {
      if (other == slot) continue;
      const fui::Rect theirThumb = wallpapersui::thumbRect(g, other);
      const fui::Rect theirCap = wallpapersui::captionRect(g, other);
      const std::string at = " (slot " + std::to_string(slot) + " into slot " + std::to_string(other) + ")";
      for (int i = 0; i < wallpapersui::MarkerRects::kCount; ++i) {
        check(!overlaps(m.r[i], theirThumb), "bracket reaches a neighbour's artwork" + at);
        check(!overlaps(m.r[i], theirCap), "bracket reaches a neighbour's caption" + at);
      }
    }
  }

  // 2. Every built-in name, measured in the face the panel uses, in every slot.
  int widest = 0;
  std::string widestName;
  check(wallpapers::builtInCount() == 21, "built-in count changed; the starter set and this proof disagree");
  for (size_t i = 0; i < wallpapers::builtInCount(); ++i) {
    const std::string stem = wallpapers::builtInStem(i);
    const wallpapers::DisplayName name = wallpapers::displayName(stem + ".bmp");
    for (int slot = 0; slot < g.perPage; ++slot) {
      const fui::Rect cap = wallpapersui::captionRect(g, slot);
      const std::string drawn = drawnCaption(target, name, cap.width, caption);
      const std::string at = " [" + name.full + " @ slot " + std::to_string(slot) + "]";
      // No ellipsis: the fallback is a shorter NAME, never a cut word.
      check(drawn == name.full || drawn == name.brief, "caption was elided" + at + " -> \"" + drawn + "\"");
      const int w = target.widthOf(fui::FONT_SLOT_SMALL, drawn);
      if (w > widest) {
        widest = w;
        widestName = drawn;
      }
      check(w <= cap.width, "caption overflows its box by real widths" + at + " (" + std::to_string(w) + " > " +
                                std::to_string(cap.width) + ")");
      // One line only: a wrapped caption would grow into the bracket row.
      check(drawn.find('\n') == std::string::npos, "caption wrapped to a second line" + at);
    }
  }

  // 3. The "+ Add wallpaper" tile. It cannot be selected today (drawGrid draws
  //    it and continues before the marker branch), so this is the assertion
  //    that keeps that true if the tile ever becomes selectable.
  for (int slot = 0; slot < g.perPage; ++slot) {
    const fui::Rect cap = wallpapersui::captionRect(g, slot);
    std::string add = toybox::fitLines(target, "Add wallpaper", cap.width, 1, caption);
    if (add != "Add wallpaper") add = "Add";
    const std::string at = " [add tile @ slot " + std::to_string(slot) + "]";
    check(add == "Add wallpaper" || add == "Add", "add-tile label was elided" + at);
    check(target.widthOf(fui::FONT_SLOT_SMALL, add) <= cap.width, "add-tile label overflows its box" + at);
  }

  // 5. The progress bar never goes backwards.
  //
  // On hardware the bar filled 0->100, RESET, and filled again, which reads as
  // the download restarting. The cause was two real phases (fetch, then unpack)
  // each driving the same widget over its own full range. This walks the entire
  // sequence the device produces and asserts the fill is monotonic and bounded
  // -- the property that was violated, expressed as arithmetic so it can be
  // checked without a panel, since the panel is the only place it was visible.
  {
    const int total = static_cast<int>(wallpapers::kBuiltInCount);
    int previous = -1;
    const int phases = 3;  // fetch, unpack, thumbnails
    for (int phase = 0; phase < phases; ++phase) {
      for (int done = 0; done <= total; ++done) {
        wallpapersui::FetchingModel m;
        m.total = total;
        m.done = done;
        m.phase = phase;
        m.phaseCount = phases;
        const wallpapersui::BarSpan span = wallpapersui::fetchBarSpan(m);
        const std::string at = " (phase " + std::to_string(phase) + " at " + std::to_string(done) + ")";
        check(span.at >= previous, "the progress bar went BACKWARDS" + at);
        check(span.at <= span.units, "the progress bar overran its track" + at);
        check(span.units == total * phases, "the bar does not span every phase" + at);
        previous = span.at;
      }
    }
    // And it actually reaches the end, rather than stopping at half.
    wallpapersui::FetchingModel done{};
    done.total = total;
    done.done = total;
    done.phase = phases - 1;
    done.phaseCount = phases;
    const wallpapersui::BarSpan end = wallpapersui::fetchBarSpan(done);
    check(end.at == end.units, "the bar does not reach full when the last phase finishes");
  }

  // 6. Moving the selection must NOT change the surface's meaning.
  //
  // The gate (RevealedInteractions.h, SurfaceGate::routable) refuses a tap while
  // a paint is in flight IF the meaning moved. Selecting a wallpaper used to
  // move it, so every tap was followed by one refresh in which every further tap
  // was silently dropped -- "I'm being denied touch until the brackets have
  // finished drawing". The selection remaps no cell, so it must not gate.
  //
  // The things that DO remap a cell still have to gate, or a tap during a page
  // turn opens whatever slid under the finger. Both halves are asserted.
  {
    const uint32_t base = wallpapersui::gridMeaning(0, 0, 21, 1, false);
    check(wallpapersui::gridMeaning(0, 0, 21, 1, false) == base, "gridMeaning is not stable for identical inputs");

    // Changing the page, the view, the library size or the chrome-tile count
    // REMAPS cells, so each must change the meaning.
    check(wallpapersui::gridMeaning(1, 0, 21, 1, false) != base, "a page turn does not gate taps");
    check(wallpapersui::gridMeaning(0, 1, 21, 1, false) != base, "a view change does not gate taps");
    check(wallpapersui::gridMeaning(0, 0, 22, 1, false) != base, "a library change does not gate taps");
    check(wallpapersui::gridMeaning(0, 0, 21, 2, false) != base, "a chrome-tile change does not gate taps");
    // Choose-a-set mode changes what a CELL does -- pin one, or toggle its
    // membership -- so a tap left against the previous frame must not act on
    // the new meaning.
    check(wallpapersui::gridMeaning(0, 0, 21, 1, true) != base, "choose-a-set mode does not gate taps");

    // And the signature that mattered: nothing in gridMeaning takes the
    // selection, so there is no argument by which it could gate. Asserted by
    // walking every selection a 21-wallpaper library can have and confirming the
    // meaning for that page never moves.
    for (int page = 0; page < 6; ++page) {
      const uint32_t forPage = wallpapersui::gridMeaning(page, 0, 21, 1, false);
      for (int selected = -1; selected < 21; ++selected) {
        // The old meaning mixed (selected + 1) in here; the new one cannot.
        check(wallpapersui::gridMeaning(page, 0, 21, 1, false) == forPage,
              "the selection moved the surface meaning, so taps will be refused mid-paint (page " +
                  std::to_string(page) + ", selected " + std::to_string(selected) + ")");
      }
    }
  }

  // The margin left, stated rather than implied: the next name added has this
  // much room before the fallback to the short form kicks in.
  // 4. User uploads. These have no entry in the built-in table, so the caption
  //    falls back to the file's own stem: an arbitrary string this app never
  //    chose. It may be ellipsised -- there is no short form to invent for
  //    "DSC_00417_final_v2" -- but it must still be ONE line inside the box,
  //    because a caption that wrapped would grow up into the bracket row. The
  //    unbreakable single word is the case that matters: fitLines breaks on
  //    spaces only, so a long stem with none has no break to take.
  const char* uploads[] = {
      "DSC_00417_final_v2.bmp",
      "a-really-long-holiday-photo-name-from-a-phone.bmp",
      "supercalifragilisticexpialidociouswallpaper.bmp",
      "SCREENSHOT 2026 09 05 AT 14 23 07.bmp",
      "x.bmp",
      ".bmp",
  };
  for (const char* file : uploads) {
    const wallpapers::DisplayName name = wallpapers::displayName(file);
    for (int slot = 0; slot < g.perPage; ++slot) {
      const fui::Rect cap = wallpapersui::captionRect(g, slot);
      const std::string drawn = drawnCaption(target, name, cap.width, caption);
      const std::string at = std::string(" [upload ") + file + " @ slot " + std::to_string(slot) + "]";
      check(target.widthOf(fui::FONT_SLOT_SMALL, drawn) <= cap.width, "upload caption overflows its box" + at);
      check(drawn.find('\n') == std::string::npos, "upload caption wrapped to a second line" + at);
    }
  }

  // 5. The hint strip's sentences, measured in the real face (#354).
  //
  //    The strip is ONE fixed line, kHintH tall, and buildGridChrome pins its
  //    style to FONT_SLOT_SMALL. In the chrome's face set that is the bottom
  //    rung, so fittedTitle -- which only steps DOWN -- has nothing left and
  //    its only move is an ellipsis. A cut sentence in the strip that exists to
  //    explain why a wallpaper is not showing is worse than no sentence at all,
  //    so every string that can land there is measured here.
  //
  //    The pin matters as much as the width. Without it the style carries
  //    themeTokens().smallText.font, which is FONT_SLOT_BODY -- toybox_20 here,
  //    whose line box is TALLER than the strip. fittedTitle would then step a
  //    long sentence down to 14 and leave a short one at 20, so which sentences
  //    overflowed the strip vertically depended on how long they were.
  //
  //    Measured against the panel inset by the X4 Pro's bezel (T10 R1 B0 L1,
  //    the bezel-insets memory), which is narrower than a bare 480 -- so the
  //    number here is the device's, not the emulator's.
  {
    const ChromeFontTarget chrome;
    const fui::Rect bezelSafe = fui::makeRect(1, 10, 478, 790);
    const int16_t stripWidth = wallpapersui::hintTextWidth(bezelSafe);

    // The strip's line box has to FIT the strip, which is the half a width
    // measurement cannot see. buildGridChrome pins the style to
    // FONT_SLOT_SMALL; the two checks below are why, and they are what a
    // future session deleting that pin has to argue with.
    check(chrome.lineHeight(fui::FONT_SLOT_SMALL) <= wallpapersui::hintStripHeight(),
          "the strip's own face does not fit the strip: lineHeight " +
              std::to_string(chrome.lineHeight(fui::FONT_SLOT_SMALL)) + " in a " +
              std::to_string(wallpapersui::hintStripHeight()) + "px box");
    check(chrome.lineHeight(fui::FONT_SLOT_BODY) > wallpapersui::hintStripHeight(),
          "FONT_SLOT_BODY now fits the strip, so buildGridChrome's pin to the SMALL slot no longer needs to "
          "be there -- re-read the comment before deleting it");
    fui::TextStyle hintStyle;
    hintStyle.font = fui::FONT_SLOT_SMALL;
    hintStyle.align = fui::TextAlign::Left;
    hintStyle.maxLines = 1;

    std::vector<std::string> lines;
    // Every reachHint sentence, walked off the enum rather than typed out.
    for (uint8_t mode = 0; mode < wallpapers::kSleepModeCount; ++mode) {
      for (int qr = 0; qr < 2; ++qr) {
        const char* hint = wallpapers::reachHint(wallpapers::reachOfPinnedSleep(mode, qr != 0));
        if (hint != nullptr) lines.emplace_back(hint);
      }
    }
    // Every post-selection sentence, the same way.
    for (uint8_t mode = 0; mode < wallpapers::kSleepModeCount; ++mode) {
      for (int qr = 0; qr < 2; ++qr) {
        const wallpapers::SleepChoice choice = wallpapers::choiceForSetWallpaper(mode, qr != 0);
        const wallpapers::StripLine note = wallpapers::stripLineAfterSelection(
            choice, wallpapers::reachOfPinnedSleep(choice.sleepScreenMode, choice.quickResumeAfterTimeout));
        if (note.text != nullptr) lines.emplace_back(note.text);
      }
    }
    // Every set sentence, walked off its own arguments the same way. The count
    // ones are measured WITH the widest number this app can put in front of
    // them: kMaxLibrary is 256, so three digits and a space, and a sentence
    // that fits bare and not with "256 " on it is a sentence the panel cuts on
    // the one card that has the most wallpapers on it.
    for (uint8_t mode = 0; mode < wallpapers::kSleepModeCount; ++mode) {
      for (int qr = 0; qr < 2; ++qr) {
        const wallpapers::Reach reach = wallpapers::reachOfPinnedSleep(mode, qr != 0);
        for (int choosing = 0; choosing < 2; ++choosing) {
          for (int shadowed = 0; shadowed < 2; ++shadowed) {
            for (int n = 0; n < 4; ++n) {
              const wallpapers::ShuffleLine set = wallpapers::shuffleStripLine(choosing != 0, n, shadowed != 0, reach);
              if (set.text == nullptr) continue;
              lines.emplace_back(set.wantsCount ? std::string("256 ") + set.text : std::string(set.text));
            }
          }
        }
      }
    }
    // And the two the strip already carried, so this check covers the strip
    // rather than only the new arrivals.
    lines.emplace_back("Tap one to set your sleep screen.");
    lines.emplace_back("Card is low on space. Saves may fail.");
    lines.emplace_back("Could not check card space.");
    lines.emplace_back(wallpapersui::chooseHint());
    lines.emplace_back(wallpapersui::liveStripLine());

    int widestHint = 0;
    std::string widestHintText;
    for (const std::string& line : lines) {
      fui::TextStyle style = hintStyle;
      const std::string fitted = toybox::fittedTitle(chrome, line.c_str(), stripWidth, style);
      const int w = chrome.widthOf(fui::FONT_SLOT_SMALL, line);
      if (w > widestHint) {
        widestHint = w;
        widestHintText = line;
      }
      check(fitted == line, "hint strip sentence was cut: \"" + line + "\" -> \"" + fitted + "\"");
      check(line.find('\n') == std::string::npos, "hint strip sentence carries a newline: \"" + line + "\"");
    }
    std::printf("wallcaption: widest hint \"%s\" = %dpx in a %dpx strip (%dpx spare)\n", widestHintText.c_str(),
                widestHint, stripWidth, stripWidth - widestHint);
  }

  // 6. THE HOLD SHEET'S CONTROLS, and the one destructive button behind them.
  //
  //    This fork has destroyed user data by putting a new meaning under a pixel
  //    a finger was already travelling towards (same-pixel-different-action).
  //    The picker is the worst host for that: a plain tap SETS the sleep screen
  //    with no confirmation, and a hold arrives as a tap unless
  //    tapWasHeldLong() says otherwise. So the defence is geometric and it is
  //    asserted here rather than described in a comment.
  //
  //    Walked at BOTH insets: the panel with no bezel, and the X4 Pro's real
  //    T10 R1 B0 L1 glass. Every rect hangs off safeRect(), so an identity that
  //    held at one inset and not the other would be a screen that is safe on a
  //    test target and not on the device.
  {
    fui::DeviceContext bezel = device();
    bezel.safeArea = fui::Insets{10, 1, 0, 1};
    const fui::DeviceContext panels[] = {device(), bezel};
    const char* labels[] = {"no bezel", "X4 Pro bezel"};
    for (int p = 0; p < 2; ++p) {
      const fui::DeviceContext& dev = panels[p];
      const std::string at = std::string(" (") + labels[p] + ")";
      const fui::Rect preview = wallpapersui::sheetPreviewRect(dev);
      const fui::Rect del = wallpapersui::sheetDeleteRect(dev);
      const fui::Rect keep = wallpapersui::confirmKeepRect(dev);
      const fui::Rect kill = wallpapersui::confirmDeleteRect(dev);

      // THE IDENTITY. The confirm's SAFE half occupies exactly the pixels the
      // sheet's DELETE did, so a repeat of the press that opened the confirm --
      // a double tap, an impatient repeat during a 0.3-2s e-ink repaint, a
      // finger that never moved -- cancels. Identical, not merely close: a
      // "nearly" here is a band of pixels with no owner.
      check(keep.x == del.x && keep.y == del.y && keep.width == del.width && keep.height == del.height,
            "the confirm's KEEP is not exactly where the sheet's DELETE was" + at);

      // THE SEPARATION. Reaching the destructive button takes a deliberate move
      // to somewhere nothing was a moment ago.
      check(!overlaps(kill, del), "the confirm's DELETE lands on the sheet's DELETE" + at);
      check(!overlaps(kill, preview), "the confirm's DELETE lands on the sheet's PREVIEW" + at);
      check(!overlaps(preview, del), "the sheet's two buttons overlap each other" + at);

      // Finger targets, on the panel, and in reading order.
      const fui::Rect all[] = {preview, del, kill};
      for (const fui::Rect& r : all) {
        check(r.height >= 44, "a hold-sheet control is under the finger-target minimum" + at);
        check(r.x >= dev.safeRect().x, "a hold-sheet control runs off the left of the safe area" + at);
        check(r.x + r.width <= dev.safeRect().right(), "a hold-sheet control runs off the right" + at);
        check(r.y >= dev.safeRect().y, "a hold-sheet control runs above the safe area" + at);
        check(r.y + r.height <= dev.safeRect().bottom(), "a hold-sheet control runs off the bottom" + at);
      }
      check(del.y > preview.y, "the sheet draws DELETE above PREVIEW" + at);
      check(kill.y > keep.y, "the confirm draws its destructive half above its safe one" + at);

      // The labels fit the buttons in the face that draws them. A label that
      // overflows does not arrive clipped in these cuts -- the faces above
      // toybox_10 carry no U+2026 -- it simply stops, so "DELETE IT" could read
      // as "DELETE I" and mean something else entirely.
      const char* buttonLabels[] = {"PREVIEW", "DELETE", "KEEP IT", "DELETE IT"};
      for (const char* label : buttonLabels) {
        check(target.widthOf(fui::FONT_SLOT_SMALL, label) <= preview.width - 8,
              std::string("button label \"") + label + "\" overflows its button" + at);
      }
    }
  }

  // 7. THE SENTENCE ON THE CONFIRM, measured rather than eyeballed.
  //
  //    This one is here because a render caught what nothing else could: at the
  //    first layout the longest of the four consequences needed six 42px lines
  //    and had five, so it was cut with an ellipsis at "It stays on your sleep
  //    scr..." -- dropping the SECOND clause, the one that only appears for the
  //    wallpaper actually in use, on the screen where it matters most
  //    (a-warning-that-can-vanish). host-tests/ui cannot see it: its target
  //    answers ten pixels a character. Here the widths are the panel's own.
  //
  //    All four combinations, and BOTH insets, because the bezel shortens the
  //    box from the bottom.
  {
    fui::TextStyle prose;
    prose.font = fui::FONT_SLOT_BODY;
    prose.align = fui::TextAlign::Left;
    fui::DeviceContext bezel = device();
    bezel.safeArea = fui::Insets{10, 1, 0, 1};
    const fui::DeviceContext panels[] = {device(), bezel};
    const char* labels[] = {"no bezel", "X4 Pro bezel"};
    for (int p = 0; p < 2; ++p) {
      const fui::Rect box = wallpapersui::confirmProseRect(panels[p]);
      const int16_t lineH = target.lineHeight(fui::FONT_SLOT_BODY);
      const int lines = lineH > 0 ? box.height / lineH : 0;
      check(lines >= 1, std::string("the confirm has no room for its sentence at all (") + labels[p] + ")");
      for (int builtIn = 0; builtIn <= 1; ++builtIn) {
        for (int active = 0; active <= 1; ++active) {
          const std::string said = wallpapers::deleteConsequence(builtIn != 0, active != 0);
          const std::string drawn = toybox::fitLines(target, said.c_str(), box.width, lines, prose);
          check(drawn == said, std::string("the delete confirm cuts its own consequence (builtIn=") +
                                   std::to_string(builtIn) + " active=" + std::to_string(active) + ", " + labels[p] +
                                   ") -> \"" + drawn + "\"");
        }
      }

      // The sheet's own sentence, both forms, in its own (shorter) box. Read
      // from wallpapersui::sheetInstruction rather than copied here: a test
      // holding its own copy of the sentence keeps measuring the old one after
      // the source is edited, and stays green while the panel cuts it.
      const fui::Rect sheetBox = wallpapersui::sheetProseRect(panels[p]);
      const int sheetLines = lineH > 0 ? sheetBox.height / lineH : 0;
      for (int active = 0; active <= 1; ++active) {
        const std::string line = wallpapersui::sheetInstruction(active != 0);
        check(toybox::fitLines(target, line.c_str(), sheetBox.width, sheetLines, prose) == line,
              std::string("the hold sheet cuts its own instruction (active=") + std::to_string(active) + ", " +
                  labels[p] + ")");
      }

      // THE NAME, which is the one string on these screens nobody chose the
      // width of: a wallpaper the user added is named by its FILE. fitLines
      // appends U+2026 on overflow and the faces above toybox_10 carry no
      // ellipsis glyph, so an over-long name does not arrive clipped -- it
      // stops mid-word with a hole where the mark should be, on the screen
      // that is about to delete it (typography-fold). Two lines of the title
      // cut is what buildSheet and buildConfirm give it.
      const fui::Rect nameBox = wallpapersui::sheetHeadRect(panels[p]);
      check(nameBox.height >= target.lineHeight(fui::FONT_SLOT_TITLE) * 2,
            std::string("the name box cannot hold the two title lines it is given (") + labels[p] + ")");
      // The same call the builders make: fittedTitle, which rewrites the style
      // to the cut it chose. That choice is what decides whether an ellipsis is
      // drawable at all, so the test has to see it.
      const auto fitName = [&](const char* name, fui::FontId& chose) {
        fui::TextStyle style;
        style.font = fui::FONT_SLOT_TITLE;
        style.align = fui::TextAlign::Left;
        style.maxLines = 2;
        const std::string drawn = toybox::fittedTitle(target, name, nameBox.width, style);
        chose = style.font;
        return drawn;
      };
      for (size_t i = 0; i < wallpapers::builtInCount(); ++i) {
        const std::string full = wallpapers::displayName(std::string(wallpapers::builtInStem(i)) + ".bmp").full;
        fui::FontId chose = fui::FONT_SLOT_TITLE;
        check(fitName(full.c_str(), chose) == full,
              "the hold sheet cuts a built-in's name [" + full + ", " + labels[p] + "]");
        check(chose == fui::FONT_SLOT_TITLE,
              "a built-in's name had to step off the display cut [" + full + ", " + labels[p] + "]");
      }
      // A user's own file names. The app's own uploader writes w0001.bmp, but
      // File Transfer and a card in a laptop do not, so the ones that matter
      // are the ones a phone or a person produces -- including the long
      // unbreakable single word, which has no space for fitLines to break at.
      const char* ownNames[] = {
          "DSC_00417_final_v2.bmp",
          "a-really-long-holiday-photo-name-from-a-phone.bmp",
          "supercalifragilisticexpialidociouswallpaper.bmp",
          "SCREENSHOT 2026 09 05 AT 14 23 07.bmp",
      };
      for (const char* file : ownNames) {
        const std::string full = wallpapers::displayName(file).full;
        fui::FontId chose = fui::FONT_SLOT_TITLE;
        const std::string drawn = fitName(full.c_str(), chose);
        const std::string at = std::string(" [") + file + ", " + labels[p] + "]";
        // fitLines and fittedTitle return the string UNWRAPPED when it fits --
        // the renderer's own text() does the wrapping, from style.maxLines. So
        // "does it fit" is answered by identity, not by measuring the return as
        // one line, and an earlier version of this block measured it as one line
        // and reported a defect that was its own (tests-that-share-the-bug, in
        // reverse).
        //
        // Nobody chose these widths, so stepping down the ladder is fine and an
        // ellipsis at the bottom rung is fine. What is never fine is a mark in a
        // cut that has no glyph for it: only toybox_10 (FONT_SLOT_SMALL here)
        // carries U+2026, and above it an ellipsised name does not arrive
        // clipped -- it stops with a HOLE after it, on the screen that is about
        // to delete it (typography-fold).
        const bool marked = drawn != full;
        check(!marked || chose == fui::FONT_SLOT_SMALL,
              "a name was ellipsised in a cut with no ellipsis glyph -- it draws as a hole" + at + " -> \"" + drawn +
                  "\"");
        // And whatever cut it landed on, two lines of it must fit the box the
        // builders draw into.
        check(target.lineHeight(chose) * 2 <= nameBox.height, "a user's wallpaper name is taller than its box" + at);
      }

      // And neither box may reach the control under it.
      check(wallpapersui::confirmProseRect(panels[p]).bottom() <= wallpapersui::confirmKeepRect(panels[p]).y,
            std::string("the confirm's sentence runs under KEEP IT (") + labels[p] + ")");
      check(sheetBox.bottom() <= wallpapersui::sheetPreviewRect(panels[p]).y,
            std::string("the sheet's sentence runs under PREVIEW (") + labels[p] + ")");
      check(wallpapersui::sheetHeadRect(panels[p]).bottom() <= sheetBox.y,
            std::string("the name overlaps the sentence under it (") + labels[p] + ")");
    }
  }

  // The strip's lowest line tells the user which control opens a set. The chip
  // is a GLYPH, so the sentence cannot quote it: what it must not do is name a
  // word that is nowhere on the screen, which is what "Tap CHOOSE to pick
  // several." became the moment the word left the band. And the chip's two
  // modes must LOOK different, or it cannot say which one you are in.
  {
    const std::string hint(wallpapersui::chooseHint());
    check(&wallpapersui::chooseChipIcon(false) != &wallpapersui::chooseChipIcon(true),
          "the chip shows the same glyph entering and leaving the mode");
    for (const char* word : {"CHOOSE", "DONE"}) {
      check(hint.find(word) == std::string::npos, std::string("the strip's hint quotes \"") + word +
                                                      "\", which the chip no longer carries: \"" + hint + "\"");
    }
    // No user-facing string in this app promises randomness: upstream's
    // recent-shown window makes a small set a strict cycle, not a shuffle.
    for (const std::string& s : {hint}) {
      check(s.find("huffl") == std::string::npos && s.find("HUFFL") == std::string::npos,
            "a user-facing string promises shuffling: \"" + s + "\"");
      check(s.find("andom") == std::string::npos && s.find("ANDOM") == std::string::npos,
            "a user-facing string promises randomness: \"" + s + "\"");
    }
  }

  // The strip's Live line. It exists because the "Your phone" tile wore the
  // selection marker while the strip went on saying "Tap one to set your sleep
  // screen." -- the marker and the words disagreeing, with nothing to say why,
  // which is card #354's shape exactly.
  {
    const std::string caption(wallpapersui::liveTileCaption());
    const std::string live(wallpapersui::liveStripLine());
    // ONE noun, not two copies of it. The sentence names the tile the marker is
    // on, so renaming the tile has to rename the sentence: a literal typed into
    // the sentence would go on saying "Your phone" after the tile stopped
    // (derived-facts-written-as-literals).
    check(live.compare(0, caption.size(), caption) == 0,
          "the strip's Live line no longer opens with the tile's caption: \"" + live + "\" vs \"" + caption + "\"");
    // And it must not be the line it displaces. A sentence that still invites
    // the user to set what is already set is the defect, whatever it is built
    // from.
    check(live != "Tap one to set your sleep screen.", "the Live line is the line it exists to replace");
    check(live.find("Tap") == std::string::npos,
          "the Live line asks for a tap, which is the sentence it replaces: \"" + live + "\"");
  }

  // The three readers of "how many tiles are there" have to agree. drawGrid and
  // the tap handler both count specialTiles() + the library; pageCount() counted
  // ONE chrome tile, so with the built-in set incomplete (two chrome tiles) a
  // library that lands exactly on a page boundary had a last wallpaper the grid
  // drew and clampPage forbade the page for. Walked rather than spot-checked.
  {
    for (int per = 1; per <= 8; ++per) {
      for (int specials = 1; specials <= 2; ++specials) {
        for (int lib = 0; lib <= 40; ++lib) {
          const int pages = wallpapersui::pageCountFor(specials, lib, per);
          const int tiles = specials + lib;
          check(pages >= 1, "pageCountFor returned no pages at all");
          // Every tile the grid draws is on a page the picker can reach.
          check(pages * per >= tiles, "the last tile is on a page pageCountFor does not count (per " +
                                          std::to_string(per) + ", specials " + std::to_string(specials) +
                                          ", library " + std::to_string(lib) + ")");
          // And not one page more than needed, or the picker shows an empty one.
          check((pages - 1) * per < tiles || tiles == 0,
                "pageCountFor counts a page with nothing on it (per " + std::to_string(per) + ", specials " +
                    std::to_string(specials) + ", library " + std::to_string(lib) + ")");
        }
      }
    }
    // The exact case that was broken: 4 a page, both chrome tiles, three
    // wallpapers -- five tiles, which is two pages and used to be called one.
    check(wallpapersui::pageCountFor(2, 3, 4) == 2, "the incomplete-set page boundary is still miscounted");
  }

  // -------------------------------------------------------------------------
  // THE LIVE SCREEN. Built for real, both states. One arrangement now: the
  // stack is what shipped, and run.sh builds this suite once because there is
  // no longer a compile-time choice for it to walk.
  {
    // The X4 Pro's glass, because the body width is what every string here is
    // measured against and the bezel takes two pixels of it.
    fui::DeviceContext ctx = device();
    ctx.safeArea = fui::Insets{10, 1, 0, 1};
    const fui::Rect panelRect = fui::makeRect(0, 0, ctx.width, ctx.height);
    const fui::InputSnapshot noInput{};

    wallpapersui::LiveModel model;
    model.code = "482 160";
    // THE ADDRESS THE DEVICE REALLY PRINTS, read out of the firmware rather
    // than typed here: it is also what the QR encodes, so a page that moves
    // host must fail this measurement rather than quietly step the line down
    // a cut (derived-facts-written-as-literals).
    model.url = wallpapersui::kLiveAddress;
    // Composed by live::, not typed: see liveHeadlines() above. The whole set
    // is walked in its own block below; this pair carries the rest of this one.
    const LivePhrases sample = liveSample();
    model.nextCheck = sample.headline.c_str();
    model.cadence = sample.note.c_str();
    model.senders[0] = {"Mario's phone", "12 Sep"};
    model.senders[1] = {"Abuela", "18 Sep"};
    model.senderCount = 2;

    for (int state = 0; state < 3; ++state) {
      // 0 unpaired, 1 paired and running, 2 paired and stopped. The third is not
      // padding: the state word and the toggle's label are two readings of one
      // bool, and "LIVE IS ON" over a button offering to turn it on is the
      // defect that pair exists to prevent.
      model.configured = state > 0;
      model.on = state == 1;
      // The line under the headline follows the toggle, because on the device
      // it does: composing it once for the ON case and drawing it over a
      // stopped screen is the model telling a lie this screen would then be
      // asserted to repeat. "Paused" over "Every 6 hours" is the contradiction
      // the layout was built to remove.
      live::Schedule shown;
      shown.on = model.on;
      shown.paired = model.configured;
      shown.intervalSeconds = 21600;
      shown.lastAttemptEpoch = live::kPlausibleEpochFloor + 1000000;
      const std::string stateNote = live::scheduleNote(shown);
      const std::string stateHeadline = live::nextCheckPhrase(shown, shown.lastAttemptEpoch + 600);
      if (model.configured) {
        model.nextCheck = stateHeadline.c_str();
        model.cadence = stateNote.c_str();
      }
      const std::string where = std::string(" [state ") + std::to_string(state) + "]";

      // EVERY fixed status sentence goes through the same checks below, plus
      // the standing lines (si == -1, status = nullptr).
      //
      // Walked from the ENUM rather than a list written here, so a status added
      // to the screen without being added to this test is impossible: the loop
      // runs to kCount and a value with no sentence comes back empty, which
      // fails on the spot.
      //
      // The status is ONE fitted row in both halves -- under the address while
      // a code is up, the foot's row once there is not -- and at these cuts an
      // overflowing sentence neither clips nor ellipsises. It stops somewhere
      // plausible and the screenshot looks fine. One shipped in this screen's
      // first paired render: "A new message is on your sleep screen." arrived
      // as "A new message is on your sleep...".
      for (int si = -1; si < static_cast<int>(wallpapersui::LiveStatus::kCount); ++si) {
        const char* statusLine =
            si < 0 ? nullptr : wallpapersui::liveStatusLine(static_cast<wallpapersui::LiveStatus>(si));
        if (si >= 0) {
          check(statusLine != nullptr && statusLine[0] != '\0',
                "LiveStatus " + std::to_string(si) + " has no sentence, so the screen would report nothing" + where);
        }
        model.status = statusLine;

        LiveTarget target(model.configured);
        toybox::Interactions interactions;
        toybox::Frame frame(target, ctx, noInput, interactions);
        toybox::Screen screen(frame);
        const fui::Rect qr = wallpapersui::buildLive(screen, model);

        if (statusLine != nullptr) {
          bool reported = false;
          for (const LiveTarget::Run& run : target.runs) {
            if (run.text == statusLine) reported = true;
          }
          check(reported, std::string("the status \"") + statusLine +
                              "\" reached no line on this screen, so nothing would report it" + where);
        }

        // 1. EVERY RUN ARRIVES WHOLE. fitLines lays the string out in the box it
        //    was given, with its own line budget, in the face that will draw it;
        //    anything it has to cut comes back different from what went in. This
        //    is the assertion the three truncations would each have failed.
        for (const LiveTarget::Run& run : target.runs) {
          const int lines = run.style.maxLines > 0 ? run.style.maxLines : 1;
          const std::string laid = toybox::fitLines(target, run.text.c_str(), run.box.width, lines, run.style);
          check(laid == run.text, "the panel cuts \"" + run.text + "\" to \"" + laid + "\" in a " +
                                      std::to_string(run.box.width) + "px box" + where);
          // AND IT WAS NOT ALREADY CUT WHEN IT GOT HERE, which the check above
          // cannot see and which is the more common failure by far. drawFitted and
          // drawFoot both run their string through the ladder first, and when the
          // ladder has no rung left it falls through to fitLines and hands text()
          // an ELLIPSISED string -- which then fits its box perfectly. Asserting
          // only that what was drawn fits is a test that shares the bug: the first
          // version of this block stayed green with "This code works for ten..."
          // on the panel. Nothing on this screen legitimately ends in an ellipsis.
          check(run.text.size() < 3 || run.text.compare(run.text.size() - 3, 3, "...") != 0,
                "\"" + run.text + "\" reached the panel already cut to fit" + where);
          check(run.box.x >= panelRect.x && run.box.x + run.box.width <= panelRect.width,
                "a text box runs off the side of the panel" + where);
          check(run.box.y >= panelRect.y && run.box.y + run.box.height <= panelRect.height,
                "a text box runs off the bottom of the panel" + where);
        }

        // 2. THE STRINGS THAT MATTER REACHED IT AT ALL. A run that fits is not a
        //    run that happened: an arrangement that forgot to draw the code would
        //    pass every check above.
        const auto drew = [&target](const std::string& want) {
          for (const LiveTarget::Run& run : target.runs) {
            if (run.text == want) return true;
          }
          return false;
        };
        if (!model.configured) {
          check(drew(model.code), "the pairing code is not on the unpaired screen" + where);
          check(drew(std::string(model.url)), "the address in words is not on the unpaired screen" + where);
          // AND IT IS THE CUT THE SCREEN CHOSE FOR IT. Same silent failure as
          // the headline below, one rung lower: fittedTitle steps DOWN through
          // the bound slots rather than refusing, so an address too wide for the
          // cut it asked for neither cuts nor fails -- it simply arrives set
          // exactly like the paragraph beneath it, and the one line a person has
          // to read off the glass and type into a phone stops standing out at
          // all. kLiveUrlSlot is that decision made deliberately (the reading
          // cut, bold); this holds the screen to it, whichever way the address
          // or the faces move next.
          for (const LiveTarget::Run& run : target.runs) {
            if (run.text != model.url) continue;
            check(run.style.font == fui::FONT_SLOT_BODY && run.style.bold,  // kLiveUrlSlot, bold
                  std::string("the address \"") + model.url +
                      "\" is not the cut the screen chose for it, so it is set like the paragraph under it" + where);
          }
          check(qr.width >= 132 && qr.height >= 132,
                "the QR square is under four module pixels a side, which does not scan" + where);
          check(qr.x >= 0 && qr.y >= 0 && qr.x + qr.width <= panelRect.width && qr.y + qr.height <= panelRect.height,
                "the QR square runs off the panel" + where);
          // Never above: the code is what a person reads down a telephone, and a
          // QR over it makes the screen look like something to scan instead.
          for (const LiveTarget::Run& run : target.runs) {
            if (run.text != model.code) continue;
            check(qr.y >= run.box.y, "the QR sits above the code" + where);
          }
          // And nothing is drawn ON it.
          for (const LiveTarget::Run& run : target.runs) {
            check(!overlaps(run.box, qr), "\"" + run.text + "\" is drawn over the QR" + where);
          }
        } else {
          check(qr.width == 0 && qr.height == 0, "the paired screen asked for a QR it has no code for" + where);
          for (int i = 0; i < model.senderCount; ++i) {
            check(drew(std::string(model.senders[i].who)),
                  std::string("sender \"") + model.senders[i].who + "\" is not on the screen" + where);
            check(drew(std::string(model.senders[i].since)),
                  std::string("sender \"") + model.senders[i].who + "\" has no date beside them" + where);
          }
          check(drew(model.nextCheck), "the next check is not on the paired screen" + where);
          check(drew(model.cadence), "how often is not on the paired screen" + where);
          // THE HEADLINE KEPT THE HEADLINE'S CUT. fittedTitle steps DOWN
          // through the bound slots when a string will not fit, so a next-check
          // phrase an inch too long does not fail -- it quietly arrives at the
          // same size as the small line under it and the screen loses the
          // hierarchy that is the whole point of this layout. Nothing else here
          // could see that: the run fits its box either way.
          for (const LiveTarget::Run& run : target.runs) {
            if (run.text != model.nextCheck) continue;
            check(run.style.font == fui::FONT_SLOT_TITLE,
                  std::string("the headline \"") + model.nextCheck +
                      "\" was stepped down a cut to fit, so it is no longer the biggest thing on the screen" + where);
          }
          // The state and its control, read from one bool in two places. The
          // band carries a STATE and the button a VERB, and the two
          // vocabularies are deliberately different: with one vocabulary both
          // words are on the screen in both states, and an assertion that each
          // is drawn cannot fail however the two are wired together.
          check(drew(model.on ? "ON" : "OFF"), "the paired screen does not say whether Live is on" + where);
          check(drew(model.on ? "STOP" : "START"), "the toggle offers the state the screen is already in" + where);

          // 3. THE CONTROLS ARE TAPPABLE, not merely drawn. A button registered
          //    at no rect is a dead control, which is what ActionAddOwn was on the
          //    offer screen for a whole release ("nothing calls it").
          const fui::ActionId wanted[3] = {wallpapersui::ActionLiveToggle, wallpapersui::ActionLiveCheck,
                                           wallpapersui::ActionLiveAdd};
          fui::Rect hits[3] = {};
          for (int i = 0; i < 3; ++i) {
            for (size_t h = 0; h < interactions.count(); ++h) {
              if (interactions.data()[h].action == wanted[i]) hits[i] = interactions.data()[h].rect;
            }
            check(hits[i].width > 0 && hits[i].height >= ctx.minTouchSize,
                  "Live control " + std::to_string(i) + " is drawn but not tappable" + where);
            check(hits[i].x >= 0 && hits[i].y >= 0 && hits[i].x + hits[i].width <= panelRect.width &&
                      hits[i].y + hits[i].height <= panelRect.height,
                  "Live control " + std::to_string(i) + " is registered off the panel" + where);
          }
          // And no two of them share a pixel. None is destructive, but a control
          // whose rect covers another's is a control you cannot press.
          for (int i = 0; i < 3; ++i) {
            for (int j = i + 1; j < 3; ++j) {
              check(!overlaps(hits[i], hits[j]),
                    "Live controls " + std::to_string(i) + " and " + std::to_string(j) + " overlap" + where);
            }
          }

          // 3b. EACH CONTROL CARRIES A MARK, AND THE THREE MARKS ARE THREE.
          //
          // The words are one apiece now, so the mark is half of what tells the
          // controls apart -- and three controls wearing the same glyph is a row
          // that says nothing, the same defect chooseChipIcon's two-glyph
          // assertion exists for. Neither is visible to a check that only reads
          // text, and a bitmap() that recorded nothing kept all of it green.
          const uint8_t* controlMarks[3] = {nullptr, nullptr, nullptr};
          for (int i = 0; i < 3; ++i) {
            int found = 0;
            for (const LiveTarget::Mark& mark : target.marks) {
              if (!contains(hits[i], mark.box)) continue;
              ++found;
              controlMarks[i] = mark.data;
            }
            check(found == 1, "Live control " + std::to_string(i) + " carries " + std::to_string(found) +
                                  " marks instead of one" + where);
          }
          for (int i = 0; i < 3; ++i) {
            for (int j = i + 1; j < 3; ++j) {
              check(controlMarks[i] != nullptr && controlMarks[i] != controlMarks[j],
                    "Live controls " + std::to_string(i) + " and " + std::to_string(j) +
                        " wear the same mark, so the row cannot say which is which" + where);
            }
          }

          // 3c. AND THE THREE LABELS ARE ONE TYPEFACE.
          //
          // Found by opening the render, not by any check here: the labels took
          // theme().smallText, which resolves to the PROSE face, so fittedTitle
          // kept "STOP" and "ADD" in the serif and shrank "CHECK" to the button
          // cut because it alone did not fit. One row, two typefaces, and
          // nothing failed -- every label fitted the box it was given, which is
          // all any assertion here was asking. Three words that are meant to be
          // read as a set have to be set alike, and the ladder will do this
          // again to whatever the words become.
          // The cut is named OUTRIGHT rather than compared between the three,
          // and a missing label is counted rather than inferred from the array.
          // FONT_SLOT_SMALL is 0 and so is a zero-initialised FontId, so
          // "nothing was drawn in this button" and "drawn at the button cut"
          // were the same value: suppressing two of the three label draws
          // entirely left three zeros, which agreed with each other, and the
          // suite passed with two empty buttons on the panel.
          for (int i = 0; i < 3; ++i) {
            int labels = 0;
            for (const LiveTarget::Run& run : target.runs) {
              if (!contains(hits[i], run.box)) continue;
              ++labels;
              check(run.style.font == fui::FONT_SLOT_SMALL,
                    "Live control " + std::to_string(i) + "'s label \"" + run.text +
                        "\" is not set in the button cut, so the row is two typefaces" + where);
            }
            check(labels == 1, "Live control " + std::to_string(i) + " carries " + std::to_string(labels) +
                                   " labels instead of one word" + where);
          }
          // And the words themselves are on the panel. A mark with no word
          // beside it is the thing this screen is not allowed to be.
          check(drew("CHECK"), "the check control has lost its word" + where);
          check(drew("ADD"), "the add control has lost its word" + where);
        }
        check(interactions.count() <= toybox::kMaxInteractions,
              "the Live screen overflows the interaction table" + where);
      }
    }
  }

  // -------------------------------------------------------------------------
  // THE SENDER LIST, the confirm that guards it, and the empty state.
  //
  // This is the half of the Live screen that TAKES SOMEBODY'S ACCESS AWAY, and
  // the person it happens to is in another country and is told nothing. So the
  // assertions here are about two things a screenshot cannot show: that a name
  // arrives whole (a sender's name that stops mid-word is a list of people with
  // a person cut out of it, and no Toybox cut above toybox_10 carries an
  // ellipsis to mark it), and that the confirm's two halves sit where the
  // safety argument says they sit.
  {
    fui::DeviceContext ctx = device();
    ctx.safeArea = fui::Insets{10, 1, 0, 1};
    const fui::Rect panelRect = fui::makeRect(0, 0, ctx.width, ctx.height);
    const fui::InputSnapshot noInput{};

    // THE REAL CORPUS, not names invented here. The browser names itself from
    // its user agent and browserName() in site/live/live.js
    // has exactly seven outputs; the service then caps anything else at 24
    // characters (store.add_sender: name[:24]), which is the backstop against a
    // hand-written POST. So the widest case this screen can be handed is 24
    // characters, and it is walked here beside the seven real ones.
    const char* kRealNames[] = {"iPhone", "iPad", "Android phone", "Mac", "Windows PC", "Linux", "A phone"};
    const char* kWidest = "WWWWWWWWWWWWWWWWWWWWWWWW";  // 24, the service's cap, at its widest glyph

    // Is this run the name `want`, drawn whole or visibly cut?
    //
    // A name the panel cannot hold is cut with toybox's "..." -- three ASCII
    // periods, which every cut in this face set can really draw. That matters
    // more than it looks: no Toybox face above toybox_10 carries U+2026, so a
    // unicode ellipsis would be a HOLE and the name would stop at a plausible
    // place with the screenshot looking fine (typography-fold). So a name is
    // accounted for if it is there verbatim, or there as its own prefix
    // followed by the mark -- and never as a bare prefix.
    const auto isName = [](const std::string& run, const std::string& want) {
      if (run == want) return true;
      if (run.size() < 4 || run.compare(run.size() - 3, 3, "...") != 0) return false;
      const std::string body = run.substr(0, run.size() - 3);
      return !body.empty() && body.size() < want.size() && want.compare(0, body.size(), body) == 0;
    };

    // ---------------------------------------------------------------------
    // THE FULL LIST. Four phones, which is the cap, so this is the tallest the
    // screen can be -- and the state that pushed the add control off the bottom
    // when the rows were still untappable text.
    for (int names = 0; names < 2; ++names) {
      const bool widest = names != 0;
      wallpapersui::LiveModel model;
      model.configured = true;
      model.on = true;
      const LivePhrases sample = liveSample();
      model.nextCheck = sample.headline.c_str();
      model.cadence = sample.note.c_str();
      model.senderCount = wallpapersui::LiveModel::kMaxSenders;
      for (int i = 0; i < model.senderCount; ++i) {
        model.senders[i].who = widest ? kWidest : kRealNames[i];
        model.senders[i].since = "12 Sep";
      }
      const std::string where = widest ? " [24-char names]" : " [real names]";

      LiveTarget target(true);
      toybox::Interactions interactions;
      toybox::Frame frame(target, ctx, noInput, interactions);
      toybox::Screen screen(frame);
      wallpapersui::buildLive(screen, model);

      // 1. EVERY RUN WHOLE AND ON THE PANEL. The same two assertions the
      //    screen's other half gets, because the names are the one set of
      //    strings on it that nobody here chose the width of.
      for (const LiveTarget::Run& run : target.runs) {
        const int lines = run.style.maxLines > 0 ? run.style.maxLines : 1;
        const std::string laid = toybox::fitLines(target, run.text.c_str(), run.box.width, lines, run.style);
        check(laid == run.text, "the panel cuts \"" + run.text + "\" to \"" + laid + "\" in a " +
                                    std::to_string(run.box.width) + "px box" + where);
        check(run.box.y >= 0 && run.box.y + run.box.height <= panelRect.height,
              "\"" + run.text + "\" runs off the bottom of the panel" + where);
      }
      // Every FIXED string arrives uncut, the same assertion the screen's other
      // half gets. A NAME is exempt and the exemption is the point: it comes
      // from a browser's user agent, so it is the one string here nobody chose
      // the width of, and a service that raised its 24-character cap could hand
      // this screen anything. What a name may not do is stop SILENTLY, which is
      // asserted below.
      for (const LiveTarget::Run& run : target.runs) {
        bool fromAName = false;
        for (int i = 0; i < model.senderCount; ++i) {
          if (isName(run.text, model.senders[i].who)) fromAName = true;
        }
        if (fromAName) continue;
        check(run.text.size() < 3 || run.text.compare(run.text.size() - 3, 3, "...") != 0,
              "\"" + run.text + "\" reached the panel already cut to fit" + where);
      }

      // 2. EVERY NAME IS THERE, whole or visibly marked. A row that fits is
      //    not a row that happened.
      for (int i = 0; i < model.senderCount; ++i) {
        bool shown = false;
        for (const LiveTarget::Run& run : target.runs) {
          if (isName(run.text, model.senders[i].who)) shown = true;
        }
        check(shown, std::string("sender \"") + model.senders[i].who +
                         "\" is neither on the full list nor visibly cut from it" + where);
      }
      // EVERY ROW ENDS IN THE REMOVE MARK, exactly once. This replaces the
      // "TAP TO REMOVE" heading the list used to carry: without some
      // affordance the four names are a list of facts, and the only way to
      // discover that pressing one does anything is to press one -- on the half
      // of the screen that takes somebody's access away. The mark is the
      // affordance now, so the mark is what is asserted.
      for (int i = 0; i < model.senderCount; ++i) {
        const fui::Rect row = wallpapersui::liveSenderRowRect(screen, i);
        int marks = 0;
        for (const LiveTarget::Mark& mark : target.marks) {
          if (contains(row, mark.box) && mark.data == wallpapersui::liveRemoveMark().bits) ++marks;
        }
        check(marks == 1, "sender row " + std::to_string(i) + " carries " + std::to_string(marks) +
                              " remove marks instead of one, so the row does not say it is a control" + where);
      }

      // 3. EVERY ROW IS TAPPABLE, at the rect the confirm reads back, carrying
      //    its own index. A row drawn and not registered is a control that does
      //    nothing, which is what ActionAddOwn was for a whole release.
      const fui::Rect band = wallpapersui::liveSendersBand(screen);
      for (int i = 0; i < model.senderCount; ++i) {
        const fui::Rect want = wallpapersui::liveSenderRowRect(screen, i);
        bool found = false;
        for (size_t h = 0; h < interactions.count(); ++h) {
          const auto& hit = interactions.data()[h];
          if (hit.action != wallpapersui::ActionLiveSender || hit.value != i) continue;
          found = true;
          check(hit.rect.x == want.x && hit.rect.y == want.y && hit.rect.width == want.width &&
                    hit.rect.height == want.height,
                "sender row " + std::to_string(i) + " is registered somewhere other than where it is drawn" + where);
        }
        check(found, "sender row " + std::to_string(i) + " is drawn but not tappable" + where);
        check(want.height >= ctx.minTouchSize,
              "sender row " + std::to_string(i) + " is under a finger tall, so it is a control that misses" + where);
        check(want.y >= band.y && want.bottom() <= band.bottom(),
              "sender row " + std::to_string(i) + " falls outside the band the confirm reserves for it" + where);
        // The value is never negative: a negative action value is dead to touch
        // in this fork, so a row carrying one would silently not exist.
        check(i >= 0, "a sender row would carry a negative action value" + where);
      }
      for (int i = 0; i < model.senderCount; ++i) {
        for (int j = i + 1; j < model.senderCount; ++j) {
          check(!overlaps(wallpapersui::liveSenderRowRect(screen, i), wallpapersui::liveSenderRowRect(screen, j)),
                "sender rows " + std::to_string(i) + " and " + std::to_string(j) + " share pixels" + where);
        }
      }

      // 4. AND THE THREE ORDINARY CONTROLS SURVIVED THE LIST. The full list is
      //    the state that used to push the add control off the bottom of an
      //    800px panel -- the control that adds a phone, gone on the screen
      //    that has four of them.
      const fui::ActionId wanted[3] = {wallpapersui::ActionLiveToggle, wallpapersui::ActionLiveCheck,
                                       wallpapersui::ActionLiveAdd};
      for (const fui::ActionId id : wanted) {
        fui::Rect hit{};
        for (size_t h = 0; h < interactions.count(); ++h) {
          if (interactions.data()[h].action == id) hit = interactions.data()[h].rect;
        }
        check(hit.width > 0 && hit.height >= ctx.minTouchSize,
              "a Live control is not tappable with four phones listed" + where);
        check(hit.y >= 0 && hit.bottom() <= panelRect.height,
              "a Live control is registered off the panel with four phones listed" + where);
      }
      check(interactions.count() <= toybox::kMaxInteractions,
            "the full sender list overflows the interaction table" + where);
      // The interaction budget, REPORTED rather than merely bounded: three
      // controls and four rows is seven of twenty-four, and a number printed
      // every run is what makes the next person's spend visible to them.
      slotsAtFullList = static_cast<int>(interactions.count());

      // 5. AND THE STRIP THE CONFIRM'S REMOVE LANDS ON CARRIES NOTHING HERE.
      //
      // It used to be ADD SOMEBODY's pixels, which was accepted with an
      // argument about the reveal gate refusing taps against a table the panel
      // has not shown. Moving the third control up beside the other two ended
      // that overlap, and this is the assertion that keeps it ended -- the
      // header claims it in prose, and a claim nothing checks goes stale the
      // first time the layout moves.
      const fui::Rect revoke = wallpapersui::liveRevokeRect(screen);
      for (size_t h = 0; h < interactions.count(); ++h) {
        check(!overlaps(revoke, interactions.data()[h].rect),
              "a control on the list sits where the confirm's REMOVE will be" + where);
      }
    }

    // ---------------------------------------------------------------------
    // EVERY HEADLINE THE DEVICE CAN COMPOSE, IN THE FACE THAT DRAWS IT.
    //
    // This is the assertion the screen's hierarchy rests on, and it is the one
    // nothing else here can make. fittedTitle does not refuse a title too wide
    // for its cut -- it steps DOWN a rung and returns it whole -- so a headline
    // an inch too long passes every "does the text fit" check in this file and
    // arrives the same size as the small line under it. "In about 45 minutes"
    // is 464px at toybox_30 against a 448px body, which is how close this is.
    //
    // Walked over every interval the service may ask for, at several moments
    // inside each, both toggle positions and the backoff, with the phrases
    // composed by live:: rather than typed here.
    {
      const std::vector<LivePhrases> phrases = liveHeadlines();
      check(phrases.size() > 100, "the headline sweep is too small to have covered the bands");
      for (const LivePhrases& pair : phrases) {
        wallpapersui::LiveModel model;
        model.configured = true;
        model.on = true;
        model.nextCheck = pair.headline.c_str();
        model.cadence = pair.note.c_str();
        model.senderCount = 2;
        model.senders[0] = {kRealNames[0], "12 Sep"};
        model.senders[1] = {kWidest, "12 Sep"};

        LiveTarget target(true);
        toybox::Interactions interactions;
        toybox::Frame frame(target, ctx, noInput, interactions);
        toybox::Screen screen(frame);
        wallpapersui::buildLive(screen, model);

        bool headlineDrawn = false;
        bool noteDrawn = false;
        for (const LiveTarget::Run& run : target.runs) {
          if (run.text == pair.headline) {
            headlineDrawn = true;
            check(run.style.font == fui::FONT_SLOT_TITLE,
                  "the headline \"" + pair.headline +
                      "\" was stepped down a cut to fit, so it is no longer the biggest thing on the screen");
          }
          if (run.text == pair.note) noteDrawn = true;
          const int lines = run.style.maxLines > 0 ? run.style.maxLines : 1;
          const std::string laid = toybox::fitLines(target, run.text.c_str(), run.box.width, lines, run.style);
          check(laid == run.text,
                "the panel cuts \"" + run.text + "\" to \"" + laid + "\" with headline \"" + pair.headline + "\"");
        }
        check(headlineDrawn, "the headline \"" + pair.headline + "\" never reached the panel");
        check(noteDrawn, "the line \"" + pair.note + "\" under headline \"" + pair.headline + "\" never reached it");
        // The two lines must not say the same thing. "Paused" over "Every 6
        // hours" and "In 15 minutes" over "Every week" are both the screen
        // contradicting itself, and both shipped before this check existed.
        check(pair.headline != pair.note, "the headline and the line under it are the same string");
      }
    }

    // ---------------------------------------------------------------------
    // THE EMPTY LIST. A reader whose last phone was just removed. It is
    // RECOVERABLE, not broken: the picture stays on the glass and ADD is
    // still there. An empty region where content belongs is this fork's most
    // repeated user-visible failure, reported as a crash by cold testers twice.
    {
      wallpapersui::LiveModel model;
      model.configured = true;
      model.on = true;
      const LivePhrases sample = liveSample();
      model.nextCheck = sample.headline.c_str();
      model.cadence = sample.note.c_str();
      model.senderCount = 0;

      LiveTarget target(true);
      toybox::Interactions interactions;
      toybox::Frame frame(target, ctx, noInput, interactions);
      toybox::Screen screen(frame);
      wallpapersui::buildLive(screen, model);

      bool saidSo = false;
      for (const LiveTarget::Run& run : target.runs) {
        if (run.text == wallpapersui::liveNobodySends()) saidSo = true;
        const int lines = run.style.maxLines > 0 ? run.style.maxLines : 1;
        const std::string laid = toybox::fitLines(target, run.text.c_str(), run.box.width, lines, run.style);
        check(laid == run.text, "the empty list cuts \"" + run.text + "\" to \"" + laid + "\"");
      }
      check(saidSo, "a reader with no senders says nothing, so it reads as a screen that failed to load");
      // And the way out is still on it.
      bool canAdd = false;
      for (size_t h = 0; h < interactions.count(); ++h) {
        if (interactions.data()[h].action == wallpapersui::ActionLiveAdd) canAdd = true;
        check(interactions.data()[h].action != wallpapersui::ActionLiveSender,
              "an empty list registered a sender row, so a tap would confirm removing nobody");
      }
      check(canAdd, "a reader with no senders cannot add one, which is a dead end rather than an empty list");
      // And no remove mark is drawn: a mark that says "take this one out" over
      // a list with nothing in it is an affordance for a control that is not on
      // the screen.
      for (const LiveTarget::Mark& mark : target.marks) {
        check(mark.data != wallpapersui::liveRemoveMark().bits,
              "the empty list still draws a remove mark, so it offers to take out a row that is not there");
      }
    }

    // ---------------------------------------------------------------------
    // ADD's CODE. A paired reader showing a six-digit number: the same
    // screen the first setup code uses, which is what liveShowsCode exists to
    // keep true in both the face the Activity binds and the stack this builds.
    {
      wallpapersui::LiveModel model;
      model.configured = true;
      model.on = true;
      model.joining = true;
      model.code = "482 160";
      model.url = wallpapersui::kLiveAddress;
      const LivePhrases sample = liveSample();
      model.nextCheck = sample.headline.c_str();
      model.cadence = sample.note.c_str();
      model.senderCount = 1;
      model.senders[0] = {"iPhone", "12 Sep"};

      check(wallpapersui::liveShowsCode(model),
            "a paired reader minting a join code does not report that it is showing one, so the Activity would "
            "bind the button cut and draw a telephone number at 20px");
      LiveTarget target(false);  // the code cut, exactly as the Activity binds it
      toybox::Interactions interactions;
      toybox::Frame frame(target, ctx, noInput, interactions);
      toybox::Screen screen(frame);
      const fui::Rect qr = wallpapersui::buildLive(screen, model);

      bool drewCode = false;
      for (const LiveTarget::Run& run : target.runs) {
        if (run.text == model.code) drewCode = true;
        const int lines = run.style.maxLines > 0 ? run.style.maxLines : 1;
        const std::string laid = toybox::fitLines(target, run.text.c_str(), run.box.width, lines, run.style);
        check(laid == run.text, "the join screen cuts \"" + run.text + "\" to \"" + laid + "\"");
        check(run.box.y >= 0 && run.box.y + run.box.height <= panelRect.height,
              "\"" + run.text + "\" runs off the panel on the join screen");
      }
      check(drewCode, "the join code is not on the screen");
      check(qr.width > 0 && qr.height > 0, "the join screen asks for no QR, so the code cannot be scanned");
      // It says WHICH code this is. Both halves of the screen are six digits
      // under the word LIVE, and the person typing this one has to know it adds
      // a phone rather than replacing the one already sending.
      bool saidWhichCode = false;
      for (const LiveTarget::Run& run : target.runs) {
        if (run.text == wallpapersui::liveJoinPrompt()) saidWhichCode = true;
      }
      check(saidWhichCode,
            "the join code screen never says this code ADDS a phone, so it reads as the setup code that "
            "replaces one");
      // And no sender row is live behind it: the list is not on this screen, so
      // a remembered tap must not reach a row that is not drawn.
      for (size_t h = 0; h < interactions.count(); ++h) {
        check(interactions.data()[h].action != wallpapersui::ActionLiveSender,
              "a sender row is registered on the join code screen, where no row is drawn");
      }
    }

    // ---------------------------------------------------------------------
    // THE SERVICE'S OWN SENTENCES, in the region that has to hold them.
    //
    // A bridge refusal is drawn verbatim: the device does not get to reword a
    // decision somebody else made (BridgeHttp.h). So these are not the device's
    // strings to shorten, and the screen has to be built to take them. The list
    // is GENERATED from server/fridge-bridge/bridge/app.py by run.sh, because a
    // copy typed here would go on measuring the old sentences after the service
    // edited one and stay green while the panel cut the new one.
    //
    // The failure this catches is one a screenshot showed and no assertion did:
    // "This reader already has 4 phones. Remove one first." reached the panel as
    // "This reader already has 4 phones...." -- drawn on the single foot line,
    // with the only actionable half of it gone.
    // AND THE DEVICE'S OWN, which were never measured anywhere at all. They
    // reach the same region by the same path: checkNow hands a transport
    // sentence straight to liveStatus_. The longest is 84 characters about a
    // refused certificate, which is longer than anything the service sends.
    // AND THE PENDING CADENCE, which is neither: it is a sentence the SERVICE
    // composes and the reader draws verbatim on this same screen, saying the
    // schedule has moved and the device has not noticed yet. It is generated
    // from store.ALLOWED_INTERVALS crossed with the template, so the whole
    // corpus is measured rather than the one example somebody looked at.
    std::vector<const char*> pairedReports;
    for (const char* s : kServiceRefusals) pairedReports.push_back(s);
    for (const char* s : kDeviceSentences) pairedReports.push_back(s);
    for (const char* s : kPendingSentences) pairedReports.push_back(s);
    for (const char* refusal : pairedReports) {
      wallpapersui::LiveModel model;
      model.configured = true;
      model.on = true;
      const LivePhrases sample = liveSample();
      model.nextCheck = sample.headline.c_str();
      model.cadence = sample.note.c_str();
      model.status = refusal;
      // The FULL list, because that is when the report region is most boxed in
      // and when the cap refusal actually happens.
      model.senderCount = wallpapersui::LiveModel::kMaxSenders;
      for (int i = 0; i < model.senderCount; ++i) model.senders[i] = {kRealNames[i], "12 Sep"};

      LiveTarget target(true);
      toybox::Interactions interactions;
      toybox::Frame frame(target, ctx, noInput, interactions);
      toybox::Screen screen(frame);
      wallpapersui::buildLive(screen, model);

      bool reported = false;
      for (const LiveTarget::Run& run : target.runs) {
        if (run.text == std::string(refusal)) reported = true;
        const int lines = run.style.maxLines > 0 ? run.style.maxLines : 1;
        const std::string laid = toybox::fitLines(target, run.text.c_str(), run.box.width, lines, run.style);
        check(laid == run.text, std::string("the panel cuts the service's \"") + run.text + "\" to \"" + laid +
                                    "\" in a " + std::to_string(run.box.width) + "px box");
        check(run.box.y >= 0 && run.box.y + run.box.height <= panelRect.height,
              std::string("\"") + run.text + "\" runs off the panel while reporting a refusal");
      }
      check(reported, std::string("the service said \"") + refusal +
                          "\" and the reader drew something else, so the user is told a sentence nobody sent");
    }
    // ---------------------------------------------------------------------
    // THE CONFIRM. Reached only by pressing a row, and the one screen in Live
    // that destroys anything.
    for (int names = 0; names < 2; ++names) {
      for (int dated = 0; dated < 2; ++dated) {
        const bool undated = dated != 0;
        wallpapersui::RevokeModel model;
        model.who = names == 0 ? "Android phone" : kWidest;
        model.since = undated ? nullptr : "12 Sep";
        const std::string where =
            std::string(names == 0 ? " [real name]" : " [24-char name]") + (undated ? " [no date]" : " [dated]");

        LiveTarget target(true);
        toybox::Interactions interactions;
        toybox::Frame frame(target, ctx, noInput, interactions);
        toybox::Screen screen(frame);
        wallpapersui::buildLiveRevoke(screen, model);

        // 1. IT NAMES WHO, and the name arrives whole. A confirm that removes
        //    "Android pho" is a confirm about somebody the reader cannot check.
        bool named = false;
        bool explained = false;
        for (const LiveTarget::Run& run : target.runs) {
          if (isName(run.text, model.who)) named = true;
          if (run.text == wallpapersui::liveRevokeConsequence()) explained = true;
          const int lines = run.style.maxLines > 0 ? run.style.maxLines : 1;
          const std::string laid = toybox::fitLines(target, run.text.c_str(), run.box.width, lines, run.style);
          check(laid == run.text, "the confirm cuts \"" + run.text + "\" to \"" + laid + "\" in a " +
                                      std::to_string(run.box.width) + "px box" + where);
          // The name is exempt for the reason it is on the list, and nothing
          // else on this screen is: every other string here is one this file
          // chose the width of.
          check(isName(run.text, model.who) || run.text.size() < 3 ||
                    run.text.compare(run.text.size() - 3, 3, "...") != 0,
                "\"" + run.text + "\" reached the confirm already cut to fit" + where);
          check(run.box.y >= 0 && run.box.y + run.box.height <= panelRect.height,
                "\"" + run.text + "\" runs off the panel on the confirm" + where);
        }
        check(named, "the confirm does not name who is being removed" + where);
        check(explained,
              "the confirm does not say what removing them costs, so it asks for a decision with nothing to "
              "decide on" +
                  where);
        if (!undated) {
          bool dateShown = false;
          bool labelShown = false;
          for (const LiveTarget::Run& run : target.runs) {
            if (run.text == std::string(model.since)) dateShown = true;
            if (run.text == std::string(wallpapersui::liveAddedLabel())) labelShown = true;
          }
          check(dateShown, "the confirm drops the date the list showed beside the name" + where);
          // And says what it is. The rows show a bare date under no heading,
          // which reads as when that phone last SENT; this is the one screen
          // with room to define it, and the one where acting on the wrong
          // reading takes somebody's access away.
          check(labelShown, "the confirm shows a bare date, so nothing on the device says what it means" + where);
        }
        // The name keeps the display cut. Writing the label INLINE with the
        // date ("Added 12 Sep", 147px) left 285px for a name, and "Abuela
        // phone" is 315px there -- the ladder would have stepped the name down
        // a rung on the one screen whose whole job is to name a person.
        for (const LiveTarget::Run& run : target.runs) {
          if (!isName(run.text, model.who)) continue;
          if (std::string(model.who) == kWidest) continue;  // 24 W's fits no cut, by construction
          check(run.style.font == fui::FONT_SLOT_TITLE,
                "the confirm's name \"" + run.text + "\" was stepped down a cut to make room beside it" + where);
        }

        // 2. THE SAFE HALF COVERS EVERY ROW. The confirm cannot know which row
        //    the finger came from, so a repeat of that press has to land on
        //    KEEP whichever row it was.
        const fui::Rect keep = wallpapersui::liveKeepRect(screen);
        const fui::Rect band = wallpapersui::liveSendersBand(screen);
        check(keep.x == band.x && keep.y == band.y && keep.width == band.width && keep.height == band.height,
              "KEEP is not the sender band, so a second press of the row that opened this confirm falls "
              "somewhere else" +
                  where);
        for (int i = 0; i < wallpapersui::LiveModel::kMaxSenders; ++i) {
          const fui::Rect row = wallpapersui::liveSenderRowRect(screen, i);
          check(row.y >= keep.y && row.bottom() <= keep.bottom(),
                "row " + std::to_string(i) + " is not covered by KEEP" + where);
        }

        // 3. AND THE DESTRUCTIVE HALF TOUCHES NO ROW. Reaching it takes a
        //    deliberate move to a place no row ever is.
        const fui::Rect revoke = wallpapersui::liveRevokeRect(screen);
        check(!overlaps(revoke, band),
              "REVOKE overlaps the band the rows occupy, so a finger that never moved could destroy access" + where);
        for (int i = 0; i < wallpapersui::LiveModel::kMaxSenders; ++i) {
          check(!overlaps(revoke, wallpapersui::liveSenderRowRect(screen, i)),
                "REVOKE shares pixels with row " + std::to_string(i) + where);
        }

        // 4. BOTH ARE REALLY TAPPABLE, and nothing else on this screen is.
        fui::Rect keepHit{};
        fui::Rect revokeHit{};
        for (size_t h = 0; h < interactions.count(); ++h) {
          const auto& hit = interactions.data()[h];
          if (hit.action == wallpapersui::ActionLiveKeep) keepHit = hit.rect;
          if (hit.action == wallpapersui::ActionLiveRevoke) revokeHit = hit.rect;
          check(hit.action != wallpapersui::ActionLiveSender && hit.action != wallpapersui::ActionLiveAdd &&
                    hit.action != wallpapersui::ActionLiveCheck && hit.action != wallpapersui::ActionLiveToggle,
                "a control from the list is still registered on the confirm" + where);
        }
        check(keepHit.height >= ctx.minTouchSize, "KEEP is drawn but not tappable" + where);
        check(keepHit.y == keep.y && keepHit.height == keep.height,
              "KEEP is registered somewhere other than the band, so its ink and its target disagree" + where);
        check(revokeHit.height >= ctx.minTouchSize, "REVOKE is drawn but not tappable" + where);
        check(revokeHit.y >= 0 && revokeHit.bottom() <= panelRect.height, "REVOKE is registered off the panel" + where);
        check(!overlaps(keepHit, revokeHit), "the confirm's two halves share pixels" + where);
        check(interactions.count() <= toybox::kMaxInteractions, "the confirm overflows the interaction table" + where);
      }
    }
  }

  // -------------------------------------------------------------------------
  // THE LIVE SCREEN WITH NO CODE, which is the state it opens in every single
  // time and stays in whenever the service cannot be reached.
  //
  // It used to draw "482 160" -- a constant in the Activity, with a comment
  // claiming a build flag guarded it and nothing checking any flag -- and a QR
  // encoding that same number. Both are things a person acts on without being
  // able to check them, and the first person through this path scanned the
  // square, was refused by the website, and typed the real code in by hand.
  //
  // Asserted as a PROPERTY rather than against the old literal: nothing that
  // could pass for a pairing code may be drawn, whatever anyone later decides
  // the placeholder should look like.
  {
    fui::DeviceContext ctx = device();
    ctx.safeArea = fui::Insets{10, 1, 0, 1};
    const fui::InputSnapshot noInput{};
    // EVERY SENTENCE THAT CAN LAND IN THE REPORT, which is three families and
    // not one: the screen's own enumerated statuses, the SERVICE's refusals
    // (generated from app.py) and the DEVICE's own transport sentences
    // (generated from the bridge and Live sources). The third family was never
    // measured anywhere, and the longest member of it -- 84 characters about a
    // refused certificate -- is drawn on exactly this screen.
    std::vector<const char*> reports;
    reports.push_back(nullptr);
    for (int si = 0; si < static_cast<int>(wallpapersui::LiveStatus::kCount); ++si) {
      reports.push_back(wallpapersui::liveStatusLine(static_cast<wallpapersui::LiveStatus>(si)));
    }
    for (const char* s : kServiceRefusals) reports.push_back(s);
    for (const char* s : kDeviceSentences) reports.push_back(s);
    for (const char* s : kPendingSentences) reports.push_back(s);

    for (int joining = 0; joining < 2; ++joining) {
      for (const char* report : reports) {
        wallpapersui::LiveModel model;
        model.configured = joining == 1;
        model.joining = joining == 1;
        model.code = "";  // the request has not answered, or it failed
        model.url = wallpapersui::kLiveAddress;
        model.status = report;
        const std::string where =
            std::string(" [no code, joining ") + std::to_string(joining) + ", \"" + (report ? report : "-") + "\"]";

        LiveTarget target(model.configured && !model.joining);
        toybox::Interactions interactions;
        toybox::Frame frame(target, ctx, noInput, interactions);
        toybox::Screen screen(frame);
        const fui::Rect qr = wallpapersui::buildLive(screen, model);

        check(qr.width == 0 && qr.height == 0,
              "a screen with no code still hands the Activity a square to draw a QR into" + where);
        for (const LiveTarget::Run& run : target.runs) {
          int digits = 0;
          for (const char c : run.text) {
            if (c >= '0' && c <= '9') ++digits;
          }
          check(digits < 6,
                "a screen with no code drew something a person would read as one: \"" + run.text + "\"" + where);
        }
        // AND IT IS NOT BLANK EITHER. An empty region where content belongs is
        // this fork's most repeated user-visible failure and has twice been
        // reported as a crash by a cold tester, so the state has to say
        // something.
        check(!target.runs.empty(), "a screen with no code draws nothing at all" + where);

        // AND NONE OF IT LEAVES THE PANEL. This state's report is the only
        // place on the unpaired screen that draws a TRANSPORT's sentence --
        // "Could not reach the sync service. Check Wi-Fi and try again.", 59
        // characters, which is not one line at any cut here -- and the first
        // version of the wrapping stepped it down to FONT_SLOT_SMALL, which on
        // THIS face set is the 82px code cut. It drew three words a screen
        // high, through the footer and off the bottom, and the simulator's
        // renderer logged a hundred out-of-range lines while the assertions
        // above stayed green.
        const fui::Rect panel = fui::makeRect(0, 0, ctx.width, ctx.height);
        for (const LiveTarget::Run& run : target.runs) {
          check(run.box.y >= 0 && run.box.bottom() <= panel.height,
                "a line on the no-code screen is drawn off the panel: \"" + run.text + "\"" + where);
          check(run.box.x >= 0 && run.box.right() <= panel.width,
                "a line on the no-code screen is drawn off the side: \"" + run.text + "\"" + where);
        }
        // THE SQUARE WHERE THE QR WILL BE is the element that went over the
        // edge, and it is a stroke rather than text -- which is why it took a
        // screenshot to find and why LiveTarget records frames now.
        bool framedSquare = false;
        for (const fui::Rect& frame : target.frames) {
          check(frame.y >= 0 && frame.bottom() <= panel.height,
                "a frame on the no-code screen is drawn off the panel" + where);
          check(frame.x >= 0 && frame.right() <= panel.width,
                "a frame on the no-code screen is drawn off the side" + where);
          if (frame.width == frame.height && frame.width > 100) framedSquare = true;
        }
        check(framedSquare, "the no-code screen does not say where the code's square will be" + where);
      }
    }
  }

  // -------------------------------------------------------------------------
  // YOUR PHONE: the tile's destination, and the route that had gone missing.
  {
    fui::DeviceContext ctx = device();
    ctx.safeArea = fui::Insets{10, 1, 0, 1};
    const fui::Rect panelRect = fui::makeRect(0, 0, ctx.width, ctx.height);
    const fui::InputSnapshot noInput{};
    // Both states of the Live line, because one of them is composed by
    // live::scheduleNote and the other is the screen's own idle sentence.
    live::Schedule running;
    running.on = true;
    running.paired = true;
    running.intervalSeconds = 21600;
    running.lastAttemptEpoch = live::kPlausibleEpochFloor + 1000000;
    const std::string note = live::scheduleNote(running);
    const char* states[] = {wallpapersui::phoneLiveIdle(), note.c_str()};

    for (const char* liveState : states) {
      wallpapersui::PhoneModel model;
      model.liveState = liveState;
      const std::string where = std::string(" [\"") + liveState + "\"]";

      LiveTarget target(true);  // the reading chrome faces, as render() binds
      toybox::Interactions interactions;
      toybox::Frame frame(target, ctx, noInput, interactions);
      toybox::Screen screen(frame);
      wallpapersui::buildPhone(screen, model);

      // 1. BOTH ROUTES ARE ON THE SCREEN. The whole defect this screen fixes
      // is one of them having no way in at all, so its absence must fail here
      // rather than be noticed by somebody looking for their own photo.
      const auto drew = [&](const std::string& want) {
        for (const LiveTarget::Run& run : target.runs) {
          if (run.text == want) return true;
        }
        return false;
      };
      check(drew(wallpapersui::phoneSendLabel()), "the destination does not offer sending a picture" + where);
      check(drew(wallpapersui::phoneLiveLabel()), "the destination does not offer Live" + where);
      check(drew(liveState), "the destination does not say what Live is doing" + where);

      // 2. AND BOTH ARE TAPPABLE, at a finger each and without sharing pixels.
      fui::Rect sendHit{};
      fui::Rect liveHit{};
      for (size_t h = 0; h < interactions.count(); ++h) {
        const auto& hit = interactions.data()[h];
        if (hit.action == wallpapersui::ActionAddOwn) sendHit = hit.rect;
        if (hit.action == wallpapersui::ActionLiveOpen) liveHit = hit.rect;
      }
      check(sendHit.height >= ctx.minTouchSize, "SEND A PICTURE is drawn but not tappable" + where);
      check(liveHit.height >= ctx.minTouchSize, "LIVE is drawn but not tappable" + where);
      check(!overlaps(sendHit, liveHit), "the two routes share pixels" + where);
      check(sendHit.y >= 0 && sendHit.bottom() <= panelRect.height,
            "SEND A PICTURE is registered off the panel" + where);
      check(liveHit.y >= 0 && liveHit.bottom() <= panelRect.height, "LIVE is registered off the panel" + where);
      check(interactions.count() <= toybox::kMaxInteractions,
            "the destination overflows the interaction table" + where);
      phoneSlots = static_cast<int>(interactions.count());

      // 3. NOTHING RUNS OFF THE PANEL OR OFF ITS BOX. At these cuts an
      // overflowing line neither clips nor ellipsises -- the faces above
      // toybox_10 carry no U+2026 -- so it stops at a plausible place and the
      // screenshot looks fine (typography-fold).
      for (const LiveTarget::Run& run : target.runs) {
        check(run.box.x >= 0 && run.box.right() <= panelRect.width,
              "a line on the destination is drawn off the panel: \"" + run.text + "\"" + where);
        check(run.box.bottom() <= panelRect.height,
              "a line on the destination is drawn below the panel: \"" + run.text + "\"" + where);
      }
    }
  }

  // -------------------------------------------------------------------------
  // ADD A WALLPAPER, ONCE A PICTURE HAS LANDED.
  //
  // The route exists because somebody wanted their own photo on the glass, and
  // it used to end with the photo merely filed -- on a reader with Live
  // running, the panel then went on showing what the website sends. It ends on
  // this screen now, and this screen has to confirm it.
  //
  // THE NAMES ARE GENERATED FROM uploadFileName(), not typed. Every upload is
  // renamed w0001.bmp, w0002.bmp and the phone's own name is discarded, so a
  // corpus of "beach" and "kids-on-the-beach" would be a corpus of names this
  // route cannot emit. It is also why the picture is the confirmation and the
  // name is not: "w0007" tells nobody which photo they just sent.
  {
    fui::DeviceContext ctx = device();
    ctx.safeArea = fui::Insets{10, 1, 0, 1};
    const fui::Rect panelRect = fui::makeRect(0, 0, ctx.width, ctx.height);
    const fui::InputSnapshot noInput{};
    // A run is the name whole, or its own prefix followed by the ellipsis this
    // face can really draw. The only way to tell "fitted" from "silently cut".
    const auto isName = [](const std::string& run, const std::string& want) {
      if (run == want) return true;
      if (run.size() < 4 || run.compare(run.size() - 3, 3, "...") != 0) return false;
      const std::string body = run.substr(0, run.size() - 3);
      return !body.empty() && body.size() < want.size() && want.compare(0, body.size(), body) == 0;
    };
    // Every width the shape can take: the first slot, a middle one, and the
    // last the handler will ever mint.
    const int slots[] = {1, 42, 9999};
    for (const int slot : slots) {
      const std::string name = wallpapers::displayName(wallpapers::uploadFileName(slot)).full;
      for (int arrived = 0; arrived < 2; ++arrived) {
        wallpapersui::AddModel model;
        model.url = "http://crossplay-a1b2c3.local/w";
        model.altUrl = "http://192.168.1.42/w";
        model.added = arrived;
        model.arrived = arrived != 0 ? name.c_str() : nullptr;
        const std::string where = std::string(" [\"") + name + "\"]" + (arrived == 0 ? " [nothing yet]" : " [landed]");

        LiveTarget target(true);
        toybox::Interactions interactions;
        toybox::Frame frame(target, ctx, noInput, interactions);
        toybox::Screen screen(frame);
        const wallpapersui::AddRects rects = wallpapersui::buildAdd(screen, model);

        // 1. EXACTLY ONE OF THE TWO SQUARES, and the picture only once there is
        //    one. A thumbnail rect handed back with no arrival would have the
        //    Activity blit a Thumb it never decoded; a code rect returned
        //    beside the picture would have it draw a QR over the photo.
        check((rects.qr.width > 0) != (rects.thumb.width > 0),
              "the screen asks for both a code and a picture, or for neither" + where);
        check((rects.thumb.width > 0) == (arrived != 0),
              "the picture's square does not follow whether a picture has arrived" + where);
        check(rects.qr.width == rects.qr.height && rects.thumb.width == rects.thumb.height,
              "a square on this screen is not square" + where);
        // THE SIDE THE ACTIVITY DECODES AT. It decodes before this rect exists,
        // off the paint, so the two are one published number or the picture
        // lands in a box it does not fit.
        if (arrived != 0) {
          check(rects.thumb.width == wallpapersui::addPictureSide(),
                "the picture is placed at a side the Activity did not decode at" + where);
        }
        check(rects.thumb.x >= 0 && rects.thumb.right() <= panelRect.width,
              "the arrival's picture is placed off the side of the panel" + where);
        check(rects.thumb.bottom() <= panelRect.height, "the arrival's picture is placed below the panel" + where);

        // 2. THERE IS ALWAYS A WAY TO SEND ANOTHER, and it is labelled. Once
        //    the code is gone the button is the only way back to it, and a hit
        //    rect whose word never reached the panel is a black box sitting
        //    exactly where the code used to be.
        bool another = false;
        for (size_t h = 0; h < interactions.count(); ++h) {
          if (interactions.data()[h].action == wallpapersui::ActionAddAnother) another = true;
        }
        check(rects.qr.width > 0 || another, "nothing on this screen leads to sending a second picture" + where);
        bool labelled = false;
        bool named = false;
        bool said = false;
        bool waiting = false;
        bool finished = false;
        for (const LiveTarget::Run& run : target.runs) {
          if (run.text == wallpapersui::addAnotherLabel()) labelled = true;
          if (run.text == wallpapersui::addArrivedHeadline()) said = true;
          if (isName(run.text, name)) named = true;
          if (run.text == wallpapersui::addFootWaiting()) waiting = true;
          if (run.text == wallpapersui::addFootArrived()) finished = true;
        }
        check(another == labelled, "the control that sends another picture is registered without its label" + where);

        // 3. IT SAYS WHERE THE PICTURE WENT, and names it -- whole, even at
        //    w9999. The name is not the confirmation (the picture is), but it
        //    is what the grid's caption will say, so it has to be the same
        //    word and it has to survive the cut.
        check(said == (arrived != 0), "the screen's headline does not follow whether a picture arrived" + where);
        check(named == (arrived != 0), "the screen does not name the picture that landed" + where);

        // 4. AND THE WAY OUT CHANGES MEANING WITH IT. Waiting, Back abandons
        //    the wait; once a picture is on the glass, Back is how the route
        //    finishes. A screen still offering to STOP after it has succeeded
        //    describes its own success as an abort.
        check(finished == (arrived != 0) && waiting == (arrived == 0),
              std::string(arrived != 0 ? "the screen still offers to stop after it has succeeded"
                                       : "the screen says leaving finishes something before anything has arrived") +
                  where);

        // 5. NOTHING RUNS OFF THE PANEL, OR OFF ITS OWN BOX. Above the 10px cut
        //    these faces carry no ellipsis, so an overflow stops at a plausible
        //    place and the screenshot looks fine (typography-fold). The box
        //    checks catch a rect in the wrong place; only measuring the string
        //    in the face that draws it catches a line too long for a rect that
        //    is in the right one.
        for (const LiveTarget::Run& run : target.runs) {
          check(run.box.x >= 0 && run.box.right() <= panelRect.width,
                "a line on the Add screen is drawn off the panel: \"" + run.text + "\"" + where);
          check(run.box.bottom() <= panelRect.height,
                "a line on the Add screen is drawn below the panel: \"" + run.text + "\"" + where);
          const int lines = run.style.maxLines > 0 ? run.style.maxLines : 1;
          check(toybox::fitLines(target, run.text.c_str(), run.box.width, lines, run.style) == run.text,
                "a line on the Add screen does not fit its box: \"" + run.text + "\"" + where);
        }
        check(interactions.count() <= toybox::kMaxInteractions,
              "the Add screen overflows the interaction table" + where);
        if (arrived != 0) addSlots = static_cast<int>(interactions.count());
      }
    }
  }

  std::printf("wallcaption: widest caption \"%s\" = %dpx in a %dpx box (%dpx spare)\n", widestName.c_str(), widest,
              wallpapersui::captionRect(g, 0).width, wallpapersui::captionRect(g, 0).width - widest);
  std::printf("wallcaption: Live spends %d of %d interaction slots with four phones listed\n", slotsAtFullList,
              static_cast<int>(toybox::kMaxInteractions));
  std::printf("wallcaption: Your phone spends %d of %d interaction slots\n", phoneSlots,
              static_cast<int>(toybox::kMaxInteractions));
  std::printf("wallcaption: Add spends %d of %d interaction slots with a picture landed\n", addSlots,
              static_cast<int>(toybox::kMaxInteractions));
  std::printf("wallcaption: %d checks, %d failed\n", checks, failed);
  for (const std::string& f : firstFailures) std::printf("  FAIL: %s\n", f.c_str());
  return failed == 0 ? 0 : 1;
}
