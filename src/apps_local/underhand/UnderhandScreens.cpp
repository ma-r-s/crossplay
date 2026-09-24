#include "UnderhandScreens.h"

#include <cstdio>
#include <string>

#include "UnderhandIcons.h"

namespace underhandui {

namespace {

using underhand::kResources;

constexpr int16_t kMargin = 16;
constexpr int16_t kPad = 10;
constexpr int16_t kGap = 8;
constexpr int16_t kBarHeight = 56;
constexpr int16_t kStatusHeight = 30;
constexpr int16_t kTokenIcon = 24;
constexpr int16_t kBarIcon = 32;
constexpr int16_t kButtonHeight = 60;
constexpr int16_t kRowHeight = 36;  // a row of tokens, and a chip
constexpr int16_t kBetween = 12;    // between tokens
constexpr int16_t kChipPad = 8;
constexpr int16_t kOrPad = 6;

const freeink::Icon* const kIcon24[kResources] = {&icon_uh_relic_24, &icon_uh_money_24,    &icon_uh_cultist_24,
                                                  &icon_uh_food_24,  &icon_uh_prisoner_24, &icon_uh_suspicion_24};
const freeink::Icon* const kIcon32[kResources] = {&icon_uh_relic_32, &icon_uh_money_32,    &icon_uh_cultist_32,
                                                  &icon_uh_food_32,  &icon_uh_prisoner_32, &icon_uh_suspicion_32};

int problems = 0;
char problem[160] = {};
int waysPages = 0;

void report(const char* what, const char* text = nullptr) {
  ++problems;
  std::snprintf(problem, sizeof(problem), "%s%s%s", what, text ? ": " : "", text ? text : "");
}

fui::TextStyle style(fui::FontId font, fui::TextAlign align, fui::Color colour = fui::Color::Black, uint8_t lines = 1) {
  fui::TextStyle s;
  s.font = font;
  s.align = align;
  s.color = colour;
  s.maxLines = lines;
  return s;
}

int right(const fui::Rect& r) { return r.x + r.width; }
int bottom(const fui::Rect& r) { return r.y + r.height; }
fui::Rect rect(int x, int y, int w, int h) { return fui::makeRect(x, y, w, h); }

// One line, centred on its cut's capitals. Too wide for its box is a problem.
void label(toybox::Screen& screen, const fui::Rect& box, const char* text, fui::FontId font,
           const toybox::CutMetrics& cut, fui::TextAlign align, fui::Color colour = fui::Color::Black) {
  const fui::TextStyle s = style(font, align, colour);
  if (screen.target().measureText(font, text, s).width > box.width) report("a label is wider than its box", text);
  screen.target().text(toybox::inkCentred(box, cut), text, s);
}

void small(toybox::Screen& screen, const fui::Rect& box, const char* text, fui::TextAlign align,
           fui::Color colour = fui::Color::Black) {
  label(screen, box, text, toybox::kSmallFont, toybox::kButtonCut, align, colour);
}

int lineHeight(toybox::Screen& screen) { return screen.target().lineHeight(toybox::kBodyFont); }

int smallLine(toybox::Screen& screen) { return screen.target().lineHeight(toybox::kSmallFont); }

// How many lines of the small face `text` needs at `width`: one or two.
int noteLines(toybox::Screen& screen, const char* text, int width) {
  const fui::TextStyle s = style(toybox::kSmallFont, fui::TextAlign::Left);
  return screen.target().measureText(toybox::kSmallFont, text, s).width <= width ? 1 : 2;
}

// A note in the small face on one or two lines, broken between words.
void note(toybox::Screen& screen, const fui::Rect& box, const char* text) {
  const int lines = box.height >= 2 * smallLine(screen) ? 2 : 1;
  if (lines == 1) {
    small(screen, box, text, fui::TextAlign::Left);
    return;
  }
  const fui::TextStyle s = style(toybox::kSmallFont, fui::TextAlign::Left, fui::Color::Black, 2);
  const std::string fitted = toybox::fitLines(screen.target(), text, box.width, 2, s);
  if (fitted != text) report("a note needs more than two lines", text);
  screen.target().text(box, fitted.c_str(), s);
}

// How many body lines `text` needs at `width`, up to `most`.
int linesFor(toybox::Screen& screen, const char* text, int width, int most) {
  for (int n = 1; n < most; ++n) {
    const fui::TextStyle s = style(toybox::kBodyFont, fui::TextAlign::Left, fui::Color::Black, static_cast<uint8_t>(n));
    if (toybox::fitLines(screen.target(), text, static_cast<int16_t>(width), n, s) == text) return n;
  }
  return most;
}

// Prose in the body face, broken between words, at the top of `box` (or in
// its middle). Needing more than `most` lines, or more than the box holds, is
// a problem.
void prose(toybox::Screen& screen, const fui::Rect& box, const char* text, int most, bool middle = false) {
  const int line = lineHeight(screen);
  const int room = line > 0 ? box.height / line : 1;
  if (room < most) most = room < 1 ? 1 : room;
  const int lines = linesFor(screen, text, box.width, most);
  const fui::TextStyle s =
      style(toybox::kBodyFont, fui::TextAlign::Left, fui::Color::Black, static_cast<uint8_t>(lines));
  const std::string fitted = toybox::fitLines(screen.target(), text, box.width, lines, s);
  if (fitted != text) report("prose needs more lines than its box holds", text);
  const int height = lines * line;
  const int y = middle ? box.y + (box.height - height) / 2 : box.y;
  screen.target().text(rect(box.x, y, box.width, height), fitted.c_str(), s);
}

int measure(toybox::Screen& screen, const char* text) {
  return screen.target().measureText(toybox::kSmallFont, text, style(toybox::kSmallFont, fui::TextAlign::Left)).width;
}

void icon(toybox::Screen& screen, const fui::Rect& box, const freeink::Icon& i, fui::Color colour) {
  screen.target().bitmap(box, fui::bitmapFromIcon(i), fui::BitmapMode::Contain, fui::Paint::solid(colour));
}

void rule(toybox::Screen& screen, int x, int y, int width, int weight) {
  screen.target().fill(rect(x, y, width, weight), fui::Paint::solid(fui::Color::Black));
}

void button(toybox::Screen& screen, const fui::Rect& box, const char* text, fui::ActionId action, bool primary) {
  if (primary) {
    screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
  } else {
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 2);
  }
  small(screen, box, text, fui::TextAlign::Center, primary ? fui::Color::White : fui::Color::Black);
  screen.frame().hit(box, action);
}

// ---- tokens: a signed count and a symbol -----------------------------------

// Draws a token with its left edge at x, centred on midY, and returns its
// width. With draw false it only measures.
int token(toybox::Screen& screen, int x, int midY, const view::Token& t, char sign, bool draw,
          fui::Color colour = fui::Color::Black) {
  char number[12];
  if (t.kind == view::Token::OnlyIfNone) {
    std::snprintf(number, sizeof(number), "NO");
  } else {
    std::snprintf(number, sizeof(number), "%c%d", sign, t.amount);
  }
  const int w = measure(screen, number);
  if (draw) small(screen, rect(x, midY - 15, w + 2, 30), number, fui::TextAlign::Left, colour);
  int at = x + w + 3;
  auto symbol = [&](int resource) {
    if (draw) icon(screen, rect(at, midY - kTokenIcon / 2, kTokenIcon, kTokenIcon), *kIcon24[resource], colour);
    at += kTokenIcon;
  };
  auto word = [&](const char* text) {
    const int width = measure(screen, text);
    if (draw) small(screen, rect(at, midY - 15, width + 2, 30), text, fui::TextAlign::Left, colour);
    at += width + 2;
  };
  switch (t.kind) {
    case view::Token::Count:
    case view::Token::OnlyIfNone:
      symbol(t.resource);
      break;
    case view::Token::Either:
      symbol(underhand::Cultist);
      word("/");
      symbol(underhand::Prisoner);
      break;
    case view::Token::Random:
      at += 3;
      word("AT RANDOM");
      break;
  }
  return at - x;
}

int tokensWidth(toybox::Screen& screen, const view::Tokens& list, char sign) {
  int total = 0;
  for (int i = 0; i < list.count; ++i) total += token(screen, 0, 0, list.token[i], sign, false) + (i ? kBetween : 0);
  return total;
}

// A row of tokens from x, or ending at x when fromRight.
void tokens(toybox::Screen& screen, int x, int midY, const view::Tokens& list, char sign, bool fromRight,
            fui::Color colour = fui::Color::Black) {
  if (fromRight) x -= tokensWidth(screen, list, sign);
  for (int i = 0; i < list.count; ++i) x += token(screen, x, midY, list.token[i], sign, true, colour) + kBetween;
}

// ---- chrome, bar, status ----------------------------------------------------

void chrome(toybox::Screen& screen, const char* rightLabel) {
  fui::HeaderProps header;
  header.title = "UNDERHAND";
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  if (rightLabel && *rightLabel) {
    const fui::Rect box = toybox::headerInkRect(screen).inset(fui::Insets{0, kMargin, 0, 0});
    small(screen, box, rightLabel, fui::TextAlign::Right, fui::Color::White);
  }
}

// One cell per resource, its symbol and its count side by side. Food and
// suspicion invert when they invite a punishment.
void bar(toybox::Screen& screen, const fui::Rect& area, const CardModel& model) {
  rule(screen, area.x, area.y, area.width, toybox::kRule);
  const int cell = area.width / kResources;
  const fui::TextStyle numberStyle = style(toybox::kDisplayFont, fui::TextAlign::Left);
  for (int r = 0; r < kResources; ++r) {
    const fui::Rect box = rect(area.x + r * cell, area.y + toybox::kRule + 3, cell, area.height - toybox::kRule - 3);
    const bool alarm =
        (r == underhand::Food && model.danger.food) || (r == underhand::Suspicion && model.danger.suspicion);
    const fui::Color ink = alarm ? fui::Color::White : fui::Color::Black;
    if (alarm) screen.target().fill(box.inset(fui::Insets{0, 2, 0, 2}), fui::Paint::solid(fui::Color::Black));
    char count[8];
    std::snprintf(count, sizeof(count), "%d", model.held[r]);
    const int numberWidth = screen.target().measureText(toybox::kDisplayFont, count, numberStyle).width;
    const int group = kBarIcon + 4 + numberWidth;
    if (group > box.width - 4) report("a count does not fit its cell", count);
    const int x = box.x + (box.width - group) / 2;
    icon(screen, rect(x, box.y + (box.height - kBarIcon) / 2, kBarIcon, kBarIcon), *kIcon32[r], ink);
    label(screen, rect(x + kBarIcon + 4, box.y, numberWidth + 2, box.height), count, toybox::kDisplayFont,
          toybox::kDisplayCut, fui::TextAlign::Left, ink);
  }
  // What the symbols mean is one tap away.
  screen.frame().hit(area, ActionHelp);
}

void status(toybox::Screen& screen, const fui::Rect& area, const CardModel& model) {
  const int midY = area.y + area.height / 2;
  if (model.statusIsDanger) {
    icon(screen, rect(area.x, midY - kTokenIcon / 2, kTokenIcon, kTokenIcon), icon_uh_alert_24, fui::Color::Black);
    const int x = area.x + kTokenIcon + 8;
    small(screen, rect(x, area.y, right(area) - x, area.height), model.status, fui::TextAlign::Left);
    return;
  }
  if (model.lastPaid.count == 0 && model.lastGained.count == 0) return;
  const char* last = "LAST TURN";
  const int labelWidth = measure(screen, last) + 12;
  const int paid = tokensWidth(screen, model.lastPaid, '-');
  const int gained = tokensWidth(screen, model.lastGained, '+');
  const int both = paid && gained ? kBetween * 2 : 0;
  int x = area.x;
  if (labelWidth + paid + both + gained <= area.width) {
    small(screen, rect(x, area.y, labelWidth, area.height), last, fui::TextAlign::Left);
    x += labelWidth;
  } else if (paid + both + gained > area.width) {
    report("the last turn does not fit the status line");
  }
  tokens(screen, x, midY, model.lastPaid, '-', false);
  if (gained) tokens(screen, x + paid + both, midY, model.lastGained, '+', false);
}

// ---- the panels ----------------------------------------------------------------

// The chips for an option paid several ways, joined by OR: as many as fit
// beside what it gives, the last becoming MORE when some are left out. The
// first, the one a tap on the rest of the option pays, is filled. Returns the
// x the chips end at.
int chips(toybox::Screen& screen, const OptionRow& o, int k, const fui::Rect& row, int getWidth) {
  const int available = row.width - getWidth - kBetween;
  int widths[kChips] = {};
  for (int i = 0; i < kChips && i < o.ways; ++i) widths[i] = tokensWidth(screen, o.way[i], '-') + kChipPad * 2;
  const int more = measure(screen, "MORE") + kChipPad * 2;
  const int orWidth = measure(screen, "OR") + kOrPad * 2;
  auto width = [&](int n) {
    int w = 0;
    for (int i = 0; i < n; ++i) w += widths[i] + (i ? orWidth : 0);
    if (n < o.ways) w += orWidth + more;
    return w;
  };
  int shown = o.ways > kChips ? kChips - 1 : o.ways;
  while (shown > 1 && width(shown) > available) --shown;
  if (width(shown) > available) report("payment chips do not fit beside what the option gives");

  const int midY = row.y + row.height / 2;
  int x = row.x;
  auto joiner = [&]() {
    small(screen, rect(x, row.y, orWidth, row.height), "OR", fui::TextAlign::Center);
    x += orWidth;
  };
  for (int i = 0; i < shown; ++i) {
    if (i) joiner();
    const fui::Rect chip = rect(x, row.y, widths[i], row.height);
    if (i == 0) {
      screen.target().fill(chip, fui::Paint::solid(fui::Color::Black));
    } else {
      screen.target().stroke(chip, fui::Paint::solid(fui::Color::Black), 2);
    }
    tokens(screen, x + kChipPad, midY, o.way[i], '-', false, i == 0 ? fui::Color::White : fui::Color::Black);
    screen.frame().hit(chip, ActionPay, static_cast<int16_t>(k * kWayStride + i));
    x += widths[i];
  }
  if (shown < o.ways) {
    joiner();
    const fui::Rect chip = rect(x, row.y, more, row.height);
    screen.target().stroke(chip, fui::Paint::solid(fui::Color::Black), 2);
    small(screen, chip, "MORE", fui::TextAlign::Center);
    screen.frame().hit(chip, ActionMore, static_cast<int16_t>(k));
    x += more;
  }
  return x;
}

void options(toybox::Screen& screen, const fui::Rect& area, const CardModel& model) {
  // Three to a card, or halves when there are fewer: every option text long
  // enough to need a third line is on a card of one or two.
  const int rows = model.optionCount == 3 ? 3 : 2;
  const int slot = (area.height - (rows - 1) * kGap) / rows;
  for (int k = 0; k < model.optionCount; ++k) {
    const OptionRow& o = model.option[k];
    const fui::Rect box = rect(area.x, area.y + k * (slot + kGap), area.width, slot);
    const bool open = o.state == view::OptionState::Open;
    if (open) {
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 2);
    } else {
      screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 1);
    }
    const fui::Rect inner = box.inset(fui::Insets{4, kPad, 3, kPad});
    const int noteH = o.note[0] ? noteLines(screen, o.note, inner.width) * smallLine(screen) : 0;
    const fui::Rect row = rect(inner.x, bottom(inner) - kRowHeight - noteH, inner.width, kRowHeight);
    prose(screen, rect(inner.x, inner.y, inner.width, row.y - inner.y), o.text, 4);
    if (noteH) note(screen, rect(inner.x, bottom(inner) - noteH, inner.width, noteH), o.note);

