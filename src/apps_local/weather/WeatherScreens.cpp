#include "WeatherScreens.h"

#include <cstdio>
#include <string>

#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxInk.h"
#include "../ui/ToyboxText.h"
#include "WeatherIcons.h"

namespace weatherui {
namespace {

constexpr int16_t kMargin = toybox::kMargin;
constexpr int16_t kGutter = toybox::kGutter;

// One scale for all three screens, every gap between INKS, cap top to baseline
// (ui/ToyboxInk.h), chosen by relationship: lines of one block a gutter
// apart, the rows of a list two, groups three.
constexpr int16_t kLineGap = kGutter;
constexpr int16_t kRowGap = kGutter * 2;
constexpr int16_t kGroupGap = kGutter * 3;

int16_t max16(const int a, const int b) { return static_cast<int16_t>(a > b ? a : b); }

// A style this screen owns outright. The alignment is always named: the small
// slot is font 0, and a style with font 0 and every other field at its default
// reads as unset to the components, which would put the theme's back.
fui::TextStyle ink(const fui::FontId font, const fui::TextAlign align = fui::TextAlign::Left, const uint8_t lines = 1,
                   const fui::Color color = fui::Color::Black) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.maxLines = lines;
  style.color = color;
  return style;
}

int16_t lineOf(toybox::Screen& screen, const fui::FontId font) { return screen.target().lineHeight(font); }

int16_t widthOf(toybox::Screen& screen, const fui::FontId font, const char* text) {
  return screen.target().measureText(font, text, ink(font)).width;
}

int16_t capOf(toybox::Screen& screen, const fui::FontId font) { return toybox::inkHeight(screen.target(), font); }

// Draws `text` with its cap top at inkTop.
void textAt(toybox::Screen& screen, const int16_t x, const int16_t inkTop, const int16_t width, const fui::FontId font,
            const char* text, const uint8_t lines = 1, const fui::TextAlign align = fui::TextAlign::Left,
            const fui::Color color = fui::Color::Black) {
  screen.target().text(toybox::inkRect(screen.target(), font, x, inkTop, width, lines), text,
                       ink(font, align, lines, color));
}

// How many lines `text` wraps to at `width`, at most `most`.
int linesOf(toybox::Screen& screen, const char* text, const fui::FontId font, const int16_t width, const int most) {
  const fui::TextStyle style = ink(font, fui::TextAlign::Left, static_cast<uint8_t>(most));
  const int16_t height = fui::measureWrappedText(screen.target(), text, style, width).height;
  const int16_t line = lineOf(screen, font);
  return line > 0 ? max16(1, height / line) : 1;
}

// --- Signs -------------------------------------------------------------------
// No Jersey cut carries U+00B0 or U+2212, so both are drawn, sized from the cap
// height so they scale with the number they belong to. The minus matters
// most: Jersey's hyphen is a short dash, and "H-28" set with it reads as one
// hyphenated word rather than a letter and a negative number.

int16_t ringRadius(const int16_t capHeight) { return max16(3, (capHeight + 4) / 7); }
int16_t ringWeight(const int16_t capHeight) { return max16(2, capHeight / 18); }
int16_t ringGap(const int16_t capHeight) { return max16(2, capHeight / 12); }

struct Minus {
  int16_t width;   // most of a figure's width, as a typeset minus is
  int16_t weight;  // about a stroke
  int16_t gap;     // to the first figure
};

Minus minusOf(toybox::Screen& screen, const fui::FontId font) {
  const int16_t cap = capOf(screen, font);
  return Minus{max16(4, widthOf(screen, font, "0") * 3 / 4), max16(2, cap / 7), max16(2, cap / 8)};
}

bool isTemperature(const std::string& text) { return text != "--"; }
bool isNegative(const std::string& text) {
  return text.size() > 1 && text[0] == '-' && text[1] >= '0' && text[1] <= '9';
}

int16_t tempWidth(toybox::Screen& screen, const fui::FontId font, const std::string& text) {
  if (!isTemperature(text)) return widthOf(screen, font, text.c_str());
  const bool negative = isNegative(text);
  int width = widthOf(screen, font, negative ? text.c_str() + 1 : text.c_str());
  if (negative) {
    const Minus minus = minusOf(screen, font);
    width += minus.width + minus.gap;
  }
  const int16_t cap = capOf(screen, font);
  return static_cast<int16_t>(width + ringGap(cap) + 2 * ringRadius(cap));
}

// A temperature, its minus and its ring, with the cap top at inkTop.
void tempAt(toybox::Screen& screen, int16_t x, const int16_t inkTop, const fui::FontId font, const std::string& text,
            const fui::Color color = fui::Color::Black) {
  const int16_t cap = capOf(screen, font);
  if (!isTemperature(text)) {
    textAt(screen, x, inkTop, static_cast<int16_t>(widthOf(screen, font, text.c_str()) + 1), font, text.c_str(), 1,
           fui::TextAlign::Left, color);
    return;
  }
  const char* figures = text.c_str();
  if (isNegative(text)) {
    const Minus minus = minusOf(screen, font);
    screen.target().fill(
        fui::makeRect(x, static_cast<int16_t>(inkTop + (cap - minus.weight) / 2), minus.width, minus.weight),
        fui::Paint::solid(color));
    x = static_cast<int16_t>(x + minus.width + minus.gap);
    ++figures;
  }
  const int16_t width = widthOf(screen, font, figures);
  textAt(screen, x, inkTop, static_cast<int16_t>(width + 1), font, figures, 1, fui::TextAlign::Left, color);
  const int16_t r = ringRadius(cap);
  toybox::ring(screen, static_cast<int16_t>(x + width + ringGap(cap) + r), static_cast<int16_t>(inkTop + r), r,
               ringWeight(cap), color, color == fui::Color::Black ? fui::Color::White : fui::Color::Black);
}

std::string number(const int value) { return std::to_string(value); }
std::string highText(const DayRow& day) { return day.hasHigh ? number(day.high) : std::string("--"); }
std::string lowText(const DayRow& day) { return day.hasLow ? number(day.low) : std::string("--"); }
std::string rainLabel(const int rain) { return rain < 0 ? std::string() : number(rain) + "%"; }

void blit(toybox::Screen& screen, const freeink::Icon& icon, const int16_t x, const int16_t y,
          const fui::Color color = fui::Color::Black) {
  screen.target().bitmap(fui::makeRect(x, y, static_cast<int16_t>(icon.w), static_cast<int16_t>(icon.h)),
                         fui::bitmapFromIcon(icon), fui::BitmapMode::Contain, fui::Paint::solid(color));
}

// An icon whose ink is centred on (centreX, centreY): the drawn shape, not the
// square around it, which carries different padding from icon to icon.
void iconCentred(toybox::Screen& screen, const freeink::Icon& icon, const int16_t centreX, const int16_t centreY) {
  const fui::Rect drawn = toybox::iconInk(icon);
  blit(screen, icon, static_cast<int16_t>(centreX - drawn.width / 2 - drawn.x),
       static_cast<int16_t>(centreY - drawn.height / 2 - drawn.y));
}

void hairline(toybox::Screen& screen, const int16_t x, const int16_t y, const int16_t width) {
  screen.target().fill(fui::makeRect(x, y, width, toybox::kHairline), fui::Paint::solid(fui::Color::Black));
}

// --- The band ------------------------------------------------------------------

// No inset under the rule: every screen here places its first ink itself.
void band(toybox::Screen& screen, const char* title, const fui::ActionId trailing, const char* trailingLabel) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  header.sidePadding = screen.theme().headerSidePadding;
  if (trailing == ActionMenu) {
    header.trailingIcon = fui::bitmapFromIcon(icon_go_settings_32);
  } else if (trailingLabel != nullptr) {
    header.trailingLabel = trailingLabel;
    header.trailingText = ink(toybox::kSmallFont, fui::TextAlign::Center, 1, fui::Color::White);
  }
  if (trailing != fui::NO_ACTION) {
    header.trailingAction = trailing;
    header.trailingStyles = toybox::bandOutlineStyles();
    header.trailingRadius = toybox::kPillRadius / 2;
  }
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{0, kMargin, kMargin, kMargin});
}

