#include "NotesScreens.h"

#include <cstdio>
#include <string>
#include <vector>

#include "../ui/ToyboxText.h"

namespace notesui {
namespace {

constexpr int kBodyTop = toybox::kBodyTop;
constexpr int kFooterHeight = toybox::kPillHeight;
constexpr int kBoxSide = 40;
constexpr int16_t kStrike = 2;  // a done line's bar: one crisp rule, not a grey dither
// One height for every row of a sheet, so the menu, the confirm and the notice
// agree about where a row starts.
constexpr int16_t kSheetRow = 96;
constexpr int16_t kSheetRowMax = 140;
constexpr int kMinRow = 72;  // a finger, with room to miss

int16_t pageWidth(const fui::DeviceContext& device) { return static_cast<int16_t>(device.width - 2 * toybox::kMargin); }

fui::TextStyle plain(const fui::FontId font, const fui::TextAlign align = fui::TextAlign::Left,
                     const uint8_t maxLines = 1) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = fui::Color::Black;
  style.maxLines = maxLines;
  return style;
}

// Header band, rule, page margin. The title is fitted before it goes on the
// band: the header component elides, kHeaderHeight is load-bearing so the band
// cannot grow, and the Toybox cuts above toybox_10 carry no ellipsis glyph at
// all -- an overflow there draws as a name that simply stops.
void chrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr,
            const freeink::Icon* trailing = nullptr, const fui::FontId nameCut = toybox::kDisplayFont) {
  fui::TextStyle titleStyle = screen.theme().titleText;
  // FIXED, and not a function of the content. It used to run through
  // fittedTitle, so a note called "Packing for Lisbon" dropped the band a whole
  // cut and a longer one dropped it two -- the app's own title bar, the one
  // element that is meant to be identical on every screen of the fork,
  // resizing itself around a filename. Names the app creates are capped at what
  // this cut holds (notes::Library::nameFits), so the only way to reach a name
  // that does not fit is to write one on the card from a computer.
  titleStyle.font = nameCut;
  fui::HeaderProps header;
  header.title = title;
  header.titleText = titleStyle;
  header.rightLabel = rightLabel;
  header.borderEdges = fui::EdgesNone;
  if (rightLabel != nullptr) {
    header.subtitleText = screen.theme().smallText;
    header.subtitleText.font = toybox::kTileFont;
    header.subtitleText.color = fui::Color::White;
    header.subtitleText.align = fui::TextAlign::Right;
  }
  if (trailing != nullptr) {
    header.trailingIcon = fui::bitmapFromIcon(*trailing);
    header.trailingAction = ActionMenu;
    header.trailingStyles = toybox::rowStyles();
  }
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kBodyGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// A row's tap target, invisible: the whole row, because a 40px box is a miss
// waiting to happen and a miss costs two refreshes, the wrong one and the undo.
void rowHit(toybox::Screen& screen, const fui::Rect& row, const fui::ActionId action, const int index) {
  fui::ButtonProps hit;
  hit.label = "";
  hit.action = action;
  hit.value = static_cast<int16_t>(index);
  hit.styles = toybox::rowStyles();
  hit.styles.normal.background = fui::Paint::none();
  hit.styles.normal.border = fui::Paint::none();
  screen.button(hit, row);
}

// Never on the last row of a page: a rule against the band edge, or against the
// bar under it, reads as a double line and is the one place a list looks
// unfinished rather than divided.
void separator(toybox::Screen& screen, const fui::Rect& row) {
  screen.target().fill(
      fui::makeRect(row.x, static_cast<int16_t>(row.y + row.height - toybox::kHairline), row.width, toybox::kHairline),
      fui::Paint::solid(fui::Color::Black));
}

// One line that must not overflow its box, set at the largest cut that holds it.
// Toybox's rule is that nothing is elided, and the cuts above toybox_10 carry no
// ellipsis glyph at all -- an overflow there draws as a sentence that stops at a
// plausible place, and the screenshot looks fine.
void fittedLine(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::TextAlign align,
                const fui::FontId font) {
  fui::TextStyle style = plain(font, align);
  const std::string drawn = toybox::fittedTitle(screen.target(), text, box.width, style);
  screen.target().text(box, drawn.c_str(), style);
}

// The tick box: an outline, filled with a smaller solid square when done. Pure
// black on pure white in the one small rect that changes, which is the fastest
// and least ghost-prone update this panel can perform. No tick glyph -- the
// Toybox face is ASCII and a check mark is not in it.
void tickBox(toybox::Screen& screen, const fui::Rect& box, const bool checked) {
  screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kRule, 4);
  if (!checked) return;
  const int16_t inset = 9;
  screen.target().fill(
      fui::makeRect(static_cast<int16_t>(box.x + inset), static_cast<int16_t>(box.y + inset),
                    static_cast<int16_t>(box.width - 2 * inset), static_cast<int16_t>(box.height - 2 * inset)),
      fui::Paint::solid(fui::Color::Black), 2);
}

// The strike is measured against the LONGEST DRAWN LINE, not the source string,
// so a line that wrapped gets a rule the width of what is really on the glass.
// THE ONE LINE-BREAK RULE IN THIS FILE. A row is sized from it, drawn from it
// one line at a time, and struck through from it, so the three cannot disagree.
//
// They used to be three rules. The row was sized by adding word
// widths; the renderer wrapped the text again by its own algorithm when handed
// a multi-line run; and the strike split the string on '\n', which wrapped text
// does not contain -- so a ticked item that ran to two lines was struck on the
// first only, with a bar as wide as the whole sentence. Nothing here hands the
// renderer a multi-line run any more: every line it draws is a line this
// function chose.
//
// Measured as whole candidate lines, never as a sum of words, because a sum is
// not what the panel draws: spacing and pairs are only right when the run is
// measured as the run.
struct Broken {
  std::vector<std::string> lines;
  bool fits = true;  // false: a word wider than `width`, or more than maxLines
};