    const int midY = row.y + row.height / 2;
    const int getWidth = tokensWidth(screen, o.get, '+');
    tokens(screen, right(row), midY, o.get, '+', true);
    if (!open) {
      // What it would take, for planning; the note says what is missing.
      if (tokensWidth(screen, o.give, '-') + kBetween + getWidth > row.width) {
        report("what an option asks runs into what it gives");
      }
      tokens(screen, row.x, midY, o.give, '-', false);
      continue;
    }
    int chipsEnd = row.x;
    if (o.ways > 1) {
      chipsEnd = chips(screen, o, k, row, getWidth);
    } else {
      const int giveWidth = tokensWidth(screen, o.way[0], '-');
      if (giveWidth + kBetween + getWidth > row.width) report("what an option takes runs into what it gives");
      tokens(screen, row.x, midY, o.way[0], '-', false);
    }
    // The rest of the option takes it, paid its first way. Registered around
    // the chips rather than over them, so a tap means one thing.
    const auto take = static_cast<int16_t>(k);
    screen.frame().hit(rect(box.x, box.y, box.width, row.y - box.y), ActionOption, take);
    screen.frame().hit(rect(chipsEnd, row.y, right(box) - chipsEnd, row.height), ActionOption, take);
    screen.frame().hit(rect(box.x, bottom(row), box.width, bottom(box) - bottom(row)), ActionOption, take);
  }
}

