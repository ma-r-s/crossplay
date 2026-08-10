#include "YahtzeeScreens.h"

#include <FreeInkUIIcon.h>

#include <cstdio>
#include <cstdlib>

#include "../link/LinkScreens.h"
#include "../ui/ToyboxIcons.h"

namespace yzui {

namespace {

namespace yz = yahtzee;

// --- TEMP ART PASS ----------------------------------------------------------
// Runtime layout switches so one build renders every candidate for Mario to
// pick from (ART_MENU / ART_HOWTO / ART_BOARD = 1 or 2, 0 = shipping layout).
// The losing layouts and this switch are deleted together in the commit that
// keeps the winner.
int artVariant(const char* name) {
#if defined(SIMULATOR)
  const char* value = std::getenv(name);
  return value == nullptr ? 0 : std::atoi(value);
#else
  (void)name;
  return 0;
#endif
}
int menuVariant() {
  static const int variant = artVariant("ART_MENU");
  return variant;
}
int howToVariant() {
  static const int variant = artVariant("ART_HOWTO");
  return variant;
}
int boardVariant() {
  static const int variant = artVariant("ART_BOARD");
  return variant;
}

void artChrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  const fui::Rect band = screen.device().screen();
  screen.target().fill(fui::makeRect(0, toybox::kHeaderHeight + 4, band.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// TEMP ART PASS: the record the menu candidates draw. The real thing needs the
// save-file decision; these stand in so the layouts can be judged on a device
// that has been played.
constexpr int kDemoPlayed = 9;
constexpr int kDemoWon = 5;
constexpr int kDemoBest = 287;
constexpr int kDemoYahtzees = 3;
constexpr uint8_t kDemoBestHand[5] = {4, 4, 4, 4, 4};

// The dice band, directly under the header.
constexpr int16_t kDieSize = 62;
constexpr int16_t kDieGap = 12;
constexpr int16_t kDiceBandHeight = kDieSize;

// The scorecard. Fifteen lines: six upper, the bonus, seven lower, the total.
// This is the reason the table is not built from the list component -- a
// ListProps row is 62px and six of those would be the whole card.
//
// Thirty-SIX, not thirty-three. Thirty-three was justified as "the number that
// makes all of them fit", and it fit with forty-six pixels left over that were
// never assigned to anything: the table is laid out from an absolute top and
// the capsule comes off takeBottom, so the slack between them belonged to
// neither. Thirty-six spends forty-five of it, which also takes the row from
// 3.8mm to 4.15mm at 220ppi -- and this row is the only irreversible tap in the
// game, so it should not have been the smallest target on the screen.
constexpr int16_t kLineHeight = 36;
constexpr int16_t kColumnHeaderHeight = 22;
// Where the two score columns end. The name has everything to the left of them.
constexpr int16_t kYourRight = 336;
constexpr int16_t kTheirRight = 464;
constexpr int16_t kBoxWidth = 62;

// Line indices in the drawn table. Categories occupy their own index up to the
// bonus, which is inserted after the upper section, so a category's LINE is not
// its index once you are past Sixes.
constexpr int kBonusLine = yz::kUpperEnd;
constexpr int kTotalLine = yz::kCategories + 1;
constexpr int kLines = yz::kCategories + 2;

int16_t contentTop() { return static_cast<int16_t>(toybox::kHeaderHeight + toybox::kGutter); }
int16_t tableTop() {
  return static_cast<int16_t>(contentTop() + kDiceBandHeight + toybox::kGutter + kColumnHeaderHeight);
}

// A category's line in the table: itself, or one lower once the bonus line has
// been passed.
constexpr int lineOfCategory(const int category) { return category < yz::kUpperEnd ? category : category + 1; }

int16_t diceLeft(const fui::DeviceContext& device) {
  const int16_t width = static_cast<int16_t>(yz::kDice * kDieSize + (yz::kDice - 1) * kDieGap);
  return static_cast<int16_t>((device.width - width) / 2);
}

// One die face, drawn with pips. Pips rather than a numeral because a die is a
// die: the count is read without reading, and five of them side by side are a
// hand rather than a number.
void drawDie(toybox::Screen& screen, const fui::Rect& box, const int face, const bool held) {
  // TEMP ART PASS, board candidate 1: a kept die inverts. The board already
  // uses inversion for "this one counts double" nowhere, so the strongest mark
  // the panel has goes to the one state a player keeps misreading.
  const bool inverted = boardVariant() == 1 && held;
  screen.target().fill(box, fui::Paint::solid(inverted ? fui::Color::Black : fui::Color::White));
  screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kRule);

  constexpr int16_t kPip = 9;
  const int16_t left = static_cast<int16_t>(box.x + 13);
  const int16_t mid = static_cast<int16_t>(box.x + box.width / 2 - kPip / 2);
  const int16_t right = static_cast<int16_t>(box.x + box.width - 13 - kPip);
  const int16_t top = static_cast<int16_t>(box.y + 13);
  const int16_t centre = static_cast<int16_t>(box.y + box.height / 2 - kPip / 2);
  const int16_t bottom = static_cast<int16_t>(box.y + box.height - 13 - kPip);

  // Which of the nine positions each face lights, as the die is actually
  // printed. A table rather than six branches, so a wrong face is a wrong entry
  // rather than a wrong shape.
  static const uint16_t kPips[7] = {
      0,            // unused
      0b000010000,  // 1
      0b100000001,  // 2
      0b100010001,  // 3
      0b101000101,  // 4
      0b101010101,  // 5
      0b101101101,  // 6
  };
  const int16_t xs[3] = {left, mid, right};
  const int16_t ys[3] = {top, centre, bottom};
  const uint16_t bits = kPips[face >= 1 && face <= yz::kFaces ? face : 1];
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      if ((bits & (1u << (8 - (row * 3 + column)))) == 0) continue;
      screen.target().fill(fui::makeRect(xs[column], ys[row], kPip, kPip),
                           fui::Paint::solid(inverted ? fui::Color::White : fui::Color::Black));
    }
  }

