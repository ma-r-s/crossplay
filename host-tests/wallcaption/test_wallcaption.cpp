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

#include "../../src/apps_local/ui/ToyboxText.h"
#include "../../src/apps_local/ui/fonts/reading_serif_14.h"
#include "../../src/apps_local/ui/fonts/toybox_10.h"
#include "../../src/apps_local/ui/fonts/toybox_14.h"
#include "../../src/apps_local/ui/fonts/toybox_20.h"
#include "../../src/apps_local/ui/fonts/toybox_30.h"
#include "../../src/apps_local/ui/fonts/toybox_64.h"
#include "../../src/apps_local/wallpapers/WallpapersCore.h"
#include "../../src/apps_local/wallpapers/WallpapersScreens.h"

namespace fui = freeink::ui;

namespace {
int checks = 0;
int failed = 0;
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

  const EpdFontFamily* familyFor(const fui::FontId font) const {
    // FONT_SLOT_SMALL is the whole difference between the two states: the huge
    // cut while there is a code on the screen, the button cut once there is not.
    if (font == fui::FONT_SLOT_SMALL) return paired_ ? &buttonFamily : &hugeFamily;
    if (font == fui::FONT_SLOT_BODY) return &readingFamily;
    return &displayFamily;
  }
  fui::Size measureText(const fui::FontId font, const char* text, const fui::TextStyle) const override {
    int w = 0;
    int h = 0;
    if (text != nullptr && text[0] != '\0') familyFor(font)->getTextDimensions(text, &w, &h);
    return fui::Size{static_cast<int16_t>(w), lineHeight(font)};
  }
  int16_t lineHeight(const fui::FontId font) const override {
    return static_cast<int16_t>(familyFor(font)->getData(EpdFontFamily::REGULAR)->advanceY);
  }
  void fill(fui::Rect, fui::Paint, uint8_t = 0, uint8_t = 0xFF) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t = 0, uint8_t = 0xFF) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(const fui::Rect box, const char* text, const fui::TextStyle style) override {
    if (text == nullptr || text[0] == '\0') return;
    runs.push_back(Run{box, std::string(text), style});
  }
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint = {},
              fui::Rotation = fui::Rotation::None) override {}

 private:
  bool paired_ = false;
};

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
  // THE LIVE SCREEN. Built for real, both states, in whichever arrangement this
  // binary was compiled with -- run.sh runs it once per arrangement, because a
  // suite that walks one of three is a suite that reports clean about the other
  // two ("a clean list hides absence").
  {
    // The X4 Pro's glass, because the body width is what every string here is
    // measured against and the bezel takes two pixels of it.
    fui::DeviceContext ctx = device();
    ctx.safeArea = fui::Insets{10, 1, 0, 1};
    const fui::Rect panelRect = fui::makeRect(0, 0, ctx.width, ctx.height);
    const fui::InputSnapshot noInput{};

    wallpapersui::LiveModel model;
    model.code = "482 160";
    model.url = "fridge.ma-r-s.com";
    model.nextCheck = "Tomorrow, 6:00";
    model.cadence = "Once a day";
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
      const std::string where =
          std::string(" [screen ") + std::to_string(WALLPAPERS_LIVE_SCREEN) + ", state " + std::to_string(state) + "]";

      LiveTarget target(model.configured);
      toybox::Interactions interactions;
      toybox::Frame frame(target, ctx, noInput, interactions);
      toybox::Screen screen(frame);
      const fui::Rect qr = wallpapersui::buildLive(screen, model);

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
        // The state and its control, read from one bool in two places.
        check(drew(model.on ? "LIVE IS ON" : "LIVE IS OFF") || drew(model.on ? "On" : "Off"),
              "the paired screen does not say whether Live is on" + where);
        check(drew(model.on ? "TURN IT OFF" : "TURN IT ON"),
              "the toggle offers the state the screen is already in" + where);

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
      }
      check(interactions.count() <= toybox::kMaxInteractions,
            "the Live screen overflows the interaction table" + where);
    }
  }

  std::printf("wallcaption: widest caption \"%s\" = %dpx in a %dpx box (%dpx spare)\n", widestName.c_str(), widest,
              wallpapersui::captionRect(g, 0).width, wallpapersui::captionRect(g, 0).width - widest);
  std::printf("wallcaption: %d checks, %d failed\n", checks, failed);
  for (const std::string& f : firstFailures) std::printf("  FAIL: %s\n", f.c_str());
  return failed == 0 ? 0 : 1;
}