Broken breakWords(const fui::DrawTarget& target, const std::string& text, const int16_t width, const int maxLines,
                  const fui::TextStyle& style, const bool splitLongWords) {
  Broken out;
  if (width <= 0 || maxLines <= 0) {
    out.fits = false;
    return out;
  }
  // Nothing this long can fit a 480px row, and the byte cap is what keeps the
  // measure honest: widths come back as int16_t, so a run wider than 32767px
  // wraps NEGATIVE, reads as fitting, and is then silently cut by the
  // renderer -- a 4000-byte word used to vanish that way. A glyph narrower
  // than three pixels would be needed to put this many bytes on one row.
  constexpr size_t kLongestLineBytes = 160;
  const auto fits = [&target, &style, width](const std::string& run) {
    if (run.size() > kLongestLineBytes) return false;
    const int16_t measured = target.measureText(style.font, run.c_str(), style).width;
    return measured >= 0 && measured <= width;
  };
  std::string line;
  size_t i = 0;
  while (i <= text.size()) {
    size_t end = text.find(' ', i);
    if (end == std::string::npos) end = text.size();
    std::string word = text.substr(i, end - i);
    i = end + 1;
    if (!word.empty()) {
      const std::string candidate = line.empty() ? word : line + " " + word;
      if (fits(candidate)) {
        line = candidate;
      } else {
        if (!line.empty()) out.lines.push_back(line);
        line.clear();
        if (!fits(word)) {
          if (!splitLongWords) out.fits = false;
          // Only reachable at the last-resort cut, with one run of letters
          // wider than the row. Cut at a CHARACTER boundary, never inside a
          // UTF-8 sequence, which would draw as a box.
          //
          // BINARY SEARCH over the character boundaries, not a walk down from
          // the end: that measured every prefix of every remaining tail, which
          // is cubic in bytes -- 53K measures and 37MB scanned for one pasted
          // 2000-byte run, on every relayout.
          while (!word.empty() && !fits(word)) {
            std::vector<size_t> ends;  // byte offsets that end a whole character
            ends.reserve(word.size());
            for (size_t at = 1; at <= word.size(); at++) {
              if (at == word.size() || (static_cast<unsigned char>(word[at]) & 0xC0) != 0x80) ends.push_back(at);
            }
            // The longest prefix that fits. Always at least one whole character,
            // even one wider than the row on its own: a cut that takes nothing
            // never ends.
            size_t lo = 0;
            size_t hi = ends.size() - 1;
            while (lo < hi) {
              const size_t mid = (lo + hi + 1) / 2;
              if (fits(word.substr(0, ends[mid]))) {
                lo = mid;
              } else {
                hi = mid - 1;
              }
            }
            const size_t n = ends[lo];
            out.lines.push_back(word.substr(0, n));
            word = word.substr(n);
          }
        }
        line = word;
      }
    }
    if (end >= text.size()) break;
  }
  if (!line.empty()) out.lines.push_back(line);
  if (out.lines.empty()) out.lines.push_back(std::string());
  if (static_cast<int>(out.lines.size()) > maxLines) {
    out.fits = false;
    out.lines.resize(static_cast<size_t>(maxLines));
  }
  return out;
}

// The last line of an item that was cut at its cap, shortened until "..." fits
// beside it. It is done HERE, on the lines that will be drawn, and not by
// shortening the text and breaking it again: a second break splits a long run
// of letters into more lines than the first counted, and the ellipsis was then
// the part that fell off the end.
void endWithEllipsis(const fui::DrawTarget& target, std::string& line, const int16_t width,
                     const fui::TextStyle& style) {
  static constexpr const char* kEllipsis = "...";
  const auto fits = [&](const std::string& run) {
    const int16_t measured = target.measureText(style.font, run.c_str(), style).width;
    return measured >= 0 && measured <= width;
  };
  while (!line.empty() && !fits(line + kEllipsis)) {
    const size_t space = line.find_last_of(' ');
    if (space != std::string::npos && space > 0) {
      line.erase(space);  // a whole word first, so the cut lands between words
    } else {
      size_t at = line.size() - 1;  // then a whole character, never half of one
      while (at > 0 && (static_cast<unsigned char>(line[at]) & 0xC0) == 0x80) at--;
      line.erase(at);
    }
  }
  line += kEllipsis;
}

// Each line its own single-line run, at its own height, and -- when `struck` --
// its own bar, exactly as wide as that line's ink. A strike is part of the line
// it crosses, so it is drawn by the same loop that draws the line.
void drawLines(toybox::Screen& screen, const fui::Rect& box, const std::vector<std::string>& lines,
               const fui::TextStyle& style, const bool struck, const int16_t advance = 0) {
  const int16_t lineHeight = screen.target().lineHeight(style.font);
  const int16_t step = advance > 0 ? advance : lineHeight;
  fui::TextStyle one = style;
  one.maxLines = 1;
  for (size_t i = 0; i < lines.size(); i++) {
    const int16_t top = static_cast<int16_t>(box.y + static_cast<int>(i) * step);
    screen.target().text(fui::makeRect(box.x, top, box.width, lineHeight), lines[i].c_str(), one);
    if (!struck || lines[i].empty()) continue;
    const int16_t width = screen.target().measureText(one.font, lines[i].c_str(), one).width;
    screen.target().fill(fui::makeRect(box.x, static_cast<int16_t>(top + lineHeight / 2), width, kStrike),
                         fui::Paint::solid(fui::Color::Black));
  }
}

// Row height comes from the TYPE, never from how many rows there are. Dividing
// the band by the count fills a short page, but it also means the same note is
// drawn with different spacing after one line is added, and a list whose rhythm
// changes as you use it reads worse than one that ends early. So: the lines the
// When everything fits on one page, the rows SHARE the band instead of stacking
// at the top under a hole. Capped, so a two-item list is not two slabs, and
// only while nothing is paged, so a row never changes size under the finger
// because a note grew past the fold.
int16_t fittedPitch(const int16_t base, const int16_t cap, const int16_t bandHeight, const int count,
                    const int visible) {
  if (count <= 0 || count > visible) return base;
  const int16_t share = static_cast<int16_t>(bandHeight / count);
  if (share <= base) return base;
  return share > cap ? cap : share;
}

