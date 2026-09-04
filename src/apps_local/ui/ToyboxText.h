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

}  // namespace toybox