// The width the body has once the band is drawn, for a pass that measures
// before it.
int16_t bodyWidth(toybox::Screen& screen) { return static_cast<int16_t>(screen.device().width - 2 * kMargin); }

// --- A place's name --------------------------------------------------------------
//
// Laid out in lines this file draws one at a time, so a line can end after a
// hyphen ("Saint-Remy-en-Bouzemont-" over "Saint-Genest-et-Isson") where the
// component would break only at a space and cut the rest. Fitted down a
// ladder of the slots it is given, which are bold reading cuts wherever a
// place is drawn: a long name gets smaller, never lighter, and never reaches a
// face without its accents.

struct PlaceText {
  std::string lines[2];
  int count = 0;
  fui::FontId font = toybox::kBodyFont;
  int16_t inkHeight = 0;   // cap top of the first line to the baseline of the last
  int16_t advance = 0;     // baseline to baseline
  int16_t largestInk = 0;  // one line at the largest rung, which sizes a box it sits in
};

bool fitsIn(const fui::DrawTarget& target, const fui::FontId font, const std::string& text, const int16_t width) {
  return target.measureText(font, text.c_str(), ink(font)).width <= width;
}

// The furthest a line from `start` can run and fit, ending before a space or
// after a hyphen; npos when not even its first word fits. `next` is where the
// line after it starts.
size_t lineEnd(const fui::DrawTarget& target, const fui::FontId font, const std::string& text, const size_t start,
               const int16_t width, size_t& next) {
  size_t best = std::string::npos;
  for (size_t i = start; i <= text.size(); ++i) {
    size_t end = 0;
    size_t resume = 0;
    if (i == text.size()) {
      end = resume = i;
    } else if (text[i] == ' ') {
      end = i;
      resume = i + 1;
    } else if (text[i] == '-' && i > start) {
      end = resume = i + 1;
    } else {
      continue;
    }
    if (end <= start) continue;
    if (!fitsIn(target, font, text.substr(start, end - start), width)) break;
    best = end;
    next = resume;
  }
  return best;
}

size_t skipSpaces(const std::string& text, size_t at) {
  while (at < text.size() && text[at] == ' ') ++at;
  return at;
}

// All of `text` in at most `most` lines; false when it will not go.
bool wrapPlace(const fui::DrawTarget& target, const fui::FontId font, const std::string& text, const int16_t width,
               const int most, PlaceText& out) {
  out.count = 0;
  size_t start = 0;
  while (start < text.size()) {
    if (out.count == most) return false;
    size_t next = 0;
    const size_t end = lineEnd(target, font, text, start, width, next);
    if (end == std::string::npos) return false;
    out.lines[out.count++] = text.substr(start, end - start);
    start = skipSpaces(text, next);
  }
  return out.count > 0;
}