// The bar every screen's actions live on: one y, one height, on the deck, the
// note and the sheets alike, so the thumb learns a single place. The filled
// button is always the one that makes something, and the outlined one on the
// RIGHT is always the one that takes something away -- CLEAR DONE, DELETE.
fui::Rect footerBand(const fui::DeviceContext& device) {
  return fui::makeRect(toybox::kMargin, static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight),
                       static_cast<int16_t>(device.width - 2 * toybox::kMargin), kFooterHeight);
}

// The page between the chrome and that bar.
fui::Rect sheetBand(const fui::DeviceContext& device) {
  const fui::Rect footer = footerBand(device);
  return fui::makeRect(toybox::kMargin, kBodyTop, footer.width,
                       static_cast<int16_t>(footer.y - toybox::kGutter * 2 - kBodyTop));
}

void footerButton(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId action,
                  const bool outlined) {
  fui::ButtonProps button;
  button.label = label;
  button.action = action;
  if (outlined) button.styles = toybox::rowStyles();
  screen.button(button, box);
}

// Prose set from the top of a sheet's page, at the body cut, wrapped to as many
// lines as the page holds.
void sheetProse(toybox::Screen& screen, const fui::Rect& band, const char* text) {
  fui::TextStyle prose = plain(toybox::kBodyFont, fui::TextAlign::Left, 5);
  const int16_t lineHeight = screen.target().lineHeight(prose.font);
  const int maxLines = band.height / lineHeight;
  prose.maxLines = static_cast<uint8_t>(maxLines < 1 ? 1 : maxLines);
  const std::string drawn = toybox::fitLines(screen.target(), text, band.width, prose.maxLines, prose);
  screen.target().text(fui::makeRect(band.x, band.y, band.width, band.height), drawn.c_str(), prose);
}

// "1 / 2" in the strip under the band. It is small, centred and the only thing
// down there, so it reads as a page number rather than as a control.
void pageLabel(toybox::Screen& screen, const fui::Rect& band, const char* label) {
  if (label == nullptr) return;
  fui::TextStyle style = plain(toybox::kTileFont, fui::TextAlign::Center);
  const int16_t lineHeight = screen.target().lineHeight(style.font);
  screen.target().text(
      fui::makeRect(band.x, static_cast<int16_t>(band.y + band.height - lineHeight), band.width, lineHeight), label,
      style);
}

void centredNotice(toybox::Screen& screen, const fui::Rect& band, const char* text) {
  fui::TextStyle style = plain(toybox::kBodyFont, fui::TextAlign::Center, 3);
  const int16_t lineHeight = screen.target().lineHeight(style.font);
  const fui::Rect box = fui::makeRect(band.x, static_cast<int16_t>(band.y + (band.height - lineHeight * 3) / 2),
                                      band.width, static_cast<int16_t>(lineHeight * 3));
  const std::string drawn = toybox::fitLines(screen.target(), text, box.width, 3, style);
  screen.target().text(box, drawn.c_str(), style);
}

}  // namespace

// --- The deck ------------------------------------------------------------

namespace {

// Everything both deck arrangements agree on: how wide the tally gutter is,
// which cut the titles share, and how the rows are drawn. Two arrangements that
// differ only in where NEW NOTE lives must not differ anywhere else.
// --- The deck's cards ----------------------------------------------------
//
// A row is a CARD: a badge on the left, the name beside it, and under the name
// either a bar (a list) or its first words (a note). The two kinds are told
// apart by shape at arm's length rather than by reading a tally, and the badge
// column gives the page the black mass this face is drawn for.

constexpr int16_t kCardHeight = 104;  // holds a two-line name AND its preview; still five a page
constexpr int16_t kCardGap = 12;
constexpr int16_t kCardMax = 132;  // a card, not a slab
constexpr int16_t kCardPitch = kCardHeight + kCardGap;
constexpr int16_t kBadgeWidth = 76;
constexpr int16_t kBarHeight = 14;
constexpr int kTitleLines = 2;        // a deck name wraps once rather than shrinking the deck
constexpr int16_t kTitleWrapGap = 6;  // name to its bar or preview, when the name took two lines
constexpr int kTitleLeading = 80;     // percent of the line box between a wrapped name's two lines

// Cuts at the last word that fits and says so with three periods. The one place
// the app elides, deliberately: a preview is a glimpse by definition, and the
// small cut is the only one carrying the glyphs to admit it.
std::string previewToWidth(const fui::DrawTarget& target, const char* text, const fui::TextStyle& style,
                           const int16_t width) {
  const std::string whole(text == nullptr ? "" : text);
  if (whole.empty() || target.measureText(style.font, whole.c_str(), style).width <= width) return whole;
  std::string out;
  size_t i = 0;
  while (i < whole.size()) {
    size_t end = whole.find(' ', i);
    if (end == std::string::npos) end = whole.size();
    const std::string word = whole.substr(i, end - i);
    const std::string candidate = out.empty() ? word : out + " " + word;
    if (target.measureText(style.font, (candidate + "...").c_str(), style).width > width) break;
    out = candidate;
    i = end + 1;
  }
  // One word wider than the whole strip: cut it by characters rather than draw
  // nothing, which would read as a note with nothing in it.
  if (out.empty()) {
    for (size_t n = 1; n <= whole.size(); n++) {
      if (target.measureText(style.font, (whole.substr(0, n) + "...").c_str(), style).width > width) break;
      out = whole.substr(0, n);
    }
  }
  return out + "...";
}

// Outline, with the done fraction filled solid. No numerals in it: the tally is
// on the badge, and a bar answers "how much is left" before it is read.
void progressBar(toybox::Screen& screen, const fui::Rect& box, const int done, const int total) {
  screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 0);
  if (total <= 0 || done <= 0) return;
  int16_t filled = static_cast<int16_t>(static_cast<long>(box.width) * done / total);
  // One item done out of many still has to be visible, or the first tick looks
  // like it did nothing.
  if (filled < toybox::kRule) filled = toybox::kRule;
  if (filled > box.width) filled = box.width;
  screen.target().fill(fui::makeRect(box.x, box.y, filled, box.height), fui::Paint::solid(fui::Color::Black), 0);
}

