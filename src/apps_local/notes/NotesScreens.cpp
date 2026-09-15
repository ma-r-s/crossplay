#include "NotesScreens.h"

#include <string>

#include "../ui/ToyboxText.h"

namespace notesui {
namespace {

constexpr int kBodyTop = toybox::kBodyTop;
constexpr int kFooterHeight = toybox::kPillHeight;
constexpr int kBoxSide = 40;      // the tick box; 40 + its padding clears a finger
constexpr int kPillGap = toybox::kGutter;

int16_t pageWidth(const fui::DeviceContext& device) {
  return static_cast<int16_t>(device.width - 2 * toybox::kMargin);
}

// Header band, rule, page margin. rightLabel is drawn in PAPER: the band is
// solid black and the header component styles a right label with subtitleText,
// whose default colour is Black, so a label left at the default is painted
// black on black and simply is not there. Study, Hacker News and Instapaper
// each paid for this once; it is copied rather than rediscovered.
void chrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  header.borderEdges = fui::EdgesNone;
  if (rightLabel != nullptr) {
    header.subtitleText = screen.theme().smallText;
    header.subtitleText.color = fui::Color::White;
    header.subtitleText.align = fui::TextAlign::Right;
  }
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kBodyGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

fui::TextStyle plain(const fui::FontId font, const fui::TextAlign align = fui::TextAlign::Left,
                     const uint8_t maxLines = 1) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = fui::Color::Black;
  style.maxLines = maxLines;
  return style;
}

// A line of text that must not overflow its box, set at the largest cut that
// holds it. Toybox's rule is that nothing is elided, and the cuts above
// toybox_10 carry no ellipsis glyph at all -- an overflow there draws as a
// sentence that stops at a plausible place and a screenshot looks fine. So
// every variable string on these screens goes through here.
void fittedLine(toybox::Screen& screen, const fui::Rect& box, const char* text, fui::TextAlign align,
                fui::FontId font) {
  fui::TextStyle style = plain(font, align);
  const std::string drawn = toybox::fittedTitle(screen.target(), text, box.width, style);
  // The line box is what gets centred, not the ink, and Screen clamps the
  // offset at zero -- so a box shorter than the chosen cut's line box drops the
  // text's foot out of the bottom. Give it the whole row and let it centre.
  screen.target().text(box, drawn.c_str(), style);
}

// The tick box: an outlined square, filled with a smaller solid square when
// done. Pure black on pure white in the one small rect that changes, which is
// the fastest and least ghost-prone update this panel can perform. No tick
// glyph: the Toybox face is ASCII-only and a check mark is not in it.
void tickBox(toybox::Screen& screen, const fui::Rect& box, const bool checked) {
  screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kRule, 4);
  if (!checked) return;
  const int16_t inset = 10;
  screen.target().fill(fui::makeRect(static_cast<int16_t>(box.x + inset), static_cast<int16_t>(box.y + inset),
                                     static_cast<int16_t>(box.width - 2 * inset),
                                     static_cast<int16_t>(box.height - 2 * inset)),
                       fui::Paint::solid(fui::Color::Black), 2);
}

// A done line is struck through, not greyed: grey is a dither on this panel and
// a dithered flat field ghosts, while a rule is one crisp row of pixels. The
// strike is measured against the DRAWN string, so a line that had to shrink to
// fit gets a strike the length of what is actually on the glass.
void strike(toybox::Screen& screen, const fui::Rect& box, const char* drawn, const fui::TextStyle& style) {
  const int16_t width = screen.target().measureText(style.font, drawn, style).width;
  const int16_t y = static_cast<int16_t>(box.y + box.height / 2);
  screen.target().line(fui::Point{box.x, y}, fui::Point{static_cast<int16_t>(box.x + width), y},
                       toybox::kHairline * 2, fui::Paint::solid(fui::Color::Black));
}