// The last resort: whole lines while they break, then the rest cut where a
// line could have ended and marked, so no word is broken in half; on a
// character only when not even its first word leaves room.
void cutPlace(const fui::DrawTarget& target, const fui::FontId font, const std::string& text, const int16_t width,
              const int most, PlaceText& out) {
  out.count = 0;
  size_t start = 0;
  while (out.count < most - 1 && start < text.size() && !fitsIn(target, font, text.substr(start), width)) {
    size_t next = 0;
    const size_t end = lineEnd(target, font, text, start, width, next);
    if (end == std::string::npos) break;
    out.lines[out.count++] = text.substr(start, end - start);
    start = skipSpaces(text, next);
  }
  std::string rest = text.substr(start);
  if (!fitsIn(target, font, rest, width)) {
    static constexpr const char* kEllipsis = "...";
    const auto trimmed = [](std::string head) {
      while (!head.empty() && (head.back() == ' ' || head.back() == ',' || head.back() == '-')) head.pop_back();
      return head;
    };
    size_t keep = std::string::npos;
    for (size_t end = 1; end < rest.size(); ++end) {
      if (rest[end] != ' ' && rest[end - 1] != '-') continue;
      const std::string head = trimmed(rest.substr(0, end));
      if (head.empty()) continue;
      if (!fitsIn(target, font, head + kEllipsis, width)) break;
      keep = end;
    }
    if (keep != std::string::npos) {
      rest.resize(keep);
    } else {
      while (!rest.empty() && !fitsIn(target, font, rest + kEllipsis, width)) {
        size_t cut = rest.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(rest[cut]) & 0xC0) == 0x80) --cut;
        rest.resize(cut);
      }
    }
    rest = trimmed(rest) + kEllipsis;
  }
  out.lines[out.count++] = rest;
}

PlaceText fitPlace(toybox::Screen& screen, const char* text, const int16_t width, const int most,
                   const fui::FontId* rungs, const int rungCount) {
  const fui::DrawTarget& target = screen.target();
  const std::string whole = text != nullptr ? text : "";
  PlaceText place;
  bool fitted = false;
  for (int i = 0; i < rungCount && !fitted; ++i) {
    place.font = rungs[i];
    fitted = wrapPlace(target, rungs[i], whole, width, most, place);
  }
  if (!fitted) cutPlace(target, place.font, whole, width, most, place);
  place.inkHeight = toybox::inkHeight(target, place.font, place.count);
  place.advance = target.lineHeight(place.font);
  place.largestInk = toybox::inkHeight(target, rungs[0]);
  return place;
}

void drawPlace(toybox::Screen& screen, const PlaceText& place, const int16_t x, const int16_t inkTop,
               const int16_t width) {
  for (int i = 0; i < place.count; ++i) {
    textAt(screen, x, static_cast<int16_t>(inkTop + i * place.advance), width, place.font, place.lines[i].c_str());
  }
}

// The two bold cuts kPlacePass binds, largest first.
constexpr fui::FontId kPlaceRungs[] = {toybox::kBodyFont, toybox::kSmallFont};

// --- The main screen, by ink ---------------------------------------------------------
//
// The composition is centred between the band's rule and the panel's foot, so
// the space it does not need is placed, equally above and below, instead of
// being handed to whichever element came last.

// The place, four lines of reading and two of state, the rule, the rows; or
// the place and the blocks of a screen with no week.
constexpr int kBlocks = 1 + 5 + 1 + kRows;
using Column = toybox::InkColumn<kBlocks>;

int16_t spanTop() { return static_cast<int16_t>(toybox::kChromeHeight); }
int16_t spanBottom(toybox::Screen& screen) { return screen.device().height; }

// What the radio did, on a line of its own under the reading, so neither it
// nor the time beside it is ever cut to fit the other.
const char* statusWords(const Status status) {
  switch (status) {
    case Status::Updating:
      return "UPDATING...";
    case Status::Offline:
      return "OFFLINE";
    case Status::Failed:
      return "COULD NOT UPDATE";
    case Status::Current:
      break;
  }
  return "";
}

// A main screen with no week: what is missing, what can be done about it,
// and how old the card's forecast is when there is one.
struct NoWeek {
  const char* headline;
  const char* message;
  const char* button;  // null when trying again cannot help yet
  bool saysStatus;     // a forecast is on the card, so the radio's state means something
};

NoWeek noWeekFor(const MainModel& model) {
  if (model.stale) {
    return NoWeek{"NO FORECAST FOR TODAY", "", model.status == Status::Updating ? nullptr : "UPDATE NOW", true};
  }
  switch (model.status) {
    case Status::Updating:
      return NoWeek{"GETTING THE FORECAST", "", nullptr, false};
    case Status::Offline:
      return NoWeek{"NO FORECAST YET", "Connect to Wi-Fi to get one.", "TRY AGAIN", false};
    case Status::Failed:
      return NoWeek{"COULD NOT GET THE FORECAST", "", "TRY AGAIN", false};
    case Status::Current:
      break;
  }
  return NoWeek{"NO FORECAST YET", "", "TRY AGAIN", false};
}