  // A held die is framed OUTSIDE its own edge, so it grows rather than shrinks.
  // The same rule the Checkers board learned: an inset frame eats the thing it
  // is marking and makes it look smaller than its neighbours.
  //
  // With a GAP. The frame used to sit flush against the die's own 3px stroke
  // and the two merged into one 7px rule -- measured, border ink 57..63
  // contiguous -- so a held die read as heavier rather than bigger. The design
  // language names this exact failure: two parallel strokes a few pixels apart
  // on a 1-bit panel merge into one thick line, and these were zero apart.
  if (!held || inverted) return;
  constexpr int16_t kGap = 4;
  const int16_t inset = static_cast<int16_t>(toybox::kFrame + kGap);
  screen.target().stroke(
      fui::makeRect(static_cast<int16_t>(box.x - inset), static_cast<int16_t>(box.y - inset),
                    static_cast<int16_t>(box.width + inset * 2), static_cast<int16_t>(box.height + inset * 2)),
      fui::Paint::solid(fui::Color::Black), toybox::kFrame);
}

const char* categoryName(const int index) {
  static const char* const kNames[yz::kCategories] = {
      "ONES",           "TWOS",       "THREES",         "FOURS",          "FIVES",   "SIXES",  "THREE OF A KIND",
      "FOUR OF A KIND", "FULL HOUSE", "SMALL STRAIGHT", "LARGE STRAIGHT", "YAHTZEE", "CHANCE",
  };
  return kNames[index];
}

// A number right-aligned at `right`, on its line.
void drawNumber(toybox::Screen& screen, const int16_t right, const int16_t y, const int value, const bool bold) {
  char text[8];
  std::snprintf(text, sizeof(text), "%d", value);
  fui::TextStyle style;
  style.font = bold ? toybox::kBodyFont : toybox::kSmallFont;
  style.align = fui::TextAlign::Right;
  screen.target().text(fui::makeRect(static_cast<int16_t>(right - 90), y, static_cast<int16_t>(90 - 8), kLineHeight),
                       text, style);
}

// An empty box: the printed card's box, waiting for a number.
// The design language says hairlines are for grid cells and secondary outlines,
// "never for anything you want noticed". This outline is the ENTIRE carrier of
// pencilled-versus-written, which is a decision-changing distinction: a
// committed zero and a previewed zero were the same glyph separated by one
// pixel. kRule is the lightest weight that survives 220ppi for load-bearing
// work.
void drawEmptyBox(toybox::Screen& screen, const int16_t right, const int16_t y) {
  screen.target().stroke(fui::makeRect(static_cast<int16_t>(right - kBoxWidth), static_cast<int16_t>(y + 4), kBoxWidth,
                                       static_cast<int16_t>(kLineHeight - 12)),
                         fui::Paint::solid(fui::Color::Black), toybox::kRule);
}

// A box with what you WOULD score in it, which is the whole decision in this
// game. Drawn as a number inside the printed box, because that is exactly what
// it is: a pencilled figure in a box you have not committed to. A filled box
// has no outline, so committed and pencilled never look alike.
void drawPreview(toybox::Screen& screen, const int16_t right, const int16_t y, const int value) {
  drawEmptyBox(screen, right, y);
  char text[8];
  std::snprintf(text, sizeof(text), "%d", value);
  // Right-aligned INSIDE the box, matching drawNumber, so a column of thirteen
  // rows has one edge. Centring the previews and right-aligning the committed
  // numbers put their digits 25px apart on a 62px column and turned the column
  // into a zigzag that alternated row by row.
  fui::TextStyle style;
  style.font = toybox::kSmallFont;
  style.align = fui::TextAlign::Right;
  screen.target().text(
      fui::makeRect(static_cast<int16_t>(right - kBoxWidth), y, static_cast<int16_t>(kBoxWidth - 8), kLineHeight), text,
      style);
}

}  // namespace

fui::Rect dieRect(const fui::DeviceContext& device, const int index) {
  return fui::makeRect(static_cast<int16_t>(diceLeft(device) + index * (kDieSize + kDieGap)), contentTop(), kDieSize,
                       kDieSize);
}