void outcome(toybox::Screen& screen, const fui::Rect& area, const CardModel& model) {
  screen.target().stroke(area, fui::Paint::solid(fui::Color::Black), 2);
  const fui::Rect inner = area.inset(fui::Insets{kPad, kPad + 4, kPad, kPad + 4});
  const fui::Rect go = rect(inner.x, bottom(inner) - kButtonHeight, inner.width, kButtonHeight);
  const int line = lineHeight(screen);
  small(screen, rect(inner.x, inner.y, inner.width, 28), "YOU CHOSE", fui::TextAlign::Left);
  int y = inner.y + 30;
  const int chose = linesFor(screen, model.chose, inner.width, 3);
  prose(screen, rect(inner.x, y, inner.width, chose * line), model.chose, 3);
  y += chose * line + 6;
  rule(screen, inner.x, y, inner.width, toybox::kHairline);
  y += 4;
  if (model.paid.count || model.gained.count) {
    if (tokensWidth(screen, model.paid, '-') + kBetween + tokensWidth(screen, model.gained, '+') > inner.width) {
      report("what was paid runs into what was gained");
    }
    tokens(screen, inner.x, y + kRowHeight / 2, model.paid, '-', false);
    tokens(screen, right(inner), y + kRowHeight / 2, model.gained, '+', true);
    y += kRowHeight + 4;
  }
  if (model.lost.count > 0) {
    small(screen, rect(inner.x, y, inner.width, 28), "LOST AT RANDOM", fui::TextAlign::Left);
    y += 28;
    if (tokensWidth(screen, model.lost, '-') > inner.width) report("the random loss does not fit its row");
    tokens(screen, inner.x, y + kRowHeight / 2, model.lost, '-', false);
    y += kRowHeight + 4;
  }
  for (int i = 0; i < model.outcomeLines; ++i) {
    const int lines = linesFor(screen, model.outcome[i], inner.width, 3);
    if (y + 6 + lines * line > go.y - kGap) {
      report("the outcome has more lines than its panel holds", model.outcome[i]);
      break;
    }
    y += 6;
    prose(screen, rect(inner.x, y, inner.width, lines * line), model.outcome[i], 3);
    y += lines * line;
  }
  // The bar says where to tap; the whole panel answers.
  screen.target().fill(go, fui::Paint::solid(fui::Color::Black));
  small(screen, go, "CONTINUE", fui::TextAlign::Center, fui::Color::White);
  screen.frame().hit(area, ActionContinue);
}

