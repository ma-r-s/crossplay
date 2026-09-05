#pragma once

// A DrawTarget that answers with the REAL font, and truncates the way the
// device really truncates.
//
// host-tests/ui's FakeTarget answers a flat ten pixels a character and records
// whatever string it is handed. Both are deliberate there and both are fatal
// here: a flat cell cannot tell one cut from another, so a font ladder measured
// against it always picks the first rung; and a target that records the string
// it was given can never see a title being cut, because the cutting happens
// inside the renderer, after the builder has handed the string over.
//
// So this one:
//
//   * measures with EpdFont, over the generated cuts in src/apps_local/ui/fonts,
//     which is the same code and the same data drawText uses on the panel;
//   * reproduces GfxRendererTarget::text()'s single-line path, INCLUDING
//     GfxRenderer::truncatedText -- ellipsis chosen by asking the resolved face
//     whether it carries U+2026 and falling back to "..." when it does not;
//   * records what it would actually have DRAWN, not what it was asked to draw.
//
// The difference between those last two is the whole instrument: `drawn ==
// asked` is the property "the panel shows the whole string", and nothing that
// measures in fake pixels can express it.

#include <EpdFont.h>
#include <FreeInkUI.h>
#include <Utf8.h>

#include <cstdint>
#include <string>
#include <vector>

namespace fui = freeink::ui;

namespace fitted {

// The three slots a GfxRendererTarget binds (ToyboxTheme.h). A screen's Faces
// set is exactly this triple, so a test binds one the same way the app does.
struct Faces {
  const EpdFontData* small;
  const EpdFontData* body;
  const EpdFontData* title;
};

struct TextRun {
  fui::Rect rect;
  std::string asked;  // what the builder handed over
  std::string drawn;  // what the panel would show
  fui::FontId font;
  fui::Color color;
  bool cut() const { return asked != drawn; }
};

class RealTarget : public fui::DrawTarget {
 public:
  explicit RealTarget(const Faces& faces) : faces_(faces) {}

  std::vector<TextRun> texts;

  const EpdFontData* face(const fui::FontId slot) const {
    switch (slot) {
      case fui::FONT_SLOT_TITLE:
        return faces_.title;
      case fui::FONT_SLOT_BODY:
        return faces_.body;
      default:
        // fui components resolve only the three slots and fall back to BODY,
        // which is the rule in the fui-font-slot-fallback memory. A test that
        // answered some fourth id would be measuring a face no component can
        // ever ask for.
        return slot == fui::FONT_SLOT_SMALL ? faces_.small : faces_.body;
    }
  }

  fui::Size measureText(const fui::FontId font, const char* text, const fui::TextStyle) const override {
    if (text == nullptr || *text == '\0') return fui::Size{0, lineHeight(font)};
    int w = 0, h = 0;
    EpdFont(face(font)).getTextDimensions(text, &w, &h);
    return fui::Size{static_cast<int16_t>(w), lineHeight(font)};
  }

  int16_t lineHeight(const fui::FontId font) const override { return static_cast<int16_t>(face(font)->advanceY); }

  // GfxRenderer::truncatedText, reproduced. The ellipsis is chosen by asking
  // the face -- of the six Toybox cuts only toybox_10 carries U+2026 -- because
  // the marker's width feeds the fitting loop, so a marker picked at draw time
  // would overflow the box the truncation exists to respect.
  std::string truncated(const fui::FontId font, const char* text, const int16_t maxWidth) const {
    std::string item(text == nullptr ? "" : text);
    const EpdFont f(face(font));
    const char* ellipsis = f.hasCodepoint(0x2026) ? "\xe2\x80\xa6" : "...";
    if (measureText(font, item.c_str(), fui::TextStyle{}).width <= maxWidth) return item;
    while (!item.empty() && measureText(font, (item + ellipsis).c_str(), fui::TextStyle{}).width >= maxWidth) {
      utf8RemoveLastChar(item);
    }
    return item.empty() ? std::string(ellipsis) : item + ellipsis;
  }

  void text(const fui::Rect rect, const char* text, const fui::TextStyle style) override {
    if (text == nullptr) return;
    const uint8_t maxLines = style.maxLines > 0 ? style.maxLines : 1;
    std::string drawn(text);
    if (measureText(style.font, text, style).width > rect.width) {
      if (maxLines == 1) {
        drawn = truncated(style.font, text, rect.width);
      } else {
        // The wrapped path joins its lines back with a space, so a wrapped run
        // that lost nothing compares equal to what it was asked to draw and one
        // that was cut does not. Only the last line can carry the marker.
        drawn = wrapped(style.font, text, rect.width, maxLines);
      }
    }
    texts.push_back(TextRun{rect, std::string(text), drawn, style.font, style.color});
  }

  void fill(fui::Rect, fui::Paint, uint8_t = 0, uint8_t = 0xFF) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t = 0, uint8_t = 0xFF) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint = {},
              fui::Rotation = fui::Rotation::None) override {}

 private:
  // GfxRenderer::wrappedText, reduced to what this suite asks of it: greedy
  // break on spaces, hard cut of a word wider than the line, ellipsis on the
  // last line when anything is left over.
  std::string wrapped(const fui::FontId font, const char* text, const int16_t maxWidth, const int maxLines) const {
    const EpdFont f(face(font));
    const std::string marker = f.hasCodepoint(0x2026) ? "\xe2\x80\xa6" : "...";
    std::string remaining(text);
    std::string out;
    int lines = 0;
    while (!remaining.empty() && lines < maxLines) {
      if (measureText(font, remaining.c_str(), fui::TextStyle{}).width <= maxWidth) {
        out += (out.empty() ? "" : " ") + remaining;
        return out;
      }
      if (lines + 1 == maxLines) {
        out += (out.empty() ? "" : " ") + truncated(font, remaining.c_str(), maxWidth);
        return out;
      }
      size_t split = std::string::npos;
      for (size_t at = remaining.find(' '); at != std::string::npos; at = remaining.find(' ', at + 1)) {
        if (measureText(font, remaining.substr(0, at).c_str(), fui::TextStyle{}).width > maxWidth) break;
        split = at;
      }
      if (split == std::string::npos) {
        out += (out.empty() ? "" : " ") + truncated(font, remaining.c_str(), maxWidth);
        return out;
      }
      out += (out.empty() ? "" : " ") + remaining.substr(0, split);
      remaining = remaining.substr(split + 1);
      ++lines;
    }
    return out;
  }

  Faces faces_;
};

}  // namespace fitted
