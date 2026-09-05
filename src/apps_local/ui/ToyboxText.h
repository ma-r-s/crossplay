#pragma once

// Fitting somebody else's words into a box this panel actually has.
//
// One function, and it is here rather than in an app because the rule it
// encodes is the fork's, not any app's: shrink to fit, break on a space, and
// say that something was dropped. The component clips instead, so a headline
// too long for its rows is simply chopped -- the Hacker News front page read
// "Show HN: Simple algorithm and colo" and "I am retiring from fulltime
// writing (" until this existed. A word broken in half looks like a rendering
// fault, which is the one thing Mario rejected on sight when the same question
// came up for Connections tiles.
//
// Header-only so that adopting it costs no build-file edit in any of the
// twelve places that compile screens.
//
// Hacker News wrote it first and still calls it through a one-line wrapper, so
// there is one implementation and its tests did not have to move.

#include <FreeInkUI.h>

#include <string>

namespace toybox {

// `text` cut to at most `lines` lines of `width`, breaking only between words
// and ending in an ellipsis when anything was dropped.
inline std::string fitLines(const freeink::ui::DrawTarget& target, const char* text, const int16_t width,
                            const int lines, const freeink::ui::TextStyle& style) {
  if (text == nullptr || width <= 0 || lines <= 0) return std::string();

  const std::string whole(text);
  const auto fits = [&target, &style, width](const std::string& run) {
    return target.measureText(style.font, run.c_str(), style).width <= width;
  };

  // Greedy word wrap, the same shape the component uses when it draws this.
  // Walk the words; when a line will not take another, start the next one. If
  // the lines run out with words still to come, the title has to be cut.
  //
  // The last line is tracked separately from the text as a whole, and that is
  // the whole trick. The first version appended the ellipsis to the entire
  // string and then measured *that* against one line's width, so every headline
  // long enough to wrap was trimmed back until all of it fitted on line one:
  // the front page came out as "In Memory of My...", "Show HN: Simple...",
  // "I am retiring from...". Only the last line has to make room.
  std::string line;          // the line being filled
  size_t lineStart = 0;      // where it begins in `whole`
  size_t consumed = 0;       // end of the last word that fitted anywhere
  size_t lastLineStart = 0;  // where the final drawn line begins
  int lineNumber = 1;
  bool overflowed = false;

  size_t i = 0;
  while (i <= whole.size()) {
    const size_t space = whole.find(' ', i);
    const size_t end = space == std::string::npos ? whole.size() : space;
    const std::string word = whole.substr(i, end - i);
    const std::string candidate = line.empty() ? word : line + " " + word;

    if (fits(candidate)) {
      line = candidate;
      consumed = end;
      lastLineStart = lineStart;
    } else if (lineNumber < lines) {
      ++lineNumber;
      lineStart = i;
      line = word;
      // A single word wider than the line has nowhere to go, so let it be cut
      // rather than looping forever looking for a break that cannot exist.
      if (fits(word)) {
        consumed = end;
        lastLineStart = lineStart;
      }
    } else {
      overflowed = true;
      break;
    }

    if (end == whole.size()) break;
    i = end + 1;
  }

  if (!overflowed && consumed >= whole.size()) return whole;

  // Nothing fitted on a word boundary, which happens whenever the text is one
  // unbreakable token: an email address, a URL, a long compound word. The
  // word-boundary rule has nothing to work with there, and returning what it
  // computed means returning the ellipsis and NOTHING ELSE.
  //
  // That is not hypothetical. The Instapaper pairing screen draws the account
  // name it is asking you to recognise, at the display cut, and it rendered as
  // "..." -- a confirmation screen showing no account. Found by looking at the
  // render; it cannot fail a build and it cannot fail a suite that only ever
  // passes it sentences.
  //
  // So: fall back to cutting mid-token. A word broken in half normally reads
  // as a rendering fault, which is why it is the fallback and not the rule --
  // but half an address beats none of one.
  if (consumed == 0) {
    static constexpr const char* kEllipsis = "...";
    std::string cut = whole;
    while (!cut.empty() && !fits(cut + kEllipsis)) cut.pop_back();
    return cut.empty() ? std::string() : cut + kEllipsis;
  }

  // Cut on a word boundary and say so. A title that stops mid-word reads as a
  // rendering fault; one that ends in an ellipsis reads as a long title, which
  // is what it is. Design language: shrink to fit, never break a word.
  std::string kept = whole.substr(0, consumed);
  while (!kept.empty() && kept.back() == ' ') kept.pop_back();
  if (lastLineStart > kept.size()) lastLineStart = 0;

  // Give back whole words from the final line until the ellipsis fits beside
  // it. Measured against that line alone, never against the paragraph.
  static constexpr const char* kEllipsis = "...";
  while (kept.size() > lastLineStart && !fits(kept.substr(lastLineStart) + kEllipsis)) {
    const size_t space = kept.rfind(' ');
    if (space == std::string::npos || space < lastLineStart) break;
    kept.resize(space);
  }
  return kept + kEllipsis;
}

// The fork's own type ladder, in one place at last.
//
// ToyboxFonts.h has stated the rule in prose since the reading cuts were added:
// "pick the largest cut it fits in, walking the available cuts down, and only
// break a word when the smallest still overflows." There was no function that
// did it. SIX apps wrote their own -- toybattle's fittedHeaderTitle, insider's
// fitted, the dungeon's fitLabel, connections' chooseTileCut, forehead's
// layOutCard and the xkcd bar's fitLabel -- and every screen that wrote none
// handed its title straight to a component that cuts rather than shrinks. That
// is how "CURSED CEMETERY" reached a panel as "CURSED CEMETER": not a wrapped
// line and not an ellipsis, a name with its last letter gone.
//
// THE RUNGS ARE ORDERED BY WHAT THE FACE MEASURES, never by what the slot is
// called, and that is the bug all six hand-rolled ladders share. Each of them
// walks TITLE, then BODY, then SMALL, which is only descending while an app
// binds descending cuts. bigNumberFaces() puts the 64px cut in BODY and the
// 30px one in TITLE; cardFaces() puts 44 in SMALL, 30 in BODY and 64 in TITLE.
// Walked by name on either of those, the "step down" steps UP into a face twice
// the size of the one it was asked to shrink. Asked of lineHeight() it cannot:
// the ladder is whatever this screen really bound, sorted.
//
// Never above the cut the caller already chose, either. A title asked for at
// the UI cut must not come back at the display cut because the display cut
// happens to fit too -- fitting is a licence to get SMALLER and nothing else.
//
// Rewrites `style.font` to the chosen cut and returns the string to draw, which
// is `text` itself whenever a rung fitted. Only when the smallest still
// overflows does it fall through to fitLines() above: a word boundary and an
// ellipsis the cut can really draw, never a silent cut mid-word.
inline std::string fittedTitle(const freeink::ui::DrawTarget& target, const char* text, const int16_t width,
                               freeink::ui::TextStyle& style) {
  namespace fui = freeink::ui;
  if (text == nullptr || *text == '\0' || width <= 0) return std::string(text == nullptr ? "" : text);

  // The three slots a GfxRendererTarget binds, plus whatever the caller asked
  // for. Three because three is all there are: the fui components resolve only
  // these and fall back to BODY for anything else, so a fourth id would name a
  // face no component could ever draw in.
  const fui::FontId slots[4] = {style.font, fui::FONT_SLOT_TITLE, fui::FONT_SLOT_BODY, fui::FONT_SLOT_SMALL};
  fui::FontId rungs[4] = {};
  int16_t heights[4] = {};
  int count = 0;
  const int16_t ceiling = target.lineHeight(style.font);
  for (const fui::FontId slot : slots) {
    const int16_t height = target.lineHeight(slot);
    if (height > ceiling) continue;  // fitting only ever goes down
    bool seen = false;
    for (int i = 0; i < count; ++i) seen = seen || rungs[i] == slot;
    if (seen) continue;
    rungs[count] = slot;
    heights[count] = height;
    ++count;
  }
  // Largest first. An insertion sort over at most four rungs, so the order is
  // the measurement rather than an assumption about the slots.
  for (int i = 1; i < count; ++i) {
    for (int j = i; j > 0 && heights[j] > heights[j - 1]; --j) {
      const fui::FontId font = rungs[j];
      const int16_t height = heights[j];
      rungs[j] = rungs[j - 1];
      heights[j] = heights[j - 1];
      rungs[j - 1] = font;
      heights[j - 1] = height;
    }
  }

  // A rung fits when the WHOLE string lays out in it, which for a style that
  // may wrap is not the same question as "is it narrower than the box". The
  // measured width answers the one-line case directly and cheaply; anything
  // wider is put through the same wrap that will draw it, and fits only if the
  // wrap gave everything back. The fit test IS the layout, which is the rule
  // the Connections tiles arrived at independently.
  const int lines = style.maxLines > 0 ? style.maxLines : 1;
  fui::TextStyle probe = style;
  for (int i = 0; i < count; ++i) {
    probe.font = rungs[i];
    if (target.measureText(rungs[i], text, probe).width <= width) {
      style.font = rungs[i];
      return std::string(text);
    }
    if (lines > 1 && fitLines(target, text, width, lines, probe) == text) {
      style.font = rungs[i];
      return std::string(text);
    }
  }

  // Even the smallest cut this screen bound will not take it. Wrap and mark,
  // which is the rule's own last clause and what fitLines has always done.
  if (count > 0) style.font = rungs[count - 1];
  return fitLines(target, text, width, lines, style);
}

}  // namespace toybox