// The note badge. Nothing in this face says "text", so three rules say it, the
// last one short the way a paragraph's last line is.
void linesBadge(toybox::Screen& screen, const fui::Rect& box) {
  screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kRule, 0);
  const int16_t inset = 18;
  const int16_t width = static_cast<int16_t>(box.width - inset * 2);
  const int16_t gap = 13;
  const int16_t top = static_cast<int16_t>(box.y + (box.height - (toybox::kRule * 3 + gap * 2)) / 2);
  for (int i = 0; i < 3; i++) {
    const int16_t w = i == 2 ? static_cast<int16_t>(width * 3 / 5) : width;
    screen.target().fill(fui::makeRect(static_cast<int16_t>(box.x + inset),
                                       static_cast<int16_t>(top + i * (toybox::kRule + gap)), w, toybox::kRule),
                         fui::Paint::solid(fui::Color::Black));
  }
}

struct DeckLayout {
  fui::TextStyle title{};
  int visible = 0;
};

DeckLayout deckLayoutFor(const fui::Rect& band) {
  DeckLayout layout;
  // The BODY cut for every card, and a long name takes a second line. It used
  // to drop the whole column a size instead, so one note called "Groceries for
  // the lake" set every other name on the deck in small print. Peers share a
  // cut; a name that needs more room takes it downward, where the card has it.
  layout.title = plain(toybox::kBodyFont, fui::TextAlign::Left, kTitleLines);

  layout.visible = (band.height + kCardGap) / kCardPitch;
  if (layout.visible < 1) layout.visible = 1;
  return layout;
}

void deckRows(toybox::Screen& screen, const DeckModel& model, const fui::Rect& band, const DeckLayout& layout,
              int16_t& y) {
  fui::TextStyle tally = plain(toybox::kTileFont, fui::TextAlign::Center);
  tally.color = fui::Color::White;
  const fui::TextStyle small = plain(toybox::kTileFont);
  const int16_t pitch =
      fittedPitch(kCardPitch, static_cast<int16_t>(kCardMax + kCardGap), band.height, model.count, layout.visible);
  for (int i = model.firstVisible; i < model.count && i - model.firstVisible < layout.visible; i++) {
    const fui::Rect card = fui::makeRect(band.x, y, band.width, static_cast<int16_t>(pitch - kCardGap));
    const DeckItem& item = model.items[i];
    const bool list = item.total > 0;

    // A square, centred: a badge as tall as a 132px card reads as a stripe
    // down the side rather than as a mark on it.
    const int16_t badgeSide = card.height < kBadgeWidth ? card.height : kBadgeWidth;
    const fui::Rect badge =
        fui::makeRect(card.x, static_cast<int16_t>(card.y + (card.height - badgeSide) / 2), badgeSide, badgeSide);
    if (list) {
      screen.target().fill(badge, fui::Paint::solid(fui::Color::Black), 0);
      fui::TextStyle drawnStyle = tally;
      const std::string drawn = toybox::fittedTitle(screen.target(), item.tally == nullptr ? "" : item.tally,
                                                    static_cast<int16_t>(badge.width - toybox::kGutter), drawnStyle);
      const int16_t lineHeight = screen.target().lineHeight(drawnStyle.font);
      screen.target().text(fui::makeRect(badge.x, static_cast<int16_t>(badge.y + (badge.height - lineHeight) / 2),
                                         badge.width, lineHeight),
                           drawn.c_str(), drawnStyle);
    } else {
      linesBadge(screen, badge);
    }

    const int16_t textX = static_cast<int16_t>(card.x + kBadgeWidth + toybox::kGutter);
    const int16_t textWidth = static_cast<int16_t>(card.x + card.width - textX);
    Broken title = breakWords(screen.target(), item.title, textWidth, kTitleLines, layout.title, true);
    if (!title.fits) endWithEllipsis(screen.target(), title.lines.back(), textWidth, layout.title);
    // A wrapped name is set TIGHTER than body text: the cut's line box carries
    // reading leading, and a two-line name at that leading is taller than the
    // card, crowding its preview into the buttons below.
    const int16_t titleLine = screen.target().lineHeight(layout.title.font);
    const int16_t titleStep = static_cast<int16_t>(titleLine * kTitleLeading / 100);
    const int16_t titleHeight =
        static_cast<int16_t>(titleLine + (static_cast<int>(title.lines.size()) - 1) * titleStep);
    const bool hasSecond = list || (item.preview != nullptr && *item.preview != '\0');
    const int16_t secondHeight = list ? kBarHeight : screen.target().lineHeight(small.font);
    // A two-line name closes the gap to its second line, so the block still
    // sits inside the smallest card with air above and below it.
    const int16_t gap = title.lines.size() > 1 ? kTitleWrapGap : toybox::kGutter;
    const int16_t block = static_cast<int16_t>(titleHeight + (hasSecond ? gap + secondHeight : 0));
    const int16_t top = static_cast<int16_t>(card.y + (card.height - block) / 2);

    drawLines(screen, fui::makeRect(textX, top, textWidth, titleHeight), title.lines, layout.title, false, titleStep);
    if (hasSecond) {
      const fui::Rect secondBox =
          fui::makeRect(textX, static_cast<int16_t>(top + titleHeight + gap), textWidth, secondHeight);
      if (list) {
        progressBar(screen, secondBox, item.done, item.total);
      } else {
        screen.target().text(secondBox, previewToWidth(screen.target(), item.preview, small, textWidth).c_str(), small);
      }
    }

    rowHit(screen, card, ActionOpenNote, i);
    y = static_cast<int16_t>(y + pitch);
  }
}

}  // namespace

