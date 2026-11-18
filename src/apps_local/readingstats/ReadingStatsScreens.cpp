#include "ReadingStatsScreens.h"

#include <cstdio>

#include "ReadingStatsTitles.h"

namespace rstatsui {
namespace {

constexpr int16_t kTitleBoxHeight = 58;  // two lines of ubuntu 12, three of ubuntu 8
constexpr int16_t kBookRowHeight = kTitleBoxHeight + 10;
constexpr int16_t kDateWidth = 64;
constexpr int16_t kDateGap = 10;
constexpr int16_t kSectionLabelHeight = 30;
constexpr int16_t kDoorHeight = toybox::kPillHeight;

const fui::Paint kInk = fui::Paint::solid(fui::Color::Black);

fui::TextStyle styleOf(const fui::FontId font, const fui::TextAlign align, const fui::Color color = fui::Color::Black) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = color;
  return style;
}

void chrome(toybox::Screen& screen, const char* title, const char* rightLabel) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

void hairline(toybox::Screen& screen, const int16_t x, const int16_t y, const int16_t width) {
  screen.target().fill(fui::makeRect(x, y, width, toybox::kHairline), kInk);
}

// The same corner brackets the chess board, the Connections grid and Study's
// timeline wear: two screens that share a bracket read as one device.
void brackets(toybox::Screen& screen, const fui::Rect& box, const int arm) {
  const int w = toybox::kFrame;
  for (int cx = 0; cx < 2; ++cx) {
    for (int cy = 0; cy < 2; ++cy) {
      const int x = cx == 0 ? box.x : box.right() - arm;
      const int y = cy == 0 ? box.y : box.bottom() - w;
      screen.target().fill(fui::makeRect(x, y, arm, w), kInk);
      const int vx = cx == 0 ? box.x : box.right() - w;
      const int vy = cy == 0 ? box.y : box.bottom() - arm;
      screen.target().fill(fui::makeRect(vx, vy, w, arm), kInk);
    }
  }
}

uint32_t minutesOf(const uint32_t seconds) { return seconds / 60; }

// A bar's number: minutes up to three digits, whole hours past that, so it
// never runs wider than its column.
void barLabel(const uint32_t seconds, char* out, const size_t size) {
  const uint32_t minutes = minutesOf(seconds);
  if (minutes < 1000) {
    std::snprintf(out, size, "%u", static_cast<unsigned>(minutes));
  } else {
    std::snprintf(out, size, "%uH", static_cast<unsigned>(minutes / 60));
  }
}

void durationText(const uint32_t seconds, char* out, const size_t size) {
  const uint32_t minutes = minutesOf(seconds);
  if (minutes < 60) {
    std::snprintf(out, size, "%u MIN", static_cast<unsigned>(minutes));
  } else if (minutes % 60 == 0) {
    std::snprintf(out, size, "%u HR", static_cast<unsigned>(minutes / 60));
  } else {
    std::snprintf(out, size, "%u HR %u MIN", static_cast<unsigned>(minutes / 60), static_cast<unsigned>(minutes % 60));
  }
}

void streakText(const MainModel& model, char* out, const size_t size) {
  if (model.streak <= 0) {
    std::snprintf(out, size, "NO STREAK");
  } else if (model.streakAtCap) {
    std::snprintf(out, size, "%d+ DAY STREAK", model.streak);
  } else {
    std::snprintf(out, size, "%d DAY STREAK", model.streak);
  }
}

// --- The chart ------------------------------------------------------------

// Two weeks of minutes, one column a day, oldest on the left and today on the
// right. Every column with reading in it carries its number; the day letters
// sit under the ground line, and today's is knocked out of a black square so
// the right-hand edge reads as "now" without a legend.
void barChart(toybox::Screen& screen, const fui::Rect& box, const MainModel& model) {
  const int labelH = toybox::kTileCut.lineHeight;
  const int letterH = 26;
  const int gap = 6;
  const int columns = kChartCells;
  const int barWidth = (box.width - gap * (columns - 1)) / columns;
  const int used = barWidth * columns + gap * (columns - 1);
  const int x0 = box.x + (box.width - used) / 2;
  const int baseline = box.bottom() - letterH - 4;
  const int ceiling = baseline - (box.y + labelH + 2);

  uint32_t peak = 60;
  for (const Day& day : model.chart) {
    if (day.seconds > peak) peak = day.seconds;
  }

  const fui::TextStyle number = styleOf(toybox::kTileFont, fui::TextAlign::Center);
  for (int i = 0; i < columns; ++i) {
    const Day& day = model.chart[i];
    const int x = x0 + i * (barWidth + gap);
    if (minutesOf(day.seconds) > 0) {
      int height = static_cast<int>((static_cast<uint64_t>(day.seconds) * ceiling) / peak);
      if (height < 3) height = 3;
      const fui::Rect bar = fui::makeRect(x, baseline - height, barWidth, height);
      const bool dim = !day.thisWeek;
      screen.target().fill(bar, dim ? fui::Paint::dither(fui::Color::DarkGray) : kInk);
      if (dim) {
        // A dithered column with no edge reads as fog; the outline makes it a bar.
        screen.target().stroke(bar, kInk, toybox::kHairline);
      }
      char label[8];
      barLabel(day.seconds, label, sizeof(label));
      screen.target().text(fui::makeRect(x - gap / 2, bar.y - labelH - 2, barWidth + gap, labelH), label, number);
    }
    const fui::Rect letterBox = fui::makeRect(x, baseline + 4, barWidth, letterH);
    char letter[2] = {day.letter, '\0'};
    if (day.today) {
      screen.target().fill(letterBox, kInk);
      screen.target().text(toybox::inkCentred(letterBox, toybox::kTileCut), letter,
                           styleOf(toybox::kTileFont, fui::TextAlign::Center, fui::Color::White));
    } else {
      screen.target().text(toybox::inkCentred(letterBox, toybox::kTileCut), letter, number);
    }
  }
  hairline(screen, static_cast<int16_t>(x0), static_cast<int16_t>(baseline), static_cast<int16_t>(used));
}

// What the chart says when there is nothing to draw in it.
void chartNotice(toybox::Screen& screen, const fui::Rect& box, const char* text) {
  screen.target().text(
      toybox::inkCentred(fui::makeRect(box.x, box.y + box.height / 2 - 12, box.width, 24), toybox::kTileCut), text,
      styleOf(toybox::kTileFont, fui::TextAlign::Center));
}

// --- The books ------------------------------------------------------------

fui::Rect titleBoxIn(const fui::Rect& row) {
  return fui::makeRect(static_cast<int16_t>(row.x + kDateWidth + kDateGap),
                       static_cast<int16_t>(row.y + (row.height - kTitleBoxHeight) / 2),
                       static_cast<int16_t>(row.width - kDateWidth - kDateGap), kTitleBoxHeight);
}

void bookRow(toybox::Screen& screen, const fui::Rect& row, const BookRow& book, Titles& titles, const bool rule) {
  screen.target().text(toybox::inkCentred(fui::makeRect(row.x, row.y, kDateWidth, row.height), toybox::kTileCut),
                       book.date != nullptr ? book.date : "", styleOf(toybox::kTileFont, fui::TextAlign::Left));
  if (titles.count < kMaxTitleSlots) {
    titles.slots[titles.count].box = titleBoxIn(row);
    titles.slots[titles.count].title = book.title;
    ++titles.count;
  }
  if (rule) hairline(screen, row.x, static_cast<int16_t>(row.bottom() - 1), row.width);
}

// "FINISHED IN 2026" over the rows, the count at the right. Rows run down from
// `top` to the body's foot; if the year holds more books than that, the last
// row becomes a door to all of them.
void booksSection(toybox::Screen& screen, const int16_t top, const MainModel& model, Titles& titles) {
  const fui::Rect body = screen.body();
  char label[32];
  std::snprintf(label, sizeof(label), "FINISHED IN %d", model.year);
  const fui::Rect labelRow = fui::makeRect(body.x, top, body.width, kSectionLabelHeight);
  screen.target().text(toybox::inkCentred(labelRow, toybox::kTileCut), label,
                       styleOf(toybox::kTileFont, fui::TextAlign::Left));
  if (model.bookCount > 0) {
    char count[24];
    std::snprintf(count, sizeof(count), "%d BOOK%s", model.bookCount, model.bookCount == 1 ? "" : "S");
    screen.target().text(toybox::inkCentred(labelRow, toybox::kTileCut), count,
                         styleOf(toybox::kTileFont, fui::TextAlign::Right));
  }
  hairline(screen, body.x, static_cast<int16_t>(labelRow.bottom()), body.width);

  int16_t y = static_cast<int16_t>(labelRow.bottom() + 1);
  if (model.bookCount <= 0) {
    screen.target().text(toybox::inkCentred(fui::makeRect(body.x, y, body.width, 40), toybox::kTileCut), "NONE YET",
                         styleOf(toybox::kTileFont, fui::TextAlign::Left));
    return;
  }

  const int room = body.bottom() - y;
  int rows = room / kBookRowHeight;
  bool door = false;
  if (model.bookCount > rows) {
    door = true;
    rows = (room - kDoorHeight - toybox::kGutter) / kBookRowHeight;
    if (rows < 0) rows = 0;
  }
  for (int i = 0; i < rows && i < model.bookCount; ++i) {
    const fui::Rect row = fui::makeRect(body.x, y, body.width, kBookRowHeight);
    // A rule between rows, and above the door; none under the last book.
    bookRow(screen, row, model.books[i], titles, (i + 1 < rows && i + 1 < model.bookCount) || door);
    y = static_cast<int16_t>(y + kBookRowHeight);
  }
  if (door) {
    char doorLabel[32];
    std::snprintf(doorLabel, sizeof(doorLabel), "ALL %d BOOKS", model.bookCount);
    fui::ButtonProps button;
    button.label = doorLabel;
    button.action = ActionAllBooks;
    button.styles = toybox::rowStyles();
    screen.button(button,
                  fui::makeRect(body.x, static_cast<int16_t>(body.bottom() - kDoorHeight), body.width, kDoorHeight));
  }
}

// --- The top half ---------------------------------------------------------

// The front door in the Study and Connections shape: the week is the headline,
// the streak shares its label's line, a rule, then the fortnight as the
// bracketed ornament -- this week's columns solid and last week's dithered, so
// the headline points at the bars it adds up -- then the year's books.
//
// Chosen over two other layouts rendered beside it (two outlined tiles over a
// full-width chart; a two-week calendar of filled days with week totals). The
// tiles' solid-and-dithered bars were the one element worth keeping, and this
// carries them.
int16_t topHalf(toybox::Screen& screen, const MainModel& model) {
  const fui::Rect body = screen.body();
  char week[32];
  durationText(model.weekSeconds, week, sizeof(week));
  char streak[32];
  streakText(model, streak, sizeof(streak));
  screen.target().text(fui::makeRect(body.x, body.y, body.width, 60), week,
                       styleOf(toybox::kDisplayFont, fui::TextAlign::Left));
  const fui::Rect state = toybox::inkCentred(fui::makeRect(body.x, body.y + 62, body.width, 26), toybox::kUiCut);
  screen.target().text(state, "THIS WEEK", styleOf(toybox::kUiFont, fui::TextAlign::Left));
  screen.target().text(state, streak, styleOf(toybox::kUiFont, fui::TextAlign::Right));
  screen.target().fill(fui::makeRect(body.x, body.y + 104, body.width, toybox::kRule), kInk);

  const fui::Rect panel = fui::makeRect(body.x + 14, body.y + 134, body.width - 28, 176);
  brackets(screen, fui::makeRect(panel.x - 14, panel.y - 14, panel.width + 28, panel.height + 28), 28);
  if (model.anySession) {
    barChart(screen, panel, model);
  } else {
    chartNotice(screen, panel, "MINUTES READ SHOW UP HERE");
  }
  return static_cast<int16_t>(panel.bottom() + 30);
}

}  // namespace