// One task row, drawn by hand because a tick box is not a ListItem: the
// component's toggle slot draws a switch, which means "a setting is on", not
// "you have bought this".
void taskRow(toybox::Screen& screen, const fui::Rect& row, const Task& task, const int index, const bool withBox,
             const bool withBar) {
  int16_t textX = row.x;
  if (withBox && task.isTask) {
    const fui::Rect box = fui::makeRect(row.x, static_cast<int16_t>(row.y + (row.height - kBoxSide) / 2), kBoxSide,
                                        kBoxSide);
    tickBox(screen, box, task.checked);
    textX = static_cast<int16_t>(row.x + kBoxSide + toybox::kGutter);
  }
  if (withBar) {
    // The margin bar: present for every task so the column is straight, solid
    // only when done. An indicator that appears and disappears moves the text
    // beside it; one that changes weight does not.
    const fui::Rect bar = fui::makeRect(row.x, static_cast<int16_t>(row.y + 6), 6,
                                        static_cast<int16_t>(row.height - 12));
    if (!task.isTask) {
      // Prose gets no bar at all, and no indent either: it is not a thing to do.
    } else if (task.checked) {
      screen.target().fill(bar, fui::Paint::solid(fui::Color::Black), 3);
    } else {
      screen.target().stroke(bar, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 3);
    }
    if (task.isTask) textX = static_cast<int16_t>(row.x + 6 + toybox::kGutter);
  }

  const fui::Rect textBox =
      fui::makeRect(textX, row.y, static_cast<int16_t>(row.x + row.width - textX), row.height);
  fui::TextStyle style = plain(task.isTask ? toybox::kBodyFont : toybox::kTileFont);
  const std::string drawn = toybox::fittedTitle(screen.target(), task.text, textBox.width, style);
  screen.target().text(textBox, drawn.c_str(), style);
  if (task.checked) strike(screen, textBox, drawn.c_str(), style);

  // The whole row is the target, not the box: a 40px square is a miss waiting
  // to happen and a miss costs two refreshes, the wrong one and the undo.
  if (task.isTask) {
    fui::ButtonProps hit;
    hit.label = "";
    hit.action = ActionToggleTask;
    hit.value = static_cast<int16_t>(index);
    hit.styles = toybox::rowStyles();
    hit.styles.normal.background = fui::Paint::none();
    hit.styles.normal.border = fui::Paint::none();
    screen.button(hit, row);
  }
}

// The OFTEN strip: pills of things this list has held before. Each is sized to
// its own word, because these are not peers being compared -- unlike a grid of
// Connections tiles, a row of shopping items has no meaning in their relative
// size. A pill that will not fit the row is simply not drawn; the add screen
// holds the whole list.
void oftenStrip(toybox::Screen& screen, const fui::Rect& strip, const char* const* items, const int count) {
  fui::TextStyle label = plain(toybox::kTileFont, fui::TextAlign::Center);
  screen.target().line(fui::Point{0, strip.y}, fui::Point{static_cast<int16_t>(strip.x + strip.width), strip.y},
                       toybox::kRule, fui::Paint::solid(fui::Color::Black));

  const fui::Rect tag = fui::makeRect(strip.x, static_cast<int16_t>(strip.y + toybox::kGutter / 2), 90,
                                      screen.target().lineHeight(toybox::kTileFont));
  fui::TextStyle tagStyle = plain(toybox::kTileFont);
  screen.target().text(tag, "OFTEN", tagStyle);

  const int16_t pillTop = static_cast<int16_t>(tag.y + tag.height + toybox::kGutter / 2);
  const int16_t pillHeight = static_cast<int16_t>(screen.target().lineHeight(toybox::kBodyFont) + toybox::kGutter);
  int16_t x = strip.x;
  for (int i = 0; i < count; i++) {
    const int16_t textWidth = screen.target().measureText(label.font, items[i], label).width;
    const int16_t pillWidth = static_cast<int16_t>(textWidth + toybox::kMargin * 2);
    if (x + pillWidth > strip.x + strip.width) break;
    fui::ButtonProps pill;
    pill.label = items[i];
    pill.action = ActionAddOften;
    pill.value = static_cast<int16_t>(i);
    pill.styles = toybox::rowStyles();
    screen.button(pill, fui::makeRect(x, pillTop, pillWidth, pillHeight));
    x = static_cast<int16_t>(x + pillWidth + kPillGap);
  }
}

void footerButton(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId action,
                  const bool outlined) {
  fui::ButtonProps button;
  button.label = label;
  button.action = action;
  if (outlined) button.styles = toybox::rowStyles();
  screen.button(button, box);
}

}  // namespace