namespace {
// The band both deck functions measure against. One definition, so capacity and
// drawing cannot drift apart.
fui::Rect deckBand(const fui::DeviceContext& device) {
  const int16_t width = static_cast<int16_t>(device.width - 2 * toybox::kMargin);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);
  return fui::makeRect(toybox::kMargin, kBodyTop, width, static_cast<int16_t>(footerY - toybox::kGutter - kBodyTop));
}
}  // namespace

int deckCapacity(const fui::DeviceContext& device) {
  const fui::Rect band = deckBand(device);
  const DeckLayout layout = deckLayoutFor(band);
  return layout.visible;
}

void buildDeck(toybox::Screen& screen, const DeckModel& model) {
  chrome(screen, "NOTES");
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);
  const fui::Rect band =
      fui::makeRect(toybox::kMargin, kBodyTop, width, static_cast<int16_t>(footerY - toybox::kGutter - kBodyTop));

  // Two buttons, not one that opens a screen asking which. The kind is the
  // only question the app has, and asking it as a whole screen cost a tap and a
  // full repaint to say one word.
  const int16_t half = static_cast<int16_t>((width - toybox::kGutter) / 2);
  footerButton(screen, fui::makeRect(toybox::kMargin, footerY, half, kFooterHeight), "+ LIST", ActionNewList, false);
  footerButton(
      screen,
      fui::makeRect(static_cast<int16_t>(toybox::kMargin + half + toybox::kGutter), footerY, half, kFooterHeight),
      "+ NOTE", ActionNewPage, true);
  if (model.count == 0) {
    centredNotice(screen, band, "Nothing here yet. A list is things to tick off. A note is words to keep.");
    return;
  }
  const DeckLayout layout = deckLayoutFor(band);
  int16_t y = band.y;
  deckRows(screen, model, band, layout, y);
  pageLabel(screen, band, model.pageLabel);
}

// --- A note, open --------------------------------------------------------