int16_t noWeekBlock(toybox::Screen& screen, const MainModel& model, const int16_t placeInk) {
  const fui::Rect body = screen.body();
  const fui::DrawTarget& target = screen.target();
  const NoWeek words = noWeekFor(model);
  const char* status = words.saysStatus ? statusWords(model.status) : "";
  const int headLines = linesOf(screen, words.headline, toybox::kUiFont, body.width, 2);
  const int messageLines = words.message[0] ? linesOf(screen, words.message, toybox::kSmallFont, body.width, 3) : 0;
  Column column;
  const int place = column.add(0, placeInk);
  const int head = column.add(kGroupGap, toybox::inkHeight(target, toybox::kUiFont, headLines));
  const int message =
      messageLines ? column.add(kLineGap, toybox::inkHeight(target, toybox::kSmallFont, messageLines)) : -1;
  const int state = status[0] ? column.add(kLineGap, capOf(screen, toybox::kSmallFont)) : -1;
  const int updated = model.updated[0] ? column.add(kLineGap, capOf(screen, toybox::kSmallFont)) : -1;
  const int retry = words.button ? column.add(kGroupGap, toybox::kPillHeight) : -1;
  column.centre(spanTop(), spanBottom(screen), kMargin);

  textAt(screen, body.x, column.top(head), body.width, toybox::kUiFont, words.headline,
         static_cast<uint8_t>(headLines));
  if (message >= 0) {
    textAt(screen, body.x, column.top(message), body.width, toybox::kSmallFont, words.message,
           static_cast<uint8_t>(messageLines));
  }
  if (state >= 0) textAt(screen, body.x, column.top(state), body.width, toybox::kSmallFont, status);
  if (updated >= 0) textAt(screen, body.x, column.top(updated), body.width, toybox::kSmallFont, model.updated);
  if (retry >= 0) {
    fui::ButtonProps button;
    button.label = words.button;
    button.action = ActionRetry;
    button.text = toybox::buttonText(screen.theme());
    button.radius = toybox::kPillRadius;
    screen.button(button,
                  fui::makeRect(body.x, column.top(retry), static_cast<int16_t>(body.width / 2), toybox::kPillHeight));
  }
  return column.top(place);
}

// "H 9  L -3  SNOW 70%" (with a drawn minus and rings), measured once and
// drawn from the same numbers. Jersey's space is a couple of pixels, so the
// gaps are set rather than typed: a word space between a letter and its value,
// two gutters between one pair and the next.
struct RangeLine {
  std::string high;
  std::string low;
  std::string chance;
  std::string pct;
  int16_t space = 0;  // between a label and its value
  int16_t pair = 0;   // between one label and value and the next
};

RangeLine rangeLine(toybox::Screen& screen, const MainModel& model, const fui::FontId font) {
  RangeLine line;
  line.high = highText(model.today);
  line.low = lowText(model.today);
  if (model.today.rain >= 0) {
    line.chance = model.chance[0] ? model.chance : "RAIN";
    line.pct = rainLabel(model.today.rain);
  }
  line.space = max16(kGutter / 2, capOf(screen, font) / 3);
  line.pair = static_cast<int16_t>(kGutter * 2);
  return line;
}

void drawRangeLine(toybox::Screen& screen, const RangeLine& line, int16_t x, const int16_t inkTop,
                   const fui::FontId font) {
  // Not `word`: Arduino defines word(...) as a macro, and a device build
  // expands it.
  const auto label = [&screen, &x, inkTop, font](const char* text) {
    const int16_t width = widthOf(screen, font, text);
    textAt(screen, x, inkTop, static_cast<int16_t>(width + 1), font, text);
    x = static_cast<int16_t>(x + width);
  };
  label("H");
  x = static_cast<int16_t>(x + line.space);
  tempAt(screen, x, inkTop, font, line.high);
  x = static_cast<int16_t>(x + tempWidth(screen, font, line.high) + line.pair);
  label("L");
  x = static_cast<int16_t>(x + line.space);
  tempAt(screen, x, inkTop, font, line.low);
  x = static_cast<int16_t>(x + tempWidth(screen, font, line.low) + line.pair);
  if (line.chance.empty()) return;
  label(line.chance.c_str());
  x = static_cast<int16_t>(x + line.space);
  label(line.pct.c_str());
}

std::string nowText(const MainModel& model) { return model.hasNow ? number(model.now) : std::string("--"); }

// The week's columns, measured from THIS week's strings, a gutter apart:
// day | sky | chance | low | the bar | high. Nothing is reserved for a string
// that is not on screen, so no dead band opens between two columns, and a
// week with no skies at all has no sky column.
struct WeekColumns {
  int16_t nameX = 0;
  int16_t skyX = 0;  // the sky column, as wide as this week's widest sky ink
  int16_t skyW = 0;
  int16_t rainX = 0;
  int16_t lowRight = 0;  // lows are right-aligned on this edge
  int16_t barX = 0;
  int16_t barW = 0;
  int16_t highRight = 0;  // and highs on this one, the screen's right edge
};

WeekColumns weekColumns(toybox::Screen& screen, const MainModel& model, const int16_t left, const int16_t right) {
  int16_t nameW = 0, skyW = 0, rainW = 0, lowW = 0, highW = 0;
  for (int i = 0; i < model.dayCount && i < kRows; ++i) {
    const DayRow& day = model.days[i];
    nameW = max16(nameW, widthOf(screen, toybox::kUiFont, day.name));
    if (const freeink::Icon* sky = skyBitmap(day.icon, 32)) skyW = max16(skyW, toybox::iconInk(*sky).width);
    rainW = max16(rainW, widthOf(screen, toybox::kSmallFont, rainLabel(day.rain).c_str()));
    lowW = max16(lowW, tempWidth(screen, toybox::kUiFont, lowText(day)));
    highW = max16(highW, tempWidth(screen, toybox::kUiFont, highText(day)));
  }
  WeekColumns c;
  c.nameX = left;
  c.skyX = static_cast<int16_t>(left + nameW + kGutter);
  c.skyW = skyW;
  c.rainX = skyW > 0 ? static_cast<int16_t>(c.skyX + skyW + kGutter) : c.skyX;
  const int16_t lowX = rainW > 0 ? static_cast<int16_t>(c.rainX + rainW + kGutter) : c.rainX;
  c.lowRight = static_cast<int16_t>(lowX + lowW);
  c.highRight = right;
  c.barX = static_cast<int16_t>(c.lowRight + kGutter);
  c.barW = static_cast<int16_t>(right - highW - kGutter - c.barX);
  return c;
}