// --- The deck ------------------------------------------------------------

namespace {

int16_t deckRowHeight(const fui::DrawTarget& target) {
  return static_cast<int16_t>(target.lineHeight(toybox::kBodyFont) + toybox::kGutter * 2);
}

void deckEmpty(toybox::Screen& screen, const fui::Rect& band) {
  // The first thing a stranger sees. It says what a note is here and how one
  // arrives, in the two sentences the panel can hold, and it never mentions a
  // phone: the device alone is the whole product.
  fui::TextStyle style = plain(toybox::kBodyFont, fui::TextAlign::Center, 4);
  const fui::Rect box = fui::makeRect(band.x, static_cast<int16_t>(band.y + band.height / 4), band.width,
                                      static_cast<int16_t>(screen.target().lineHeight(toybox::kBodyFont) * 4));
  const std::string drawn =
      toybox::fitLines(screen.target(), "Nothing here yet. A note is a list you tick, or a page you keep.",
                       box.width, 4, style);
  screen.target().text(box, drawn.c_str(), style);
}

}  // namespace

void buildDeckList(toybox::Screen& screen, const DeckModel& model) {
  chrome(screen, "NOTES");
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);
  const fui::Rect band = fui::makeRect(toybox::kMargin, kBodyTop, width,
                                       static_cast<int16_t>(footerY - toybox::kGutter - kBodyTop));

  // Taken first so the list can never grow into it, and drawn on the empty
  // deck too: an empty deck is exactly when someone needs the way to start.
  footerButton(screen, fui::makeRect(toybox::kMargin, footerY, width, kFooterHeight), "NEW NOTE", ActionNewNote,
               false);

  if (model.count == 0) {
    deckEmpty(screen, band);
    return;
  }

  fui::ListProps list;
  list.items = model.items;
  list.count = static_cast<uint16_t>(model.count);
  list.topIndex = static_cast<uint16_t>(model.topIndex);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionOpenNote;
  list.rowHeight = deckRowHeight(screen.target());
  list.labelText = screen.theme().bodyText;
  list.labelText.maxLines = 1;
  list.valueText = screen.theme().smallText;
  list.valueText.font = toybox::kTileFont;
  list.valueText.align = fui::TextAlign::Right;
  // The title is the content and the tally is a footnote, so the title is not
  // capped at 60% of the row to sit prettily beside it.
  list.balanceWrappedLabelWithValue = false;
  screen.list(list, band.height, fui::LayoutAnchor::Top);
}

void buildDeckTally(toybox::Screen& screen, const DeckModel& model) {
  chrome(screen, "NOTES");
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  const fui::Rect band = fui::makeRect(toybox::kMargin, kBodyTop, width,
                                       static_cast<int16_t>(device.height - toybox::kMargin - kBodyTop));

  const int16_t rowHeight = deckRowHeight(screen.target());
  int16_t y = band.y;
  for (int i = 0; i < model.count; i++) {
    if (y + rowHeight > band.y + band.height - rowHeight) break;  // keep room for the last row
    const fui::Rect row = fui::makeRect(band.x, y, band.width, rowHeight);

    // The tally sits in its own reserved gutter whether or not this note has
    // one, so a prose note and a list note keep the same left edge and the
    // column of titles does not jog.
    const int16_t tallyWidth = 86;
    const fui::Rect titleBox =
        fui::makeRect(row.x, row.y, static_cast<int16_t>(row.width - tallyWidth - toybox::kGutter), row.height);
    fittedLine(screen, titleBox, model.items[i].label, fui::TextAlign::Left, toybox::kBodyFont);
    if (model.items[i].value != nullptr) {
      const fui::Rect tally = fui::makeRect(static_cast<int16_t>(row.x + row.width - tallyWidth), row.y, tallyWidth,
                                            row.height);
      fittedLine(screen, tally, model.items[i].value, fui::TextAlign::Right, toybox::kTileFont);
    }

    fui::ButtonProps hit;
    hit.label = "";
    hit.action = ActionOpenNote;
    hit.value = static_cast<int16_t>(i);
    hit.styles = toybox::rowStyles();
    hit.styles.normal.background = fui::Paint::none();
    hit.styles.normal.border = fui::Paint::none();
    screen.button(hit, row);

    screen.target().line(fui::Point{row.x, static_cast<int16_t>(row.y + row.height)},
                         fui::Point{static_cast<int16_t>(row.x + row.width),
                                        static_cast<int16_t>(row.y + row.height)},
                         toybox::kHairline, fui::Paint::solid(fui::Color::Black));
    y = static_cast<int16_t>(y + rowHeight);
  }

  if (model.count == 0) deckEmpty(screen, band);

  // NEW NOTE is the last row rather than a footer bar, so the page is one
  // column of things and there is no dead bar at the bottom of a short deck.
  // Dead space at the bottom is a real defect on a screen that holds still.
  const fui::Rect newRow = fui::makeRect(band.x, static_cast<int16_t>(band.y + band.height - kFooterHeight),
                                         band.width, kFooterHeight);
  footerButton(screen, newRow, "+  NEW NOTE", ActionNewNote, true);
}