namespace {

// --- The note: every row the body cut, every row as tall as its own words ---
//
// ONE CUT FOR EVERY ROW, AND IT IS THE BODY CUT. Each row is as tall as its own
// text. The first two versions chose one geometry for the whole list from its
// LONGEST item: every row was sized for the tallest, and when any item would
// not fit two body lines the whole list dropped to toybox_10. One pasted
// sentence turned a shopping list into small print with three-line gaps, a
// four-item list paged because every row was the height of its longest, and a
// note with one long paragraph was set entirely in the smallest face. Peers
// still share a cut. They no longer share a height.

constexpr int16_t kItemPad = 16;  // above and below an item's text block
constexpr int16_t kParaGap = 12;  // between two paragraphs of a note
constexpr int16_t kGrowCap = 36;  // the most a row grows when the whole list fits one page

// A list: one block per item. A note: one block per LINE, so a long paragraph
// continues on the next page rather than being shrunk or cut.
struct Block {
  int item = 0;
  std::vector<std::string> lines;  // exactly what is drawn, already broken
  int16_t height = 0;              // before any growth
  bool paragraphStart = false;     // a note: the first line of a paragraph
};

struct NoteFlow {
  bool prose = false;  // a note's lines rather than a list's items
  fui::TextStyle body{};
  int16_t lineHeight = 0;
  int16_t textWidth = 0;
  std::vector<Block> blocks;
};

// Room the page label takes at the foot of a paged band.
int16_t pageLabelReserve(const fui::DrawTarget& target) {
  return static_cast<int16_t>(target.lineHeight(toybox::kTileFont) + toybox::kGutter);
}

NoteFlow flowNote(const fui::DrawTarget& target, const NoteModel& model, const fui::Rect& band) {
  NoteFlow flow;
  flow.prose = model.page;
  flow.body = plain(toybox::kBodyFont);
  flow.lineHeight = target.lineHeight(flow.body.font);
  if (flow.lineHeight <= 0) flow.lineHeight = 1;
  flow.textWidth = model.page ? band.width : static_cast<int16_t>(band.width - kBoxSide - toybox::kGutter);

  // An item never outgrows a page, measured against the band a PAGED list has
  // (the label takes its foot). Past that it is cut with three periods, which
  // every cut carries, and the whole item is still on the phone page.
  const int usable = band.height - pageLabelReserve(target) - 2 * kItemPad;
  const int itemCap = usable / flow.lineHeight < 1 ? 1 : usable / flow.lineHeight;

  flow.blocks.reserve(static_cast<size_t>(model.count));
  for (int i = 0; i < model.count; i++) {
    const std::string text = model.tasks[i].text != nullptr ? model.tasks[i].text : "";
    if (model.page) {
      const Broken broken = breakWords(target, text, flow.textWidth, 1000, flow.body, true);
      for (size_t l = 0; l < broken.lines.size(); l++) {
        Block line;
        line.item = i;
        line.lines.push_back(broken.lines[l]);
        line.height = flow.lineHeight;
        line.paragraphStart = (l == 0);
        flow.blocks.push_back(std::move(line));
      }
      continue;
    }
    // A run of letters wider than the row (a pasted link) is split at the
    // body cut. Shrinking the whole list to fit one link was the old answer.
    Broken broken = breakWords(target, text, flow.textWidth, itemCap, flow.body, true);
    if (!broken.fits) endWithEllipsis(target, broken.lines.back(), flow.textWidth, flow.body);
    Block row;
    row.item = i;
    row.lines = std::move(broken.lines);
    const int textHeight = static_cast<int>(row.lines.size()) * flow.lineHeight + 2 * kItemPad;
    row.height = static_cast<int16_t>(textHeight < kMinRow ? kMinRow : textHeight);
    flow.blocks.push_back(std::move(row));
  }
  return flow;
}

// Greedy: a block starts a new page when it would not fit what is left. A
// paragraph's gap is dropped at the top of a page, where it would only push the
// first line down.
//
// And a paragraph never leaves ONE line behind at a break, at either end. A
// lone first line at the foot of a page reads as a sentence that stops; a lone
// last line at the top of the next reads as a stray. So the break moves back:
// to the paragraph's start when only its first line would remain, or one line
// earlier when only its last would carry over -- provided the page it is
// moving off keeps something of its own.
std::vector<int> pageStartsIn(const NoteFlow& flow, const int height) {
  const int count = static_cast<int>(flow.blocks.size());
  const auto paragraphOf = [&flow, count](const int i, int& begin, int& end) {
    begin = i;
    while (begin > 0 && !flow.blocks[static_cast<size_t>(begin)].paragraphStart) begin--;
    end = i + 1;
    while (end < count && !flow.blocks[static_cast<size_t>(end)].paragraphStart) end++;
  };
  std::vector<int> starts{0};
  int y = 0;
  int i = 0;
  while (i < count) {
    const Block& block = flow.blocks[static_cast<size_t>(i)];
    const int gap = (block.paragraphStart && y > 0) ? kParaGap : 0;
    if (y > 0 && y + gap + block.height > height) {
      int at = i;
      // Notes only. A list's items are blocks with no paragraphs, so the whole
      // list read as ONE paragraph and the rule moved a page's last row onto
      // the next page although it had room.
      if (flow.prose && !block.paragraphStart) {
        int begin = 0;
        int end = 0;
        paragraphOf(i, begin, end);
        if (i - begin == 1) {
          at = begin;  // only the first line would stay behind
        } else if (end - i == 1 && i - begin >= 3) {
          at = i - 1;  // only the last line would carry over
        }
        if (at <= starts.back()) at = i;  // never empty the page it leaves
      }
      starts.push_back(at);
      y = 0;
      i = at;
      continue;
    }
    y += gap + block.height;
    i++;
  }
  return starts;
}

struct NotePlan {
  NoteFlow flow;
  std::vector<int> starts;
  int16_t usable = 0;  // the band height the pages were cut against
};

NotePlan planNote(const fui::DrawTarget& target, const NoteModel& model, const fui::Rect& band) {
  NotePlan plan;
  plan.flow = flowNote(target, model, band);
  plan.usable = band.height;
  plan.starts = pageStartsIn(plan.flow, plan.usable);
  if (plan.starts.size() > 1) {
    plan.usable = static_cast<int16_t>(band.height - pageLabelReserve(target));
    plan.starts = pageStartsIn(plan.flow, plan.usable);
  }
  return plan;
}

void noteRows(toybox::Screen& screen, const NoteModel& model, const fui::Rect& band, const NotePlan& plan) {
  if (model.count == 0) {
    centredNotice(screen, band, model.page ? "This note is empty. Tap ADD." : "Nothing on this list. Tap ADD.");
    return;
  }
  const NoteFlow& flow = plan.flow;
  size_t page = 0;
  for (size_t p = 0; p < plan.starts.size(); p++) {
    if (plan.starts[p] <= model.firstVisible) page = p;
  }
  const int begin = plan.starts[page];
  const int end = page + 1 < plan.starts.size() ? plan.starts[page + 1] : static_cast<int>(flow.blocks.size());

  // Rows share the page when the whole list already fits on one, so three items
  // are not three lines over a hole. Capped, and never while paging, so a row
  // does not change size under the finger because the list grew past the fold.
  int16_t grow = 0;
  if (!model.page && plan.starts.size() == 1 && end > begin) {
    int total = 0;
    for (int i = begin; i < end; i++) total += flow.blocks[i].height;
    const int share = (plan.usable - total) / (end - begin);
    if (share > 0) grow = static_cast<int16_t>(share < kGrowCap ? share : kGrowCap);
  }

  int16_t y = band.y;
  for (int i = begin; i < end; i++) {
    const Block& block = flow.blocks[i];
    if (model.page) {
      if (block.paragraphStart && y > band.y) y = static_cast<int16_t>(y + kParaGap);
      drawLines(screen, fui::makeRect(band.x, y, flow.textWidth, flow.lineHeight), block.lines, flow.body, false);
      y = static_cast<int16_t>(y + block.height);
      continue;
    }
    const Task& task = model.tasks[block.item];
    const fui::Rect row = fui::makeRect(band.x, y, band.width, static_cast<int16_t>(block.height + grow));
    const int16_t textHeight = static_cast<int16_t>(static_cast<int>(block.lines.size()) * flow.lineHeight);
    const int16_t textTop = static_cast<int16_t>(row.y + (row.height - textHeight) / 2);
    // The box sits on the FIRST line, the way every checklist sets a wrapped
    // item: centred on the row it reads as belonging to the middle of the
    // sentence, and for a one-line row the two are the same place anyway.
    const int16_t boxTop = static_cast<int16_t>(textTop + flow.lineHeight / 2 - kBoxSide / 2);
    tickBox(screen, fui::makeRect(row.x, boxTop, kBoxSide, kBoxSide), task.checked);
    const int16_t textX = static_cast<int16_t>(row.x + kBoxSide + toybox::kGutter);
    drawLines(screen, fui::makeRect(textX, textTop, flow.textWidth, textHeight), block.lines, flow.body, task.checked);
    rowHit(screen, row, ActionToggleTask, block.item);
    y = static_cast<int16_t>(y + row.height);
  }
}

}  // namespace