int16_t weekBlock(toybox::Screen& screen, const MainModel& model, const int16_t placeInk) {
  const fui::Rect body = screen.body();
  const int16_t right = static_cast<int16_t>(body.x + body.width);
  const char* status = statusWords(model.status);
  const int days = model.dayCount < kRows ? model.dayCount : kRows;
  // A row is its text's capitals: the rhythm is the words', and each sky is
  // centred on them (a 32px sky's ink is at most 29, so it reaches two pixels
  // into the row gap either side and never near a neighbour).
  const int16_t rowBand = capOf(screen, toybox::kUiFont);
  const int16_t smallCap = capOf(screen, toybox::kSmallFont);

  // The whole composition, gaps first; when it will not fit with a margin to
  // spare, the rows close up to a gutter, then the groups to two.
  int rowsAt[kRows] = {};
  int place = 0, hero = 0, sky = -1, range = 0, state = -1, updated = -1, rule = -1;
  Column column;
  const int16_t room = static_cast<int16_t>(spanBottom(screen) - spanTop() - 2 * kMargin);
  for (const int16_t tighten : {int16_t{0}, int16_t{1}, int16_t{2}}) {
    const int16_t rowGap = tighten ? kLineGap : kRowGap;
    const int16_t groupGap = tighten > 1 ? kRowGap : kGroupGap;
    column = Column{};
    place = column.add(0, placeInk);
    hero = column.add(groupGap, capOf(screen, toybox::kDisplayFont));
    sky = model.sky[0] ? column.add(kLineGap, capOf(screen, toybox::kUiFont)) : -1;
    range = column.add(kLineGap, capOf(screen, toybox::kUiFont));
    state = status[0] ? column.add(kLineGap, smallCap) : -1;
    updated = model.updated[0] ? column.add(kLineGap, smallCap) : -1;
    rule = days > 0 ? column.add(groupGap, toybox::kHairline) : -1;
    for (int i = 0; i < days; ++i) rowsAt[i] = column.add(i == 0 ? groupGap : rowGap, rowBand);
    if (column.height() <= room) break;
  }
  column.centre(spanTop(), spanBottom(screen), kMargin);

  // The reading: the number at the left edge, its sky with its ink centred on
  // the number's capitals at the right edge, then the lines under it.
  const int16_t heroTop = column.top(hero);
  tempAt(screen, body.x, heroTop, toybox::kDisplayFont, nowText(model));
  if (const freeink::Icon* icon = skyBitmap(model.nowIcon, 88)) {
    const fui::Rect drawn = toybox::iconInk(*icon);
    iconCentred(screen, *icon, static_cast<int16_t>(right - drawn.width / 2),
                static_cast<int16_t>(heroTop + capOf(screen, toybox::kDisplayFont) / 2));
  }
  if (sky >= 0) textAt(screen, body.x, column.top(sky), body.width, toybox::kUiFont, model.sky);
  drawRangeLine(screen, rangeLine(screen, model, toybox::kUiFont), body.x, column.top(range), toybox::kUiFont);
  if (state >= 0) textAt(screen, body.x, column.top(state), body.width, toybox::kSmallFont, status);
  if (updated >= 0) textAt(screen, body.x, column.top(updated), body.width, toybox::kSmallFont, model.updated);
  if (rule < 0) return column.top(place);
  hairline(screen, body.x, column.top(rule), body.width);

  // One scale for the whole week, today included, so every bar is comparable.
  int lo = 0, hi = 0;
  bool any = false;
  const auto take = [&lo, &hi, &any](const DayRow& d) {
    if (d.hasLow) {
      lo = any ? (d.low < lo ? d.low : lo) : d.low;
      hi = any ? (d.low > hi ? d.low : hi) : d.low;
      any = true;
    }
    if (d.hasHigh) {
      lo = any ? (d.high < lo ? d.high : lo) : d.high;
      hi = any ? (d.high > hi ? d.high : hi) : d.high;
      any = true;
    }
  };
  take(model.today);
  for (int i = 0; i < days; ++i) take(model.days[i]);
  if (hi == lo) ++hi;

  // The week: text on one baseline per row, skies and bars centred on its caps.
  const WeekColumns cols = weekColumns(screen, model, body.x, right);
  for (int i = 0; i < days; ++i) {
    const DayRow& day = model.days[i];
    const int16_t top = column.top(rowsAt[i]);
    const int16_t mid = static_cast<int16_t>(top + rowBand / 2);
    const int16_t baseline = static_cast<int16_t>(top + rowBand);
    textAt(screen, cols.nameX, top, widthOf(screen, toybox::kUiFont, day.name), toybox::kUiFont, day.name);
    if (const freeink::Icon* icon = skyBitmap(day.icon, 32)) {
      iconCentred(screen, *icon, static_cast<int16_t>(cols.skyX + cols.skyW / 2), mid);
    }
    const std::string rain = rainLabel(day.rain);
    if (!rain.empty()) {
      textAt(screen, cols.rainX, static_cast<int16_t>(baseline - smallCap),
             widthOf(screen, toybox::kSmallFont, rain.c_str()), toybox::kSmallFont, rain.c_str());
    }
    const std::string low = lowText(day);
    const std::string high = highText(day);
    tempAt(screen, static_cast<int16_t>(cols.lowRight - tempWidth(screen, toybox::kUiFont, low)), top, toybox::kUiFont,
           low);
    tempAt(screen, static_cast<int16_t>(cols.highRight - tempWidth(screen, toybox::kUiFont, high)), top,
           toybox::kUiFont, high);
    if (cols.barW <= 0) continue;
    screen.target().fill(fui::makeRect(cols.barX, static_cast<int16_t>(mid - 3), cols.barW, 6),
                         fui::Paint::dither(fui::Color::LightGray));
    if (day.hasLow && day.hasHigh) {
      const int16_t x0 = static_cast<int16_t>(cols.barX + static_cast<long>(cols.barW) * (day.low - lo) / (hi - lo));
      const int16_t x1 = static_cast<int16_t>(cols.barX + static_cast<long>(cols.barW) * (day.high - lo) / (hi - lo));
      screen.target().fill(fui::makeRect(x0, static_cast<int16_t>(mid - 6), max16(8, x1 - x0), 12),
                           fui::Paint::solid(fui::Color::Black), 6);
    }
  }
  return column.top(place);
}