int dieAt(const fui::DeviceContext& device, const int x, const int y) {
  // The band is widened by the held frame, because the frame is part of the die
  // as far as a finger is concerned. It used to sit entirely outside the hit
  // rect, so tapping the visible black border of a held die -- the obvious
  // place to press to release it -- did nothing. That is the pill()/pillRect()
  // defect building-apps.md uses as its worked example.
  constexpr int16_t kReach = toybox::kFrame + 4;
  const int16_t top = static_cast<int16_t>(contentTop() - kReach);
  if (y < top || y >= contentTop() + kDiceBandHeight + kReach) return -1;
  const int16_t left = static_cast<int16_t>(diceLeft(device) - kReach);
  if (x < left) return -1;
  const int offset = x - diceLeft(device) + kReach;
  const int slot = (offset - kReach) / (kDieSize + kDieGap);
  if (slot < 0 || slot >= yz::kDice) return -1;
  // The gap between two dice still belongs to neither, minus the reach each
  // die now claims for its frame. A tap in what is left must miss rather than
  // land on whichever die happens to be to its left: these are five separate
  // switches and a fat-fingered hold is a wasted turn.
  const int within = offset - kReach - slot * (kDieSize + kDieGap);
  if (within < -kReach || within >= kDieSize + kReach) return -1;
  return slot;
}

fui::Rect rowRect(const fui::DeviceContext& device, const int category) {
  const int line = lineOfCategory(category);
  return fui::makeRect(toybox::kMargin, static_cast<int16_t>(tableTop() + line * kLineHeight),
                       static_cast<int16_t>(device.width - toybox::kMargin * 2), kLineHeight);
}

int categoryAt(const fui::DeviceContext& device, const int x, const int y) {
  if (x < toybox::kMargin || x >= device.width - toybox::kMargin) return -1;
  const int16_t top = tableTop();
  if (y < top) return -1;
  const int line = (y - top) / kLineHeight;
  if (line < 0 || line >= kLines) return -1;
  if (line == kBonusLine || line == kTotalLine) return -1;
  return line < kBonusLine ? line : line - 1;
}

// TEMP ART PASS: both how-to candidates gain a fourth page, the one the
// shipping version never says -- what the lower boxes pay, and the Joker.
int howToPages() { return howToVariant() > 0 ? 4 : 3; }

namespace {

// TEMP ART PASS, menu candidate 1: the documented band order. Record, rule,
// the best hand as the ornament, doors anchored bottom.
void buildMenuBands(toybox::Screen& screen, const MenuModel& model) {
  artChrome(screen, "YAHTZEE");

  char record[64];
  std::snprintf(record, sizeof(record), "%d PLAYED   %d WON   BEST %d", kDemoPlayed, kDemoWon, kDemoBest);
  const fui::Rect line = screen.takeTop(26);
  fui::TextStyle small;
  small.font = toybox::kTileFont;
  small.align = fui::TextAlign::Left;
  screen.target().text(line, record, small);
  screen.target().fill(fui::makeRect(line.x, static_cast<int16_t>(line.bottom() + 6), line.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  fui::ListItem rows[static_cast<int>(MenuRow::Count)] = {};
  rows[static_cast<int>(MenuRow::Play)].label = "PLAY";
  rows[static_cast<int>(MenuRow::Play)].actionValue = static_cast<int16_t>(MenuRow::Play);
  rows[static_cast<int>(MenuRow::PlayNearby)].label = "PLAY NEARBY";
  rows[static_cast<int>(MenuRow::PlayNearby)].subtitle = model.nearbyName;
  rows[static_cast<int>(MenuRow::PlayNearby)].actionValue = static_cast<int16_t>(MenuRow::PlayNearby);
  rows[static_cast<int>(MenuRow::HowTo)].label = "HOW TO PLAY";
  rows[static_cast<int>(MenuRow::HowTo)].actionValue = static_cast<int16_t>(MenuRow::HowTo);

  const int selected = model.selected < 0 ? 0 : model.selected;
  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(MenuRow::Count);
  list.selectedIndex = static_cast<int16_t>(selected);
  list.action = ActionMenuRow;
  const int count = static_cast<int>(MenuRow::Count);
  const int16_t listHeight =
      static_cast<int16_t>(count * toybox::kRowHeight + (count - 1) * toybox::kGutter / 2 + toybox::kGutter);
  const fui::Rect content = screen.contentRect();
  const fui::Rect listBand =
      fui::makeRect(content.x, static_cast<int16_t>(content.bottom() - listHeight), content.width, listHeight);
  screen.list(list, listHeight, fui::LayoutAnchor::Bottom);
  toybox::iconAtRowRight(screen, listBand, static_cast<int>(MenuRow::PlayNearby), 0, linkui::nearbyMark(),
                         selected == static_cast<int>(MenuRow::PlayNearby));

  // The hand your record is proudest of, at the real dice size, held the way
  // the board holds a kept hand.
  const fui::DeviceContext device = screen.device();
  const int16_t areaTop = static_cast<int16_t>(line.bottom() + 6 + toybox::kRule);
  const int16_t room = static_cast<int16_t>(listBand.y - areaTop);
  const int16_t blockH = static_cast<int16_t>(kDieSize + 14 + 24);
  const int16_t top = static_cast<int16_t>(areaTop + (room > blockH ? (room - blockH) / 2 : 12));
  for (int i = 0; i < yz::kDice; ++i) {
    const fui::Rect box =
        fui::makeRect(static_cast<int16_t>(diceLeft(device) + i * (kDieSize + kDieGap)), top, kDieSize, kDieSize);
    drawDie(screen, box, kDemoBestHand[i], false);
  }
  char capText[32];
  std::snprintf(capText, sizeof(capText), "%d YAHTZEES ROLLED", kDemoYahtzees);
  fui::TextStyle cap;
  cap.font = toybox::kTileFont;
  cap.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(content.x, static_cast<int16_t>(top + kDieSize + 14), content.width, 24), capText,
                       cap);
}

// TEMP ART PASS, menu candidate 2: the dungeon's shape, with the number as the
// centrepiece -- the personal best in the display cut, the hand that earned it
// under it, one solid PLAY.
void buildMenuScoreboard(toybox::Screen& screen, const MenuModel& model) {
  artChrome(screen, "YAHTZEE");

  fui::ButtonProps play;
  play.label = "PLAY";
  play.action = ActionMenuRow;
  play.value = static_cast<int16_t>(MenuRow::Play);
  play.text = toybox::buttonText(screen.theme());
  play.radius = toybox::kPillRadius;
  screen.button(play, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect lesser = screen.takeBottom(toybox::kRowHeight, toybox::kGutter);
  const int16_t half = static_cast<int16_t>((lesser.width - toybox::kGutter) / 2);
  fui::ButtonProps nearby;
  nearby.label = "NEARBY";
  nearby.action = ActionMenuRow;
  nearby.value = static_cast<int16_t>(MenuRow::PlayNearby);
  nearby.styles = toybox::rowStyles();
  screen.button(nearby, fui::makeRect(lesser.x, lesser.y, half, lesser.height));
  fui::ButtonProps how;
  how.label = "HOW TO PLAY";
  how.action = ActionMenuRow;
  how.value = static_cast<int16_t>(MenuRow::HowTo);
  how.styles = toybox::rowStyles();
  screen.button(how,
                fui::makeRect(static_cast<int16_t>(lesser.x + half + toybox::kGutter), lesser.y, half, lesser.height));

  const fui::Rect area = screen.body();
  const fui::DeviceContext device = screen.device();
  const int16_t blockH = static_cast<int16_t>(24 + 64 + 18 + kDieSize + 18 + 24);
  const int16_t blockTop = static_cast<int16_t>(area.y + (area.height > blockH ? (area.height - blockH) / 2 : 0));

  fui::TextStyle label;
  label.font = toybox::kTileFont;
  label.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(area.x, blockTop, area.width, 24), "PERSONAL BEST", label);

  char best[8];
  std::snprintf(best, sizeof(best), "%d", kDemoBest);
  fui::TextStyle big;
  big.font = toybox::kDisplayFont;
  big.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(blockTop + 28), area.width, 64), best, big);

