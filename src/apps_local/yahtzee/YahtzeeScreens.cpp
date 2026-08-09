#include "YahtzeeScreens.h"

#include <FreeInkUIIcon.h>

#include <cstdio>

#include "../link/LinkScreens.h"
#include "../ui/ToyboxIcons.h"

namespace yzui {

namespace {

namespace yz = yahtzee;

// The dice band, directly under the header.
constexpr int16_t kDieSize = 62;
constexpr int16_t kDieGap = 12;
constexpr int16_t kDiceBandHeight = kDieSize;

// The scorecard. Fifteen lines: six upper, the bonus, seven lower, the total.
// Thirty-three pixels each is the number that makes all of them fit under the
// dice with the capsule still on screen, and it is the reason this table is not
// built from the list component -- a ListProps row is 62px and six of those
// would be the whole card.
constexpr int16_t kLineHeight = 33;
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
  screen.target().fill(box, fui::Paint::solid(fui::Color::White));
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
      0,           // unused
      0b000010000, // 1
      0b100000001, // 2
      0b100010001, // 3
      0b101000101, // 4
      0b101010101, // 5
      0b101101101, // 6
  };
  const int16_t xs[3] = {left, mid, right};
  const int16_t ys[3] = {top, centre, bottom};
  const uint16_t bits = kPips[face >= 1 && face <= yz::kFaces ? face : 1];
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      if ((bits & (1u << (8 - (row * 3 + column)))) == 0) continue;
      screen.target().fill(fui::makeRect(xs[column], ys[row], kPip, kPip), fui::Paint::solid(fui::Color::Black));
    }
  }

  // A held die is framed OUTSIDE its own edge, so it grows rather than shrinks.
  // The same rule the Checkers board learned: an inset frame eats the thing it
  // is marking and makes it look smaller than its neighbours.
  if (!held) return;
  screen.target().stroke(fui::makeRect(static_cast<int16_t>(box.x - toybox::kFrame),
                                       static_cast<int16_t>(box.y - toybox::kFrame),
                                       static_cast<int16_t>(box.width + toybox::kFrame * 2),
                                       static_cast<int16_t>(box.height + toybox::kFrame * 2)),
                         fui::Paint::solid(fui::Color::Black), toybox::kFrame);
}

const char* categoryName(const int index) {
  static const char* const kNames[yz::kCategories] = {
      "ONES", "TWOS", "THREES", "FOURS", "FIVES", "SIXES", "THREE OF A KIND",
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
  screen.target().text(fui::makeRect(static_cast<int16_t>(right - 90), y, 90, kLineHeight), text, style);
}

// An empty box: the printed card's box, waiting for a number.
void drawEmptyBox(toybox::Screen& screen, const int16_t right, const int16_t y) {
  screen.target().stroke(fui::makeRect(static_cast<int16_t>(right - kBoxWidth), static_cast<int16_t>(y + 4), kBoxWidth,
                                       static_cast<int16_t>(kLineHeight - 10)),
                         fui::Paint::solid(fui::Color::Black), toybox::kHairline);
}

// A box with what you WOULD score in it, which is the whole decision in this
// game. Drawn as a number inside the printed box, because that is exactly what
// it is: a pencilled figure in a box you have not committed to. A filled box
// has no outline, so committed and pencilled never look alike.
void drawPreview(toybox::Screen& screen, const int16_t right, const int16_t y, const int value) {
  drawEmptyBox(screen, right, y);
  char text[8];
  std::snprintf(text, sizeof(text), "%d", value);
  fui::TextStyle style;
  style.font = toybox::kSmallFont;
  style.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(static_cast<int16_t>(right - kBoxWidth), y, kBoxWidth, kLineHeight), text, style);
}

}  // namespace

fui::Rect dieRect(const fui::DeviceContext& device, const int index) {
  return fui::makeRect(static_cast<int16_t>(diceLeft(device) + index * (kDieSize + kDieGap)), contentTop(), kDieSize,
                       kDieSize);
}