void buildDeckCards(toybox::Screen& screen, const DeckModel& model) {
  chrome(screen, "NOTES");
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);
  const fui::Rect band = fui::makeRect(toybox::kMargin, kBodyTop, width,
                                       static_cast<int16_t>(footerY - toybox::kGutter - kBodyTop));

  const int16_t cardWidth = static_cast<int16_t>((band.width - toybox::kGutter) / 2);
  const int16_t cardHeight = 140;
  const int rows = 3;
  for (int i = 0; i < model.count && i < rows * 2; i++) {
    const int16_t cx = static_cast<int16_t>(band.x + (i % 2) * (cardWidth + toybox::kGutter));
    const int16_t cy = static_cast<int16_t>(band.y + (i / 2) * (cardHeight + toybox::kGutter));
    const fui::Rect card = fui::makeRect(cx, cy, cardWidth, cardHeight);
    screen.target().stroke(card, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 6);

    // The title gets three lines inside the card, wrapped rather than shrunk:
    // a card is a box of text and a long name is the normal case, not the
    // exception. The tally is reserved its own line at the foot whether or not
    // it is used, so two cards side by side agree on where their text ends.
    const int16_t pad = toybox::kGutter;
    fui::TextStyle title = plain(toybox::kTileFont, fui::TextAlign::Left, 3);
    const int16_t tallyLine = screen.target().lineHeight(toybox::kTileFont);
    const fui::Rect titleBox =
        fui::makeRect(static_cast<int16_t>(card.x + pad), static_cast<int16_t>(card.y + pad),
                      static_cast<int16_t>(card.width - 2 * pad),
                      static_cast<int16_t>(card.height - 2 * pad - tallyLine));
    const std::string drawn =
        toybox::fitLines(screen.target(), model.items[i].label, titleBox.width, 3, title);
    screen.target().text(titleBox, drawn.c_str(), title);

    if (model.items[i].value != nullptr) {
      const fui::Rect tally =
          fui::makeRect(static_cast<int16_t>(card.x + pad),
                        static_cast<int16_t>(card.y + card.height - pad - tallyLine),
                        static_cast<int16_t>(card.width - 2 * pad), tallyLine);
      fittedLine(screen, tally, model.items[i].value, fui::TextAlign::Left, toybox::kTileFont);
    }

    if (i == model.selected) screen.target().stroke(card, fui::Paint::solid(fui::Color::Black), toybox::kFrame, 6);

    fui::ButtonProps hit;
    hit.label = "";
    hit.action = ActionOpenNote;
    hit.value = static_cast<int16_t>(i);
    hit.styles = toybox::rowStyles();
    hit.styles.normal.background = fui::Paint::none();
    hit.styles.normal.border = fui::Paint::none();
    screen.button(hit, card);
  }

  if (model.count == 0) deckEmpty(screen, band);
  footerButton(screen, fui::makeRect(toybox::kMargin, footerY, width, kFooterHeight), "NEW NOTE", ActionNewNote,
               false);
}

// --- A note, open --------------------------------------------------------