  const int16_t diceTop = static_cast<int16_t>(blockTop + 28 + 64 + 18);
  for (int i = 0; i < yz::kDice; ++i) {
    const fui::Rect box =
        fui::makeRect(static_cast<int16_t>(diceLeft(device) + i * (kDieSize + kDieGap)), diceTop, kDieSize, kDieSize);
    drawDie(screen, box, kDemoBestHand[i], false);
  }

  char tally[48];
  std::snprintf(tally, sizeof(tally), "%d PLAYED   %d WON   %d YAHTZEES", kDemoPlayed, kDemoWon, kDemoYahtzees);
  fui::TextStyle rec;
  rec.font = toybox::kTileFont;
  rec.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(diceTop + kDieSize + 18), area.width, 24), tally,
                       rec);
}

}  // namespace

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  // TEMP ART PASS: candidate layouts, picked by env var, deleted with the
  // losers once Mario chooses.
  if (menuVariant() == 1) {
    buildMenuBands(screen, model);
    return;
  }
  if (menuVariant() == 2) {
    buildMenuScoreboard(screen, model);
    return;
  }

  fui::HeaderProps header;
  header.title = "YAHTZEE";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::ListItem rows[static_cast<int>(MenuRow::Count)] = {};
  rows[static_cast<int>(MenuRow::Play)].label = "PLAY";
  rows[static_cast<int>(MenuRow::Play)].actionValue = static_cast<int16_t>(MenuRow::Play);
  rows[static_cast<int>(MenuRow::PlayNearby)].label = "PLAY NEARBY";
  rows[static_cast<int>(MenuRow::PlayNearby)].subtitle = model.nearbyName;
  rows[static_cast<int>(MenuRow::PlayNearby)].actionValue = static_cast<int16_t>(MenuRow::PlayNearby);
  rows[static_cast<int>(MenuRow::HowTo)].label = "HOW TO PLAY";
  rows[static_cast<int>(MenuRow::HowTo)].actionValue = static_cast<int16_t>(MenuRow::HowTo);

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(MenuRow::Count);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionMenuRow;
  const fui::Rect band = screen.body();
  screen.list(list);

  toybox::iconAtRowRight(screen, band, static_cast<int>(MenuRow::PlayNearby), 0, linkui::nearbyMark(),
                         model.selected == static_cast<int>(MenuRow::PlayNearby));
}

