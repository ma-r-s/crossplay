#include "ForeheadScreens.h"

#include <cstdio>
#include <cstring>

#include "ForeheadCategoryIcons.h"

namespace foreheadui {

namespace {

namespace fh = forehead;

// ---------------------------------------------------------------------------
// Shared chrome

// The header band with the offset rule under it, as jaipur, yahtzee and the
// dungeon wear it. A local copy rather than a shared helper, for the reason
// LinkScreens gives: a copy is cheaper than a header dependency between apps.
void toyboxChrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  // rightLabel is drawn with subtitleText, whose theme default is black -- on
  // the black band that is an invisible label indistinguishable from one never
  // set. Jaipur paid for this discovery; see its toyboxChrome.
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  toybox::headerRule(screen);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

fui::TextStyle textStyle(const fui::FontId font, const fui::TextAlign align,
                         const fui::Color colour = fui::Color::Black) {
  fui::TextStyle style;
  style.font = font;
  // Named even when it is the default the component would apply anyway.
  // FONT_SLOT_SMALL is 0, and textStyleUnset() calls a style unset when its
  // font is 0 and every other field is default -- so a small-slot label with
  // nothing else set comes back at the theme's size, looking exactly as though
  // the assignment never happened.
  style.align = align;
  style.color = colour;
  return style;
}

void drawText(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
              const fui::TextAlign align, const toybox::CutMetrics& cut,
              const fui::Color colour = fui::Color::Black) {
  screen.target().text(toybox::inkCentred(box, cut), text, textStyle(font, align, colour));
}

// ---------------------------------------------------------------------------
// The paging pips, the shelf folder's shape.
//
// Pips rather than prev/next arrows: arrows are up to pageCount-1 taps to the
// far end and say nothing about where you are, and on this device a right
// chevron already means "opens". A small centred cluster with air round it, and
// the targets are contiguous WITHIN the cluster only, so the screen edges do
// nothing.
constexpr int16_t kPipSize = 10;
constexpr int16_t kPipStep = 34;
constexpr int16_t kPipBand = 44;

void drawPips(toybox::Screen& screen, const fui::Rect& band, const int pages, const int current,
              const fui::ActionId action) {
  if (pages <= 1) return;
  const int16_t width = static_cast<int16_t>(pages * kPipStep);
  const int16_t left = static_cast<int16_t>(band.x + (band.width - width) / 2);
  const int16_t cy = static_cast<int16_t>(band.y + band.height / 2);
  for (int page = 0; page < pages; ++page) {
    const int16_t cx = static_cast<int16_t>(left + page * kPipStep + kPipStep / 2);
    if (page == current) {
      toybox::disc(screen, cx, cy, kPipSize / 2, fui::Color::Black);
    } else {
      toybox::ring(screen, cx, cy, kPipSize / 2, toybox::kHairline + 1, fui::Color::Black, fui::Color::White);
    }
    screen.frame().hit(fui::makeRect(static_cast<int16_t>(left + page * kPipStep),
                                     static_cast<int16_t>(band.y), kPipStep, band.height),
                       action, static_cast<int16_t>(page));
  }
}

// ---------------------------------------------------------------------------
// The two key bands.
//
// This is the only screen in the fork whose labels are placed by where the
// HARDWARE is rather than by where the layout wants them, and it has to be:
// the guesser is holding the panel against their forehead and cannot see any
// of it. The bands are for the room, which reads them out.
//
// Rotated into LandscapeCounterClockwise the device's two side keys land on the
// long edges of what you are looking at -- the key that is on the physical left
// in portrait is at the BOTTOM, and the one on the physical right is at the TOP
// (GfxRenderer.cpp rotateCoordinates: portrait maps logical y to panel x, so
// the portrait right edge becomes the landscape top). So a band that runs the
// whole width of an edge cannot be pointing at the wrong key: the entire edge
// is the label.
//
// Bottom is GOT IT and top is PASS, which is the phone game's tilt-down and
// tilt-up in the only vocabulary this hardware has.
constexpr int16_t kKeyBandHeight = 54;

void drawKeyBands(toybox::Screen& screen) {
  const int16_t w = screen.device().screen().width;
  const int16_t h = screen.device().screen().height;
  const fui::Color ground = fui::Color::Black;
  const fui::Color ink = fui::Color::White;

  const fui::Rect top = fui::makeRect(0, 0, w, kKeyBandHeight);
  const fui::Rect bottom = fui::makeRect(0, static_cast<int16_t>(h - kKeyBandHeight), w, kKeyBandHeight);
  screen.target().fill(top, fui::Paint::solid(ground));
  screen.target().fill(bottom, fui::Paint::solid(ground));

  drawText(screen, top, "PASS", toybox::kBodyFont, fui::TextAlign::Center, toybox::kDisplayCut, ink);
  drawText(screen, bottom, "GOT IT", toybox::kBodyFont, fui::TextAlign::Center, toybox::kDisplayCut, ink);
}

// Touch, standing in for the two keys.
//
// Every page-key action in this fork stays reachable by touch, and this one has
// to be as well -- but not as two full halves. The guesser's fingers curl over
// the long edges to reach the keys, which is exactly where a half-screen target
// would be, so the tappable region is a band across the MIDDLE of each half,
// clear of every grip. A player who wants to play with a thumb still can; a
// player holding it normally cannot answer their own card by accident.
constexpr int16_t kTouchInsetX = 140;
constexpr int16_t kTouchInsetY = 90;

void registerKeyTouch(toybox::Screen& screen) {
  const int16_t w = screen.device().screen().width;
  const int16_t h = screen.device().screen().height;
  const int16_t bandW = static_cast<int16_t>(w - kTouchInsetX * 2);
  const int16_t bandH = static_cast<int16_t>(h / 2 - kTouchInsetY);
  screen.frame().hit(fui::makeRect(kTouchInsetX, kTouchInsetY, bandW, bandH), ActionMissed);
  screen.frame().hit(fui::makeRect(kTouchInsetX, static_cast<int16_t>(h / 2), bandW, bandH), ActionGot);
}

}  // namespace

// ---------------------------------------------------------------------------
// The card's type ladder.

CardLayout layOutCard(toybox::Screen& screen, const fui::Rect& box, const char* text) {
  CardLayout out;
  if (text == nullptr || *text == '\0') return out;
  const int length = static_cast<int>(std::strlen(text));

  // Largest first. All three are bound at once (title = 64, small = 44,
  // body = 30) precisely so this can measure them without a rebind, which a
  // freestanding builder has no way to ask for.
  const fui::FontId ladder[] = {toybox::kDisplayFont, toybox::kSmallFont, toybox::kBodyFont};

  for (const fui::FontId font : ladder) {
    const fui::TextStyle style = textStyle(font, fui::TextAlign::Center);
    const int16_t lineHeight = screen.target().lineHeight(font);

    // Greedy wrap on spaces. A break inside a word is never taken: the design
    // language settled that the day a hyphenated tile was rejected on sight.
    int16_t start[kCardMaxLines] = {};
    int16_t end[kCardMaxLines] = {};
    int16_t width[kCardMaxLines] = {};
    int lines = 0;
    int cursor = 0;
    bool fits = true;

    while (cursor < length && fits) {
      if (lines == kCardMaxLines) {
        fits = false;
        break;
      }
      int lineEnd = length;
      int accepted = -1;
      // Walk break points right to left: the last space at which the run still
      // fits is the greedy choice.
      for (int at = length; at > cursor; --at) {
        if (at != length && text[at] != ' ') continue;
        char buffer[fh::kMaxEntryLen + 1];
        const int count = at - cursor;
        if (count > fh::kMaxEntryLen) continue;
        std::memcpy(buffer, text + cursor, static_cast<size_t>(count));
        buffer[count] = '\0';
        const int16_t measured = screen.target().measureText(font, buffer, style).width;
        if (measured <= box.width) {
          lineEnd = at;
          accepted = measured;
          break;
        }
      }
      if (accepted < 0) {
        // Not even the first word fits at this cut.
        fits = false;
        break;
      }
      start[lines] = static_cast<int16_t>(cursor);
      end[lines] = static_cast<int16_t>(lineEnd);
      width[lines] = static_cast<int16_t>(accepted);
      ++lines;
      cursor = lineEnd;
      while (cursor < length && text[cursor] == ' ') ++cursor;
    }

    if (!fits || lines == 0) continue;
    // Leading is tightened to the ink rather than the line box: Jersey carries
    // a 133px line box round an 82px capital, and three of those is more than
    // the panel is tall.
    const int16_t stacked = static_cast<int16_t>(lines * lineHeight - (lines - 1) * lineHeight / 4);
    if (stacked > box.height) continue;

    out.font = font;
    out.lines = lines;
    out.lineHeight = lineHeight;
    for (int i = 0; i < lines; ++i) {
      out.start[i] = start[i];
      out.end[i] = end[i];
      out.width[i] = width[i];
    }
    return out;
  }

  // Nothing fit. The generator's length cap makes this unreachable for shipped
  // content, so it means somebody raised the cap without re-checking the card:
  // draw the smallest cut on one line and let it clip rather than draw nothing.
  out.font = toybox::kBodyFont;
  out.lines = 1;
  out.lineHeight = screen.target().lineHeight(toybox::kBodyFont);
  out.start[0] = 0;
  out.end[0] = static_cast<int16_t>(length);
  out.width[0] = box.width;
  return out;
}

namespace {

// Draws a laid-out card centred in `box`.
void drawCard(toybox::Screen& screen, const fui::Rect& box, const char* text, const CardLayout& layout,
              const fui::Color colour) {
  const int16_t gap = static_cast<int16_t>(layout.lineHeight - layout.lineHeight / 4);
  const int16_t stacked = static_cast<int16_t>(layout.lineHeight + (layout.lines - 1) * gap);
  int16_t y = static_cast<int16_t>(box.y + (box.height - stacked) / 2);

  const toybox::CutMetrics cut = layout.font == toybox::kDisplayFont ? toybox::kHugeCut
                                 : layout.font == toybox::kSmallFont ? toybox::kLargeCut
                                                                     : toybox::kDisplayCut;
  char buffer[fh::kMaxEntryLen + 1];
  for (int line = 0; line < layout.lines; ++line) {
    const int count = layout.end[line] - layout.start[line];
    if (count <= 0 || count > fh::kMaxEntryLen) continue;
    std::memcpy(buffer, text + layout.start[line], static_cast<size_t>(count));
    buffer[count] = '\0';
    drawText(screen, fui::makeRect(box.x, y, box.width, layout.lineHeight), buffer, layout.font,
             fui::TextAlign::Center, cut, colour);
    y = static_cast<int16_t>(y + gap);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// The front door (portrait)

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  const fh::CategoryInfo& category = fh::kCategories[model.category];
  toyboxChrome(screen, "FOREHEAD");

  // The doors' height is reserved up front so the slack between the record and
  // them belongs to the ornament rather than to nobody: layouts are anchored,
  // not centred. Screen::list() takes what is LEFT of the content rect, so the
  // reservation is arithmetic here and the list is drawn last.
  constexpr int16_t kDoorsHeight = 3 * toybox::kRowHeight + 2 * 4;

  // Headline: the category, and the tap that starts a round. The most common
  // action is the largest thing on the screen and needs no button.
  const fui::Rect headline = screen.takeTop(72);
  drawText(screen, headline, category.title, toybox::kDisplayFont, fui::TextAlign::Left, toybox::kDisplayCut);

  const fui::Rect hint = screen.takeTop(30);
  drawText(screen, hint, category.hint, toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut);

  // The state line carries the affordance as well as the state, because nothing
  // else on this screen offers to play: the three doors below are CATEGORY,
  // ROUND and HOW TO PLAY, so a player who does not know the headline is the
  // button has nowhere to find out.
  char state[48];
  if (model.record != nullptr && model.record->everPlayed(model.category)) {
    std::snprintf(state, sizeof(state), "TAP TO PLAY   BEST HERE %d", model.record->bestIn[model.category]);
  } else {
    std::snprintf(state, sizeof(state), "TAP TO PLAY   NEVER PLAYED");
  }
  const fui::Rect stateRow = screen.takeTop(30);
  drawText(screen, stateRow, state, toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut);

  // Everything above the rule is the start button, so a thumb landing anywhere
  // near the name plays. Derived from the rects that drew it rather than
  // recomputed: two functions that are only wrong together are the bug this
  // fork keeps re-finding.
  screen.frame().hit(fui::makeRect(headline.x, headline.y, headline.width,
                                   static_cast<int16_t>(stateRow.y + stateRow.height - headline.y)),
                     ActionReady);

  screen.takeTop(toybox::kGutter);
  const fui::Rect ruleRow = screen.takeTop(toybox::kRule);
  screen.target().fill(ruleRow, fui::Paint::solid(fui::Color::Black));
  screen.takeTop(toybox::kGutter);

  char record[64];
  if (model.record != nullptr && model.record->rounds > 0) {
    std::snprintf(record, sizeof(record), "%d ROUNDS   %d WORDS   BEST %d", model.record->rounds,
                  model.record->words, model.record->best);
  } else {
    std::snprintf(record, sizeof(record), "NO ROUNDS PLAYED YET");
  }
  drawText(screen, screen.takeTop(28), record, toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut);

  // The ornament: the last sixteen rounds as bars, in the bracketed panel the
  // chess board and the connections grid both wear.
  //
  // It is the app's own material (a row of cards) carrying the app's own data
  // (how many you got), which is the only kind of decoration this design
  // language allows. A screenshot of it is different on every device and
  // different every evening, which is the test.
  screen.takeTop(toybox::kGutter);
  const fui::Rect panel =
      screen.takeTop(static_cast<int16_t>(screen.body().height - kDoorsHeight - toybox::kGutter));
  screen.takeTop(toybox::kGutter);
  toybox::bracket(screen, panel, 22, toybox::kRule);

  if (model.record != nullptr && model.record->recentCount > 0) {
    const int peak = model.record->recentPeak();
    const int16_t inset = 26;
    const int16_t plotW = static_cast<int16_t>(panel.width - inset * 2);
    const int16_t plotH = static_cast<int16_t>(panel.height - inset * 2);
    const int16_t step = static_cast<int16_t>(plotW / fh::Record::kRecentCount);
    const int16_t barW = static_cast<int16_t>(step - 6);
    const int16_t base = static_cast<int16_t>(panel.y + inset + plotH);
    for (int i = 0; i < fh::Record::kRecentCount; ++i) {
      const int score = model.record->recentAt(i);
      if (score < 0) continue;
      const int16_t x = static_cast<int16_t>(panel.x + inset + i * step + 3);
      // A zero-score round still gets a mark: an empty column and a round never
      // played must not look the same.
      const int16_t barH = static_cast<int16_t>(score <= 0 ? 3 : 3 + (score * (plotH - 3)) / peak);
      const bool newest = i == fh::Record::kRecentCount - 1;
      const fui::Rect bar = fui::makeRect(x, static_cast<int16_t>(base - barH), barW, barH);
      screen.target().fill(bar, fui::Paint::solid(newest ? fui::Color::Black : fui::Color::White));
      screen.target().stroke(bar, fui::Paint::solid(fui::Color::Black), toybox::kHairline + 1);
    }
  } else {
    drawText(screen, panel, "YOUR SCORES SHOW UP HERE", toybox::kUiFont, fui::TextAlign::Center, toybox::kUiCut);
  }

  // The doors, quietest, at the bottom.
  char length[16];
  std::snprintf(length, sizeof(length), "%d SECONDS", model.roundSeconds);
  fui::ListItem rows[static_cast<int>(MenuRow::Count)];
  rows[static_cast<int>(MenuRow::Category)].label = "CATEGORY";
  rows[static_cast<int>(MenuRow::Category)].value = category.title;
  rows[static_cast<int>(MenuRow::Length)].label = "ROUND";
  rows[static_cast<int>(MenuRow::Length)].value = length;
  rows[static_cast<int>(MenuRow::HowTo)].label = "HOW TO PLAY";
  for (int i = 0; i < static_cast<int>(MenuRow::Count); ++i) rows[i].actionValue = static_cast<int16_t>(i);

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<int>(MenuRow::Count);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  // One action for the whole list; the row is carried in actionValue. That is
  // the component's shape, and it is why MenuRow exists rather than three ids.
  list.action = ActionMenuRow;
  screen.list(list);
}

// ---------------------------------------------------------------------------
// The category picker (portrait)

namespace {
// Nine, which is what the band holds without a footer, so seventeen lists are
// two pages rather than three with one lonely row on the last.
constexpr int kPickerRows = 9;
}

int pickerRowsPerPage() { return kPickerRows; }
int pickerPages() { return (fh::kCategoryCount + kPickerRows - 1) / kPickerRows; }

void buildPicker(toybox::Screen& screen, const PickerModel& model) {
  char right[16];
  std::snprintf(right, sizeof(right), "%d LISTS", fh::kCategoryCount);
  toyboxChrome(screen, "CATEGORY", right);

  const fui::Rect pips = screen.takeBottom(kPipBand);
  const int pages = pickerPages();
  const int page = model.page < 0 ? 0 : (model.page >= pages ? pages - 1 : model.page);
  drawPips(screen, pips, pages, page, ActionPage);

  const int first = page * kPickerRows;
  const int count = fh::kCategoryCount - first < kPickerRows ? fh::kCategoryCount - first : kPickerRows;

  // The screen is handed a SLICE, never the whole list plus an offset. The list
  // component clamps topIndex so its last screen is always full, which is right
  // for scrolling and wrong for paging: page two would repeat half of page one.
  fui::ListItem rows[kPickerRows];
  char values[kPickerRows][16];
  for (int i = 0; i < count; ++i) {
    const int index = first + i;
    rows[i].label = fh::kCategories[index].title;
    if (model.record != nullptr && model.record->everPlayed(index)) {
      std::snprintf(values[i], sizeof(values[i]), "BEST %d", model.record->bestIn[index]);
    } else {
      std::snprintf(values[i], sizeof(values[i]), "NEW");
    }
    rows[i].value = values[i];
    rows[i].icon = fui::bitmapFromIcon(*fh::categoryIcon(index));
    // The ABSOLUTE index, so a tap reports which category it is rather than
    // which row of which page.
    rows[i].actionValue = static_cast<int16_t>(index);
  }

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(count);
  // The list the player is on is shown selected, so opening the picker says
  // where you already are rather than making you remember.
  list.selectedIndex =
      model.current >= first && model.current < first + count ? static_cast<int16_t>(model.current - first) : -1;
  list.action = ActionCategoryRow;
  screen.list(list);
}

// ---------------------------------------------------------------------------
// The ready card (landscape, arriving sideways on purpose)

void buildReady(toybox::Screen& screen, const ReadyModel& model) {
  const fh::CategoryInfo& category = fh::kCategories[model.category];
  const int16_t w = screen.device().screen().width;
  const int16_t h = screen.device().screen().height;

  // The key bands are drawn here, in the positions they keep for the whole
  // round, so the one screen anybody reads before playing is also the one that
  // teaches the controls -- and it teaches them by being in the right place
  // rather than by naming a button nobody can see.
  //
  // Under cardFaces the BODY slot is the 30px cut and SMALL is the 44px cut, so
  // every cut named below is the one actually bound. A slot is not a cut: pass
  // kUiCut for a 30px slot and the label is centred against a line box 21px
  // shorter than the real one, which reads as text that hangs.
  drawKeyBands(screen);

  const int16_t top = kKeyBandHeight;
  const int16_t bottom = static_cast<int16_t>(h - kKeyBandHeight);

  int16_t y = static_cast<int16_t>(top + 40);
  drawText(screen, fui::makeRect(0, y, w, 62), category.title, toybox::kSmallFont, fui::TextAlign::Center,
           toybox::kLargeCut);
  y = static_cast<int16_t>(y + 74);
  drawText(screen, fui::makeRect(0, y, w, 42), category.hint, toybox::kBodyFont, fui::TextAlign::Center,
           toybox::kDisplayCut);

  // The instructions are anchored to the bottom band rather than stacked under
  // the title, so the card reads as a page with a footer instead of a block of
  // type with a hole under it.
  drawText(screen, fui::makeRect(0, static_cast<int16_t>(bottom - 108), w, 44), "HOLD IT ON YOUR FOREHEAD",
           toybox::kBodyFont, fui::TextAlign::Center, toybox::kDisplayCut);
  drawText(screen, fui::makeRect(0, static_cast<int16_t>(bottom - 58), w, 44), "PRESS EITHER KEY TO START",
           toybox::kBodyFont, fui::TextAlign::Center, toybox::kDisplayCut);

  // Tapping the middle also starts it, for the same reason the round accepts
  // touch: never only a button.
  screen.frame().hit(fui::makeRect(kTouchInsetX, top, static_cast<int16_t>(w - kTouchInsetX * 2),
                                   static_cast<int16_t>(h - kKeyBandHeight * 2)),
                     ActionStart);
}

// ---------------------------------------------------------------------------
// The round (landscape). Three arrangements; see the macro at the top.

void buildPlay(toybox::Screen& screen, const PlayModel& model) {
  const int16_t w = screen.device().screen().width;
  const int16_t h = screen.device().screen().height;

  drawKeyBands(screen);
  const int16_t bodyTop = kKeyBandHeight;
  const int16_t bodyH = static_cast<int16_t>(h - kKeyBandHeight * 2);

  // The clock is a bar, and no figures appear on this screen except the score.
  //
  // Chosen against a version with the seconds in figures and a version with no
  // clock at all, by rendering all three and looking. Two things decided it.
  // A bar costs FIVE forced repaints across a sixty-second round where a
  // ticking numeral costs eleven, and on a panel that takes a third of a second
  // to update, a screen that blinks every five seconds while nobody has pressed
  // anything is a screen somebody is trying to read through. And the bar is
  // legible from the sofa, which is where the people who need the clock are
  // sitting: the guesser cannot see any of this, so every element here is for
  // the room, and a 30px numeral is not a room-sized thing.
  //
  // Eight segments, never more than one segment stale.
  constexpr int16_t kBarH = 14;
  const int16_t segments = static_cast<int16_t>(fh::barSegments(model.secondsLeft, model.lengthSeconds));
  const int16_t segW = static_cast<int16_t>((w - toybox::kMargin * 2) / fh::kBarSegments);
  for (int i = 0; i < fh::kBarSegments; ++i) {
    const fui::Rect cell = fui::makeRect(static_cast<int16_t>(toybox::kMargin + i * segW),
                                         static_cast<int16_t>(bodyTop + 10), static_cast<int16_t>(segW - 4), kBarH);
    // A spent segment keeps its outline rather than disappearing. A bar that
    // only shortens says how much is left; one that also shows where it started
    // says how fast it is going, which is the thing a room shouts about.
    if (i < segments) {
      screen.target().fill(cell, fui::Paint::solid(fui::Color::Black));
    } else {
      screen.target().stroke(cell, fui::Paint::solid(fui::Color::Black), toybox::kHairline + 1);
    }
  }

  // The score sits UNDER the bar rather than beside it: the bar runs the full
  // width on purpose (it is the clock, and a clock you have to hunt for is not
  // one), so there is no room beside it and the first version overlapped.
  constexpr int16_t kChromeH = 10 + kBarH + 10;
  constexpr int16_t kScoreBand = 48;
  char scoreText[8];
  std::snprintf(scoreText, sizeof(scoreText), "%d", model.score);
  drawText(screen,
           fui::makeRect(static_cast<int16_t>(w - toybox::kMargin - 120), static_cast<int16_t>(bodyTop + kChromeH),
                         120, 44),
           scoreText, toybox::kBodyFont, fui::TextAlign::Right, toybox::kDisplayCut);

  const fui::Rect card = fui::makeRect(toybox::kMargin, static_cast<int16_t>(bodyTop + kChromeH + kScoreBand),
                                       static_cast<int16_t>(w - toybox::kMargin * 2),
                                       static_cast<int16_t>(bodyH - kChromeH - kScoreBand - 8));
  drawCard(screen, card, model.word, layOutCard(screen, card, model.word), fui::Color::Black);

  registerKeyTouch(screen);
}

// ---------------------------------------------------------------------------
// The results (landscape)

namespace {
constexpr int kResultRows = 6;
constexpr int kResultCols = 2;
constexpr int kResultsPerPage = kResultRows * kResultCols;
}  // namespace

int resultsPerPage() { return kResultsPerPage; }
int resultPages(const int cards) {
  const int pages = (cards + kResultsPerPage - 1) / kResultsPerPage;
  return pages < 1 ? 1 : pages;
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  const fh::Round* round = model.round;
  const int cards = round != nullptr ? round->cards() : 0;
  toyboxChrome(screen, fh::kCategories[model.category].title);

  const fui::Rect footer = screen.takeBottom(toybox::kPillHeight);
  const fui::Rect pips = screen.takeBottom(kPipBand);

  // Under bigNumberFaces the BODY slot is the 64px cut and SMALL is the 20px
  // one, so the score reads from BODY and everything else from SMALL. The
  // TITLE slot stays the 30px display cut, which is what keeps the header band
  // from clipping its own title in half.
  constexpr int16_t kScoreColumn = 180;
  const fui::Rect content = screen.body();
  char scoreText[8];
  std::snprintf(scoreText, sizeof(scoreText), "%d", model.score);
  drawText(screen, fui::makeRect(content.x, content.y, kScoreColumn, 140), scoreText, toybox::kBodyFont,
           fui::TextAlign::Center, toybox::kHugeCut);
  drawText(screen, fui::makeRect(content.x, static_cast<int16_t>(content.y + 146), kScoreColumn, 32),
           model.score == 1 ? "WORD" : "WORDS", toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut);
  char outOf[24];
  std::snprintf(outOf, sizeof(outOf), "OUT OF %d", cards);
  drawText(screen, fui::makeRect(content.x, static_cast<int16_t>(content.y + 180), kScoreColumn, 30), outOf,
           toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut);

  // A rule between the number and the list, so the two read as two things.
  screen.target().fill(fui::makeRect(static_cast<int16_t>(content.x + kScoreColumn), content.y, toybox::kHairline + 1,
                                     content.height),
                       fui::Paint::solid(fui::Color::Black));

  // The list. This is the screen people argue over, and e-ink holds it for free
  // while they do, so it gets the space rather than a summary line.
  const fui::Rect band =
      fui::makeRect(static_cast<int16_t>(content.x + kScoreColumn + toybox::kGutter * 2), content.y,
                    static_cast<int16_t>(content.width - kScoreColumn - toybox::kGutter * 2), content.height);
  const int pages = resultPages(cards);
  const int page = model.page < 0 ? 0 : (model.page >= pages ? pages - 1 : model.page);
  drawPips(screen, pips, pages, page, ActionResultsPage);

  const int16_t colW = static_cast<int16_t>(band.width / kResultCols);
  const int16_t rowH = static_cast<int16_t>(band.height / kResultRows);
  for (int slot = 0; slot < kResultsPerPage; ++slot) {
    const int index = page * kResultsPerPage + slot;
    if (index >= cards) break;
    const int16_t x = static_cast<int16_t>(band.x + (slot / kResultRows) * colW);
    const int16_t y = static_cast<int16_t>(band.y + (slot % kResultRows) * rowH);

    // The mark is not a tick and a cross: at 1 bit and this size a cross is two
    // diagonals that merge into a blob, so a word you got is a filled disc and
    // one you gave up on is the same disc hollow. Same shape, opposite weight,
    // no glyph to squint at.
    const fh::Mark mark = round->markAt(index);
    const int16_t cy = static_cast<int16_t>(y + rowH / 2);
    const int16_t cx = static_cast<int16_t>(x + 10);
    if (mark == fh::Mark::Got) {
      toybox::disc(screen, cx, cy, 8, fui::Color::Black);
    } else if (mark == fh::Mark::Missed) {
      toybox::ring(screen, cx, cy, 8, toybox::kHairline + 1, fui::Color::Black, fui::Color::White);
    } else {
      // The card in hand when the clock ran out. Neither, and it says so with a
      // bar rather than by looking like one you gave up on.
      screen.target().fill(fui::makeRect(static_cast<int16_t>(cx - 8), static_cast<int16_t>(cy - 2), 16, 4),
                           fui::Paint::solid(fui::Color::Black));
    }
    drawText(screen, fui::makeRect(static_cast<int16_t>(x + 26), y, static_cast<int16_t>(colW - 30), rowH),
             round->textAt(index), toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut);
  }

  // The button component takes its label style from the theme's bodyText, which
  // is the BODY slot -- and on this screen the BODY slot is the 64px card cut,
  // so both labels drew at display size in a 52px pill and ran off the bottom
  // of the panel. toybox::buttonText() moves them to the small slot, which is
  // exactly the case it exists for.
  const fui::TextStyle label = toybox::buttonText(screen.theme());
  fui::ButtonProps again;
  again.label = "PLAY AGAIN";
  again.text = label;
  again.action = ActionAgain;
  screen.button(again, fui::makeRect(footer.x, footer.y, static_cast<int16_t>(footer.width / 2 - 6), footer.height));
  fui::ButtonProps done;
  done.label = "DONE";
  done.text = label;
  done.action = ActionDone;
  screen.button(done, fui::makeRect(static_cast<int16_t>(footer.x + footer.width / 2 + 6), footer.y,
                                    static_cast<int16_t>(footer.width / 2 - 6), footer.height));
}

// ---------------------------------------------------------------------------
// How to play (portrait, paged by the side keys and by the pips)

namespace {

struct Page {
  const char* title;
  // Nine lines at the 14px cut, which is about 32 characters across this panel.
  // The 20px UI cut fits 24, and the component truncates an overrun with U+2026
  // -- a glyph Jersey does not carry, so it draws as NOTHING and the sentence
  // just stops mid-word. Two pages shipped that way before anybody rendered one.
  const char* body[9];
  // The controls page draws the two key bands underneath its text, at the size
  // and in the positions they occupy during a round. A picture of the thing
  // being explained, made of the thing itself -- and it fills a page that was
  // otherwise two-thirds empty, which on a screen that holds its image for
  // hours is a real defect rather than untidiness.
  bool keys = false;
};

// Written for somebody who has never played, in the order they need it. The
// controls come SECOND, before the scoring, because they are the only part this
// device does differently from every other version of this game.
constexpr Page kPages[] = {
    {"THE GAME",
     {"ONE PLAYER HOLDS THE DEVICE FLAT",
      "AGAINST THEIR FOREHEAD, SCREEN",
      "FACING OUT. THEY CANNOT SEE IT.",
      "",
      "EVERYBODY ELSE CAN, AND SHOUTS",
      "CLUES UNTIL THEY GET IT.",
      "",
      "SIXTY SECONDS. ONE POINT PER",
      "WORD. THAT IS THE WHOLE GAME."},
     false},
    {"THE TWO KEYS",
     {"TURN THE DEVICE SIDEWAYS AND THE",
      "TWO PAGE KEYS LAND ON THE TOP",
      "AND BOTTOM EDGES, WHERE YOUR",
      "FINGERS ALREADY ARE.",
      "",
      "THE SCREEN LABELS BOTH EDGES SO",
      "THE ROOM CAN CALL THEM OUT.",
      "",
      ""},
     true},
    {"SCORING",
     {"ONE POINT FOR EVERY WORD YOU",
      "GET. GIVING UP COSTS NOTHING",
      "BUT THE SECONDS IT TOOK.",
      "",
      "THE WORD IN YOUR HAND WHEN THE",
      "CLOCK RUNS OUT IS NEITHER, AND",
      "THE RESULTS SCREEN SAYS SO",
      "RATHER THAN QUIETLY COUNTING IT",
      "AGAINST YOU."},
     false},
    {"THE LISTS",
     {"SEVENTEEN OF THEM, AND NONE",
      "REPEATS A WORD UNTIL YOU HAVE",
      "SEEN EVERY ONE IN THE LIST.",
      "",
      "ACT IT OUT AND MAKE A SOUND ARE",
      "THE TWO THAT RUIN FRIENDSHIPS.",
      "",
      "FOR KIDS AND TRICKY ARE THE",
      "EASY AND HARD ENDS OF THE SHELF."},
     false},
};

constexpr int kPageCount = static_cast<int>(sizeof(kPages) / sizeof(kPages[0]));

}  // namespace

int howToPages() { return kPageCount; }

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  const int page = model.page < 0 ? 0 : (model.page >= kPageCount ? kPageCount - 1 : model.page);
  char right[12];
  std::snprintf(right, sizeof(right), "%d/%d", page + 1, kPageCount);
  toyboxChrome(screen, "HOW TO PLAY", right);

  const fui::Rect pips = screen.takeBottom(kPipBand);
  drawPips(screen, pips, kPageCount, page, ActionPage);

  drawText(screen, screen.takeTop(56), kPages[page].title, toybox::kDisplayFont, fui::TextAlign::Left,
           toybox::kDisplayCut);
  screen.takeTop(toybox::kGutter);

  for (const char* line : kPages[page].body) {
    const fui::Rect row = screen.takeTop(38);
    if (line != nullptr && *line != '\0') {
      drawText(screen, row, line, toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut);
    }
  }

  if (kPages[page].keys) {
    // A little device, drawn portrait-shaped so it reads as the thing in your
    // hand turned on its side: the labels sit on the long edges exactly where
    // the round puts them.
    const fui::Rect left = screen.body();
    const int16_t h = static_cast<int16_t>(left.height - toybox::kGutter);
    const int16_t w = static_cast<int16_t>(h * 8 / 5);
    const fui::Rect device =
        fui::makeRect(static_cast<int16_t>(left.x + (left.width - w) / 2), left.y, w, h);
    screen.target().stroke(device, fui::Paint::solid(fui::Color::Black), toybox::kFrame);

    const int16_t band = 40;
    const fui::Rect top = fui::makeRect(static_cast<int16_t>(device.x + toybox::kFrame),
                                        static_cast<int16_t>(device.y + toybox::kFrame),
                                        static_cast<int16_t>(device.width - toybox::kFrame * 2), band);
    const fui::Rect bottom =
        fui::makeRect(top.x, static_cast<int16_t>(device.y + device.height - toybox::kFrame - band), top.width, band);
    screen.target().fill(top, fui::Paint::solid(fui::Color::Black));
    screen.target().fill(bottom, fui::Paint::solid(fui::Color::Black));
    drawText(screen, top, "PASS", toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut,
             fui::Color::White);
    drawText(screen, bottom, "GOT IT", toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut,
             fui::Color::White);
    drawText(screen, fui::makeRect(device.x, static_cast<int16_t>(device.y + device.height / 2 - 20), device.width, 40),
             "PENGUIN", toybox::kUiFont, fui::TextAlign::Center, toybox::kUiCut);
  }

  // Anywhere below the header advances, matching the tutorial in Insider: this
  // is a deck of pages and a tap is how you turn one.
  screen.frame().hit(screen.body(), ActionHowToNext);
}

}  // namespace foreheadui