void buildMain(toybox::Screen& screen, const MainModel& model, Titles& titles) {
  titles.count = 0;
  chrome(screen, "READING STATS", nullptr);
  const fui::Rect body = screen.body();
  if (!model.clockSet) {
    screen.target().text(fui::makeRect(body.x, body.y, body.width, 60), "NO DATE YET",
                         styleOf(toybox::kDisplayFont, fui::TextAlign::Left));
    screen.target().text(toybox::inkCentred(fui::makeRect(body.x, body.y + 62, body.width, 26), toybox::kUiCut),
                         "JOIN WI-FI TO SET IT", styleOf(toybox::kUiFont, fui::TextAlign::Left));
    screen.target().fill(fui::makeRect(body.x, body.y + 104, body.width, toybox::kRule), kInk);
    screen.target().text(toybox::inkCentred(fui::makeRect(body.x, body.y + 130, body.width, 24), toybox::kTileCut),
                         "READING IS COUNTED ONCE THE DATE IS SET", styleOf(toybox::kTileFont, fui::TextAlign::Left));
    return;
  }
  const int16_t booksTop = topHalf(screen, model);
  booksSection(screen, booksTop, model, titles);
}

int booksPerPage(const fui::DeviceContext& device) {
  const int top = toybox::kBodyTop + kSectionLabelHeight + 1;
  const int bottom = device.height - toybox::kMargin;
  const int rows = (bottom - top) / kBookRowHeight;
  return rows > 0 ? rows : 1;
}