int dieAt(const fui::DeviceContext& device, const int x, const int y) {
  const int16_t top = contentTop();
  if (y < top || y >= top + kDiceBandHeight) return -1;
  const int16_t left = diceLeft(device);
  if (x < left) return -1;
  const int offset = x - left;
  const int slot = offset / (kDieSize + kDieGap);
  if (slot < 0 || slot >= yz::kDice) return -1;
  // The gap between two dice belongs to neither. A tap there must miss rather
  // than land on whichever die happens to be to its left: these are five
  // separate switches and a fat-fingered hold is a wasted turn.
  if (offset - slot * (kDieSize + kDieGap) >= kDieSize) return -1;
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

int howToPages() { return 3; }

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
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

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  const int page = model.page < 0 ? 0 : (model.page >= howToPages() ? howToPages() - 1 : model.page);

  fui::HeaderProps header;
  header.title = "HOW TO PLAY";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::ButtonProps next;
  next.label = page + 1 < howToPages() ? "NEXT" : "GOT IT";
  next.action = ActionHowToNext;
  screen.button(next, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect area = screen.body();
  char progress[8];
  std::snprintf(progress, sizeof(progress), "%d/%d", page + 1, howToPages());
  fui::TextStyle counter;
  counter.font = toybox::kSmallFont;
  counter.align = fui::TextAlign::Right;
  screen.target().text(fui::makeRect(area.x, area.y, area.width, 20), progress, counter);

  static const char* const kLines[] = {
      "ROLL FIVE DICE. TAP THE ONES YOU WANT TO KEEP AND ROLL AGAIN, UP TO THREE ROLLS.",
      "THEN PUT THE HAND IN ONE BOX ON YOUR CARD. EVERY BOX IS USED EXACTLY ONCE.",
      "SIXTY-THREE IN THE TOP HALF EARNS THIRTY-FIVE MORE. THIRTEEN TURNS EACH.",
  };
  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  body.maxLines = 4;
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + 24), area.width, 150), kLines[page], body);

  // Real dice at the real size, showing the page: a hand being kept, then the
  // hand that hand becomes, then a top-half run worth the bonus.
  static const uint8_t kFaces[3][yz::kDice] = {{5, 5, 2, 5, 3}, {5, 5, 5, 5, 1}, {4, 4, 4, 4, 4}};
  static const uint8_t kHeld[3] = {0b01011, 0b01111, 0b11111};
  const fui::DeviceContext device = screen.device();
  const int16_t top = static_cast<int16_t>(area.y + 210);
  for (int i = 0; i < yz::kDice; ++i) {
    const fui::Rect box =
        fui::makeRect(static_cast<int16_t>(diceLeft(device) + i * (kDieSize + kDieGap)), top, kDieSize, kDieSize);
    drawDie(screen, box, kFaces[page][i], (kHeld[page] & (1 << i)) != 0);
  }
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
  if (!model.yourTurn) {
    action.label = "THEIR TURN";
    action.action = fui::NO_ACTION;
  } else if (stage == yz::Stage::Spent) {
    action.label = "TAKE A BOX";
    action.action = fui::NO_ACTION;
  } else {
    const int left = yz::kRollsPerTurn - game.rollsUsed;
    std::snprintf(rollLabel, sizeof(rollLabel), left == yz::kRollsPerTurn ? "ROLL" : "ROLL AGAIN (%d LEFT)", left);
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
    screen.target().text(
        fui::makeRect(toybox::kMargin, static_cast<int16_t>(contentTop() + kDiceBandHeight / 2 - 12),
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
  if (yz::bonusEarned(yours) > 0) {
    std::snprintf(bonusText, sizeof(bonusText), "BONUS  +%d", yz::kUpperBonus);
  } else if (yz::upperBonusStillPossible(yours)) {
    std::snprintf(bonusText, sizeof(bonusText), "BONUS  %d MORE", yz::upperShortfall(yours));
  } else {
    // Say it is gone rather than counting down toward a number that can no
    // longer arrive. A shortfall you cannot close is worse than no shortfall.
    std::snprintf(bonusText, sizeof(bonusText), "BONUS  OUT OF REACH");
  }
  screen.target().text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(bonusY + 4), 260, kLineHeight), bonusText,
                       name);
  drawNumber(screen, kTheirRight, static_cast<int16_t>(bonusY + 4), yz::bonusEarned(theirs), false);

  // The totals, under a rule, in the heavier face.
  const int16_t totalY = static_cast<int16_t>(tableTop() + kTotalLine * kLineHeight);
  screen.target().fill(fui::makeRect(toybox::kMargin, totalY, static_cast<int16_t>(device.width - toybox::kMargin * 2),
                                     toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));
  fui::TextStyle totalName;
  totalName.font = toybox::kBodyFont;
  totalName.align = fui::TextAlign::Left;
  screen.target().text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(totalY + 6), 210, kLineHeight), "TOTAL",
                       totalName);
  drawNumber(screen, kYourRight, static_cast<int16_t>(totalY + 6), yz::total(yours), true);
  drawNumber(screen, kTheirRight, static_cast<int16_t>(totalY + 6), yz::total(theirs), true);
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