void foresight(toybox::Screen& screen, const fui::Rect& area, const CardModel& model) {
  screen.target().stroke(area, fui::Paint::solid(fui::Color::Black), 2);
  const fui::Rect inner = area.inset(fui::Insets{kPad, kPad + 4, kPad, kPad + 4});
  small(screen, rect(inner.x, inner.y, inner.width, 28),
        model.mayDiscard ? "NEXT CARDS. TAP TO DISCARD" : "THE NEXT CARDS, IN ORDER", fui::TextAlign::Left);
  constexpr int kRow = 88;
  for (int i = 0; i < model.seenCount; ++i) {
    const fui::Rect row = rect(inner.x, inner.y + 36 + i * (kRow + kGap), inner.width, kRow);
    const bool marked = model.discard[i];
    screen.target().stroke(row, fui::Paint::solid(fui::Color::Black), marked ? 3 : 1);
    char number[4];
    std::snprintf(number, sizeof(number), "%d", i + 1);
    small(screen, rect(row.x + 8, row.y, 20, row.height), number, fui::TextAlign::Left);
    const int tag = model.mayDiscard ? 108 : 0;
    const int textRight = model.mayDiscard ? right(row) - tag - 16 : right(row) - 10;
    prose(screen, rect(row.x + 32, row.y + 4, textRight - row.x - 32, row.height - 8), model.seenTitle[i], 2, true);
    if (!model.mayDiscard) continue;
    const fui::Rect pill = rect(right(row) - tag - 8, row.y + 16, tag, row.height - 32);
    if (marked) {
      screen.target().fill(pill, fui::Paint::solid(fui::Color::Black));
    } else {
      screen.target().stroke(pill, fui::Paint::solid(fui::Color::Black), 2);
    }
    small(screen, pill.inset(fui::Insets{0, 8, 0, 8}), marked ? "DISCARD" : "KEEP", fui::TextAlign::Center,
          marked ? fui::Color::White : fui::Color::Black);
    screen.frame().hit(row, ActionSeen, static_cast<int16_t>(i));
  }
  const int last = inner.y + 36 + model.seenCount * (kRow + kGap);
  if (last > bottom(inner) - kButtonHeight) report("the cards seen run into CONTINUE");
  button(screen, rect(inner.x, bottom(inner) - kButtonHeight, inner.width, kButtonHeight), "CONTINUE", ActionContinue,
         true);
}