// --- Search ----------------------------------------------------------------------

constexpr int16_t kPageCell = 44;

// What was typed, from its END: the letters going in are the ones that have
// to stay in view, so a long search loses its start behind an ellipsis.
std::string typedTail(const fui::DrawTarget& target, const char* query, const int16_t width) {
  std::string tail = query != nullptr ? query : "";
  if (fitsIn(target, toybox::kBodyFont, tail, width)) return tail;
  static constexpr const char* kEllipsis = "...";
  while (!tail.empty() && !fitsIn(target, toybox::kBodyFont, kEllipsis + tail, width)) {
    size_t cut = 1;
    while (cut < tail.size() && (static_cast<unsigned char>(tail[cut]) & 0xC0) == 0x80) ++cut;
    tail.erase(0, cut);
  }
  return kEllipsis + tail;
}

// One page of places from `first`, as many as fit in `room`.
int pageFrom(const int16_t* heights, const int count, const int first, const int16_t room) {
  int end = first + 1;
  int used = heights[first];
  while (end < count && used + kRowGap + heights[end] <= room) {
    used += kRowGap + heights[end];
    ++end;
  }
  return end;
}

// The ink height of each place's name and where the layout put it, carried
// from the pass that measures names to the pass that draws them.
struct Names {
  int16_t ink[weather::kMaxMatches] = {};
  int16_t top[weather::kMaxMatches] = {};
  int16_t x = 0;
};

