#include "UnderhandScreens.h"

#include <cstdio>
#include <string>

#include "../ui/ToyboxIcons.h"
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
constexpr int16_t kChipPad = 6;
constexpr int16_t kOrPad = 4;

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

// How many lines of the small face `text` needs at `width`, up to three.
int noteLines(toybox::Screen& screen, const char* text, int width) {
  for (int n = 1; n < 3; ++n) {
    const fui::TextStyle s =
        style(toybox::kSmallFont, fui::TextAlign::Left, fui::Color::Black, static_cast<uint8_t>(n));
    if (toybox::fitLines(screen.target(), text, static_cast<int16_t>(width), n, s) == text) return n;
  }
  return 3;
}

// A note in the small face on as many lines as its box holds, broken between
// words. Needing more is a problem.
void note(toybox::Screen& screen, const fui::Rect& box, const char* text) {
  const int line = smallLine(screen);
  int lines = line > 0 ? box.height / line : 1;
  if (lines < 1) lines = 1;
  if (lines == 1) {
    small(screen, box, text, fui::TextAlign::Left);
    return;
  }
  const fui::TextStyle s =
      style(toybox::kSmallFont, fui::TextAlign::Left, fui::Color::Black, static_cast<uint8_t>(lines));
  const std::string fitted = toybox::fitLines(screen.target(), text, box.width, lines, s);
  if (fitted != text) report("a note needs more lines than its box holds", text);
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
  char number[16];
  std::snprintf(number, sizeof(number), "%c%d", sign, t.amount);
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

// One cell per resource, its symbol and its count side by side. The whole
// bar takes one size, the largest every count fits: the large symbol, the
// smaller one, or the small face when two digits need it. Food and suspicion
// invert while they invite a punishment.
void bar(toybox::Screen& screen, const fui::Rect& area, const CardModel& model) {
  rule(screen, area.x, area.y, area.width, toybox::kRule);
  const int cell = area.width / kResources;
  const int room = cell - 4;
  const fui::TextStyle numberStyle = style(toybox::kDisplayFont, fui::TextAlign::Left);
  char count[kResources][12];
  int widest = 0;
  int widestSmall = 0;
  for (int r = 0; r < kResources; ++r) {
    std::snprintf(count[r], sizeof(count[r]), "%d", model.held[r]);
    const int big = screen.target().measureText(toybox::kDisplayFont, count[r], numberStyle).width;
    const int little = measure(screen, count[r]);
    widest = big > widest ? big : widest;
    widestSmall = little > widestSmall ? little : widestSmall;
  }
  const bool roomy = kBarIcon + 3 + widest <= room;
  const bool big = roomy || kTokenIcon + 3 + widest <= room;
  const int size = roomy ? kBarIcon : kTokenIcon;
  if (size + 3 + (big ? widest : widestSmall) > room) report("a count does not fit its cell");
  for (int r = 0; r < kResources; ++r) {
    const fui::Rect box = rect(area.x + r * cell, area.y + toybox::kRule + 3, cell, area.height - toybox::kRule - 3);
    // By each roll's own chance: a certain Greed hides the others from the
    // warning line, not from the roll after it.
    const bool alarm =
        (r == underhand::Food && model.rolls.desperate > 0) || (r == underhand::Suspicion && model.rolls.police > 0);
    const fui::Color ink = alarm ? fui::Color::White : fui::Color::Black;
    if (alarm) screen.target().fill(box.inset(fui::Insets{0, 1, 0, 1}), fui::Paint::solid(fui::Color::Black));
    const int numberWidth = big ? screen.target().measureText(toybox::kDisplayFont, count[r], numberStyle).width
                                : measure(screen, count[r]);
    const int group = size + 3 + numberWidth;
    const int x = box.x + (box.width - group) / 2;
    icon(screen, rect(x, box.y + (box.height - size) / 2, size, size), roomy ? *kIcon32[r] : *kIcon24[r], ink);
    const fui::Rect number = rect(x + size + 3, box.y, numberWidth + 2, box.height);
    if (big) {
      label(screen, number, count[r], toybox::kDisplayFont, toybox::kDisplayCut, fui::TextAlign::Left, ink);
    } else {
      small(screen, number, count[r], fui::TextAlign::Left, ink);
    }
  }
}

// The line above the bar: each punishment the hand invites, with its chance,
// then what the last choice paid and gained if there is room, and a mark that
// says the symbols can be explained.
void status(toybox::Screen& screen, const fui::Rect& area, const CardModel& model) {
  const int midY = area.y + area.height / 2;
  int end = right(area) - kTokenIcon - 10;
  int x = area.x;
  const underhand::Odds& odds = model.odds;
  if (odds.greed || odds.police || odds.desperate) {
    char text[64] = {};
    size_t used = 0;
    auto add = [&](const char* name, int percent) {
      if (!percent || used + 1 >= sizeof(text)) return;
      const int w = std::snprintf(text + used, sizeof(text) - used, "%s%s %d%%", used ? " " : "", name, percent);
      if (w > 0)
        used += static_cast<size_t>(w) < sizeof(text) - used ? static_cast<size_t>(w) : sizeof(text) - used - 1;
    };
    add("GREED", odds.greed);
    add("RAID", odds.police);
    add("DESPERATE", odds.desperate);
    icon(screen, rect(x, midY - kTokenIcon / 2, kTokenIcon, kTokenIcon), icon_uh_alert_24, fui::Color::Black);
    x += kTokenIcon + 6;
    const int width = measure(screen, text);
    // The warning outranks the mark that says the bar can be tapped.
    if (x + width > end) end = right(area);
    small(screen, rect(x, area.y, end - x, area.height), text, fui::TextAlign::Left);
    x += width + 18;
  }
  if (end < right(area)) {
    icon(screen, rect(right(area) - kTokenIcon, midY - kTokenIcon / 2, kTokenIcon, kTokenIcon), icon_uh_help_24,
         fui::Color::Black);
  }
  if (model.lastPaid.count == 0 && model.lastGained.count == 0) return;
  // The last turn takes what is left: with its label, without it, or not at
  // all beside a danger, which matters more.
  const char* last = "LAST";
  const int labelWidth = measure(screen, last) + 10;
  const int paid = tokensWidth(screen, model.lastPaid, '-');
  const int gained = tokensWidth(screen, model.lastGained, '+');
  const int both = paid && gained ? kBetween * 2 : 0;
  const int room = end - x;
  if (labelWidth + paid + both + gained <= room) {
    small(screen, rect(x, area.y, labelWidth, area.height), last, fui::TextAlign::Left);
    x += labelWidth;
  } else if (paid + both + gained > room) {
    if (x == area.x) report("the last turn does not fit the status line");
    return;
  }
  tokens(screen, x, midY, model.lastPaid, '-', false);
  if (gained) tokens(screen, x + paid + both, midY, model.lastGained, '+', false);
}

// ---- the panels ----------------------------------------------------------------

// The chips for an option paid several ways, joined by OR: as many as fit
// beside what it gives, the last becoming ALL when some are left out. The
// first, the one a tap on the rest of the option pays, is filled. Returns the
// x the chips end at.
int chips(toybox::Screen& screen, const OptionRow& o, int k, int turn, const fui::Rect& row, const fui::Rect& reach,
          int getWidth) {
  const int available = row.width - getWidth - kBetween;
  int widths[kChips] = {};
  for (int i = 0; i < kChips && i < o.ways; ++i) widths[i] = tokensWidth(screen, o.way[i], '-') + kChipPad * 2;
  // ALL says how many ways there are, sized for the most there can be.
  const int more = measure(screen, "ALL 64") + kChipPad * 2;
  const int orWidth = measure(screen, "OR") + kOrPad * 2;
  auto width = [&](int n) {
    int w = 0;
    for (int i = 0; i < n; ++i) w += widths[i] + (i ? orWidth : 0);
    if (n < o.ways) w += orWidth + more;
    return w;
  };
  int shown = o.ways < kChips ? o.ways : kChips;
  if (shown < o.ways && shown == kChips) shown = kChips - 1;
  while (shown > 1 && width(shown) > available) --shown;
  if (width(shown) > available) report("payment chips do not fit beside what the option gives");

  const int midY = row.y + row.height / 2;
  // The last chip also answers a little past its right edge, short of what
  // the option gives: a near miss there should not pay the black way.
  const int gains = right(row) - getWidth - kBetween / 2;
  auto pastEnd = [&](int end) { return end + 20 < gains ? end + 20 : (gains > end ? gains : end); };
  int x = row.x;
  int answered = x;
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
    // A finger is wider than the chip is tall: the chip answers over the band
    // around it and half of each OR beside it.
    const int left = i ? x - orWidth / 2 : reach.x;
    const bool last = i + 1 == shown && shown == o.ways;
    const int rightEdge = last ? pastEnd(x + widths[i]) : x + widths[i] + orWidth / 2;
    screen.frame().hit(rect(left, reach.y, rightEdge - left, reach.height), ActionPay, stamp(turn, k * kWayStride + i));
    x += widths[i];
    answered = rightEdge;
  }
  if (shown < o.ways) {
    joiner();
    const fui::Rect chip = rect(x, row.y, more, row.height);
    screen.target().stroke(chip, fui::Paint::solid(fui::Color::Black), 2);
    char label[24];
    std::snprintf(label, sizeof(label), "ALL %d", o.ways);
    small(screen, chip, label, fui::TextAlign::Center);
    const int end = pastEnd(x + more);
    screen.frame().hit(rect(x - orWidth / 2, reach.y, end - (x - orWidth / 2), reach.height), ActionMore,
                       stamp(turn, k));
    x += more;
    answered = end;
  }
  return answered;
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
    // A closed option's note says what it does and why it is closed; where
    // that leaves too little room for the option's own words, only why.
    const char* said = o.note;
    int noteH = said[0] ? noteLines(screen, said, inner.width) * smallLine(screen) : 0;
    const int textRoom = inner.height - kRowHeight - noteH;
    if (o.shortNote[0] && linesFor(screen, o.text, inner.width, 4) * lineHeight(screen) > textRoom) {
      said = o.shortNote;
      noteH = noteLines(screen, said, inner.width) * smallLine(screen);
    }
    const fui::Rect row = rect(inner.x, bottom(inner) - kRowHeight - noteH, inner.width, kRowHeight);
    prose(screen, rect(inner.x, inner.y, inner.width, row.y - inner.y), o.text, 4);
    const int textBottom = inner.y + linesFor(screen, o.text, inner.width, 4) * lineHeight(screen);
    if (noteH) note(screen, rect(inner.x, bottom(inner) - noteH, inner.width, noteH), said);

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
    // The chips answer over the band from the text to the note: up to 20px
    // above them, never over the option's own words.
    const int reachTop = row.y - 20 > textBottom + 2 ? row.y - 20 : textBottom + 2;
    const int reachBottom = noteH ? bottom(row) + 4 : bottom(box);
    const fui::Rect reach = rect(box.x, reachTop, box.width, reachBottom - reachTop);
    if (o.guarded) {
      // Relics would pay for suspicion that is not held, and the option does
      // nothing else: not something one stray tap should do. It opens the list.
      tokens(screen, row.x, midY, o.way[0], '-', false);
      screen.frame().hit(box, ActionMore, stamp(model.turn, k));
      continue;
    }
    int chipsEnd = row.x;
    if (o.ways > 1) {
      chipsEnd = chips(screen, o, k, model.turn, row, reach, getWidth);
    } else {
      const int giveWidth = tokensWidth(screen, o.way[0], '-');
      if (giveWidth + kBetween + getWidth > row.width) report("what an option takes runs into what it gives");
      tokens(screen, row.x, midY, o.way[0], '-', false);
    }
    // The rest of the option takes it, paid its first way. Registered around
    // the chips rather than over them, so a tap means one thing.
    const int16_t take = stamp(model.turn, k);
    if (o.ways > 1) {
      screen.frame().hit(rect(box.x, box.y, box.width, reach.y - box.y), ActionOption, take);
      screen.frame().hit(rect(chipsEnd, reach.y, right(box) - chipsEnd, reach.height), ActionOption, take);
      if (bottom(reach) < bottom(box)) {
        screen.frame().hit(rect(box.x, bottom(reach), box.width, bottom(box) - bottom(reach)), ActionOption, take);
      }
    } else {
      screen.frame().hit(box, ActionOption, take);
    }
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
    const int lines = linesFor(screen, model.outcome[i], inner.width, 4);
    if (y + 6 + lines * line > go.y - kGap) {
      report("the outcome has more lines than its panel holds", model.outcome[i]);
      break;
    }
    y += 6;
    prose(screen, rect(inner.x, y, inner.width, lines * line), model.outcome[i], 4);
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
        model.mayDiscard ? "TAP A CARD TO DISCARD IT" : "THE NEXT CARDS, IN ORDER", fui::TextAlign::Left);
  constexpr int kRow = 88;
  for (int i = 0; i < model.seenCount; ++i) {
    const fui::Rect row = rect(inner.x, inner.y + 36 + i * (kRow + kGap), inner.width, kRow);
    const bool marked = model.discard[i];
    // A card marked for discard greys out, like anything else not in play.
    if (marked) screen.target().fill(row, fui::Paint::dither(fui::Color::LightGray));
    screen.target().stroke(row, fui::Paint::solid(fui::Color::Black), marked ? 1 : 2);
    char number[12];
    std::snprintf(number, sizeof(number), "%d", i + 1);
    small(screen, rect(row.x + 8, row.y, 20, row.height), number, fui::TextAlign::Left);
    const int tag = model.mayDiscard ? measure(screen, "KEPT") + 2 : 0;
    const int textRight = model.mayDiscard ? right(row) - tag - 18 : right(row) - 10;
    prose(screen, rect(row.x + 32, row.y + 4, textRight - row.x - 32, row.height - 8), model.seenTitle[i], 2, true);
    if (!model.mayDiscard) continue;
    // A state, not a button: the whole row is what a tap flips.
    small(screen, rect(right(row) - tag - 8, row.y, tag, row.height), marked ? "OUT" : "KEPT", fui::TextAlign::Right);
    screen.frame().hit(row, ActionSeen, static_cast<int16_t>(i));
  }
  const int last = inner.y + 36 + model.seenCount * (kRow + kGap);
  if (model.mayDiscard) {
    small(screen, rect(inner.x, last, inner.width, 28), "DISCARDS RETURN AT A RESHUFFLE", fui::TextAlign::Left);
  }
  if (last + (model.mayDiscard ? 28 : 0) > bottom(inner) - kButtonHeight - 4) {
    report("the cards seen run into CONTINUE");
  }
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
  int below = inner.y + 32 + lines * line + 8;
  if (model.waysGuarded) {
    // The list opened instead of paying; this is why.
    const char* why = "YOU HOLD NO SUSPICION: RELICS WOULD BUY NOTHING";
    const int height = noteLines(screen, why, inner.width) * smallLine(screen);
    note(screen, rect(inner.x, below, inner.width, height), why);
    below += height + 8;
  }

  // As many rows as the room left holds, a page at a time.
  constexpr int kRow = 56;
  const int top = below;
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

  small(screen, rect(inner.x, inner.y, inner.width, 28), model.waysGuarded ? "PAY ANYWAY?" : "PAY WHICH WAY?",
        fui::TextAlign::Left);
  if (pages > 1) {
    char range[48];
    std::snprintf(range, sizeof(range), "%d-%d OF %d", first + 1, first + shown, model.wayCount);
    small(screen, rect(inner.x, inner.y, inner.width, 28), range, fui::TextAlign::Right);
  }
  for (int i = 0; i < shown; ++i) {
    const int way = first + i;
    const fui::Rect row = rect(inner.x, top + i * kRow, inner.width, kRow - 8);
    if (tokensWidth(screen, model.listed[way], '-') + kChipPad * 2 > row.width) {
      report("a way to pay is wider than its row");
    }
    const fui::Color ink = way == 0 ? fui::Color::White : fui::Color::Black;
    if (way == 0) {
      screen.target().fill(row, fui::Paint::solid(fui::Color::Black));
    } else {
      screen.target().stroke(row, fui::Paint::solid(fui::Color::Black), 2);
    }
    tokens(screen, row.x + kChipPad, row.y + row.height / 2, model.listed[way], '-', false, ink);
    // The gap between rows belongs to the row above it.
    screen.frame().hit(rect(row.x, row.y, row.width, kRow), ActionPay,
                       stamp(model.turn, model.waysFor * kWayStride + way));
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
  char deck[24];
  std::snprintf(deck, sizeof(deck), "DECK %d", model.deck);
  chrome(screen, deck);
  const fui::Rect body = screen.body();
  const int x = body.x + kMargin;
  const int width = body.width - kMargin * 2;

  // The bar takes the whole width: a count of two digits needs every pixel.
  const fui::Rect barArea = rect(body.x + 4, bottom(body) - kBarHeight, body.width - 8, kBarHeight);
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
  // What the symbols and the warnings mean is one tap away.
  screen.frame().hit(rect(barArea.x, statusArea.y, barArea.width, bottom(barArea) - statusArea.y), ActionHelp);
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
    sub = "Summon a god to win.";
    go = "CONTINUE";
  } else if (model.tutorial) {
    std::snprintf(headline, sizeof(headline), "FIRST RUN");
    sub = "It teaches the game. Summon a god to win.";
    go = "BEGIN";
  } else {
    std::snprintf(headline, sizeof(headline), "NEW RUN");
    sub = "Summon a god to win.";
  }
  if (model.saveSetAside) sub = "The last save could not be read, and was kept aside.";
  label(screen, rect(x, body.y + 20, width, 70), headline, toybox::kDisplayFont, toybox::kDisplayCut,
        fui::TextAlign::Left);
  const int subLines = linesFor(screen, sub, width, 2);
  prose(screen, rect(x, body.y + 90, width, subLines * lineHeight(screen)), sub, 2);
  int y = body.y + 90 + subLines * lineHeight(screen) + 14;
  rule(screen, x, y, width, toybox::kRule);
  y += 14;

  int summoned = 0;
  for (int g = 0; g < model.gods; ++g) summoned += model.summoned[g] ? 1 : 0;
  char gods[48];
  std::snprintf(gods, sizeof(gods), "GODS SUMMONED  %d OF %d", summoned, model.gods);
  small(screen, rect(x, y, width, 30), gods, fui::TextAlign::Left);
  y += 36;
  // A record, not a list of controls: a skull by each god already summoned,
  // a hairline for each still to come.
  constexpr int kRow = 40;
  constexpr int kMark = 24;
  for (int g = 0; g < model.gods; ++g) {
    const fui::Rect row = rect(x, y + g * kRow, width, kRow);
    if (bottom(row) > secondary - kGap) report("the gods run into the buttons");
    if (model.summoned[g]) {
      icon(screen, rect(row.x, row.y + (kRow - kMark) / 2, kMark, kMark), icon_underhand_24, fui::Color::Black);
    } else {
      rule(screen, row.x + 4, row.y + kRow / 2, kMark - 8, toybox::kRule);
    }
    small(screen, rect(row.x + kMark + 12, row.y, row.width - kMark - 12, kRow), model.godName[g],
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
  // The headline in the display face, on a second line when a god's name
  // needs one.
  const int headLine = screen.target().lineHeight(toybox::kDisplayFont);
  fui::TextStyle headStyle = style(toybox::kDisplayFont, fui::TextAlign::Left, fui::Color::Black, 1);
  int headLines = 1;
  if (screen.target().measureText(toybox::kDisplayFont, model.headline, headStyle).width > width) {
    headLines = 2;
    headStyle.maxLines = 2;
  }
  const std::string head =
      toybox::fitLines(screen.target(), model.headline, static_cast<int16_t>(width), headLines, headStyle);
  if (head != model.headline) report("the end headline does not fit two lines", model.headline);
  screen.target().text(rect(x, body.y + 24, width, headLines * headLine), head.c_str(), headStyle);
  int y = body.y + 24 + headLines * headLine + 16;
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
  if (model.leaveOnly) {
    button(screen, rect(x, buttons, width, kButtonHeight), "BACK", ActionMenu, false);
    return;
  }
  const int half = (width - kGap) / 2;
  button(screen, rect(x, buttons, half, kButtonHeight), "PLAY AGAIN", ActionNewRun, true);
  button(screen, rect(x + half + kGap, buttons, half, kButtonHeight), "MENU", ActionMenu, false);
}

void buildHelp(toybox::Screen& screen, int page) {
  fui::HeaderProps header;
  header.title = "HOW TO PLAY";
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  const fui::Rect ink = toybox::headerInkRect(screen).inset(fui::Insets{0, kMargin, 0, 0});
  char of[32];
  std::snprintf(of, sizeof(of), "%d OF %d", page + 1, kHelpPages);
  small(screen, ink, of, fui::TextAlign::Right, fui::Color::White);

  const fui::Rect body = screen.body();
  const int x = body.x + kMargin;
  const int width = body.width - kMargin * 2;
  const int line = lineHeight(screen);
  int y = body.y + 8;
  auto paragraph = [&](const char* text, int most) {
    const int lines = linesFor(screen, text, width, most);
    prose(screen, rect(x, y, width, lines * line), text, most);
    y += lines * line + 10;
  };

  if (page == 0) {
    struct Entry {
      const freeink::Icon* icon;
      const char* name;
      const char* note;
    };
    const Entry entries[] = {
        {kIcon32[underhand::Relic], "RELIC", "PAYS IN PLACE OF ANYTHING"},
        {kIcon32[underhand::Money], "MONEY", "BUYS AND BRIBES"},
        {kIcon32[underhand::Cultist], "CULTIST", "ONE OF YOUR FOLLOWERS"},
        {kIcon32[underhand::Food], "FOOD", "NONE LEFT: DESPERATE MEASURES"},
        {kIcon32[underhand::Prisoner], "PRISONER", "ONE OF THEIRS, HELD CAPTIVE"},
        {kIcon32[underhand::Suspicion], "SUSPICION", "AT 5 OR MORE: A POLICE RAID"},
        {&icon_uh_alert_32, "A PUNISHMENT MAY COME", "GREED: 16+ HELD, SUSPICION TOO"},
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
    paragraph("A cultist and a prisoner joined by a slash means either will do.", 2);
  } else {
    paragraph("Summon a god to win. Some chains of cards end in one.", 2);
    paragraph("Tap a choice to take it, paid the black way, or tap another way. ALL shows every way.", 3);
    paragraph("A grey choice cannot be taken, and says why.", 2);
    paragraph("The warning is each punishment's chance if your hand stays as it is. A black count invites one.", 4);
    paragraph("LAST is what your last choice paid and gained. DECK counts cards before a reshuffle.", 3);
  }

  const int buttons = bottom(body) - kMargin - kButtonHeight;
  if (y - 10 + kGap > buttons) report("how to play runs into its buttons");
  const int half = (width - kGap) / 2;
  button(screen, rect(x, buttons, half, kButtonHeight), "BACK", ActionCancel, false);
  const bool last = page + 1 >= kHelpPages;
  const fui::Rect turn = rect(x + half + kGap, buttons, half, kButtonHeight);
  screen.target().stroke(turn, fui::Paint::solid(fui::Color::Black), 2);
  small(screen, turn, last ? "SYMBOLS" : "RULES", fui::TextAlign::Center);
  screen.frame().hit(turn, ActionHelpPage, static_cast<int16_t>(last ? 0 : page + 1));
}

}  // namespace underhandui