void buildBooks(toybox::Screen& screen, const BooksModel& model, Titles& titles) {
  titles.count = 0;
  chrome(screen, "FINISHED", model.pageLabel);
  const fui::Rect body = screen.body();
  char label[32];
  std::snprintf(label, sizeof(label), "IN %d", model.year);
  const fui::Rect labelRow = fui::makeRect(body.x, body.y, body.width, kSectionLabelHeight);
  screen.target().text(toybox::inkCentred(labelRow, toybox::kTileCut), label,
                       styleOf(toybox::kTileFont, fui::TextAlign::Left));
  char count[24];
  std::snprintf(count, sizeof(count), "%d BOOK%s", model.count, model.count == 1 ? "" : "S");
  screen.target().text(toybox::inkCentred(labelRow, toybox::kTileCut), count,
                       styleOf(toybox::kTileFont, fui::TextAlign::Right));
  hairline(screen, body.x, static_cast<int16_t>(labelRow.bottom()), body.width);

  const int perPage = booksPerPage(screen.device());
  int16_t y = static_cast<int16_t>(labelRow.bottom() + 1);
  for (int i = 0; i < perPage && model.first + i < model.count; ++i) {
    const fui::Rect row = fui::makeRect(body.x, y, body.width, kBookRowHeight);
    const bool more = i + 1 < perPage && model.first + i + 1 < model.count;
    bookRow(screen, row, model.books[model.first + i], titles, more);
    y = static_cast<int16_t>(y + kBookRowHeight);
  }
}

void drawTitles(fui::DrawTarget& target, const Titles& titles) {
  for (int i = 0; i < titles.count; ++i) {
    const TitleSlot& slot = titles.slots[i];
    const TitleLayout layout = layOutTitle(target, slot.title, slot.box.width, slot.box.height);
    drawTitle(target, slot.box, layout);
  }
}

int16_t titleWidth(const fui::DeviceContext& device) {
  return static_cast<int16_t>(device.width - 2 * toybox::kMargin - kDateWidth - kDateGap);
}

int16_t titleHeight() { return kTitleBoxHeight; }

}  // namespace rstatsui