void ways(toybox::Screen& screen, const fui::Rect& area, const CardModel& model) {
  screen.target().stroke(area, fui::Paint::solid(fui::Color::Black), 2);
  const fui::Rect inner = area.inset(fui::Insets{kPad, kPad + 4, kPad, kPad + 4});
  const int line = lineHeight(screen);
  const char* text = model.option[model.waysFor].text;
  const int lines = linesFor(screen, text, inner.width, 3);
  prose(screen, rect(inner.x, inner.y + 32, inner.width, lines * line), text, 3);

  // As many rows as the room left holds, a page at a time.
  constexpr int kRow = 48;
  const int top = inner.y + 32 + lines * line + 8;
  const int last = bottom(inner) - kButtonHeight - kGap;
  int perPage = (last - top) / kRow;
  if (perPage < 1) {
    report("no room for a single way to pay");
    perPage = 1;
  }
  const int pages = (model.wayCount + perPage - 1) / perPage;
  waysPages = pages;
  const int page = pages > 0 ? model.wayPage % pages : 0;
  const int first = page * perPage;
  const int shown = model.wayCount - first < perPage ? model.wayCount - first : perPage;

  small(screen, rect(inner.x, inner.y, inner.width, 28), "PAY WHICH WAY?", fui::TextAlign::Left);
  if (pages > 1) {
    char range[32];
    std::snprintf(range, sizeof(range), "%d-%d OF %d", first + 1, first + shown, model.wayCount);
    small(screen, rect(inner.x, inner.y, inner.width, 28), range, fui::TextAlign::Right);
  }
  for (int i = 0; i < shown; ++i) {
    const int way = first + i;
    const fui::Rect row = rect(inner.x, top + i * kRow, inner.width, kRow - 6);
    if (tokensWidth(screen, model.listed[way], '-') + kChipPad * 2 > row.width) {
      report("a way to pay is wider than its row");
    }
    if (way == 0) {
      screen.target().fill(row, fui::Paint::solid(fui::Color::Black));
    } else {
      screen.target().stroke(row, fui::Paint::solid(fui::Color::Black), 2);
    }
    tokens(screen, row.x + kChipPad, row.y + row.height / 2, model.listed[way], '-', false,
           way == 0 ? fui::Color::White : fui::Color::Black);
    screen.frame().hit(row, ActionPay, static_cast<int16_t>(model.waysFor * kWayStride + way));
  }
  const fui::Rect buttons = rect(inner.x, bottom(inner) - kButtonHeight, inner.width, kButtonHeight);
  if (pages > 1) {
    const int half = (buttons.width - kGap) / 2;
    button(screen, rect(buttons.x, buttons.y, half, buttons.height), "BACK", ActionCancel, false);
    const fui::Rect next = rect(buttons.x + half + kGap, buttons.y, half, buttons.height);
    const bool end = page + 1 == pages;
    screen.target().stroke(next, fui::Paint::solid(fui::Color::Black), 2);
    small(screen, next, end ? "FIRST PAGE" : "NEXT PAGE", fui::TextAlign::Center);
    screen.frame().hit(next, ActionNextWays, static_cast<int16_t>(end ? 0 : page + 1));
  } else {
    button(screen, buttons, "BACK", ActionCancel, false);
  }
}

}  // namespace