int16_t noteRowHeight(const fui::DrawTarget& target) {
  // One line of body plus air, and never less than a finger: 64px is the floor
  // every control in this fork keeps.
  const int16_t line = static_cast<int16_t>(target.lineHeight(toybox::kBodyFont) + toybox::kGutter);
  return line < 64 ? 64 : line;
}

fui::Rect noteBand(const fui::DeviceContext& device, const bool withPills) {
  const int bottom = withPills ? toybox::kMargin + 120 : toybox::kMargin + kFooterHeight + toybox::kGutter;
  return fui::makeRect(toybox::kMargin, kBodyTop, static_cast<int16_t>(device.width - 2 * toybox::kMargin),
                       static_cast<int16_t>(device.height - bottom - kBodyTop));
}

namespace {

// The title goes on the band, and the band is a fixed height, so a long note
// name is fitted DOWN rather than allowed to push the rule. kHeaderHeight is
// load-bearing; a taller band would move every body on the device.
void noteChrome(toybox::Screen& screen, const NoteModel& model, const char* rightLabel) {
  chrome(screen, model.title, rightLabel);
}

void noteRows(toybox::Screen& screen, const NoteModel& model, const fui::Rect& band, const bool withBox,
              const bool withBar) {
  const int16_t rowHeight = noteRowHeight(screen.target());
  int16_t y = band.y;
  for (int i = model.firstVisible; i < model.count; i++) {
    if (y + rowHeight > band.y + band.height) break;
    taskRow(screen, fui::makeRect(band.x, y, band.width, rowHeight), model.tasks[i], i, withBox, withBar);
    y = static_cast<int16_t>(y + rowHeight);
  }
  if (model.count == 0) {
    fui::TextStyle style = plain(toybox::kBodyFont, fui::TextAlign::Center, 2);
    const fui::Rect box = fui::makeRect(band.x, static_cast<int16_t>(band.y + band.height / 3), band.width,
                                        static_cast<int16_t>(screen.target().lineHeight(toybox::kBodyFont) * 2));
    const std::string drawn =
        toybox::fitLines(screen.target(), "This note is empty. Add the first line.", box.width, 2, style);
    screen.target().text(box, drawn.c_str(), style);
  }
}

}  // namespace

void buildNoteBoxes(toybox::Screen& screen, const NoteModel& model) {
  noteChrome(screen, model, nullptr);
  const fui::DeviceContext& device = screen.device();
  const fui::Rect band = noteBand(device, true);
  noteRows(screen, model, band, true, false);

  const fui::Rect strip =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(band.y + band.height + toybox::kGutter), pageWidth(device),
                    static_cast<int16_t>(device.height - band.y - band.height - toybox::kGutter));
  if (model.oftenCount > 0) oftenStrip(screen, strip, model.often, model.oftenCount);
}

void buildNoteBars(toybox::Screen& screen, const NoteModel& model) {
  noteChrome(screen, model, nullptr);
  const fui::DeviceContext& device = screen.device();
  const fui::Rect band = noteBand(device, true);
  noteRows(screen, model, band, false, true);

  const fui::Rect strip =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(band.y + band.height + toybox::kGutter), pageWidth(device),
                    static_cast<int16_t>(device.height - band.y - band.height - toybox::kGutter));
  if (model.oftenCount > 0) oftenStrip(screen, strip, model.often, model.oftenCount);
}

void buildNoteQuiet(toybox::Screen& screen, const NoteModel& model) {
  noteChrome(screen, model, nullptr);
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);
  noteRows(screen, model, noteBand(device, false), true, false);

  // ADD keeps the left edge, which is the fork-wide home for a primary action
  // and the pixel a thumb learns. CLEAR DONE appears only when there is
  // something done to clear, and it appears on the RIGHT, so the control that
  // removes lines never occupies the pixels ADD had a moment ago.
  if (model.anyDone) {
    const int16_t half = static_cast<int16_t>((width - toybox::kGutter) / 2);
    footerButton(screen, fui::makeRect(toybox::kMargin, footerY, half, kFooterHeight), "ADD", ActionAdd, false);
    footerButton(screen,
                 fui::makeRect(static_cast<int16_t>(toybox::kMargin + half + toybox::kGutter), footerY, half,
                               kFooterHeight),
                 "CLEAR DONE", ActionClearDone, true);
  } else {
    footerButton(screen, fui::makeRect(toybox::kMargin, footerY, width, kFooterHeight), "ADD", ActionAdd, false);
  }
}