namespace {

// The page's picture: a hand of dice for the first three, the pay table for
// the fourth. `top` is the diagram's own top edge; returns its height.
int16_t howToDiagram(toybox::Screen& screen, const int16_t top, const int page) {
  const fui::DeviceContext device = screen.device();
  if (page < 3) {
    // Real dice at the real size: a hand being kept, then the hand that hand
    // becomes, then a top-half run worth the bonus.
    static const uint8_t kFaces[3][yz::kDice] = {{5, 5, 2, 5, 3}, {5, 5, 5, 5, 1}, {4, 4, 4, 4, 4}};
    static const uint8_t kHeld[3] = {0b01011, 0b01111, 0b11111};
    for (int i = 0; i < yz::kDice; ++i) {
      const fui::Rect box =
          fui::makeRect(static_cast<int16_t>(diceLeft(device) + i * (kDieSize + kDieGap)), top, kDieSize, kDieSize);
      drawDie(screen, box, kFaces[page][i], (kHeld[page] & (1 << i)) != 0);
    }
    return kDieSize;
  }

  // TEMP ART PASS: the pay table, name left, value right, the card's own
  // hairline between rows.
  static const char* const kNames[7] = {"THREE OF A KIND", "FOUR OF A KIND", "FULL HOUSE", "SMALL STRAIGHT",
                                        "LARGE STRAIGHT",  "YAHTZEE",        "CHANCE"};
  static const char* const kValues[7] = {"ALL FIVE DICE", "ALL FIVE DICE", "25", "30", "40", "50", "ALL FIVE DICE"};
  constexpr int16_t kRowH = 32;
  const int16_t width = static_cast<int16_t>(device.width - toybox::kMargin * 2);
  fui::TextStyle nameStyle;
  nameStyle.font = toybox::kTileFont;
  nameStyle.align = fui::TextAlign::Left;
  fui::TextStyle valueStyle;
  valueStyle.font = toybox::kTileFont;
  valueStyle.align = fui::TextAlign::Right;
  for (int i = 0; i < 7; ++i) {
    const int16_t rowY = static_cast<int16_t>(top + i * kRowH);
    screen.target().text(fui::makeRect(toybox::kMargin, rowY, 260, kRowH), kNames[i], nameStyle);
    screen.target().text(fui::makeRect(toybox::kMargin, rowY, width, kRowH), kValues[i], valueStyle);
    if (i > 0) {
      screen.target().fill(fui::makeRect(toybox::kMargin, static_cast<int16_t>(rowY - 2), width, toybox::kHairline),
                           fui::Paint::dither(fui::Color::DarkGray));
    }
  }
  return kRowH * 7;
}

const char* const kHowToLines[] = {
    "ROLL FIVE DICE. TAP THE ONES YOU WANT TO KEEP AND ROLL AGAIN, UP TO THREE ROLLS.",
    "THEN PUT THE HAND IN ONE BOX ON YOUR CARD. EVERY BOX IS USED EXACTLY ONCE.",
    "SIXTY-THREE IN THE TOP HALF EARNS THIRTY-FIVE MORE. THIRTEEN TURNS EACH.",
    "A SECOND YAHTZEE SCORES 100 MORE, AND MAY FORCE THE ONE MARKED BOX.",
};

// TEMP ART PASS, how-to candidate 2: the tutorial shape jaipur ships.
void buildHowToGuide(toybox::Screen& screen, const HowToModel& model) {
  const int pages = howToPages();
  const int page = model.page < 0 ? 0 : (model.page >= pages ? pages - 1 : model.page);

  char progress[16];
  std::snprintf(progress, sizeof(progress), "%d OF %d", page + 1, pages);
  artChrome(screen, "HOW TO PLAY", progress);
  const fui::Rect body = screen.body();
  screen.frame().hit(body, ActionHowToNext, 0);

  constexpr int16_t kDot = 14;
  constexpr int16_t kDotGap = 10;
  const int16_t dotRow = static_cast<int16_t>(pages * kDot + (pages - 1) * kDotGap);
  const int16_t dotX = static_cast<int16_t>(body.x + (body.width - dotRow) / 2);
  const int16_t dotY = static_cast<int16_t>(body.bottom() - kDot);
  for (int i = 0; i < pages; ++i) {
    const fui::Rect dotAt = fui::makeRect(static_cast<int16_t>(dotX + i * (kDot + kDotGap)), dotY, kDot, kDot);
    if (i == page) {
      screen.target().fill(dotAt, fui::Paint::solid(fui::Color::Black), 7);
    } else {
      screen.target().stroke(dotAt, fui::Paint::dither(fui::Color::DarkGray), toybox::kHairline, 7);
    }
  }
  fui::TextStyle tapLine;
  tapLine.font = toybox::kTileFont;
  tapLine.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(dotY - 34), body.width, 22),
                       page + 1 == pages ? "TAP TO FINISH" : "TAP TO CONTINUE", tapLine);

  static const char* const kTitles[] = {"THE ROLLS", "THE CARD", "THE BONUS", "WHAT PAYS WHAT"};
  fui::TextStyle title;
  title.font = toybox::kDisplayFont;
  title.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(body.x, body.y, body.width, 46), kTitles[page], title);

  fui::TextStyle cap;
  cap.font = toybox::kUiFont;
  cap.align = fui::TextAlign::Center;
  cap.maxLines = 4;
  const int16_t capTop = static_cast<int16_t>(body.bottom() - 56 - 204);
  screen.target().text(fui::makeRect(body.x, capTop, body.width, 200), kHowToLines[page], cap);

  const int16_t bandTop = static_cast<int16_t>(body.y + 92);
  const int16_t height = page < 3 ? kDieSize : 32 * 7;
  int16_t top = static_cast<int16_t>(bandTop + (capTop - bandTop - height) / 2);
  if (top < bandTop) top = bandTop;
  howToDiagram(screen, top, page);
}

}  // namespace

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  const int page = model.page < 0 ? 0 : (model.page >= howToPages() ? howToPages() - 1 : model.page);

  if (howToVariant() == 2) {
    buildHowToGuide(screen, model);
    return;
  }

  // TEMP ART PASS, how-to candidate 1: the page counter moves into the black
  // band and the diagram centres in the room it frees.
  const bool banded = howToVariant() == 1;
  char progress[16];
  if (banded) {
    std::snprintf(progress, sizeof(progress), "%d OF %d", page + 1, howToPages());
    artChrome(screen, "HOW TO PLAY", progress);
  } else {
    fui::HeaderProps header;
    header.title = "HOW TO PLAY";
    header.borderEdges = fui::EdgesNone;
    screen.header(header);
    screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  }

  fui::ButtonProps next;
  next.label = page + 1 < howToPages() ? "NEXT" : "GOT IT";
  next.action = ActionHowToNext;
  screen.button(next, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect area = screen.body();
  if (!banded) {
    char counterText[8];
    std::snprintf(counterText, sizeof(counterText), "%d/%d", page + 1, howToPages());
    fui::TextStyle counter;
    counter.font = toybox::kSmallFont;
    counter.align = fui::TextAlign::Right;
    screen.target().text(fui::makeRect(area.x, area.y, area.width, 20), counterText, counter);
  }

  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  body.maxLines = 4;
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + (banded ? 0 : 24)), area.width, 200),
                       kHowToLines[page], body);

  if (banded) {
    const int16_t bandTop = static_cast<int16_t>(area.y + 210);
    const int16_t height = page < 3 ? kDieSize : 32 * 7;
    int16_t top = static_cast<int16_t>(bandTop + (area.bottom() - bandTop - height) / 2);
    if (top < bandTop) top = bandTop;
    howToDiagram(screen, top, page);
    return;
  }

  howToDiagram(screen, static_cast<int16_t>(area.y + 210), page);
}