SearchLayout buildSearch(toybox::Screen& screen, const SearchModel& model, Names& names) {
  band(screen, model.firstRun ? "CHOOSE A CITY" : "CHANGE CITY", fui::NO_ACTION, nullptr);
  const fui::Rect body = screen.body();
  const fui::DrawTarget& target = screen.target();
  const int16_t bottom = static_cast<int16_t>(body.y + body.height - model.keyboardHeight);
  const int16_t smallCap = capOf(screen, toybox::kSmallFont);
  SearchLayout layout;

  // The field: an outlined box around what has been typed, its caret after the
  // last letter while the keyboard is up. Tapping it raises the keyboard.
  const int16_t typedCap = capOf(screen, toybox::kBodyFont);
  const int16_t pad = static_cast<int16_t>(kGutter * 3 / 2);
  const fui::Rect field = fui::makeRect(body.x, static_cast<int16_t>(body.y + kGroupGap), body.width,
                                        static_cast<int16_t>(typedCap + 2 * pad));
  screen.target().stroke(field, fui::Paint::solid(fui::Color::Black), 2, toybox::kPillRadius / 2);
  const int16_t textX = static_cast<int16_t>(field.x + 14);
  const int16_t textW = static_cast<int16_t>(field.width - 28);
  const bool empty = model.query == nullptr || model.query[0] == '\0';
  if (empty && model.keyboardHeight == 0) {
    textAt(screen, textX, static_cast<int16_t>(field.y + (field.height - smallCap) / 2), textW, toybox::kSmallFont,
           "Tap here to type a city");
  } else {
    constexpr int16_t kCaretRoom = 8;
    const std::string shown = typedTail(target, model.query, static_cast<int16_t>(textW - kCaretRoom));
    const int16_t typedW = widthOf(screen, toybox::kBodyFont, shown.c_str());
    const int16_t textTop = static_cast<int16_t>(field.y + pad);
    textAt(screen, textX, textTop, static_cast<int16_t>(typedW + 1), toybox::kBodyFont, shown.c_str());
    if (model.keyboardHeight > 0) {
      screen.target().fill(fui::makeRect(static_cast<int16_t>(textX + typedW + 3), static_cast<int16_t>(textTop - 3), 3,
                                         static_cast<int16_t>(typedCap + 6)),
                           fui::Paint::solid(fui::Color::Black));
    }
  }
  screen.frame().hit(field, ActionField);

  // Sentences under it, by ink: the first a group under the field, each next a
  // line under the one before.
  int16_t y = static_cast<int16_t>(field.y + field.height + kGroupGap);
  bool first = true;
  const auto say = [&screen, &y, &first, body](const char* text, const fui::FontId font) {
    if (!first) y = static_cast<int16_t>(y + kLineGap);
    first = false;
    const int lines = linesOf(screen, text, font, body.width, 3);
    textAt(screen, body.x, y, body.width, font, text, static_cast<uint8_t>(lines));
    y = static_cast<int16_t>(y + toybox::inkHeight(screen.target(), font, lines));
  };

  // With the keyboard up the field is being edited, and places found for what
  // it said before would answer a question nobody is asking any more.
  const SearchModel::State state =
      model.state == SearchModel::State::Results && model.keyboardHeight > 0 ? SearchModel::State::Typing : model.state;
  switch (state) {
    case SearchModel::State::Typing:
      say("Type at least two letters of a city, then GO.", toybox::kSmallFont);
      return layout;
    case SearchModel::State::Searching:
      say("Searching...", toybox::kBodyFont);
      return layout;
    case SearchModel::State::NoMatch: {
      const std::string head = std::string("No place called \"") + model.query + "\".";
      say(head.c_str(), toybox::kBodyFont);
      say("Check the spelling, or try a larger town nearby.", toybox::kSmallFont);
      return layout;
    }
    case SearchModel::State::Failed:
      say("Could not search.", toybox::kBodyFont);
      if (model.offline) say("Not connected to Wi-Fi.", toybox::kSmallFont);
      return layout;
    case SearchModel::State::Results:
      break;
  }

  // Each place: its name (measured and drawn in kPlacePass, like the place on
  // every other screen), its region a line under it when it has one. A place
  // with no region is one line tall, not two.
  const int count = model.rowCount < weather::kMaxMatches ? model.rowCount : weather::kMaxMatches;
  if (count <= 0) return layout;
  names.x = body.x;
  int16_t heights[weather::kMaxMatches] = {};
  for (int i = 0; i < count; ++i) {
    heights[i] = static_cast<int16_t>(names.ink[i] + (model.rows[i].region[0] ? kLineGap + smallCap : 0));
  }

  // Pages, so every place can be reached: rows from the field to the foot,
  // and when they do not all fit, to a group above a bar of page numbers.
  const int16_t barTop = static_cast<int16_t>(bottom - kPageCell);
  int starts[weather::kMaxMatches + 1] = {};
  int pages = 0;
  const auto paginate = [&](const int16_t room) {
    pages = 0;
    for (int at = 0; at < count; at = pageFrom(heights, count, at, room)) starts[pages++] = at;
    starts[pages] = count;
  };
  paginate(static_cast<int16_t>(bottom - y));
  if (pages > 1) paginate(static_cast<int16_t>(barTop - kGroupGap - y));
  layout.pageCount = pages;
  layout.page = model.page < 0 ? 0 : (model.page >= pages ? pages - 1 : model.page);
  layout.first = starts[layout.page];
  layout.shown = starts[layout.page + 1] - layout.first;

  for (int i = layout.first; i < layout.first + layout.shown; ++i) {
    if (i > layout.first) hairline(screen, body.x, static_cast<int16_t>(y - kRowGap / 2), body.width);
    names.top[i] = y;
    if (model.rows[i].region[0]) {
      const std::string region = toybox::fitLines(target, model.rows[i].region, body.width, 1, ink(toybox::kSmallFont));
      textAt(screen, body.x, static_cast<int16_t>(y + names.ink[i] + kLineGap), body.width, toybox::kSmallFont,
             region.c_str());
    }
    // The whole row answers, out to the rules either side of it.
    screen.frame().hit(fui::makeRect(body.x, static_cast<int16_t>(y - kRowGap / 2), body.width,
                                     static_cast<int16_t>(heights[i] + kRowGap)),
                       ActionMatch, static_cast<int16_t>(i));
    y = static_cast<int16_t>(y + heights[i] + kRowGap);
  }

  // The page numbers, the shelf's way: a cell a thumb wide for each, filled
  // for this page and outlined for the others, each a door to its page.
  if (pages > 1) {
    const int16_t clusterX = static_cast<int16_t>(body.x + (body.width - kPageCell * pages) / 2);
    for (int p = 0; p < pages; ++p) {
      const fui::Rect door =
          fui::makeRect(static_cast<int16_t>(clusterX + p * kPageCell), barTop, kPageCell, kPageCell);
      const fui::Rect cell = fui::makeRect(static_cast<int16_t>(door.x + 2), static_cast<int16_t>(door.y + 2),
                                           static_cast<int16_t>(kPageCell - 4), static_cast<int16_t>(kPageCell - 4));
      const bool here = p == layout.page;
      if (here) {
        screen.target().fill(cell, fui::Paint::solid(fui::Color::Black), 6);
      } else {
        screen.target().stroke(cell, fui::Paint::solid(fui::Color::Black), 2, 6);
      }
      const std::string label = number(p + 1);
      textAt(screen, cell.x, static_cast<int16_t>(cell.y + (cell.height - typedCap) / 2), cell.width, toybox::kBodyFont,
             label.c_str(), 1, fui::TextAlign::Center, here ? fui::Color::White : fui::Color::Black);
      screen.frame().hit(door, ActionPage, static_cast<int16_t>(p));
    }
  }
  return layout;
}

}  // namespace

// --- Main --------------------------------------------------------------------------

void drawMain(toybox::Screen& screen, const MainModel& model, const Rebind& bind) {
  bind(kPlacePass);
  band(screen, "WEATHER", ActionMenu, nullptr);
  const fui::Rect body = screen.body();
  const PlaceText place = fitPlace(screen, model.place, body.width, 2, kPlaceRungs, 2);
  bind(kDataPass);
  const int16_t placeTop =
      model.hasForecast ? weekBlock(screen, model, place.inkHeight) : noWeekBlock(screen, model, place.inkHeight);
  bind(kPlacePass);
  drawPlace(screen, place, body.x, placeTop, body.width);
}

// --- Search ---------------------------------------------------------------------------

SearchLayout drawSearch(toybox::Screen& screen, const SearchModel& model, const Rebind& bind) {
  // The names first, in the place's faces, so a long one steps down to bold
  // 12 rather than to the regular cut the regions are set in; then the rest;
  // then the names where the rest put them.
  const int count = model.rowCount < weather::kMaxMatches ? model.rowCount : weather::kMaxMatches;
  const int16_t width = bodyWidth(screen);
  Names names;
  bind(kPlacePass);
  for (int i = 0; i < count; ++i) {
    names.ink[i] = fitPlace(screen, model.rows[i].name, width, 2, kPlaceRungs, 2).inkHeight;
  }
  bind(kSearchPass);
  const SearchLayout layout = buildSearch(screen, model, names);
  bind(kPlacePass);
  for (int i = layout.first; i < layout.first + layout.shown; ++i) {
    drawPlace(screen, fitPlace(screen, model.rows[i].name, width, 2, kPlaceRungs, 2), names.x, names.top[i], width);
  }
  return layout;
}

