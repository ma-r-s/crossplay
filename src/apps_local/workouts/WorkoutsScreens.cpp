#include "WorkoutsScreens.h"

#include <cstdio>
#include <string>

#include "../ui/ToyboxText.h"
#include "WorkoutsIcons.h"

namespace workoutsui {
namespace {

constexpr int16_t kFooterHeight = toybox::kPillHeight;
constexpr int kMinRow = 72;  // a finger, with room to miss

// The schedule cards, in Notes' deck proportions so the two apps read as one
// family: a black badge, the name beside it, a second line under the name.
constexpr int16_t kCardHeight = 104;
constexpr int16_t kCardGap = 12;
constexpr int16_t kCardMax = 132;
constexpr int16_t kCardPitch = kCardHeight + kCardGap;
constexpr int16_t kBadgeSide = 76;
constexpr int16_t kBarHeight = 12;

// An exercise row: its name, then its boxes beside its weight. Tall enough that the
// boxes are a target in their own right, short enough for five a page.
constexpr int16_t kExerciseRow = 112;
// The weight's - and +, square.
constexpr int16_t kStepSide = 48;
constexpr int16_t kExerciseMax = 140;
constexpr int16_t kSetBoxMax = 40;
constexpr int16_t kSetBoxGap = 8;

// The calendar: a caption, then two weeks of seven cells, each a day label over
// a square.
constexpr int16_t kDaySquare = 44;
constexpr int16_t kStripCaptionGap = 6;
constexpr int16_t kCellLabelGap = 4;
constexpr int16_t kWeekGap = 6;

constexpr const char* kWeekdayLetters[7] = {"M", "T", "W", "T", "F", "S", "S"};

fui::TextStyle plain(const fui::FontId font, const fui::TextAlign align = fui::TextAlign::Left,
                     const uint8_t maxLines = 1) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = fui::Color::Black;
  style.maxLines = maxLines;
  return style;
}

const fui::Paint kInk = fui::Paint::solid(fui::Color::Black);

// Header band, rule, page margin. headerBand() fits the title to the band, so a
// long schedule name steps down a cut rather than running under the counter.
//
// The counter ("1/2", "7/17") is placed by hand at the UI cut with its ink
// centred in the band, the way the shelf's folders place theirs: the
// component's rightLabel slot sits a small label on the title's line box, low
// against the display cut's baseline. Its width is reserved so the title is
// fitted to the room that is really left.
//
// `edit` puts the pencil on the band's right, the opening screen's one action,
// with the counter just left of it.
void chrome(toybox::Screen& screen, const char* title, const char* counter = nullptr, const bool edit = false) {
  fui::HeaderProps header;
  header.title = title;
  header.titleText = screen.theme().titleText;
  header.titleText.font = toybox::kDisplayFont;
  header.borderEdges = fui::EdgesNone;
  int16_t trailingW = 0;
  if (edit) {
    header.trailingIcon = fui::bitmapFromIcon(icon_w_edit_32);
    header.trailingAction = ActionUsePhone;
    header.trailingStyles = toybox::rowStyles();
    // The component's own button width and gap (headerTitleWidth()).
    trailingW = static_cast<int16_t>(screen.theme().headerHeight - 8 + 8);
  }
  fui::TextStyle counterStyle;
  counterStyle.font = toybox::kUiFont;
  counterStyle.align = fui::TextAlign::Right;
  counterStyle.color = fui::Color::White;  // paper, on the black band
  if (counter != nullptr) {
    header.rightReserve = static_cast<int16_t>(
        screen.target().measureText(counterStyle.font, counter, counterStyle).width + toybox::kGutter);
  }
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  if (counter != nullptr) {
    const int16_t right = static_cast<int16_t>(screen.device().width - toybox::kMargin - trailingW);
    const fui::Rect box =
        fui::makeRect(0, toybox::bandCenterY(screen, toybox::kUiCut.inkHeight), right, toybox::kUiCut.inkHeight);
    screen.target().text(toybox::inkCentred(box, toybox::kUiCut), counter, counterStyle);
  }
  screen.insetContent(fui::Insets{toybox::kBodyGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// The whole row is the target, invisible.
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

void separator(toybox::Screen& screen, const fui::Rect& row) {
  screen.target().fill(
      fui::makeRect(row.x, static_cast<int16_t>(row.y + row.height - toybox::kHairline), row.width, toybox::kHairline),
      kInk);
}

// One line set at the largest cut that holds it, never above the one asked for.
void fittedLine(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::TextAlign align,
                const fui::FontId font) {
  fui::TextStyle style = plain(font, align);
  const std::string drawn = toybox::fittedTitle(screen.target(), text, box.width, style);
  screen.target().text(box, drawn.c_str(), style);
}

// Rows share a short page instead of stacking under a hole, up to a cap, and
// only while nothing is paged so a row never changes size under the finger.
int16_t fittedPitch(const int16_t base, const int16_t cap, const int16_t bandHeight, const int count,
                    const int visible) {
  if (count <= 0 || count > visible) return base;
  const int16_t share = static_cast<int16_t>(bandHeight / count);
  if (share <= base) return base;
  return share > cap ? cap : share;
}

// The bar every screen's actions live on, at Notes' y and height.
fui::Rect footerBand(const fui::DeviceContext& device) {
  return fui::makeRect(toybox::kMargin, static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight),
                       static_cast<int16_t>(device.width - 2 * toybox::kMargin), kFooterHeight);
}

void footerButton(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId action,
                  const bool outlined) {
  fui::ButtonProps button;
  button.label = label;
  button.action = action;
  if (outlined) button.styles = toybox::rowStyles();
  screen.button(button, box);
}

void pageLabel(toybox::Screen& screen, const fui::Rect& band, const char* label) {
  if (label == nullptr) return;
  fui::TextStyle style = plain(toybox::kTileFont, fui::TextAlign::Center);
  const int16_t lineHeight = screen.target().lineHeight(style.font);
  screen.target().text(
      fui::makeRect(band.x, static_cast<int16_t>(band.y + band.height - lineHeight), band.width, lineHeight), label,
      style);
}

void centredNotice(toybox::Screen& screen, const fui::Rect& band, const char* text, const int lines) {
  fui::TextStyle style = plain(toybox::kBodyFont, fui::TextAlign::Center, static_cast<uint8_t>(lines));
  const int16_t lineHeight = screen.target().lineHeight(style.font);
  const fui::Rect box = fui::makeRect(band.x, static_cast<int16_t>(band.y + (band.height - lineHeight * lines) / 2),
                                      band.width, static_cast<int16_t>(lineHeight * lines));
  const std::string drawn = toybox::fitLines(screen.target(), text, box.width, lines, style);
  screen.target().text(box, drawn.c_str(), style);
}

void icon(toybox::Screen& screen, const fui::Rect& box, const freeink::Icon& mark, const fui::Color colour) {
  const fui::Rect where = fui::makeRect(static_cast<int16_t>(box.x + (box.width - mark.w) / 2),
                                        static_cast<int16_t>(box.y + (box.height - mark.h) / 2),
                                        static_cast<int16_t>(mark.w), static_cast<int16_t>(mark.h));
  screen.target().bitmap(where, fui::bitmapFromIcon(mark), fui::BitmapMode::Contain, fui::Paint::solid(colour));
}

void progressBar(toybox::Screen& screen, const fui::Rect& box, const int done, const int total) {
  screen.target().stroke(box, kInk, toybox::kHairline, 0);
  if (total <= 0 || done <= 0) return;
  int16_t filled = static_cast<int16_t>(static_cast<long>(box.width) * done / total);
  if (filled < toybox::kRule) filled = toybox::kRule;
  if (filled > box.width) filled = box.width;
  screen.target().fill(fui::makeRect(box.x, box.y, filled, box.height), kInk, 0);
}

// --- Home geometry, shared by drawing and capacity ------------------------

int16_t weekRowHeight(const int16_t tileLine) { return static_cast<int16_t>(tileLine + kCellLabelGap + kDaySquare); }

int16_t stripHeight(const int16_t tileLine) {
  return static_cast<int16_t>(tileLine + kStripCaptionGap + weekRowHeight(tileLine) * 2 + kWeekGap);
}

struct HomeGeometry {
  fui::Rect cards;
  fui::Rect strip;
};

HomeGeometry homeGeometry(const fui::DeviceContext& device, const int16_t tileLine) {
  HomeGeometry g;
  const int16_t width = static_cast<int16_t>(device.width - 2 * toybox::kMargin);
  const int16_t stripH = stripHeight(tileLine);
  const int16_t stripY = static_cast<int16_t>(device.height - toybox::kMargin - stripH);
  g.strip = fui::makeRect(toybox::kMargin, stripY, width, stripH);
  // The cards stop a rule's width above the calendar, with a gutter either side
  // of it, so the weeks read as their own panel rather than one more card.
  const int16_t cardsBottom = static_cast<int16_t>(stripY - toybox::kGutter * 2 - toybox::kRule);
  g.cards = fui::makeRect(toybox::kMargin, static_cast<int16_t>(toybox::kBodyTop), width,
                          static_cast<int16_t>(cardsBottom - toybox::kBodyTop));
  return g;
}

int cardsVisible(const fui::Rect& band) {
  const int visible = (band.height + kCardGap) / kCardPitch;
  return visible < 1 ? 1 : visible;
}

// The tile cut's line height without a target: the capacity question is asked
// by the Activity with only a device in hand.
constexpr int16_t kTileLine = toybox::kTileCut.lineHeight;

void card(toybox::Screen& screen, const fui::Rect& box, const ScheduleCard& item, const int index) {
  const int16_t side = box.height < kBadgeSide ? box.height : kBadgeSide;
  const fui::Rect badge = fui::makeRect(box.x, static_cast<int16_t>(box.y + (box.height - side) / 2), side, side);
  screen.target().fill(badge, kInk, 0);
  icon(screen, badge, scheduleIcon(item.icon, false), fui::Color::White);

  const int16_t textX = static_cast<int16_t>(box.x + kBadgeSide + toybox::kGutter);
  const int16_t textW = static_cast<int16_t>(box.x + box.width - textX);
  const int16_t titleLine = screen.target().lineHeight(toybox::kBodyFont);
  const int16_t smallLine = screen.target().lineHeight(toybox::kTileFont);
  const bool started = item.done > 0;
  const int16_t block = static_cast<int16_t>(titleLine + toybox::kGutter / 2 + smallLine +
                                             (started ? toybox::kGutter / 2 + kBarHeight : 0));
  const int16_t top = static_cast<int16_t>(box.y + (box.height - block) / 2);

  fittedLine(screen, fui::makeRect(textX, top, textW, titleLine), item.title, fui::TextAlign::Left, toybox::kBodyFont);

  char detail[48];
  if (!started) {
    std::snprintf(detail, sizeof(detail), "%d %s, %d SETS", item.exercises,
                  item.exercises == 1 ? "EXERCISE" : "EXERCISES", item.sets);
  } else if (item.done >= item.sets) {
    std::snprintf(detail, sizeof(detail), "DONE TODAY");
  } else {
    std::snprintf(detail, sizeof(detail), "%d OF %d SETS TODAY", item.done, item.sets);
  }
  const int16_t detailY = static_cast<int16_t>(top + titleLine + toybox::kGutter / 2);
  fittedLine(screen, fui::makeRect(textX, detailY, textW, smallLine), detail, fui::TextAlign::Left, toybox::kTileFont);
  if (started) {
    progressBar(
        screen,
        fui::makeRect(textX, static_cast<int16_t>(detailY + smallLine + toybox::kGutter / 2), textW, kBarHeight),
        item.done, item.sets);
  }
  rowHit(screen, box, ActionOpenSchedule, index);
}

void calendar(toybox::Screen& screen, const fui::Rect& strip, const HomeModel& model) {
  // A rule across the top: the weeks are a panel of their own, not one more card.
  screen.target().fill(fui::makeRect(strip.x, static_cast<int16_t>(strip.y - toybox::kGutter - toybox::kRule),
                                     strip.width, toybox::kRule),
                       kInk);

  const fui::TextStyle small = plain(toybox::kTileFont);
  const int16_t tileLine = screen.target().lineHeight(small.font);
  int trained = 0;
  if (model.clockSet) {
    for (const workouts::WeekCell& cell : model.days) trained += cell.icon >= 0 ? 1 : 0;
  }
  screen.target().text(fui::makeRect(strip.x, strip.y, strip.width, tileLine), "LAST WEEK AND THIS", small);
  if (model.clockSet) {
    char count[24];
    std::snprintf(count, sizeof(count), "%d %s", trained, trained == 1 ? "DAY TRAINED" : "DAYS TRAINED");
    screen.target().text(fui::makeRect(strip.x, strip.y, strip.width, tileLine), count,
                         plain(toybox::kTileFont, fui::TextAlign::Right));
  }

  const int16_t cellsY = static_cast<int16_t>(strip.y + tileLine + kStripCaptionGap);
  if (!model.clockSet) {
    const fui::Rect box =
        fui::makeRect(strip.x, cellsY, strip.width, static_cast<int16_t>(strip.y + strip.height - cellsY));
    screen.target().stroke(box, kInk, toybox::kHairline, 0);
    fittedLine(screen,
               fui::makeRect(box.x, static_cast<int16_t>(box.y + (box.height - tileLine) / 2), box.width, tileLine),
               "THE CLOCK IS NOT SET. SAVE FROM YOUR PHONE TO SET IT.", fui::TextAlign::Center, toybox::kTileFont);
    return;
  }

  // Seven columns, Monday first, the squares centred in each so the gaps come
  // out even whatever the panel's width divides into.
  const int16_t column = static_cast<int16_t>(strip.width / 7);
  const int16_t squareSide = column - 6 < kDaySquare ? static_cast<int16_t>(column - 6) : kDaySquare;
  const int16_t rowPitch = static_cast<int16_t>(weekRowHeight(tileLine) + kWeekGap);
  for (int i = 0; i < workouts::kCalendarDays; i++) {
    const workouts::WeekCell& cell = model.days[i];
    const int16_t colX = static_cast<int16_t>(strip.x + (i % 7) * column);
    const int16_t rowY = static_cast<int16_t>(cellsY + (i / 7) * rowPitch);
    char label[12];
    std::snprintf(label, sizeof(label), "%s %d", kWeekdayLetters[cell.weekday % 7], cell.dayOfMonth);
    screen.target().text(fui::makeRect(colX, rowY, column, tileLine), label,
                         plain(toybox::kTileFont, fui::TextAlign::Center));
    const fui::Rect square =
        fui::makeRect(static_cast<int16_t>(colX + (column - squareSide) / 2),
                      static_cast<int16_t>(rowY + tileLine + kCellLabelGap), squareSide, squareSide);
    if (cell.icon >= 0) {
      // A trained day is the solid one: at arm's length the weeks are read as
      // black squares and white squares before any mark is made out.
      screen.target().fill(square, kInk, 0);
      icon(screen, square, scheduleIcon(cell.icon, true), fui::Color::White);
    } else if (cell.future) {
      // Days still to come are only marked at their corners, so the empty
      // squares that count -- the days that went by untrained -- stand out.
      const int16_t tick = 8;
      const int16_t x2 = static_cast<int16_t>(square.x + square.width - tick);
      const int16_t y2 = static_cast<int16_t>(square.y + square.height - toybox::kHairline);
      screen.target().fill(fui::makeRect(square.x, square.y, tick, toybox::kHairline), kInk);
      screen.target().fill(fui::makeRect(x2, square.y, tick, toybox::kHairline), kInk);
      screen.target().fill(fui::makeRect(square.x, y2, tick, toybox::kHairline), kInk);
      screen.target().fill(fui::makeRect(x2, y2, tick, toybox::kHairline), kInk);
    } else {
      screen.target().stroke(square, kInk, toybox::kHairline, 0);
    }
    // Today wears a heavier frame, trained or not, so the calendar says where
    // "now" is without a word.
    const bool today = !cell.future && (i + 1 == workouts::kCalendarDays || model.days[i + 1].future);
    if (today) {
      const fui::Rect frame =
          fui::makeRect(static_cast<int16_t>(square.x - 4), static_cast<int16_t>(square.y - 4),
                        static_cast<int16_t>(square.width + 8), static_cast<int16_t>(square.height + 8));
      screen.target().stroke(frame, kInk, toybox::kRule, 0);
    }
  }
}

// --- Schedule geometry ---------------------------------------------------

fui::Rect scheduleBand(const fui::DeviceContext& device) {
  const fui::Rect footer = footerBand(device);
  return fui::makeRect(footer.x, static_cast<int16_t>(toybox::kBodyTop), footer.width,
                       static_cast<int16_t>(footer.y - toybox::kGutter * 2 - toybox::kBodyTop));
}

int exercisesVisible(const fui::Rect& band) {
  const int visible = band.height / kExerciseRow;
  return visible < 1 ? 1 : visible;
}

// The row's own target, everywhere the weight's buttons are not. A piece too
// short for a finger is left out: a button grows a short target to a finger's
// height, which would reach over - and +.
void rowHitAround(toybox::Screen& screen, const fui::Rect& row, const fui::Rect& stepper, const int index) {
  constexpr int16_t kFinger = 44;
  rowHit(screen, fui::makeRect(row.x, row.y, static_cast<int16_t>(stepper.x - row.x), row.height), ActionAddSet, index);
  const int16_t right = static_cast<int16_t>(row.x + row.width - stepper.x);
  const int16_t aboveH = static_cast<int16_t>(stepper.y - row.y);
  if (aboveH >= kFinger) rowHit(screen, fui::makeRect(stepper.x, row.y, right, aboveH), ActionAddSet, index);
  const int16_t below = static_cast<int16_t>(stepper.y + stepper.height);
  const int16_t belowH = static_cast<int16_t>(row.y + row.height - below);
  if (belowH >= kFinger) rowHit(screen, fui::makeRect(stepper.x, below, right, belowH), ActionAddSet, index);
}

void stepButton(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId action,
                const int index) {
  fui::ButtonProps button;
  button.label = label;
  button.action = action;
  button.value = static_cast<int16_t>(index);
  button.styles = toybox::rowStyles();
  screen.button(button, box);
}

void exerciseRow(toybox::Screen& screen, const fui::Rect& row, const ExerciseRow& item, const int index,
                 const bool last) {
  const int16_t nameLine = screen.target().lineHeight(toybox::kBodyFont);
  // The second line: the boxes on the left, the weight on the right as - , the
  // kilograms, +. The label is sized for the widest weight there can be, so
  // the buttons never move under a finger as the number grows a digit.
  fui::TextStyle weightStyle = plain(toybox::kUiFont, fui::TextAlign::Center);
  const int16_t labelW = static_cast<int16_t>(
      screen.target().measureText(weightStyle.font, "999 KG", weightStyle).width + toybox::kGutter);
  const int16_t stepperW = static_cast<int16_t>(kStepSide * 2 + labelW);
  const int16_t boxRoom = static_cast<int16_t>(row.width - stepperW - toybox::kGutter);

  const int sets = item.sets < 1 ? 1 : item.sets;
  // Boxes shrink only when a long scheme would not fit beside the weight.
  int16_t side = static_cast<int16_t>((boxRoom - (sets - 1) * kSetBoxGap) / sets);
  if (side > kSetBoxMax) side = kSetBoxMax;
  const int16_t block = static_cast<int16_t>(nameLine + toybox::kGutter / 2 + kStepSide);
  const int16_t top = static_cast<int16_t>(row.y + (row.height - block) / 2);
  const int16_t lineTwo = static_cast<int16_t>(top + nameLine + toybox::kGutter / 2);

  // The name has the whole first line.
  fittedLine(screen, fui::makeRect(row.x, top, row.width, nameLine), item.name, fui::TextAlign::Left,
             toybox::kBodyFont);

  const fui::Rect stepper =
      fui::makeRect(static_cast<int16_t>(row.x + row.width - stepperW), lineTwo, stepperW, kStepSide);
  rowHitAround(screen, row, stepper, index);
  stepButton(screen, fui::makeRect(stepper.x, stepper.y, kStepSide, kStepSide), "-", ActionWeightDown, index);
  stepButton(
      screen,
      fui::makeRect(static_cast<int16_t>(stepper.x + stepper.width - kStepSide), stepper.y, kStepSide, kStepSide), "+",
      ActionWeightUp, index);
  char weight[16];
  std::snprintf(weight, sizeof(weight), "%d KG", item.weight);
  const fui::Rect labelBox = fui::makeRect(static_cast<int16_t>(stepper.x + kStepSide), stepper.y, labelW, kStepSide);
  screen.target().text(toybox::inkCentred(labelBox, toybox::kUiCut), weight, weightStyle);

  const int16_t boxY = static_cast<int16_t>(lineTwo + (kStepSide - side) / 2);
  for (int s = 0; s < sets; s++) {
    const fui::Rect box = fui::makeRect(static_cast<int16_t>(row.x + s * (side + kSetBoxGap)), boxY, side, side);
    screen.target().stroke(box, kInk, toybox::kRule, 4);
    if (s < item.done) {
      const int16_t inset = side >= 32 ? 8 : (side >= 20 ? 5 : 4);
      screen.target().fill(
          fui::makeRect(static_cast<int16_t>(box.x + inset), static_cast<int16_t>(box.y + inset),
                        static_cast<int16_t>(box.width - 2 * inset), static_cast<int16_t>(box.height - 2 * inset)),
          kInk, 2);
    }
  }
  if (!last) separator(screen, row);
}

}  // namespace

const freeink::Icon& scheduleIcon(const int index, const bool small) {
  static const freeink::Icon* const kSmall[] = {
      &icon_w_dumbbell_24, &icon_w_arms_24, &icon_w_legs_24, &icon_w_push_24, &icon_w_pull_24,    &icon_w_core_24,
      &icon_w_cardio_24,   &icon_w_bike_24, &icon_w_swim_24, &icon_w_hike_24, &icon_w_stretch_24, &icon_w_power_24,
  };
  static const freeink::Icon* const kLarge[] = {
      &icon_w_dumbbell_32, &icon_w_arms_32, &icon_w_legs_32, &icon_w_push_32, &icon_w_pull_32,    &icon_w_core_32,
      &icon_w_cardio_32,   &icon_w_bike_32, &icon_w_swim_32, &icon_w_hike_32, &icon_w_stretch_32, &icon_w_power_32,
  };
  static_assert(sizeof(kSmall) / sizeof(kSmall[0]) == workouts::kIconCount, "one 24px mark per icon option");
  static_assert(sizeof(kLarge) / sizeof(kLarge[0]) == workouts::kIconCount, "one 32px mark per icon option");
  const int i = index >= 0 && index < workouts::kIconCount ? index : 0;
  return small ? *kSmall[i] : *kLarge[i];
}

// --- The schedules -------------------------------------------------------

int homeCapacity(const fui::DeviceContext& device) { return cardsVisible(homeGeometry(device, kTileLine).cards); }

void buildHome(toybox::Screen& screen, const HomeModel& model) {
  // The page count rides on the band, where the shelf's own folders put it:
  // the cards fill their band exactly, and a label under them sat on the last
  // card's second line.
  // The phone is where schedules are written, and the pencil on the band is
  // the way there, for the empty app and the full one alike.
  chrome(screen, "WORKOUTS", model.pageLabel, true);
  const fui::DeviceContext& device = screen.device();
  const HomeGeometry g = homeGeometry(device, screen.target().lineHeight(toybox::kTileFont));
  calendar(screen, g.strip, model);

  if (model.count == 0) {
    centredNotice(screen, g.cards, "No workouts yet. Tap the pencil to write your schedules on your phone.", 4);
    return;
  }
  const int visible = cardsVisible(g.cards);
  const int16_t pitch =
      fittedPitch(kCardPitch, static_cast<int16_t>(kCardMax + kCardGap), g.cards.height, model.count, visible);
  int16_t y = g.cards.y;
  for (int i = model.firstVisible; i < model.count && i - model.firstVisible < visible; i++) {
    card(screen, fui::makeRect(g.cards.x, y, g.cards.width, static_cast<int16_t>(pitch - kCardGap)), model.cards[i], i);
    y = static_cast<int16_t>(y + pitch);
  }
}

// --- One schedule --------------------------------------------------------

int scheduleCapacity(const fui::DeviceContext& device) { return exercisesVisible(scheduleBand(device)); }

void buildSchedule(toybox::Screen& screen, const ScheduleModel& model) {
  chrome(screen, model.title, model.tally);
  const fui::DeviceContext& device = screen.device();
  const fui::Rect band = scheduleBand(device);

  const fui::Rect footer = footerBand(device);
  const int16_t half = static_cast<int16_t>((footer.width - toybox::kGutter) / 2);
  footerButton(screen, fui::makeRect(footer.x, footer.y, half, footer.height), "DONE", ActionDone, false);
  const fui::Rect right =
      fui::makeRect(static_cast<int16_t>(footer.x + half + toybox::kGutter), footer.y, half, footer.height);
  if (model.canReset) {
    footerButton(screen, right, "RESET", ActionReset, true);
  } else if (model.canUndo) {
    footerButton(screen, right, "UNDO", ActionUndo, true);
  }

  if (model.count == 0) {
    centredNotice(screen, band, "This schedule has no exercises yet. Add some on your phone.", 3);
    return;
  }
  const int visible = exercisesVisible(band);
  // The page label takes the strip under the last row when there is more than
  // one page, so rows are pitched against what is left.
  const int16_t labelRoom =
      model.pageLabel != nullptr ? static_cast<int16_t>(screen.target().lineHeight(toybox::kTileFont)) : 0;
  const int16_t pitch =
      fittedPitch(kExerciseRow, kExerciseMax, static_cast<int16_t>(band.height - labelRoom), model.count, visible);
  int16_t y = band.y;
  for (int i = model.firstVisible; i < model.count && i - model.firstVisible < visible; i++) {
    const bool last = i + 1 >= model.count || i + 1 - model.firstVisible >= visible;
    const int16_t height = pitch < kMinRow ? static_cast<int16_t>(kMinRow) : pitch;
    exerciseRow(screen, fui::makeRect(band.x, y, band.width, height), model.rows[i], i, last);
    y = static_cast<int16_t>(y + pitch);
  }
  pageLabel(screen, band, model.pageLabel);
}

// --- The reset confirm --------------------------------------------------

void buildResetConfirm(toybox::Screen& screen, const char* title, const char* prose) {
  chrome(screen, title);
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  const fui::Rect band = fui::makeRect(footer.x, static_cast<int16_t>(toybox::kBodyTop), footer.width,
                                       static_cast<int16_t>(footer.y - toybox::kGutter * 2 - toybox::kBodyTop));
  centredNotice(screen, band, prose, 5);
  const int16_t half = static_cast<int16_t>((footer.width - toybox::kGutter) / 2);
  footerButton(screen, fui::makeRect(footer.x, footer.y, half, footer.height), "RESET IT", ActionResetConfirm, true);
  footerButton(screen,
               fui::makeRect(static_cast<int16_t>(footer.x + half + toybox::kGutter), footer.y, half, footer.height),
               "KEEP IT", ActionResetKeep, false);
}

// --- The phone -----------------------------------------------------------

fui::Rect buildPhone(toybox::Screen& screen, const PhoneModel& model) {
  chrome(screen, "WORKOUTS");
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  const int16_t width = footer.width;

  const int16_t lineHeight = screen.target().lineHeight(toybox::kTileFont);
  const fui::Rect caption =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(toybox::kBodyTop + toybox::kGutter), width, lineHeight);
  fittedLine(screen, caption, "POINT YOUR PHONE CAMERA HERE", fui::TextAlign::Center, toybox::kTileFont);

  // Notes' arrangement exactly: the code about half the panel, the address
  // under it in the largest type it fits, and whether anything has arrived.
  const int16_t top = static_cast<int16_t>(caption.y + caption.height + toybox::kGutter * 2);
  const int16_t room = static_cast<int16_t>(footer.y - toybox::kGutter * 2 - top - lineHeight * 5);
  int16_t side = room < width ? room : width;
  if (side > 300) side = 300;
  if (side < 120) side = 120;
  const fui::Rect qr = fui::makeRect(static_cast<int16_t>((device.width - side) / 2), top, side, side);

  const fui::Rect url =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(qr.y + qr.height + toybox::kGutter), width, lineHeight);
  fittedLine(screen, url, model.readable, fui::TextAlign::Center, toybox::kTileFont);
  const fui::Rect state =
      fui::makeRect(toybox::kMargin, static_cast<int16_t>(url.y + url.height + toybox::kGutter), width, lineHeight);
  fittedLine(screen, state, model.saved ? "SAVED FROM YOUR PHONE" : "WAITING FOR YOUR PHONE", fui::TextAlign::Center,
             toybox::kTileFont);

  footerButton(screen, footer, "DONE", ActionDismiss, false);
  return qr;
}

void buildNotice(toybox::Screen& screen, const char* prose) {
  chrome(screen, "WORKOUTS");
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = footerBand(device);
  const fui::Rect band = fui::makeRect(footer.x, static_cast<int16_t>(toybox::kBodyTop), footer.width,
                                       static_cast<int16_t>(footer.y - toybox::kGutter * 2 - toybox::kBodyTop));
  fui::TextStyle style = plain(toybox::kBodyFont, fui::TextAlign::Left, 6);
  const std::string drawn = toybox::fitLines(screen.target(), prose, band.width, 6, style);
  screen.target().text(band, drawn.c_str(), style);
  footerButton(screen, footer, "BACK", ActionDismiss, false);
}

}  // namespace workoutsui
