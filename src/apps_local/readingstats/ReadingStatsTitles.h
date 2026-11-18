#pragma once

// A book's title, fitted into the box a list row gives it.
//
// Titles are the one thing on these screens that is not the app's own words,
// so they are set in the system face rather than in Jersey: the activity binds
// ubuntu 12, 10 and 8 to the three slots for this pass (ReadingStatsActivity::
// render), the face Home and Recent Books draw the same title in. It carries
// Latin and its extensions, Cyrillic and Hebrew; what it lacks (Greek, CJK)
// draws as its replacement box, unless an SD font is installed, which the
// renderer's fallback then reaches by the same font id. Jersey carries ASCII
// and nothing else, so a title set in it would lose every other letter.
//
// The rule is the design language's, applied to a box rather than a line:
// largest cut first, as many lines as the box holds at that cut, breaking
// between words -- and, in CJK, between characters, where there are no
// words to break between. Only the smallest cut may break inside a word, and
// only past it does a title end in "...". Every line handed to the target is
// measured to fit, so nothing is ever drawn past the box, whatever the title.
//
// Freestanding: FreeInkUI only, so host-tests/ui and host-tests/readingstats
// (which measures with the real ubuntu cuts) can both drive it.

#include <FreeInkUI.h>

#include <string>

namespace rstatsui {

namespace fui = freeink::ui;

constexpr int kMaxTitleLines = 3;

struct TitleLayout {
  fui::FontId font = fui::FONT_SLOT_BODY;
  int lines = 0;
  std::string text[kMaxTitleLines];
  int16_t lineHeight = 0;
  // Every character of the title is on the lines. False only for a title no
  // bound cut can hold in the box, whose last line then ends in "...".
  bool whole = true;
};

// The largest cut of the three bound slots `title` fits in, within `box`.
TitleLayout layOutTitle(const fui::DrawTarget& target, const char* title, int16_t width, int16_t height);

// Draws a layout, its block centred vertically in `box`, one line per call.
void drawTitle(fui::DrawTarget& target, const fui::Rect& box, const TitleLayout& layout);

}  // namespace rstatsui