// --- Menu ------------------------------------------------------------------------------

void drawMenu(toybox::Screen& screen, const MenuModel& model, const Rebind& bind) {
  // The city row: the name a gutter in from its left edge, the chevron a
  // gutter in from its right, a gutter between them.
  constexpr int16_t kChevron = 32;
  const int16_t rowWidth = bodyWidth(screen);
  const int16_t nameW = static_cast<int16_t>(rowWidth - kChevron - 3 * kGutter);

  bind(kPlacePass);
  const PlaceText place = fitPlace(screen, model.place, nameW, 2, kPlaceRungs, 2);

  bind(kMenuPass);
  band(screen, "WEATHER", ActionCloseMenu, "DONE");
  const fui::Rect body = screen.body();
  const int16_t captionCap = capOf(screen, toybox::kSmallFont);
  const int16_t pad = static_cast<int16_t>(kGutter * 3 / 2);
  int16_t y = static_cast<int16_t>(body.y + kGroupGap);
  const auto caption = [&screen, &y, body, captionCap](const char* text) {
    textAt(screen, body.x, y, body.width, toybox::kSmallFont, text);
    y = static_cast<int16_t>(y + captionCap + kLineGap);
  };

  // The city: the whole row is the door back to the search. It is at least a
  // line of the largest cut tall, so a name that steps down does not move
  // what is under it, and two lines when the name takes two.
  caption("CITY");
  const fui::Rect row =
      fui::makeRect(body.x, y, body.width, static_cast<int16_t>(max16(place.largestInk, place.inkHeight) + 2 * pad));
  screen.target().stroke(row, fui::Paint::solid(fui::Color::Black), toybox::kHairline, toybox::kPillRadius / 2);
  iconCentred(screen, icon_opens_32, static_cast<int16_t>(row.x + row.width - kGutter - kChevron / 2),
              static_cast<int16_t>(row.y + row.height / 2));
  screen.frame().hit(row, ActionChangeCity);
  const int16_t placeX = static_cast<int16_t>(row.x + kGutter);
  const int16_t placeTop = static_cast<int16_t>(row.y + (row.height - place.inkHeight) / 2);
  y = static_cast<int16_t>(row.y + row.height + kGroupGap);

  // Celsius or Fahrenheit: two segments, the one in force filled and inert.
  caption("UNITS");
  {
    const int16_t half = static_cast<int16_t>((body.width - kGutter) / 2);
    struct Segment {
      const char* label;
      fui::ActionId action;
      bool on;
    };
    const Segment segments[] = {{"CELSIUS", ActionCelsius, !model.fahrenheit},
                                {"FAHRENHEIT", ActionFahrenheit, model.fahrenheit}};
    for (int i = 0; i < 2; ++i) {
      fui::ButtonProps button;
      button.label = segments[i].label;
      button.action = segments[i].on ? fui::NO_ACTION : segments[i].action;
      button.styles = segments[i].on ? toybox::invertedStyles() : toybox::rowStyles();
      button.text = toybox::buttonText(screen.theme());
      button.radius = toybox::kPillRadius / 2;
      screen.button(button, fui::makeRect(static_cast<int16_t>(body.x + i * (half + kGutter)), y,
                                          i == 0 ? half : static_cast<int16_t>(body.width - half - kGutter),
                                          toybox::kPillHeight));
    }
    y = static_cast<int16_t>(y + toybox::kPillHeight + kGroupGap);
  }

  // An update, and when the forecast on the card was read.
  caption("FORECAST");
  {
    fui::ButtonProps update;
    update.label = "UPDATE NOW";
    update.action = ActionRefresh;
    update.styles = toybox::rowStyles();
    update.text = toybox::buttonText(screen.theme());
    update.radius = toybox::kPillRadius / 2;
    screen.button(update, fui::makeRect(body.x, y, body.width, toybox::kPillHeight));
    y = static_cast<int16_t>(y + toybox::kPillHeight + kLineGap);
    if (model.updated[0]) textAt(screen, body.x, y, body.width, toybox::kSmallFont, model.updated);
  }

  bind(kPlacePass);
  drawPlace(screen, place, placeX, placeTop, nameW);
}

// --- Skies ---------------------------------------------------------------------------------

const freeink::Icon* skyBitmap(const weather::Icon icon, const int size) {
  // One row per sky, one column per generated size: 32 for the rows, 88 for
  // the reading, whose ink then fits inside the number's capitals.
  static const freeink::Icon* const kTable[][2] = {
      {&icon_wx_sun_32, &icon_wx_sun_88},
      {&icon_wx_moon_32, &icon_wx_moon_88},
      {&icon_wx_cloud_sun_32, &icon_wx_cloud_sun_88},
      {&icon_wx_cloud_moon_32, &icon_wx_cloud_moon_88},
      {&icon_wx_cloud_32, &icon_wx_cloud_88},
      {&icon_wx_fog_32, &icon_wx_fog_88},
      {&icon_wx_drizzle_32, &icon_wx_drizzle_88},
      {&icon_wx_rain_32, &icon_wx_rain_88},
      {&icon_wx_snow_32, &icon_wx_snow_88},
      {&icon_wx_thunder_32, &icon_wx_thunder_88},
  };
  static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<size_t>(weather::Icon::None),
                "one row per weather::Icon that has a picture");
  const size_t row = static_cast<size_t>(icon);
  if (row >= sizeof(kTable) / sizeof(kTable[0])) return nullptr;
  return kTable[row][size >= 88 ? 1 : 0];
}

}  // namespace weatherui