int layoutProblems() { return problems; }
int lastWaysPages() { return waysPages; }
const char* lastLayoutProblem() { return problem; }
void resetLayoutProblems() {
  problems = 0;
  problem[0] = '\0';
}

void buildCard(toybox::Screen& screen, const CardModel& model) {
  char deck[16];
  std::snprintf(deck, sizeof(deck), "DECK %d", model.deck);
  chrome(screen, deck);
  const fui::Rect body = screen.body();
  const int x = body.x + kMargin;
  const int width = body.width - kMargin * 2;

  const fui::Rect barArea = rect(x, bottom(body) - kBarHeight, width, kBarHeight);
  const fui::Rect statusArea = rect(x, barArea.y - kStatusHeight - 2, width, kStatusHeight);

  // The card: its name as a label, its text under it.
  int y = body.y + 8;
  small(screen, rect(x, y, width, 30), model.title, fui::TextAlign::Left);
  y += 32;
  const int line = lineHeight(screen);
  // Two lines, or three for the few long ones; the panel starts under it.
  const int flavor = linesFor(screen, model.flavor, width, 3) < 3 ? 2 : 3;
  prose(screen, rect(x, y, width, line * flavor), model.flavor, 3);
  y += line * flavor + 8;

  const fui::Rect panel = rect(x, y, width, statusArea.y - kGap - y);
  switch (model.panel) {
    case Panel::Options:
      options(screen, panel, model);
      break;
    case Panel::Outcome:
      outcome(screen, panel, model);
      break;
    case Panel::Foresight:
      foresight(screen, panel, model);
      break;
    case Panel::Ways:
      ways(screen, panel, model);
      break;
  }
  status(screen, statusArea, model);
  bar(screen, barArea, model);
}

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  chrome(screen, nullptr);
  const fui::Rect body = screen.body();
  const int x = body.x + kMargin;
  const int width = body.width - kMargin * 2;
  const int half = (width - kGap) / 2;
  const int primary = bottom(body) - kMargin - kButtonHeight;
  const int secondary = primary - kGap - kButtonHeight;

  if (model.confirmGiveUp) {
    label(screen, rect(x, body.y + 20, width, 70), "GIVE UP?", toybox::kDisplayFont, toybox::kDisplayCut,
          fui::TextAlign::Left);
    char sub[96];
    std::snprintf(sub, sizeof(sub), "The run on turn %d ends, and a new one begins. It cannot be undone.", model.turn);
    prose(screen, rect(x, body.y + 100, width, lineHeight(screen) * 3), sub, 3);
    button(screen, rect(x, primary, half, kButtonHeight), "KEEP PLAYING", ActionCancel, true);
    button(screen, rect(x + half + kGap, primary, half, kButtonHeight), "GIVE UP", ActionNewRun, false);
    return;
  }

  char headline[24];
  const char* sub = "";
  const char* go = "START";
  if (model.inRun) {
    std::snprintf(headline, sizeof(headline), "TURN %d", model.turn);
    sub = "A run is in progress.";
    go = "CONTINUE";
  } else if (model.tutorial) {
    std::snprintf(headline, sizeof(headline), "FIRST RUN");
    sub = "It teaches the game as you play.";
    go = "BEGIN";
  } else {
    std::snprintf(headline, sizeof(headline), "NEW RUN");
    sub = "Summon a god to win.";
  }
  label(screen, rect(x, body.y + 20, width, 70), headline, toybox::kDisplayFont, toybox::kDisplayCut,
        fui::TextAlign::Left);
  const int subLines = linesFor(screen, sub, width, 2);
  prose(screen, rect(x, body.y + 90, width, subLines * lineHeight(screen)), sub, 2);
  int y = body.y + 90 + subLines * lineHeight(screen) + 14;
  rule(screen, x, y, width, toybox::kRule);
  y += 14;

  int summoned = 0;
  for (int g = 0; g < model.gods; ++g) summoned += model.summoned[g] ? 1 : 0;
  char gods[32];
  std::snprintf(gods, sizeof(gods), "GODS SUMMONED  %d OF %d", summoned, model.gods);
  small(screen, rect(x, y, width, 30), gods, fui::TextAlign::Left);
  y += 36;
  constexpr int kRow = 40;
  constexpr int kMark = 16;
  for (int g = 0; g < model.gods; ++g) {
    const fui::Rect row = rect(x, y + g * kRow, width, kRow);
    if (bottom(row) > secondary - kGap) report("the gods run into the buttons");
    const fui::Rect mark = rect(row.x + 2, row.y + (kRow - kMark) / 2, kMark, kMark);
    if (model.summoned[g]) {
      screen.target().fill(mark, fui::Paint::solid(fui::Color::Black));
    } else {
      screen.target().stroke(mark, fui::Paint::solid(fui::Color::Black), 2);
    }
    small(screen, rect(row.x + kMark + 14, row.y, row.width - kMark - 14, kRow), model.godName[g],
          fui::TextAlign::Left);
  }

  if (model.inRun) {
    button(screen, rect(x, secondary, half, kButtonHeight), "HOW TO PLAY", ActionHelp, false);
    button(screen, rect(x + half + kGap, secondary, half, kButtonHeight), "GIVE UP", ActionGiveUp, false);
  } else {
    button(screen, rect(x, secondary, width, kButtonHeight), "HOW TO PLAY", ActionHelp, false);
  }
  button(screen, rect(x, primary, width, kButtonHeight), go, ActionMain, true);
}