namespace {
// The strip under the band on a list: what is left, and a bar. It is the first
// thing the eye lands on after the name, and it is what the screen is FOR --
// the rows answer "which", the strip answers "how much".
// The strip sits in EQUAL air: the gap from the chrome down to the bar is the
// same as the gap from its rule down to the first row. It used to start at
// kBodyTop and keep the body's own 36px inset above it as well as a gutter
// below, which put a 14px bar inside 90px of page.
constexpr int16_t kStripGap = 28;      // above the bar, and below the rule
constexpr int16_t kStripRuleGap = 14;  // bar to its rule, tighter: they are one block
constexpr int16_t kChromeBottom = kBodyTop - toybox::kBodyGutter;

int16_t stripBarTop() { return static_cast<int16_t>(kChromeBottom + kStripGap); }
int16_t stripRuleY() { return static_cast<int16_t>(stripBarTop() + kBarHeight + kStripRuleGap); }

int16_t stripSpace(const NoteModel& model) {
  if (model.page || model.total <= 0) return 0;
  // The rows begin immediately under the rule and NO gap is added: a row is
  // taller than its tick box and centres it, so the row's own padding is
  // already the air below the rule -- about the same as the air above the bar,
  // which is what makes the strip sit in an even band. Adding a gutter here too
  // counted that space twice and pushed the first item a third of a page down.
  return static_cast<int16_t>(stripRuleY() + toybox::kHairline - kBodyTop);
}

fui::Rect noteBandFor(const fui::DeviceContext& device, const NoteModel& model) {
  const int16_t width = static_cast<int16_t>(device.width - 2 * toybox::kMargin);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);
  const int16_t top = static_cast<int16_t>(kBodyTop + stripSpace(model));
  return fui::makeRect(toybox::kMargin, top, width, static_cast<int16_t>(footerY - toybox::kGutter * 2 - top));
}

// "3 LEFT" and a bar on one line, with a rule under it closing the block off
// from the rows. Drawn against the page rather than on a slab: a second black
// band under the header would fight the header for the top of the screen.
void progressStrip(toybox::Screen& screen, const fui::Rect& band, const NoteModel& model) {
  const int16_t barTop = stripBarTop();
  const int left = model.total - model.done;
  char label[32];
  if (left == 0) {
    std::snprintf(label, sizeof(label), "ALL DONE");
  } else {
    std::snprintf(label, sizeof(label), "%d LEFT OF %d", left, model.total);
  }
  fui::TextStyle style = plain(toybox::kTileFont);
  const int16_t lineHeight = screen.target().lineHeight(style.font);
  const int16_t labelWidth =
      static_cast<int16_t>(screen.target().measureText(style.font, label, style).width + toybox::kGutter * 2);
  // The label rides the bar's centre line, not a box of its own: its line box is
  // taller than the bar, and centring the two boxes separately leaves the words
  // sitting a few pixels off the rule they belong to.
  const int16_t barCentre = static_cast<int16_t>(barTop + kBarHeight / 2);
  screen.target().text(fui::makeRect(band.x, static_cast<int16_t>(barCentre - lineHeight / 2), labelWidth, lineHeight),
                       label, style);
  const fui::Rect bar = fui::makeRect(static_cast<int16_t>(band.x + labelWidth), barTop,
                                      static_cast<int16_t>(band.width - labelWidth), kBarHeight);
  progressBar(screen, bar, model.done, model.total);
  screen.target().fill(fui::makeRect(band.x, stripRuleY(), band.width, toybox::kHairline),
                       fui::Paint::solid(fui::Color::Black));
}
}  // namespace

std::vector<int> notePageStarts(const fui::DrawTarget& target, const fui::DeviceContext& device,
                                const NoteModel& model) {
  return planNote(target, model, noteBandFor(device, model)).starts;
}

int notePageOfItem(const fui::DrawTarget& target, const fui::DeviceContext& device, const NoteModel& model,
                   const int item) {
  const NotePlan plan = planNote(target, model, noteBandFor(device, model));
  int first = 0;
  for (size_t b = 0; b < plan.flow.blocks.size(); b++) {
    if (plan.flow.blocks[b].item == item) {
      first = static_cast<int>(b);
      break;
    }
  }
  int page = 0;
  for (size_t p = 0; p < plan.starts.size(); p++) {
    if (plan.starts[p] <= first) page = static_cast<int>(p);
  }
  return page;
}

void buildNote(toybox::Screen& screen, const NoteModel& model) {
  chrome(screen, model.title, nullptr, model.menuIcon, toybox::kBodyFont);
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);
  const fui::Rect band = noteBandFor(device, model);
  if (stripSpace(model) > 0) progressStrip(screen, band, model);

  noteRows(screen, model, band, planNote(screen.target(), model, band));
  pageLabel(screen, band, model.pageLabel);

  // ADD keeps the left edge, the fork-wide home for a primary action and the
  // pixel a thumb learns. CLEAR DONE appears only when there is something to
  // clear, and it appears on the RIGHT, so the control that removes lines never
  // occupies the pixels ADD had a moment ago.
  if (model.anyDone && !model.page) {
    const int16_t half = static_cast<int16_t>((width - toybox::kGutter) / 2);
    footerButton(screen, fui::makeRect(toybox::kMargin, footerY, half, kFooterHeight), "ADD", ActionAddLine, false);
    footerButton(
        screen,
        fui::makeRect(static_cast<int16_t>(toybox::kMargin + half + toybox::kGutter), footerY, half, kFooterHeight),
        "CLEAR DONE", ActionClearDone, true);
  } else {
    footerButton(screen, fui::makeRect(toybox::kMargin, footerY, width, kFooterHeight), "ADD", ActionAddLine, false);
  }
}

// --- The menu ------------------------------------------------------------

void buildConfirm(toybox::Screen& screen, const ConfirmModel& model) {
  chrome(screen, model.title, nullptr, model.menuIcon, toybox::kBodyFont);
  const fui::DeviceContext& device = screen.device();
  // What the delete costs, because the only thing a person can do about a note
  // they did not mean to delete is not delete it.
  sheetProse(screen, sheetBand(device), model.prose);

  // KEEP takes the left, filled: the pixels ADD and the safe action occupy on
  // every other screen. DELETE takes the right, outlined, where CLEAR DONE is
  // -- the side of the bar that takes things away.
  const fui::Rect footer = footerBand(device);
  const int16_t half = static_cast<int16_t>((footer.width - toybox::kGutter) / 2);
  footerButton(screen, fui::makeRect(footer.x, footer.y, half, footer.height), "KEEP IT", ActionDismiss, false);
  footerButton(screen,
               fui::makeRect(static_cast<int16_t>(footer.x + half + toybox::kGutter), footer.y, half, footer.height),
               "DELETE IT", ActionDelete, true);
}