// --- Adding --------------------------------------------------------------

fui::Rect addQrBox(const fui::DeviceContext& device) {
  const int16_t side = 200;
  return fui::makeRect(static_cast<int16_t>((device.width - side) / 2),
                       static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight - toybox::kGutter - side),
                       side, side);
}

void buildAddPills(toybox::Screen& screen, const AddModel& model) {
  chrome(screen, "ADD", model.noteTitle);
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);

  // Two columns of equal width. These pills ARE peers -- the eye runs down them
  // as one set -- so unlike the OFTEN strip they take one shared cut, chosen
  // from the widest of them, and a long item shrinks the whole grid rather than
  // being the odd one out.
  const int16_t colWidth = static_cast<int16_t>((width - toybox::kGutter) / 2);
  fui::TextStyle label = plain(toybox::kBodyFont, fui::TextAlign::Center);
  for (int i = 0; i < model.oftenCount; i++) {
    fui::TextStyle probe = label;
    toybox::fittedTitle(screen.target(), model.often[i], static_cast<int16_t>(colWidth - toybox::kMargin), probe);
    if (screen.target().lineHeight(probe.font) < screen.target().lineHeight(label.font)) label.font = probe.font;
  }

  const int16_t pillHeight = static_cast<int16_t>(screen.target().lineHeight(toybox::kBodyFont) + toybox::kGutter * 2);
  int16_t y = kBodyTop;
  for (int i = 0; i < model.oftenCount; i++) {
    if (y + pillHeight > footerY - toybox::kGutter) break;
    const int16_t x = static_cast<int16_t>(toybox::kMargin + (i % 2) * (colWidth + toybox::kGutter));
    fui::ButtonProps pill;
    pill.label = model.often[i];
    pill.action = ActionAddOften;
    pill.value = static_cast<int16_t>(i);
    pill.styles = toybox::rowStyles();
    pill.text = label;
    screen.button(pill, fui::makeRect(x, y, colWidth, pillHeight));
    if (i % 2 == 1) y = static_cast<int16_t>(y + pillHeight + toybox::kGutter);
  }

  const int16_t half = static_cast<int16_t>((width - toybox::kGutter) / 2);
  footerButton(screen, fui::makeRect(toybox::kMargin, footerY, half, kFooterHeight), "TYPE IT", ActionTypeHere,
               false);
  footerButton(screen,
               fui::makeRect(static_cast<int16_t>(toybox::kMargin + half + toybox::kGutter), footerY, half,
                             kFooterHeight),
               "USE PHONE", ActionUsePhone, true);
}

void buildAddList(toybox::Screen& screen, const AddModel& model) {
  chrome(screen, "ADD", model.noteTitle);
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);

  // A row holds a long item that a pill cannot, at the price of half the
  // density. TYPE IT is the first row rather than a footer button, so the two
  // ways of adding are one column of choices instead of two grammars.
  const int16_t rowHeight = noteRowHeight(screen.target());
  int16_t y = kBodyTop;

  fui::ButtonProps type;
  type.label = "TYPE IT";
  type.action = ActionTypeHere;
  screen.button(type, fui::makeRect(toybox::kMargin, y, width, rowHeight));
  y = static_cast<int16_t>(y + rowHeight + toybox::kGutter);

  for (int i = 0; i < model.oftenCount; i++) {
    if (y + rowHeight > footerY - toybox::kGutter) break;
    const fui::Rect row = fui::makeRect(toybox::kMargin, y, width, rowHeight);
    const fui::Rect textBox = fui::makeRect(static_cast<int16_t>(row.x + toybox::kGutter), row.y,
                                            static_cast<int16_t>(row.width - toybox::kGutter * 2), row.height);
    fittedLine(screen, textBox, model.often[i], fui::TextAlign::Left, toybox::kBodyFont);
    fui::ButtonProps hit;
    hit.label = "";
    hit.action = ActionAddOften;
    hit.value = static_cast<int16_t>(i);
    hit.styles = toybox::rowStyles();
    hit.styles.normal.background = fui::Paint::none();
    hit.styles.normal.border = fui::Paint::none();
    screen.button(hit, row);
    screen.target().line(fui::Point{row.x, static_cast<int16_t>(row.y + row.height)},
                         fui::Point{static_cast<int16_t>(row.x + row.width),
                                        static_cast<int16_t>(row.y + row.height)},
                         toybox::kHairline, fui::Paint::solid(fui::Color::Black));
    y = static_cast<int16_t>(y + rowHeight);
  }

  footerButton(screen, fui::makeRect(toybox::kMargin, footerY, width, kFooterHeight), "USE PHONE", ActionUsePhone,
               true);
}