void buildCard(toybox::Screen& screen, const CardModel& model) {
  fui::HeaderProps header;
  header.title = "YAHTZEE";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const fui::DeviceContext device = screen.device();
  const yz::Game& game = model.game;
  const yz::Stage stage = yz::stageOf(game);
  const yz::Card& yours = model.game.card[model.seat];
  const yz::Card& theirs = model.game.card[1 - model.seat];

  // The capsule is the ROLL button while a roll is left, and the instruction
  // when it is not. One control, two jobs, because they are never both live:
  // there is nothing to say while you can still roll, and nothing to roll once
  // you cannot.
  fui::ButtonProps action;
  char rollLabel[24];
  // A capsule that cannot act DIMS. It was solid black with a white knockout,
  // identical to a live ROLL, so a player learns "the black bar is the button"
  // and then taps it twice for nothing. Jaipur and Battleship both already do
  // this on the same control.
  if (!model.yourTurn) {
    action.label = "THEIR TURN";
    action.action = fui::NO_ACTION;
    action.styles = toybox::disabledButtonStyles();
  } else if (stage == yz::Stage::Spent) {
    action.label = model.joker ? "YAHTZEE! TAKE THE MARKED BOX" : "TAKE A BOX";
    action.action = fui::NO_ACTION;
    action.styles = toybox::disabledButtonStyles();
  } else {
    const int left = yz::kRollsPerTurn - game.rollsUsed;
    if (model.joker) {
      // The one moment in Yahtzee where a free row refuses a tap. Unexplained,
      // twelve boxes silently stop showing previews and the card reads as
      // simply dead.
      std::snprintf(rollLabel, sizeof(rollLabel), "YAHTZEE! ONE BOX ONLY");
    } else {
      std::snprintf(rollLabel, sizeof(rollLabel), left == yz::kRollsPerTurn ? "ROLL" : "ROLL AGAIN (%d LEFT)", left);
    }
    action.label = rollLabel;
    action.action = ActionRoll;
  }
  action.borderEdges = fui::EdgesNone;
  const fui::Rect capsule = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  screen.button(
      action, model.opponentName != nullptr ? linkui::withOpponentFace(screen, capsule, model.opponentName) : capsule);

  // The dice. Before the first roll of a turn they are the previous player's
  // and mean nothing, so they are not drawn at all: five stale dice sitting
  // above a live card is the screen telling you something untrue.
  if (stage != yz::Stage::Fresh) {
    for (int i = 0; i < yz::kDice; ++i) {
      drawDie(screen, dieRect(device, i), game.die[i], (game.held & (1 << i)) != 0);
    }
  } else {
    fui::TextStyle prompt;
    prompt.font = toybox::kBodyFont;
    prompt.align = fui::TextAlign::Center;
    screen.target().text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(contentTop() + kDiceBandHeight / 2 - 12),
                                       static_cast<int16_t>(device.width - toybox::kMargin * 2), 26),
                         model.yourTurn ? "ROLL TO START YOUR TURN" : "THEY ARE ROLLING", prompt);
  }

  // Column headers.
  fui::TextStyle small;
  small.font = toybox::kSmallFont;
  small.align = fui::TextAlign::Right;
  const int16_t headerY = static_cast<int16_t>(tableTop() - kColumnHeaderHeight);
  screen.target().text(fui::makeRect(static_cast<int16_t>(kYourRight - 90), headerY, 90, kColumnHeaderHeight), "YOU",
                       small);
  screen.target().text(fui::makeRect(static_cast<int16_t>(kTheirRight - 90), headerY, 90, kColumnHeaderHeight),
                       model.opponentName != nullptr ? "THEM" : "THEM", small);

  fui::TextStyle name;
  name.font = toybox::kSmallFont;
  name.align = fui::TextAlign::Left;

  for (int index = 0; index < yz::kCategories; ++index) {
    const fui::Rect row = rowRect(device, index);
    screen.target().text(fui::makeRect(row.x, row.y, 210, kLineHeight), categoryName(index), name);

    if (yours.box[index] != yz::kUnscored) {
      drawNumber(screen, kYourRight, row.y, yours.box[index], false);
    } else if ((model.takeable & (1u << index)) != 0) {
      // What this box would take, which is the entire decision this turn.
      drawPreview(screen, kYourRight, row.y, yz::scoreFor(yours, game.die, static_cast<yz::Category>(index)));
    } else {
      drawEmptyBox(screen, kYourRight, row.y);
    }

    if (theirs.box[index] != yz::kUnscored) {
      drawNumber(screen, kTheirRight, row.y, theirs.box[index], false);
    } else {
      drawEmptyBox(screen, kTheirRight, row.y);
    }
  }

  // The bonus line, between the sections, doing the arithmetic nobody wants to
  // do in their head. The printed card has a box for exactly this.
  const int16_t bonusY = static_cast<int16_t>(tableTop() + kBonusLine * kLineHeight);
  screen.target().fill(fui::makeRect(toybox::kMargin, bonusY, static_cast<int16_t>(device.width - toybox::kMargin * 2),
                                     toybox::kHairline),
                       fui::Paint::solid(fui::Color::Black));
  char bonusText[40];
  std::snprintf(bonusText, sizeof(bonusText), "BONUS");
  screen.target().text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(bonusY + 4), 260, kLineHeight), bonusText,
                       name);

  // BOTH columns get the same thing. It used to put your shortfall as a
  // sentence in the NAME column and their bonusEarned as a bare integer 300px
  // away -- one row carrying two quantities in two notations, and theirs was
  // uninformative besides: it read 0 for the whole game whether they were three
  // points off or mathematically out.
  for (int column = 0; column < 2; ++column) {
    const yz::Card& card = column == 0 ? yours : theirs;
    const int16_t right = column == 0 ? kYourRight : kTheirRight;
    char cell[16];
    if (yz::bonusEarned(card) > 0) {
      std::snprintf(cell, sizeof(cell), "+%d", yz::kUpperBonus);
    } else if (yz::upperBonusStillPossible(card)) {
      std::snprintf(cell, sizeof(cell), "%d MORE", yz::upperShortfall(card));
    } else {
      // Say it is gone rather than counting down toward a number that can no
      // longer arrive. A shortfall you cannot close is worse than none.
      std::snprintf(cell, sizeof(cell), "GONE");
    }
    fui::TextStyle cellStyle;
    cellStyle.font = toybox::kSmallFont;
    cellStyle.align = fui::TextAlign::Right;
    screen.target().text(fui::makeRect(static_cast<int16_t>(right - 100), static_cast<int16_t>(bonusY + 4),
                                       static_cast<int16_t>(100 - 8), kLineHeight),
                         cell, cellStyle);
  }

  // The Yahtzee bonus, on the Yahtzee row, when there is one. A hundred points
  // is the largest scoring event in the game and it landed in TOTAL with no row
  // and no event to point at.
  if (yours.yahtzeeBonuses > 0 || theirs.yahtzeeBonuses > 0) {
    const fui::Rect yahtzeeRow = rowRect(device, static_cast<int>(yz::Category::Yahtzee));
    char extra[24];
    std::snprintf(extra, sizeof(extra), "+%d", (yours.yahtzeeBonuses + theirs.yahtzeeBonuses) * yz::kYahtzeeBonus);
    fui::TextStyle mark;
    mark.font = toybox::kSmallFont;
    mark.align = fui::TextAlign::Left;
    screen.target().text(fui::makeRect(static_cast<int16_t>(yahtzeeRow.x + 150), yahtzeeRow.y, 110, kLineHeight), extra,
                         mark);
  }

  // The totals, under a rule, in the heavier face.
  const int16_t totalY = static_cast<int16_t>(tableTop() + kTotalLine * kLineHeight);
  screen.target().fill(
      fui::makeRect(toybox::kMargin, totalY, static_cast<int16_t>(device.width - toybox::kMargin * 2), toybox::kRule),
      fui::Paint::solid(fui::Color::Black));
  fui::TextStyle totalName;
  totalName.font = toybox::kBodyFont;
  totalName.align = fui::TextAlign::Left;
  screen.target().text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(totalY + 6), 210, kLineHeight), "TOTAL",
                       totalName);
  // TEMP ART PASS, board candidate 2: the leading total inverts, so who is
  // ahead reads from across the table without arithmetic.
  const int yourTotal = yz::total(yours);
  const int theirTotal = yz::total(theirs);
  if (boardVariant() == 2 && yourTotal != theirTotal) {
    const int16_t right = yourTotal > theirTotal ? kYourRight : kTheirRight;
    const fui::Rect chip = fui::makeRect(static_cast<int16_t>(right - kBoxWidth - 6), static_cast<int16_t>(totalY + 4),
                                         static_cast<int16_t>(kBoxWidth + 6), static_cast<int16_t>(kLineHeight + 2));
    screen.target().fill(chip, fui::Paint::solid(fui::Color::Black), 6);
    char text[8];
    std::snprintf(text, sizeof(text), "%d", yourTotal > theirTotal ? yourTotal : theirTotal);
    fui::TextStyle lead;
    lead.font = toybox::kBodyFont;
    lead.align = fui::TextAlign::Right;
    lead.color = fui::Color::White;
    screen.target().text(
        fui::makeRect(chip.x, static_cast<int16_t>(totalY + 6), static_cast<int16_t>(chip.width - 8), kLineHeight),
        text, lead);
    drawNumber(screen, yourTotal > theirTotal ? kTheirRight : kYourRight, static_cast<int16_t>(totalY + 6),
               yourTotal > theirTotal ? theirTotal : yourTotal, true);
  } else {
    drawNumber(screen, kYourRight, static_cast<int16_t>(totalY + 6), yourTotal, true);
    drawNumber(screen, kTheirRight, static_cast<int16_t>(totalY + 6), theirTotal, true);
  }
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  const bool won = model.yourTotal > model.theirTotal;
  const bool drawn = model.yourTotal == model.theirTotal;

  fui::HeaderProps header;
  header.title = drawn ? "A TIE" : (won ? "YOU WIN" : "THEY WIN");
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter * 2, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::ButtonProps done;
  done.label = "DONE";
  done.action = ActionDone;
  screen.button(done, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  fui::ButtonProps again;
  again.label = "PLAY AGAIN";
  again.action = ActionAgain;
  screen.button(again, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect area = screen.body();
  char line[32];
  std::snprintf(line, sizeof(line), "%d - %d", model.yourTotal, model.theirTotal);
  fui::TextStyle big;
  big.font = toybox::kDisplayFont;
  big.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + 20), area.width, 70), line, big);

  // WHERE it was won, not just that it was. Yahtzee is decided by three or four
  // boxes and a bonus, and a bare pair of totals says none of that; the losing
  // player's question is which box did it.
  fui::TextStyle small;
  small.font = toybox::kSmallFont;
  small.align = fui::TextAlign::Left;
  fui::TextStyle right = small;
  right.align = fui::TextAlign::Right;

  int16_t y = static_cast<int16_t>(area.y + 110);
  screen.target().text(fui::makeRect(area.x, y, 200, kLineHeight), "BIGGEST SWINGS", small);
  y = static_cast<int16_t>(y + kLineHeight + 4);

  // The three boxes where the gap was widest, in either direction.
  bool used[yahtzee::kCategories] = {};
  for (int shown = 0; shown < 3; ++shown) {
    int bestIndex = -1;
    int bestGap = 0;
    for (int i = 0; i < yahtzee::kCategories; ++i) {
      if (used[i]) continue;
      const int gap = model.yours.box[i] - model.theirs.box[i];
      const int size = gap < 0 ? -gap : gap;
      if (bestIndex < 0 || size > bestGap) {
        bestIndex = i;
        bestGap = size;
      }
    }
    if (bestIndex < 0 || bestGap == 0) break;
    used[bestIndex] = true;
    screen.target().text(fui::makeRect(area.x, y, 260, kLineHeight), categoryName(bestIndex), small);
    char scores[24];
    std::snprintf(scores, sizeof(scores), "%d - %d", model.yours.box[bestIndex], model.theirs.box[bestIndex]);
    screen.target().text(fui::makeRect(static_cast<int16_t>(area.x + area.width - 140), y, 140, kLineHeight), scores,
                         right);
    y = static_cast<int16_t>(y + kLineHeight);
  }

  // And the bonus, which is a 35-point swing that appears in no box at all.
  if (yahtzee::bonusEarned(model.yours) != yahtzee::bonusEarned(model.theirs)) {
    char bonus[40];
    std::snprintf(bonus, sizeof(bonus), "TOP HALF BONUS  %d - %d", yahtzee::bonusEarned(model.yours),
                  yahtzee::bonusEarned(model.theirs));
    screen.target().text(fui::makeRect(area.x, y, area.width, kLineHeight), bonus, small);
  }
}

}  // namespace yzui
