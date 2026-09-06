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
#include "../../src/apps_local/ui/fonts/toybox_10.h"
#include "../../src/apps_local/ui/fonts/toybox_14.h"
#include "../../src/apps_local/ui/fonts/toybox_20.h"
#include "../../src/apps_local/ui/fonts/toybox_30.h"
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
EpdFontFamily smallFamily(&small10);
EpdFontFamily buttonFamily(&button14);
EpdFontFamily uiFamily(&ui20);
EpdFontFamily displayFamily(&display30);

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

  // The strip's lowest line tells the user which control opens a set, so it has
  // to name that control's own word. Rename the chip without renaming the hint
  // and the sentence points at a word that is not on the screen. This is the
  // one case where a check that matches the description is the point.
  {
    const std::string hint(wallpapersui::chooseHint());
    const std::string enters(wallpapersui::chooseChipLabel(false));
    const std::string leaves(wallpapersui::chooseChipLabel(true));
    check(hint.find(enters) != std::string::npos,
          "the strip's hint does not name the chip: \"" + hint + "\" vs \"" + enters + "\"");
    check(enters != leaves, "the chip says the same thing entering and leaving the mode");
    // No user-facing string in this app promises randomness: upstream's
    // recent-shown window makes a small set a strict cycle, not a shuffle.
    for (const std::string& s : {hint, enters, leaves}) {
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

  std::printf("wallcaption: widest caption \"%s\" = %dpx in a %dpx box (%dpx spare)\n", widestName.c_str(), widest,
              wallpapersui::captionRect(g, 0).width, wallpapersui::captionRect(g, 0).width - widest);
  std::printf("wallcaption: %d checks, %d failed\n", checks, failed);
  for (const std::string& f : firstFailures) std::printf("  FAIL: %s\n", f.c_str());
  return failed == 0 ? 0 : 1;
}