fui::Rect buildPhone(toybox::Screen& screen, const PhoneModel& model) {
  chrome(screen, model.title, nullptr, model.menuIcon, toybox::kBodyFont);
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);

  const int16_t lineHeight = screen.target().lineHeight(toybox::kTileFont);
  const fui::Rect caption =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(kBodyTop + toybox::kGutter), width, lineHeight);
  fittedLine(screen, caption, "POINT YOUR PHONE CAMERA HERE", fui::TextAlign::Center, toybox::kTileFont);

  // The code, the address under it, and the state under that, stacked from the
  // caption down rather than from the footer up: the block grows downward into
  // space that is empty, instead of upward into the button.
  const int16_t top = static_cast<int16_t>(caption.y + caption.height + toybox::kGutter * 2);
  // The code takes about half the panel, not all of it. It used to grow into
  // every pixel left over, which pushed the address -- the one string somebody
  // may have to read off the glass and type into a browser -- to the bottom in
  // the smallest type on the screen, under a code they had already scanned.
  const int16_t room = static_cast<int16_t>(footerY - toybox::kGutter * 2 - top - lineHeight * 5);
  int16_t side = room < width ? room : width;
  if (side > 300) side = 300;
  if (side < 120) side = 120;
  const fui::Rect qr = fui::makeRect(static_cast<int16_t>((device.width - side) / 2), top, side, side);

  const fui::Rect url =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(qr.y + qr.height + toybox::kGutter), width, lineHeight);
  fittedLine(screen, url, model.readable, fui::TextAlign::Center, toybox::kTileFont);

  // The state line says whether anything has arrived. A screen whose whole
  // promise is "type over there and it appears here" has to answer "did it".
  const fui::Rect state =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(url.y + url.height + toybox::kGutter), width, lineHeight);
  fittedLine(screen, state, model.saved ? "SAVED FROM YOUR PHONE" : "WAITING FOR YOUR PHONE", fui::TextAlign::Center,
             toybox::kTileFont);

  footerButton(screen, fui::makeRect(toybox::kMargin, footerY, width, kFooterHeight), "DONE", ActionDismiss, false);
  return qr;
}

void buildNotice(toybox::Screen& screen, const ConfirmModel& model) {
  chrome(screen, model.title, nullptr, nullptr, toybox::kBodyFont);
  const fui::DeviceContext& device = screen.device();
  sheetProse(screen, sheetBand(device), model.prose);
  footerButton(screen, footerBand(device), "BACK", ActionDismiss, false);
}

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  chrome(screen, model.title, nullptr, model.menuIcon, toybox::kBodyFont);
  const fui::DeviceContext& device = screen.device();
  const fui::Rect band = sheetBand(device);

  // Rows stack from the top at one height, rather than dividing the page by
  // however many there are: a sheet that re-spaces itself when a row appears is
  // a sheet whose rows move under the finger.
  struct Row {
    const char* label;
    const char* note;
    fui::ActionId action;
  };
  const Row rows[] = {
      // ALWAYS offered, even with no Wi-Fi: tapping it is what OFFERS to join
      // one. A row disabled with "join Wi-Fi first" would send a person to
      // Settings to do by hand the job this row is holding the tools for.
      {"TYPE ON YOUR PHONE", model.phoneHint, ActionUsePhone},
      // The kind is inferred from the file, so it can be inferred wrong: a
      // shopping list typed as plain lines on a computer opens as a note with
      // nothing to tick. This is how a person fixes that without knowing that a
      // tick box is written `- [ ]`.
      // Only one direction gets a caption, because only one loses something.
      {model.isList ? "MAKE IT A NOTE" : "MAKE IT A LIST", model.isList ? "the ticks are lost" : nullptr,
       ActionSwitchKind},
      {"RENAME", nullptr, ActionRename},
  };
  const int count = static_cast<int>(sizeof(rows) / sizeof(rows[0]));

  const fui::TextStyle label = plain(toybox::kBodyFont);
  const fui::TextStyle note = plain(toybox::kTileFont);
  const int16_t labelHeight = screen.target().lineHeight(label.font);
  const int16_t noteHeight = screen.target().lineHeight(note.font);
  const int16_t pitch = fittedPitch(kSheetRow, kSheetRowMax, band.height, count, band.height / kSheetRow);
  int16_t y = band.y;
  for (int i = 0; i < count; i++) {
    const fui::Rect row = fui::makeRect(band.x, y, band.width, pitch);
    const int16_t captionGap = 4;
    const int16_t block = static_cast<int16_t>(labelHeight + (rows[i].note != nullptr ? noteHeight + captionGap : 0));
    const int16_t top = static_cast<int16_t>(row.y + (row.height - block) / 2);
    const fui::Rect labelBox = fui::makeRect(row.x, top, row.width, labelHeight);
    fui::TextStyle drawStyle = label;
    const std::string drawn = toybox::fittedTitle(screen.target(), rows[i].label, labelBox.width, drawStyle);
    screen.target().text(labelBox, drawn.c_str(), drawStyle);
    if (rows[i].note != nullptr) {
      screen.target().text(
          fui::makeRect(labelBox.x, static_cast<int16_t>(top + labelHeight + captionGap), labelBox.width, noteHeight),
          rows[i].note, note);
    }
    rowHit(screen, row, rows[i].action, i);
    if (i + 1 < count) separator(screen, row);
    y = static_cast<int16_t>(y + pitch);
  }

  // DELETE is not one of those rows. It sits alone on the action bar, outlined,
  // a page away from anything a thumb reaches for by habit.
  footerButton(screen, footerBand(device), "DELETE NOTE", ActionDelete, true);
}

}  // namespace notesui
