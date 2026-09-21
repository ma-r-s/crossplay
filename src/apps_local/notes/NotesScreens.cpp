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
constexpr int kRowPad = toybox::kGutter;
// One height for every row of a sheet, so the menu, the confirm and the notice
// agree about where a row starts.
constexpr int16_t kSheetRow = 96;
constexpr int16_t kSheetRowMax = 140;
constexpr int kMinRow = 72;       // a finger, with room to miss
constexpr int16_t kRowMax = 108;  // and no more than a finger and a half

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

// Text drawn from the top of its box rather than centred, so a one-line row and
// a two-line row start their first line at the same height. Screen::text
// centres the LINE BOX, which would put a one-liner's baseline somewhere a
// two-liner's is not, and a column of rows that disagree about that reads as
// bad spacing even when every row is correct on its own.
void topText(toybox::Screen& screen, const fui::Rect& box, const std::string& text, const fui::TextStyle& style,
             const int lines) {
  const int16_t lineHeight = screen.target().lineHeight(style.font);
  const fui::Rect drawn = fui::makeRect(box.x, box.y, box.width, static_cast<int16_t>(lineHeight * lines));
  screen.target().text(drawn, text.c_str(), style);
}

// The strike is measured against the LONGEST DRAWN LINE, not the source string,
// so a line that wrapped gets a rule the width of what is really on the glass.
void strikeLines(toybox::Screen& screen, const fui::Rect& box, const std::string& drawn, const fui::TextStyle& style,
                 const int lines) {
  const int16_t lineHeight = screen.target().lineHeight(style.font);
  size_t start = 0;
  for (int i = 0; i < lines; i++) {
    size_t stop = drawn.find('\n', start);
    if (stop == std::string::npos) stop = drawn.size();
    const std::string run = drawn.substr(start, stop - start);
    const int16_t width = screen.target().measureText(style.font, run.c_str(), style).width;
    const int16_t y = static_cast<int16_t>(box.y + i * lineHeight + lineHeight / 2);
    screen.target().fill(fui::makeRect(box.x, y, width, 2), fui::Paint::solid(fui::Color::Black));
    if (stop >= drawn.size()) break;
    start = stop + 1;
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

// cut needs plus air, floored at a finger.
int16_t typeRowHeight(const int16_t lineHeight, const int lines) {
  const int height = lineHeight * lines + kRowPad * 2;
  return static_cast<int16_t>(height < kMinRow ? kMinRow : height);
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

// --- Shared measuring ----------------------------------------------------

int linesNeeded(const fui::DrawTarget& target, const char* text, const int16_t width, const int maxLines,
                const fui::TextStyle& style) {
  if (text == nullptr || *text == '\0') return 1;
  if (width <= 0) return 0;
  int lines = 1;
  int16_t used = 0;
  const std::string whole(text);
  size_t i = 0;
  while (i < whole.size()) {
    size_t end = whole.find(' ', i);
    if (end == std::string::npos) end = whole.size();
    const std::string word = whole.substr(i, end - i);
    const int16_t wordWidth = target.measureText(style.font, word.c_str(), style).width;
    const int16_t spaceWidth = used == 0 ? 0 : target.measureText(style.font, " ", style).width;
    // A single word wider than the whole line can never be placed: the rule is
    // shrink, never hyphenate, so this cut is simply not available.
    if (wordWidth > width) return 0;
    if (used + spaceWidth + wordWidth > width) {
      lines++;
      if (lines > maxLines) return 0;
      used = wordWidth;
    } else {
      used = static_cast<int16_t>(used + spaceWidth + wordWidth);
    }
    i = end + 1;
  }
  return lines;
}

fui::FontId pickCut(const fui::DrawTarget& target, const char* const* strings, const int count, const int16_t width,
                    const int maxLines, const fui::TextStyle& probe, const bool onlyProbeCut) {
  // The three slots a target binds, largest first. There is no fourth: the fui
  // components resolve only these and fall back to BODY for anything else.
  const fui::FontId all[3] = {fui::FONT_SLOT_TITLE, fui::FONT_SLOT_BODY, fui::FONT_SLOT_SMALL};
  const fui::FontId one[1] = {probe.font};
  const fui::FontId* rungs = onlyProbeCut ? one : all;
  const int rungCount = onlyProbeCut ? 1 : 3;
  fui::FontId best = 0;
  int16_t bestHeight = 0;
  for (int r = 0; r < rungCount; r++) {
    const fui::FontId rung = rungs[r];
    const int16_t height = target.lineHeight(rung);
    if (height > target.lineHeight(probe.font)) continue;  // fitting only goes down
    fui::TextStyle trial = probe;
    trial.font = rung;
    bool all = true;
    for (int i = 0; i < count; i++) {
      if (linesNeeded(target, strings[i], width, maxLines, trial) == 0) {
        all = false;
        break;
      }
    }
    if (!all) continue;
    if (best == 0 || height > bestHeight) {
      best = rung;
      bestHeight = height;
    }
  }
  return best;
}

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

constexpr int16_t kCardHeight = 96;
constexpr int16_t kCardGap = 12;
constexpr int16_t kCardMax = 132;  // a card, not a slab
constexpr int16_t kCardPitch = kCardHeight + kCardGap;
constexpr int16_t kBadgeWidth = 76;
constexpr int16_t kBarHeight = 14;

int16_t titleColumn(const fui::Rect& band) { return static_cast<int16_t>(band.width - kBadgeWidth - toybox::kGutter); }

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

DeckLayout deckLayoutFor(const fui::DrawTarget& target, const DeckModel& model, const fui::Rect& band) {
  DeckLayout layout;
  layout.title = plain(toybox::kBodyFont, fui::TextAlign::Left, 1);
  const int16_t width = titleColumn(band);
  std::vector<const char*> titles;
  titles.reserve(static_cast<size_t>(model.count));
  for (int i = 0; i < model.count; i++) titles.push_back(model.items[i].title);

  // ONE cut for every card on the page. Peers share a cut, so a long name pulls
  // the whole column down a size rather than singling itself out, and no card
  // is ever a different size from the card under it.
  fui::FontId cut = model.count > 0 ? pickCut(target, titles.data(), model.count, width, 1, layout.title) : 0;
  if (cut == 0) cut = fui::FONT_SLOT_SMALL;  // the only cut with an ellipsis to admit the cut
  layout.title.font = cut;

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
    const int16_t titleHeight = screen.target().lineHeight(layout.title.font);
    const bool hasSecond = list || (item.preview != nullptr && *item.preview != '\0');
    const int16_t secondHeight = list ? kBarHeight : screen.target().lineHeight(small.font);
    const int16_t block = static_cast<int16_t>(titleHeight + (hasSecond ? toybox::kGutter + secondHeight : 0));
    const int16_t top = static_cast<int16_t>(card.y + (card.height - block) / 2);

    const fui::Rect titleBox = fui::makeRect(textX, top, textWidth, titleHeight);
    const std::string drawn = toybox::fitLines(screen.target(), item.title, textWidth, 1, layout.title);
    topText(screen, titleBox, drawn, layout.title, 1);
    if (hasSecond) {
      const fui::Rect secondBox =
          fui::makeRect(textX, static_cast<int16_t>(top + titleHeight + toybox::kGutter), textWidth, secondHeight);
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

int deckCapacity(const fui::DrawTarget& target, const fui::DeviceContext& device, const DeckModel& model) {
  const fui::Rect band = deckBand(device);
  const DeckLayout layout = deckLayoutFor(target, model, band);
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
  const DeckLayout layout = deckLayoutFor(screen.target(), model, band);
  int16_t y = band.y;
  deckRows(screen, model, band, layout, y);
  pageLabel(screen, band, model.pageLabel);
}

// --- A note, open --------------------------------------------------------

namespace {

struct NoteLayout {
  fui::TextStyle body{};
  int16_t textWidth = 0;
  int16_t rowHeight = 0;
};

NoteLayout noteLayoutFor(const fui::DrawTarget& target, const NoteModel& model, const fui::Rect& band) {
  NoteLayout layout;
  // The page label owns the last line of the band when there is one, so the
  // rows are laid out against what is left rather than drawn over it.
  layout.textWidth = static_cast<int16_t>(band.width - kBoxSide - toybox::kGutter);

  std::vector<const char*> texts;
  texts.reserve(static_cast<size_t>(model.count));
  for (int i = 0; i < model.count; i++) texts.push_back(model.tasks[i].text);

  // BODY on one line, then BODY on two, and only then the small cut. The order
  // matters and it used to be the other way round: a single long item would
  // drop the WHOLE list to toybox_10, halving every row to fit one of them.
  // That trade buys nothing -- typeRowHeight floors at a finger, so
  // small-on-one-line and body-on-two-lines produce the identical 72px row and
  // the same eight rows per page. The small cut survives only for a single word
  // too wide for the body cut, which cannot be broken and must not be elided.
  layout.body = plain(toybox::kBodyFont, fui::TextAlign::Left, 2);
  fui::TextStyle bodyOnly = layout.body;
  bodyOnly.font = toybox::kBodyFont;

  // NOTHING IS EVER ELIDED, and this is where that promise is actually kept.
  // The rungs are walked in the order a reader would want -- as big as
  // possible, as few lines as possible -- and the FIRST one that holds every
  // item wins. Falling off the end used to mean handing the job to fitLines,
  // which appends an ellipsis: Mario's own note drew "Hola esto es una..." on
  // the real panel, which is precisely the defect this ladder exists to
  // prevent. Four lines at the small cut is a deep enough last rung that a
  // line reaching it is a paragraph somebody pasted, not a list item.
  struct Rung {
    bool bodyCut;
    int lines;
  };
  static constexpr Rung kRungs[] = {{true, 1}, {true, 2}, {false, 2}, {true, 3}, {false, 3}, {false, 4}};
  fui::FontId cut = 0;
  int lines = 1;
  if (model.count == 0) {
    cut = toybox::kBodyFont;
  } else {
    for (const Rung& rung : kRungs) {
      const fui::TextStyle& probe = rung.bodyCut ? bodyOnly : layout.body;
      cut = pickCut(target, texts.data(), model.count, layout.textWidth, rung.lines, probe, rung.bodyCut);
      if (cut != 0) {
        lines = rung.lines;
        break;
      }
    }
  }
  if (cut == 0) {
    cut = fui::FONT_SLOT_SMALL;
    lines = 4;
  }
  layout.body.font = cut;
  layout.body.maxLines = static_cast<uint8_t>(lines);

  layout.rowHeight = typeRowHeight(target.lineHeight(cut), lines);
  return layout;
}

void noteRows(toybox::Screen& screen, const NoteModel& model, const fui::Rect& band, const NoteLayout& layout,
              int16_t& y) {
  const int visible = layout.rowHeight > 0 ? band.height / layout.rowHeight : 1;
  const int16_t pitch = fittedPitch(layout.rowHeight, kRowMax, band.height, model.count, visible);
  for (int i = model.firstVisible; i < model.count; i++) {
    if (y + pitch > band.y + band.height) break;
    const fui::Rect row = fui::makeRect(band.x, y, band.width, pitch);
    const Task& task = model.tasks[i];

    // EVERY row is an item, with a box, at the same cut. There used to be a
    // second class -- a line the parser did not recognise as a task was drawn
    // with no box at toybox_10, which is 13px of ink beside 25px. Two lines of
    // one list, which a person reads as the same kind of thing, differed by
    // half; and the way to get one was to type a line on the phone, which is
    // exactly what the phone is for.
    if (!model.page) {
      tickBox(screen,
              fui::makeRect(row.x, static_cast<int16_t>(row.y + (row.height - kBoxSide) / 2), kBoxSide, kBoxSide),
              task.checked);
    }
    const int16_t textX = model.page ? row.x : static_cast<int16_t>(row.x + kBoxSide + toybox::kGutter);

    fui::TextStyle style = layout.body;
    const int16_t boxWidth = static_cast<int16_t>(row.x + row.width - textX);
    const int lines = linesNeeded(screen.target(), task.text, boxWidth, style.maxLines, style);
    const std::string drawn = toybox::fitLines(screen.target(), task.text, boxWidth, style.maxLines, style);
    const int16_t lineHeight = screen.target().lineHeight(style.font);
    const int drawnLines = lines < 1 ? 1 : lines;
    // Vertically centred as a BLOCK, so a two-line row and a one-line row share
    // a centre line and the tick boxes beside them stay on one axis.
    const fui::Rect textBox =
        fui::makeRect(textX, static_cast<int16_t>(row.y + (row.height - lineHeight * drawnLines) / 2), boxWidth,
                      static_cast<int16_t>(lineHeight * drawnLines));
    topText(screen, textBox, drawn, style, drawnLines);
    if (task.checked) strikeLines(screen, textBox, drawn, style, drawnLines);
    // A page has nothing to tick, so its rows take no tap at all rather than
    // taking one that does nothing.
    if (!model.page) rowHit(screen, row, ActionToggleTask, i);
    y = static_cast<int16_t>(y + pitch);
  }
  if (model.count == 0) {
    centredNotice(screen, band, model.page ? "This note is empty. Tap ADD." : "Nothing on this list. Tap ADD.");
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

int noteCapacity(const fui::DrawTarget& target, const fui::DeviceContext& device, const NoteModel& model) {
  const fui::Rect band = noteBandFor(device, model);
  const NoteLayout layout = noteLayoutFor(target, model, band);
  if (layout.rowHeight <= 0) return 1;
  const int rows = band.height / layout.rowHeight;
  return rows < 1 ? 1 : rows;
}

void buildNote(toybox::Screen& screen, const NoteModel& model) {
  chrome(screen, model.title, nullptr, model.menuIcon, toybox::kBodyFont);
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);
  const fui::Rect band = noteBandFor(device, model);
  if (stripSpace(model) > 0) progressStrip(screen, band, model);

  const NoteLayout layout = noteLayoutFor(screen.target(), model, band);
  int16_t y = band.y;
  noteRows(screen, model, band, layout, y);
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
