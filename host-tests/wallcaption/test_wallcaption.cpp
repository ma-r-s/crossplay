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
EpdFont ui20(&toybox_20);
EpdFont display30(&toybox_30);
EpdFontFamily smallFamily(&small10);
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
    const uint32_t base = wallpapersui::gridMeaning(0, 0, 21, 1);
    check(wallpapersui::gridMeaning(0, 0, 21, 1) == base, "gridMeaning is not stable for identical inputs");

    // Changing the page, the view, the library size or the chrome-tile count
    // REMAPS cells, so each must change the meaning.
    check(wallpapersui::gridMeaning(1, 0, 21, 1) != base, "a page turn does not gate taps");
    check(wallpapersui::gridMeaning(0, 1, 21, 1) != base, "a view change does not gate taps");
    check(wallpapersui::gridMeaning(0, 0, 22, 1) != base, "a library change does not gate taps");
    check(wallpapersui::gridMeaning(0, 0, 21, 2) != base, "a chrome-tile change does not gate taps");

    // And the signature that mattered: nothing in gridMeaning takes the
    // selection, so there is no argument by which it could gate. Asserted by
    // walking every selection a 21-wallpaper library can have and confirming the
    // meaning for that page never moves.
    for (int page = 0; page < 6; ++page) {
      const uint32_t forPage = wallpapersui::gridMeaning(page, 0, 21, 1);
      for (int selected = -1; selected < 21; ++selected) {
        // The old meaning mixed (selected + 1) in here; the new one cannot.
        check(wallpapersui::gridMeaning(page, 0, 21, 1) == forPage,
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

  // 5. THE HOLD SHEET'S CONTROLS, and the one destructive button behind them.
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

  // 6. THE SENTENCE ON THE CONFIRM, measured rather than eyeballed.
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

  std::printf("wallcaption: widest caption \"%s\" = %dpx in a %dpx box (%dpx spare)\n", widestName.c_str(), widest,
              wallpapersui::captionRect(g, 0).width, wallpapersui::captionRect(g, 0).width - widest);
  std::printf("wallcaption: %d checks, %d failed\n", checks, failed);
  for (const std::string& f : firstFailures) std::printf("  FAIL: %s\n", f.c_str());
  return failed == 0 ? 0 : 1;
}
