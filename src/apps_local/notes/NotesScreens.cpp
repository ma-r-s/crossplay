#include "NotesScreens.h"

#include <string>
#include <vector>

#include "../ui/ToyboxText.h"

namespace notesui {
namespace {

constexpr int kBodyTop = toybox::kBodyTop;
constexpr int kFooterHeight = toybox::kPillHeight;
constexpr int kBoxSide = 40;
constexpr int kRowPad = toybox::kGutter;
// Shared by the menu and by the confirm that lands on top of it. The confirm's
// KEEP must occupy the pixels the menu's DELETE NOTE had, so both divide the
// band by the same number and a static_assert in buildMenu holds the table to it.
constexpr int kMenuRows = 4;
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
            const freeink::Icon* trailing = nullptr) {
  const int16_t band = static_cast<int16_t>(screen.device().width - 2 * toybox::kMargin);
  fui::TextStyle titleStyle = screen.theme().titleText;
  int16_t room = band;
  if (rightLabel != nullptr) {
    fui::TextStyle right = screen.theme().smallText;
    right.font = toybox::kTileFont;
    room = static_cast<int16_t>(band - screen.target().measureText(right.font, rightLabel, right).width -
                                toybox::kGutter * 2);
  }
  static std::string fitted;
  fitted = toybox::fittedTitle(screen.target(), title, room, titleStyle);

  fui::HeaderProps header;
  header.title = fitted.c_str();
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
// cut needs plus air, floored at a finger.
int16_t typeRowHeight(const int16_t lineHeight, const int lines) {
  const int height = lineHeight * lines + kRowPad * 2;
  return static_cast<int16_t>(height < kMinRow ? kMinRow : height);
}

void footerButton(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId action,
                  const bool outlined) {
  fui::ButtonProps button;
  button.label = label;
  button.action = action;
  if (outlined) button.styles = toybox::rowStyles();
  screen.button(button, box);
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
                    const int maxLines, const fui::TextStyle& probe) {
  // The three slots a target binds, largest first. There is no fourth: the fui
  // components resolve only these and fall back to BODY for anything else.
  const fui::FontId rungs[3] = {fui::FONT_SLOT_TITLE, fui::FONT_SLOT_BODY, fui::FONT_SLOT_SMALL};
  fui::FontId best = 0;
  int16_t bestHeight = 0;
  for (const fui::FontId rung : rungs) {
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
struct DeckLayout {
  int16_t tallyWidth = 0;
  fui::TextStyle title{};
  int16_t rowHeight = 0;
  int visible = 0;
};

DeckLayout deckLayoutFor(const fui::DrawTarget& target, const DeckModel& model, const fui::Rect& band) {
  DeckLayout layout;
  fui::TextStyle tally = plain(toybox::kTileFont, fui::TextAlign::Right);

  // The gutter is the widest tally in the deck, reserved for every row whether
  // or not that row has one, so a prose note and a list note keep the same left
  // edge and the column of titles does not jog.
  for (int i = 0; i < model.count; i++) {
    if (model.items[i].tally == nullptr) continue;
    const int16_t width = target.measureText(tally.font, model.items[i].tally, tally).width;
    if (width > layout.tallyWidth) layout.tallyWidth = width;
  }
  if (layout.tallyWidth > 0) layout.tallyWidth = static_cast<int16_t>(layout.tallyWidth + toybox::kGutter);

  const int16_t titleWidth = static_cast<int16_t>(band.width - layout.tallyWidth - toybox::kGutter);
  std::vector<const char*> titles;
  titles.reserve(static_cast<size_t>(model.count));
  for (int i = 0; i < model.count; i++) titles.push_back(model.items[i].title);

  layout.title = plain(toybox::kBodyFont, fui::TextAlign::Left, 2);
  fui::FontId cut =
      model.count > 0 ? pickCut(target, titles.data(), model.count, titleWidth, 1, layout.title) : layout.title.font;
  int lines = 1;
  if (cut == 0) {
    // Nothing holds every title on one line. Two lines at the largest cut that
    // does beats one line at a cut so small the deck reads as a footnote.
    cut = pickCut(target, titles.data(), model.count, titleWidth, 2, layout.title);
    lines = 2;
  }
  if (cut == 0) {
    cut = fui::FONT_SLOT_SMALL;
    lines = 2;
  }
  layout.title.font = cut;
  layout.title.maxLines = static_cast<uint8_t>(lines);

  layout.rowHeight = typeRowHeight(target.lineHeight(cut), lines);
  layout.visible = band.height / layout.rowHeight;
  if (layout.visible < 1) layout.visible = 1;
  return layout;
}

void deckRows(toybox::Screen& screen, const DeckModel& model, const fui::Rect& band, const DeckLayout& layout,
              int16_t& y) {
  fui::TextStyle tally = plain(toybox::kTileFont, fui::TextAlign::Right);
  for (int i = model.firstVisible; i < model.count && i - model.firstVisible < layout.visible; i++) {
    const fui::Rect row = fui::makeRect(band.x, y, band.width, layout.rowHeight);
    const int16_t titleWidth = static_cast<int16_t>(row.width - layout.tallyWidth - toybox::kGutter);
    const int lines =
        linesNeeded(screen.target(), model.items[i].title, titleWidth, layout.title.maxLines, layout.title);
    const int drawnLines = lines < 1 ? 1 : lines;
    const int16_t lineHeight = screen.target().lineHeight(layout.title.font);
    const int16_t textTop = static_cast<int16_t>(row.y + (row.height - lineHeight * drawnLines) / 2);
    const fui::Rect titleBox = fui::makeRect(row.x, textTop, titleWidth, static_cast<int16_t>(lineHeight * drawnLines));
    const std::string drawn =
        toybox::fitLines(screen.target(), model.items[i].title, titleBox.width, layout.title.maxLines, layout.title);
    topText(screen, titleBox, drawn, layout.title, drawnLines);

    if (model.items[i].tally != nullptr) {
      // On the first line of the title, not centred in the row: a tally beside a
      // two-line name belongs to its first line, the way a page number does.
      const fui::Rect tallyBox = fui::makeRect(static_cast<int16_t>(row.x + row.width - layout.tallyWidth), textTop,
                                               layout.tallyWidth, screen.target().lineHeight(tally.font));
      screen.target().text(tallyBox, model.items[i].tally, tally);
    }

    rowHit(screen, row, ActionOpenNote, i);
    const bool last = (i + 1 >= model.count) || (i + 1 - model.firstVisible >= layout.visible);
    if (!last) separator(screen, row);
    y = static_cast<int16_t>(y + layout.rowHeight);
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
  if (layout.rowHeight <= 0) return 1;
  const int rows = band.height / layout.rowHeight;
  return rows < 1 ? 1 : rows;
}

void buildDeck(toybox::Screen& screen, const DeckModel& model) {
  chrome(screen, "NOTES");
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);
  const fui::Rect band =
      fui::makeRect(toybox::kMargin, kBodyTop, width, static_cast<int16_t>(footerY - toybox::kGutter - kBodyTop));

  footerButton(screen, fui::makeRect(toybox::kMargin, footerY, width, kFooterHeight), "NEW NOTE", ActionNewNote, false);
  if (model.count == 0) {
    centredNotice(screen, band, "Nothing here yet. A note is a list you tick, or a page you keep.");
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

  layout.body = plain(toybox::kBodyFont, fui::TextAlign::Left, 2);
  fui::FontId cut =
      model.count > 0 ? pickCut(target, texts.data(), model.count, layout.textWidth, 1, layout.body) : layout.body.font;
  int lines = 1;
  if (cut == 0) {
    // A long line wraps rather than shrinking. A shopping list is a list of
    // things and a long thing takes two lines, exactly as it would on paper;
    // shrinking it instead makes one item look less important than its
    // neighbours, which is the one thing a list must never say.
    cut = pickCut(target, texts.data(), model.count, layout.textWidth, 2, layout.body);
    lines = 2;
  }
  if (cut == 0) {
    cut = fui::FONT_SLOT_SMALL;
    lines = 2;
  }
  layout.body.font = cut;
  layout.body.maxLines = static_cast<uint8_t>(lines);

  layout.rowHeight = typeRowHeight(target.lineHeight(cut), lines);
  return layout;
}

void noteRows(toybox::Screen& screen, const NoteModel& model, const fui::Rect& band, const NoteLayout& layout,
              int16_t& y) {
  for (int i = model.firstVisible; i < model.count; i++) {
    if (y + layout.rowHeight > band.y + band.height) break;
    const fui::Rect row = fui::makeRect(band.x, y, band.width, layout.rowHeight);
    const Task& task = model.tasks[i];

    int16_t textX = row.x;
    if (task.isTask) {
      tickBox(screen,
              fui::makeRect(row.x, static_cast<int16_t>(row.y + (row.height - kBoxSide) / 2), kBoxSide, kBoxSide),
              task.checked);
      textX = static_cast<int16_t>(row.x + kBoxSide + toybox::kGutter);
    }

    fui::TextStyle style = layout.body;
    if (!task.isTask) style.font = toybox::kTileFont;  // prose is the app's aside, not one of the things to do
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

    if (task.isTask) rowHit(screen, row, ActionToggleTask, i);
    y = static_cast<int16_t>(y + layout.rowHeight);
  }
  if (model.count == 0) centredNotice(screen, band, "This note is empty. Add the first line.");
}

}  // namespace

namespace {
fui::Rect noteBandFor(const fui::DeviceContext& device) {
  const int16_t width = static_cast<int16_t>(device.width - 2 * toybox::kMargin);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);
  return fui::makeRect(toybox::kMargin, kBodyTop, width,
                       static_cast<int16_t>(footerY - toybox::kGutter * 2 - kBodyTop));
}
}  // namespace

int noteCapacity(const fui::DrawTarget& target, const fui::DeviceContext& device, const NoteModel& model) {
  const fui::Rect band = noteBandFor(device);
  const NoteLayout layout = noteLayoutFor(target, model, band);
  if (layout.rowHeight <= 0) return 1;
  const int rows = band.height / layout.rowHeight;
  return rows < 1 ? 1 : rows;
}

void buildNote(toybox::Screen& screen, const NoteModel& model) {
  chrome(screen, model.title, nullptr, model.menuIcon);
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);
  const fui::Rect band =
      fui::makeRect(toybox::kMargin, kBodyTop, width, static_cast<int16_t>(footerY - toybox::kGutter * 2 - kBodyTop));

  const NoteLayout layout = noteLayoutFor(screen.target(), model, band);
  int16_t y = band.y;
  noteRows(screen, model, band, layout, y);
  pageLabel(screen, band, model.pageLabel);

  // ADD keeps the left edge, the fork-wide home for a primary action and the
  // pixel a thumb learns. CLEAR DONE appears only when there is something to
  // clear, and it appears on the RIGHT, so the control that removes lines never
  // occupies the pixels ADD had a moment ago.
  if (model.anyDone) {
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

fui::Rect menuRowRect(const fui::DeviceContext& device, const int index) {
  const int16_t width = static_cast<int16_t>(device.width - 2 * toybox::kMargin);
  const fui::Rect band =
      fui::makeRect(toybox::kMargin, kBodyTop, width, static_cast<int16_t>(device.height - toybox::kMargin - kBodyTop));
  const int16_t rowHeight = static_cast<int16_t>(band.height / kMenuRows);
  return fui::makeRect(band.x, static_cast<int16_t>(band.y + index * rowHeight), band.width, rowHeight);
}

void buildConfirm(toybox::Screen& screen, const ConfirmModel& model) {
  chrome(screen, model.title, nullptr, model.menuIcon);
  const fui::DeviceContext& device = screen.device();

  // The prose gets the first three menu rows' worth of page, wrapped at the
  // body cut. It says what the delete costs, because the only thing a person
  // can do about a note they did not mean to delete is not delete it.
  const fui::Rect top = menuRowRect(device, 0);
  const fui::Rect third = menuRowRect(device, 2);
  fui::TextStyle prose = plain(toybox::kBodyFont, fui::TextAlign::Left, 5);
  const int16_t proseHeight = static_cast<int16_t>(third.y + third.height - top.y);
  const fui::Rect box = fui::makeRect(top.x, top.y, top.width, proseHeight);
  const int16_t lineHeight = screen.target().lineHeight(prose.font);
  const int maxLines = proseHeight / lineHeight;
  prose.maxLines = static_cast<uint8_t>(maxLines < 1 ? 1 : maxLines);
  const std::string drawn = toybox::fitLines(screen.target(), model.prose, box.width, prose.maxLines, prose);
  screen.target().text(box, drawn.c_str(), prose);

  // KEEP is the LAST row, exactly where DELETE NOTE sat on the menu. DELETE is
  // the row above it, where nothing was.
  const fui::Rect keepRow = menuRowRect(device, kMenuRows - 1);
  const fui::Rect deleteRow = menuRowRect(device, kMenuRows - 2);
  const int16_t buttonHeight = kFooterHeight;
  const int16_t keepTop = static_cast<int16_t>(keepRow.y + (keepRow.height - buttonHeight) / 2);
  const int16_t deleteTop = static_cast<int16_t>(deleteRow.y + (deleteRow.height - buttonHeight) / 2);
  footerButton(screen, fui::makeRect(deleteRow.x, deleteTop, deleteRow.width, buttonHeight), "DELETE IT", ActionDelete,
               true);
  footerButton(screen, fui::makeRect(keepRow.x, keepTop, keepRow.width, buttonHeight), "KEEP IT", ActionDismiss, false);
}

fui::Rect buildPhone(toybox::Screen& screen, const PhoneModel& model) {
  chrome(screen, model.title, nullptr, model.menuIcon);
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
  const int16_t room = static_cast<int16_t>(footerY - toybox::kGutter * 2 - top - lineHeight * 3);
  int16_t side = room < width ? room : width;
  if (side < 120) side = 120;
  const fui::Rect qr = fui::makeRect(static_cast<int16_t>((device.width - side) / 2), top, side, side);

  const fui::Rect url =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(qr.y + qr.height + toybox::kGutter), width, lineHeight);
  fittedLine(screen, url, model.readable, fui::TextAlign::Center, toybox::kTileFont);

  // The state line says whether anything has arrived. A screen whose whole
  // promise is "type over there and it appears here" has to answer "did it".
  const fui::Rect state =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(url.y + url.height + toybox::kGutter), width, lineHeight);
  fittedLine(screen, state, model.saved ? "SAVED FROM YOUR PHONE" : "WAITING", fui::TextAlign::Center,
             toybox::kTileFont);

  footerButton(screen, fui::makeRect(toybox::kMargin, footerY, width, kFooterHeight), "DONE", ActionDismiss, false);
  return qr;
}

void buildNotice(toybox::Screen& screen, const ConfirmModel& model) {
  chrome(screen, model.title, nullptr, nullptr);
  const fui::DeviceContext& device = screen.device();
  const fui::Rect top = menuRowRect(device, 0);
  const fui::Rect third = menuRowRect(device, 2);
  fui::TextStyle prose = plain(toybox::kBodyFont, fui::TextAlign::Left, 5);
  const int16_t proseHeight = static_cast<int16_t>(third.y + third.height - top.y);
  const int16_t lineHeight = screen.target().lineHeight(prose.font);
  const int maxLines = proseHeight / lineHeight;
  prose.maxLines = static_cast<uint8_t>(maxLines < 1 ? 1 : maxLines);
  const std::string drawn = toybox::fitLines(screen.target(), model.prose, top.width, prose.maxLines, prose);
  screen.target().text(fui::makeRect(top.x, top.y, top.width, proseHeight), drawn.c_str(), prose);

  const fui::Rect row = menuRowRect(device, kMenuRows - 1);
  footerButton(
      screen,
      fui::makeRect(row.x, static_cast<int16_t>(row.y + (row.height - kFooterHeight) / 2), row.width, kFooterHeight),
      "BACK", ActionDismiss, false);
}

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  chrome(screen, model.title, nullptr, model.menuIcon);
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  const fui::Rect band =
      fui::makeRect(toybox::kMargin, kBodyTop, width, static_cast<int16_t>(device.height - toybox::kMargin - kBodyTop));

  // Four rows, and the destructive one is last and outlined rather than filled.
  // The rows fill the band, so the sheet is a page rather than a stack of
  // controls with a slab under it.
  struct Row {
    const char* label;
    const char* note;
    fui::ActionId action;
    bool enabled;
  };
  const Row rows[] = {
      // ALWAYS enabled, even with no Wi-Fi: tapping it is what OFFERS to join
      // one. A row disabled with "join Wi-Fi first" would send a person to
      // Settings to do by hand the job this row is holding the tools for.
      {"TYPE ON YOUR PHONE", model.phoneHint, ActionUsePhone, true},
      {"CLEAR DONE", model.anyDone ? nullptr : "nothing is ticked", ActionClearDone, model.anyDone},
      {"RENAME", nullptr, ActionRename, true},
      {"DELETE NOTE", nullptr, ActionDelete, true},
  };
  const int count = static_cast<int>(sizeof(rows) / sizeof(rows[0]));
  static_assert(sizeof(rows) / sizeof(rows[0]) == kMenuRows,
                "menuRowRect divides the band by kMenuRows; the confirm's KEEP lands on the last row of THIS table, "
                "so a row added here without changing kMenuRows would put KEEP somewhere DELETE NOTE never was");
  const int16_t rowHeight = static_cast<int16_t>(band.height / count);

  fui::TextStyle label = plain(toybox::kBodyFont);
  fui::TextStyle note = plain(toybox::kTileFont);
  for (int i = 0; i < count; i++) {
    const fui::Rect row = fui::makeRect(band.x, static_cast<int16_t>(band.y + i * rowHeight), band.width, rowHeight);
    const int16_t labelHeight = screen.target().lineHeight(label.font);
    const int16_t noteHeight = rows[i].note != nullptr ? screen.target().lineHeight(note.font) : 0;
    const int16_t top = static_cast<int16_t>(row.y + (row.height - labelHeight - noteHeight) / 2);
    const fui::Rect labelBox = fui::makeRect(static_cast<int16_t>(row.x + toybox::kGutter), top,
                                             static_cast<int16_t>(row.width - toybox::kGutter * 2), labelHeight);
    fui::TextStyle drawStyle = label;
    const std::string drawn = toybox::fittedTitle(screen.target(), rows[i].label, labelBox.width, drawStyle);
    screen.target().text(labelBox, drawn.c_str(), drawStyle);

    // A disabled row says WHY under its label instead of vanishing. A control
    // that appears and disappears teaches nobody where it lives, and the two
    // that can be unavailable here are unavailable for reasons a sentence
    // fixes.
    if (rows[i].note != nullptr) {
      const fui::Rect noteBox =
          fui::makeRect(labelBox.x, static_cast<int16_t>(top + labelHeight), labelBox.width, noteHeight);
      screen.target().text(noteBox, rows[i].note, note);
    }
    if (rows[i].enabled) rowHit(screen, row, rows[i].action, i);
    if (i + 1 < count) separator(screen, row);
  }
}

}  // namespace notesui