void buildAddSplit(toybox::Screen& screen, const AddModel& model) {
  chrome(screen, "ADD", model.noteTitle);
  const fui::DeviceContext& device = screen.device();
  const int16_t width = pageWidth(device);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);
  const fui::Rect qr = addQrBox(device);

  const int16_t pillHeight = static_cast<int16_t>(screen.target().lineHeight(toybox::kBodyFont) + toybox::kGutter);
  fui::TextStyle label = plain(toybox::kTileFont, fui::TextAlign::Center);
  int16_t x = toybox::kMargin;
  int16_t y = kBodyTop;
  for (int i = 0; i < model.oftenCount; i++) {
    const int16_t textWidth = screen.target().measureText(label.font, model.often[i], label).width;
    const int16_t pillWidth = static_cast<int16_t>(textWidth + toybox::kMargin * 2);
    if (x + pillWidth > toybox::kMargin + width) {
      x = toybox::kMargin;
      y = static_cast<int16_t>(y + pillHeight + kPillGap);
    }
    if (y + pillHeight > qr.y - toybox::kGutter * 3) break;
    fui::ButtonProps pill;
    pill.label = model.often[i];
    pill.action = ActionAddOften;
    pill.value = static_cast<int16_t>(i);
    pill.styles = toybox::rowStyles();
    pill.text = label;
    screen.button(pill, fui::makeRect(x, y, pillWidth, pillHeight));
    x = static_cast<int16_t>(x + pillWidth + kPillGap);
  }

  // The divider says the bottom half is a different route, not more of the
  // same list.
  const int16_t ruleY = static_cast<int16_t>(qr.y - toybox::kGutter * 2 - toybox::kRule);
  screen.target().fill(fui::makeRect(toybox::kMargin, ruleY, width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  const fui::Rect caption =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(ruleY + toybox::kGutter), width,
                    static_cast<int16_t>(screen.target().lineHeight(toybox::kTileFont)));
  if (model.phoneUrl != nullptr) {
    fittedLine(screen, caption, "OR TYPE ON YOUR PHONE", fui::TextAlign::Center, toybox::kTileFont);
    screen.target().stroke(qr, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 0);
    const fui::Rect url = fui::makeRect(toybox::kMargin, static_cast<int16_t>(qr.y + qr.height + toybox::kGutter / 2),
                                        width, static_cast<int16_t>(screen.target().lineHeight(toybox::kTileFont)));
    fittedLine(screen, url, model.phoneUrl, fui::TextAlign::Center, toybox::kTileFont);
  } else {
    // No Wi-Fi: say so where the code would have been, rather than drawing a
    // code that cannot be reached. A screen whose promise is "point your camera
    // at this" has no worse failure than a square that does nothing.
    fui::TextStyle style = plain(toybox::kTileFont, fui::TextAlign::Center, 3);
    const fui::Rect box = fui::makeRect(toybox::kMargin, static_cast<int16_t>(ruleY + toybox::kGutter * 2), width,
                                        static_cast<int16_t>(screen.target().lineHeight(toybox::kTileFont) * 3));
    const std::string drawn = toybox::fitLines(
        screen.target(), "Join Wi-Fi to type on your phone. You do not need one to use this note.", box.width, 3,
        style);
    screen.target().text(box, drawn.c_str(), style);
  }

  footerButton(screen, fui::makeRect(toybox::kMargin, footerY, width, kFooterHeight), "TYPE IT HERE", ActionTypeHere,
               false);
}

}  // namespace notesui