void buildEnd(toybox::Screen& screen, const EndModel& model) {
  chrome(screen, nullptr);
  const fui::Rect body = screen.body();
  const int x = body.x + kMargin;
  const int width = body.width - kMargin * 2;
  fui::TextStyle headStyle = style(toybox::kDisplayFont, fui::TextAlign::Left);
  if (screen.target().measureText(toybox::kDisplayFont, model.headline, headStyle).width > width) {
    report("the end headline is wider than the screen", model.headline);
  }
  const std::string head = toybox::fittedTitle(screen.target(), model.headline, static_cast<int16_t>(width), headStyle);
  screen.target().text(rect(x, body.y + 24, width, 70), head.c_str(), headStyle);
  int y = body.y + 110;
  rule(screen, x, y, width, toybox::kRule);
  y += 20;
  const int line = lineHeight(screen);
  for (const char* d : model.detail) {
    if (!d[0]) continue;
    const int lines = linesFor(screen, d, width, 4);
    prose(screen, rect(x, y, width, lines * line), d, 4);
    y += lines * line + 16;
  }
  const int buttons = bottom(body) - kMargin - kButtonHeight;
  if (y > buttons) report("the end screen's text runs into its buttons");
  const int half = (width - kGap) / 2;
  button(screen, rect(x, buttons, half, kButtonHeight), "PLAY AGAIN", ActionNewRun, true);
  button(screen, rect(x + half + kGap, buttons, half, kButtonHeight), "MENU", ActionMenu, false);
}

void buildHelp(toybox::Screen& screen) {
  fui::HeaderProps header;
  header.title = "HOW TO PLAY";
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  const fui::Rect body = screen.body();
  const int x = body.x + kMargin;
  const int width = body.width - kMargin * 2;
  const int line = lineHeight(screen);
  int y = body.y + 8;

  struct Entry {
    const freeink::Icon* icon;
    const char* name;
    const char* note;
  };
  const Entry entries[] = {
      {kIcon32[underhand::Relic], "RELIC", "PAYS IN PLACE OF ANYTHING"},
      {kIcon32[underhand::Money], "MONEY", "BUYS AND BRIBES"},
      {kIcon32[underhand::Cultist], "CULTIST", "ONE OF YOUR FOLLOWERS"},
      {kIcon32[underhand::Food], "FOOD", "AT NONE, DESPERATION MAY STRIKE"},
      {kIcon32[underhand::Prisoner], "PRISONER", "ONE OF THEIRS, HELD CAPTIVE"},
      {kIcon32[underhand::Suspicion], "SUSPICION", "AT 5 OR MORE, POLICE MAY RAID"},
      {&icon_uh_alert_32, "16 OR MORE IN ALL", "INVITES GREED"},
  };
  constexpr int kEntry = 56;
  for (const Entry& e : entries) {
    icon(screen, rect(x, y + (kEntry - kBarIcon) / 2, kBarIcon, kBarIcon), *e.icon, fui::Color::Black);
    const int textX = x + kBarIcon + 16;
    const int textWidth = width - (textX - x);
    small(screen, rect(textX, y + 4, textWidth, 22), e.name, fui::TextAlign::Left);
    small(screen, rect(textX, y + 29, textWidth, 22), e.note, fui::TextAlign::Left);
    y += kEntry;
  }
  y += 10;
  auto paragraph = [&](const char* text, int most) {
    const int lines = linesFor(screen, text, width, most);
    prose(screen, rect(x, y, width, lines * line), text, most);
    y += lines * line + 8;
  };
  paragraph("A cultist and a prisoner joined by a slash means either will do.", 2);
  paragraph("Tap a choice to take it, paid the black way. Tap another chip to pay that way instead.", 3);
  const int buttons = bottom(body) - kMargin - kButtonHeight;
  if (y > buttons) report("how to play runs into its button");
  button(screen, rect(x, buttons, width, kButtonHeight), "BACK", ActionCancel, false);
}

}  // namespace underhandui
