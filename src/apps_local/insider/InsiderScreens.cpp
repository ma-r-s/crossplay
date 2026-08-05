#include "InsiderScreens.h"

#include <cstdio>

#include "InsiderArt.h"

namespace insiderui {

namespace {

using insider::kNoInsider;
using insider::Outcome;
using insider::Role;

// The header band, and the rule Toybox draws under it rather than flush against
// it. Lifted from Connections deliberately: two games that share a band read as
// one device, and the subtitle trap below has now bitten this fork three times.
void chrome(toybox::Screen& screen, const char* title, const char* rightLabel) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  // rightLabel is drawn with subtitleText, NOT trailingText, and Screen
  // substitutes the theme's smallText when it is unset -- which is black, on a
  // solid black band. The label is then invisible and indistinguishable from
  // never having been set.
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  const fui::Rect band = screen.device().screen();
  screen.target().fill(fui::makeRect(0, toybox::kHeaderHeight + 4, band.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));
}

// Largest cut the string actually fits in, walking down. EpdFont is a bitmap
// format with one pre-rasterised set per size, so "fit this text" can only mean
// "pick a smaller cut" -- there is no scaling at draw time.
//
// This is not a nicety. The renderer ellipsises what does not fit, the Toybox
// face is subset to ASCII, and U+2026 is not in it, so an overflowing label
// loses its tail to a glyph that draws as *nothing*: "WE SAID THE WORD" became
// "WE SAID THE WOR" with an [ERR] line nobody was reading. Every label built
// from a value goes through here.
fui::FontId fitted(toybox::Screen& screen, const char* text, const int16_t width, const fui::TextStyle& probe) {
  const fui::FontId cuts[3] = {toybox::kDisplayFont, toybox::kUiFont, toybox::kTileFont};
  fui::TextStyle measure = probe;
  for (const fui::FontId cut : cuts) {
    if (cut == toybox::kDisplayFont && probe.font != toybox::kDisplayFont) continue;
    measure.font = cut;
    if (screen.target().measureText(cut, text, measure).width <= width) return cut;
  }
  return toybox::kTileFont;
}

fui::TextStyle styled(const fui::FontId font, const fui::TextAlign align, const fui::Color color = fui::Color::Black) {
  fui::TextStyle style;
  style.font = font;
  // Always named, even when it is the default the component would apply anyway:
  // FONT_SLOT_SMALL is 0, and a style whose font is 0 with every other field at
  // its default is read as *unset*, so Screen helpfully puts the theme's style
  // back and a small label silently returns at full size.
  style.align = align;
  style.color = color;
  return style;
}

// Corner brackets, the same shape the chess board and the Connections ornament
// wear. This is the fork's one piece of shared ornament grammar.
void brackets(toybox::Screen& screen, const fui::Rect& box, const int16_t arm) {
  const auto ink = fui::Paint::solid(fui::Color::Black);
  const int16_t w = toybox::kRule;
  screen.target().fill(fui::makeRect(box.x, box.y, arm, w), ink);
  screen.target().fill(fui::makeRect(box.x, box.y, w, arm), ink);
  screen.target().fill(fui::makeRect(static_cast<int16_t>(box.right() - arm), box.y, arm, w), ink);
  screen.target().fill(fui::makeRect(static_cast<int16_t>(box.right() - w), box.y, w, arm), ink);
  screen.target().fill(fui::makeRect(box.x, static_cast<int16_t>(box.bottom() - w), arm, w), ink);
  screen.target().fill(fui::makeRect(box.x, static_cast<int16_t>(box.bottom() - arm), w, arm), ink);
  screen.target().fill(fui::makeRect(static_cast<int16_t>(box.right() - arm), static_cast<int16_t>(box.bottom() - w),
                                     arm, w),
                       ink);
  screen.target().fill(
      fui::makeRect(static_cast<int16_t>(box.right() - w), static_cast<int16_t>(box.bottom() - arm), w, arm), ink);
}

// A seat at the table: this game's one unit of material. Drawn by one function
// so that the person you pass the device to, the person you accuse and the
// person who turns out to be the Insider are visibly the same object.
//
// `filled` inverts it, which is how both "already seen their role" and "this is
// the one we are accusing" read -- they are the same idea, a seat that has been
// dealt with. `dimmed` is the Master on the vote screen: present, and not
// available. It dims by dithering the GROUND, because there is no grey type on
// this renderer and a non-white text colour draws solid black.
enum class SeatLook : uint8_t { Plain, Filled, Dimmed };

void seat(toybox::Screen& screen, const fui::Rect& box, const int number, const SeatLook look) {
  const bool filled = look == SeatLook::Filled;
  if (filled) {
    screen.target().fill(box, fui::Paint::solid(fui::Color::Black), 10);
  } else if (look == SeatLook::Dimmed) {
    screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray), 10);
    screen.target().stroke(box, fui::Paint::dither(fui::Color::DarkGray), toybox::kHairline, 10);
  } else {
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kFrame, 10);
  }

  char label[4];
  std::snprintf(label, sizeof(label), "%d", number + 1);
  const auto style = styled(toybox::kDisplayFont, fui::TextAlign::Center,
                            filled ? fui::Color::White : fui::Color::Black);
  const int16_t line = screen.target().lineHeight(toybox::kDisplayFont);
  screen.target().text(
      fui::makeRect(box.x, static_cast<int16_t>(box.y + (box.height - line) / 2), box.width, line), label, style);
}

const char* roleName(const Role role) {
  switch (role) {
    case Role::Master:
      return "MASTER";
    case Role::Insider:
      return "INSIDER";
    case Role::Citizen:
    default:
      return "CITIZEN";
  }
}

// One line telling you how to play *your* role, on the card, at the moment it
// is the only thing you are looking at. This is where the rules of this game
// actually live: nobody reads a rules screen at a bar, and everybody reads the
// card they were just handed.
const char* roleAdvice(const Role role) {
  switch (role) {
    case Role::Master:
      return "ANSWER YES OR NO. NOTHING ELSE.";
    case Role::Insider:
      return "YOU KNOW IT. DO NOT LOOK LIKE IT.";
    case Role::Citizen:
    default:
      return "FIND THE WORD. THEN FIND THE INSIDER.";
  }
}

const freeink::Icon& roleArt(const Role role) {
  switch (role) {
    case Role::Master:
      return icon_roleMaster_96;
    case Role::Insider:
      return icon_roleInsider_96;
    case Role::Citizen:
    default:
      return icon_roleCitizen_96;
  }
}

// The record, as sixteen marks in two rows of eight. Two rows rather than
// Connections' 4x4, because these are rounds in the order they happened and a
// square grid would read as a board -- which is the wrong material here.
//
// The vocabulary is the fork's existing one: solid for a clean win, outline for
// a loss, a cross for a round nobody won, nothing at all for a round not played.
void recordGrid(toybox::Screen& screen, const fui::Rect& box, const insider::Record& record) {
  constexpr int kCols = 8;
  constexpr int kRows = 2;
  constexpr int16_t kGap = 6;
  const int16_t cell = static_cast<int16_t>((box.width - (kCols - 1) * kGap) / kCols);

  for (int i = 0; i < kCols * kRows; ++i) {
    const int16_t cx = static_cast<int16_t>(box.x + (i % kCols) * (cell + kGap));
    const int16_t cy = static_cast<int16_t>(box.y + (i / kCols) * (cell + kGap));
    const fui::Rect at = fui::makeRect(cx, cy, cell, cell);
    switch (record.at(i)) {
      case Outcome::Won:
        screen.target().fill(at, fui::Paint::solid(fui::Color::Black), 4);
        break;
      case Outcome::Lost:
        screen.target().stroke(at, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 4);
        break;
      case Outcome::OutOfTime:
        screen.target().stroke(at, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 4);
        screen.target().line(fui::Point{cx, cy},
                             fui::Point{static_cast<int16_t>(cx + cell - 1), static_cast<int16_t>(cy + cell - 1)},
                             toybox::kHairline, fui::Paint::solid(fui::Color::Black));
        break;
      case Outcome::Unplayed:
      default:
        // A dithered cell, not a dot and not nothing. On a fresh device all
        // sixteen are empty, and at four pixels each the ornament read as a
        // bracket around a blank space -- which is what the "seed the card
        // before you judge a screen" rule exists to catch. A filled ground
        // shows the shape of the record waiting to be written.
        screen.target().fill(at, fui::Paint::dither(fui::Color::LightGray), 4);
        break;
    }
  }
}

}  // namespace

void formatClock(const int secondsLeft, char* out, const int cap) {
  const int clamped = secondsLeft < 0 ? 0 : secondsLeft;
  std::snprintf(out, static_cast<size_t>(cap), "%d:%02d", clamped / 60, clamped % 60);
}

// ---------------------------------------------------------------------------

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  chrome(screen, "INSIDER", nullptr);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  const fui::Rect body = screen.body();

  // The headline is the one thing a table sets before every round, so it is the
  // loudest thing on the screen and the steppers sit inside its band.
  char headline[20];
  std::snprintf(headline, sizeof(headline), "%d PLAYERS", model.players);
  // Every button on this screen is one object at one size: the steppers take
  // the same height and the same 4px gap as the two doors at the bottom, and
  // the headline is set in a band of that height so its ink centres on theirs.
  // They were 56 tall with a 12px gap next to 62-tall doors with a 4px gap,
  // which is three different rhythms on one screen.
  constexpr int16_t kStep = toybox::kRowHeight;
  constexpr int16_t kStepGap = 4;
  const int16_t headlineW = static_cast<int16_t>(body.width - kStep * 2 - kStepGap - toybox::kGutter);
  fui::TextStyle hero = styled(toybox::kDisplayFont, fui::TextAlign::Left);
  hero.font = fitted(screen, headline, headlineW, hero);
  screen.target().text(fui::makeRect(body.x, body.y, headlineW, kStep), headline, hero);

  // A spent arrow dims rather than disappearing: a control that vanishes takes
  // its space with it and you lose your place.
  const bool canLess = model.players > insider::kMinPlayers;
  const bool canMore = model.players < insider::kMaxPlayers;
  for (int i = 0; i < 2; ++i) {
    const bool more = i == 1;
    const bool live = more ? canMore : canLess;
    fui::ButtonProps arrow;
    arrow.label = more ? "+" : "-";
    arrow.action = live ? ActionPlayers : fui::NO_ACTION;
    arrow.value = more ? 1 : -1;
    arrow.text = styled(toybox::kDisplayFont, fui::TextAlign::Center, live ? fui::Color::White : fui::Color::Black);
    arrow.styles = live ? toybox::invertedStyles() : toybox::disabledStepperStyles();
    arrow.radius = 10;
    screen.button(arrow, fui::makeRect(static_cast<int16_t>(body.right() - kStep * 2 - kStepGap +
                                                            i * (kStep + kStepGap)),
                                       body.y, kStep, kStep));
  }

  const int16_t ruleY = static_cast<int16_t>(body.y + 92);
  screen.target().fill(fui::makeRect(body.x, ruleY, body.width, toybox::kRule), fui::Paint::solid(fui::Color::Black));

  const insider::Record blank;
  const insider::Record& record = model.record ? *model.record : blank;
  char stats[72];
  std::snprintf(stats, sizeof(stats), "%d ROUNDS   %d CAUGHT   %d AWAY   %d RAN OUT", record.rounds, record.won,
                record.lost, record.outOfTime);
  screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(ruleY + 14), body.width, 22), stats,
                       styled(toybox::kTileFont, fui::TextAlign::Left));

  // The table you are about to deal to. It is the game's own material and it is
  // live: the chips appear and disappear under the steppers, so the number and
  // the thing the number means are never on screen disagreeing.
  screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(body.y + 150), body.width, 22), "AT THE TABLE",
                       styled(toybox::kTileFont, fui::TextAlign::Center));
  constexpr int16_t kChair = 50;
  constexpr int16_t kChairGap = 6;
  const int16_t chairsW = static_cast<int16_t>(model.players * kChair + (model.players - 1) * kChairGap);
  const int16_t chairX = static_cast<int16_t>(body.x + (body.width - chairsW) / 2);
  for (int i = 0; i < model.players; ++i) {
    seat(screen, fui::makeRect(static_cast<int16_t>(chairX + i * (kChair + kChairGap)),
                               static_cast<int16_t>(body.y + 180), kChair, kChair),
         i, SeatLook::Plain);
  }

  // The ornament: this table's last sixteen rounds. It passes the test that
  // rules out wallpaper -- a screenshot of it is different on every device,
  // because it is nobody else's evening.
  screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(body.y + 270), body.width, 22), "LAST 16 ROUNDS",
                       styled(toybox::kTileFont, fui::TextAlign::Center));
  constexpr int16_t kGridW = 398;
  constexpr int16_t kGridH = 94;
  const fui::Rect grid = fui::makeRect(static_cast<int16_t>(body.x + (body.width - kGridW) / 2),
                                       static_cast<int16_t>(body.y + 312), kGridW, kGridH);
  brackets(screen, fui::makeRect(static_cast<int16_t>(grid.x - 20), static_cast<int16_t>(grid.y - 20),
                                 static_cast<int16_t>(kGridW + 40), static_cast<int16_t>(kGridH + 40)),
           30);
  recordGrid(screen, grid, record);

  // Bottom-anchored doors, quietest first: takeBottom pops upward, so the row
  // taken first ends up lowest.
  fui::ButtonProps rules;
  rules.label = "HOW TO PLAY";
  rules.action = ActionRules;
  rules.text = styled(toybox::kUiFont, fui::TextAlign::Center);
  rules.styles = toybox::rowStyles();
  rules.radius = 10;
  screen.button(rules, fui::LayoutAnchor::Bottom);

  fui::ButtonProps deal;
  deal.label = "DEAL";
  deal.action = ActionDeal;
  deal.text = styled(toybox::kDisplayFont, fui::TextAlign::Center, fui::Color::White);
  deal.styles = toybox::invertedStyles();
  deal.radius = 10;
  screen.button(deal, fui::LayoutAnchor::Bottom);
}

// ---------------------------------------------------------------------------

void buildPass(toybox::Screen& screen, const PassModel& model) {
  char progress[16];
  std::snprintf(progress, sizeof(progress), "%d OF %d", model.seat + 1, model.players);
  // Not the game's name, which is also the name of one of the roles: on the
  // card an Insider is handed, the band would then say INSIDER above a card
  // saying INSIDER, and on everybody else's it would sit above CITIZEN looking
  // like it meant something. The other two moments in a round already name
  // themselves (QUESTIONS, THE VOTE); this one does too.
  chrome(screen, "PASS IT ON", progress);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  const fui::Rect body = screen.body();

  if (!model.revealed) {
    // How far round the table we are, anchored to the bottom margin: it is
    // context rather than the point, and keeping it out of the upper half is
    // what lets the ask sit on the centre of the screen.
    constexpr int16_t kDot = 34;
    constexpr int16_t kDotGap = 12;
    const int16_t row = static_cast<int16_t>(model.players * kDot + (model.players - 1) * kDotGap);
    const int16_t startX = static_cast<int16_t>(body.x + (body.width - row) / 2);
    for (int i = 0; i < model.players; ++i) {
      const fui::Rect at = fui::makeRect(static_cast<int16_t>(startX + i * (kDot + kDotGap)),
                                         static_cast<int16_t>(body.bottom() - kDot), kDot, kDot);
      if (i < model.seat) {
        screen.target().fill(at, fui::Paint::solid(fui::Color::Black), 8);
      } else if (i == model.seat) {
        screen.target().stroke(at, fui::Paint::solid(fui::Color::Black), toybox::kFrame, 8);
      } else {
        screen.target().stroke(at, fui::Paint::dither(fui::Color::DarkGray), toybox::kHairline, 8);
      }
    }

    // The whole body is the target: this screen exists in the half second while
    // the device is being put into somebody's hand, and making them aim at a
    // button then is the one place a big region is right. The emptiness around
    // the ask is the control, which is why nothing else is competing for it.
    //
    // Centred on the whole screen, not on the body under the header, and not on
    // what is left under the dots. Mario's call, and he is right: this screen
    // has one thing on it, so the eye expects that thing in the middle of the
    // panel. Centring inside the body instead puts it 48px low, which does not
    // look like a choice -- it looks like it slipped.
    constexpr int16_t kAsk = 52 + 8 + 30;
    const int16_t top = static_cast<int16_t>((screen.device().screen().height - kAsk) / 2);
    char who[20];
    std::snprintf(who, sizeof(who), "PLAYER %d", model.seat + 1);
    screen.target().text(fui::makeRect(body.x, top, body.width, 52), who,
                         styled(toybox::kDisplayFont, fui::TextAlign::Center));
    screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(top + 60), body.width, 30),
                         "TAP TO SEE YOUR ROLE", styled(toybox::kUiFont, fui::TextAlign::Center));
    screen.frame().hit(body, ActionAdvance, 0);
    return;
  }

  // Revealed. Here the tap has to be aimed: an accidental one takes the card
  // away and there is no going back to it.
  char onward[28];
  if (model.lastSeat) {
    std::snprintf(onward, sizeof(onward), "START THE CLOCK");
  } else {
    std::snprintf(onward, sizeof(onward), "PASS TO PLAYER %d", model.seat + 2);
  }
  fui::ButtonProps pass;
  pass.label = onward;
  pass.action = ActionAdvance;
  pass.text = styled(toybox::kUiFont, fui::TextAlign::Center, fui::Color::White);
  pass.styles = toybox::invertedStyles();
  pass.radius = 10;
  screen.button(pass, fui::LayoutAnchor::Bottom);

  // Whatever is left after the button is the card, so it grows to the screen
  // instead of floating in it with a gap underneath.
  const fui::Rect card = screen.body().inset(fui::Insets{0, 0, toybox::kGutter, 0});
  brackets(screen, card, 40);

  const bool knowsWord = model.role != Role::Citizen;
  constexpr int16_t kArt = 96;
  // Must match what the branches below actually advance y by, or the block
  // centres against a height it does not have and sits high in the card.
  const int16_t blockH = knowsWord ? static_cast<int16_t>(kArt + 24 + 44 + 118 + 44)
                                   : static_cast<int16_t>(kArt + 24 + 44 + 20 + 76);
  int16_t y = static_cast<int16_t>(card.y + (card.height - blockH) / 2);

  screen.target().bitmap(fui::makeRect(static_cast<int16_t>(card.x + (card.width - kArt) / 2), y, kArt, kArt),
                         fui::bitmapFromIcon(roleArt(model.role)), fui::BitmapMode::Contain,
                         fui::Paint::solid(fui::Color::Black));
  y = static_cast<int16_t>(y + kArt + 24);

  screen.target().text(fui::makeRect(card.x, y, card.width, 44), roleName(model.role),
                       styled(toybox::kDisplayFont, fui::TextAlign::Center));
  y = static_cast<int16_t>(y + 44);

  if (knowsWord) {
    screen.target().text(fui::makeRect(card.x, static_cast<int16_t>(y + 22), card.width, 22), "THE WORD IS",
                         styled(toybox::kTileFont, fui::TextAlign::Center));
    fui::TextStyle word = styled(toybox::kDisplayFont, fui::TextAlign::Center);
    word.font = fitted(screen, model.word, static_cast<int16_t>(card.width - 40), word);
    screen.target().text(fui::makeRect(card.x, static_cast<int16_t>(y + 48), card.width, 46), model.word, word);
    y = static_cast<int16_t>(y + 118);
  } else {
    y = static_cast<int16_t>(y + 20);
  }

  // No dead space at the bottom of the card: whichever role it is, the last
  // thing on it is the one instruction that role needs.
  fui::TextStyle advice = styled(knowsWord ? toybox::kTileFont : toybox::kUiFont, fui::TextAlign::Center);
  advice.maxLines = 2;
  screen.target().text(fui::makeRect(static_cast<int16_t>(card.x + 30), y, static_cast<int16_t>(card.width - 60),
                                     knowsWord ? 44 : 76),
                       roleAdvice(model.role), advice);
}

// ---------------------------------------------------------------------------

void buildQuestions(toybox::Screen& screen, const QuestionsModel& model) {
  char count[16];
  std::snprintf(count, sizeof(count), "%d PLAYERS", model.players);
  chrome(screen, "QUESTIONS", count);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::ButtonProps got;
  got.label = "WE SAID THE WORD";
  got.action = ActionFoundWord;
  got.text = styled(toybox::kUiFont, fui::TextAlign::Center, fui::Color::White);
  got.styles = toybox::invertedStyles();
  got.radius = 10;
  screen.button(got, fui::LayoutAnchor::Bottom);

  const fui::Rect body = screen.body();

  // One block, centred: who to ask, how long is left, and what a question may
  // be. Three separate things floating at fixed offsets read as a screen with a
  // hole in it, which is what the first version was.
  constexpr int16_t kAsk = 30;
  constexpr int16_t kDigits = 60;
  constexpr int16_t kBarH = 46;
  constexpr int16_t kHint = 22;
  constexpr int16_t kBlock = kAsk + 20 + kDigits + 16 + kBarH + 26 + kHint;
  int16_t y = static_cast<int16_t>(body.y + (body.height - kBlock) / 2);

  char asking[28];
  std::snprintf(asking, sizeof(asking), "PLAYER %d ANSWERS", model.masterSeat + 1);
  screen.target().text(fui::makeRect(body.x, y, body.width, kAsk), asking,
                       styled(toybox::kUiFont, fui::TextAlign::Center));
  y = static_cast<int16_t>(y + kAsk + 20);

  char digits[8];
  formatClock(model.secondsLeft, digits, sizeof(digits));
  screen.target().text(fui::makeRect(body.x, y, body.width, kDigits), digits,
                       styled(toybox::kDisplayFont, fui::TextAlign::Center));
  y = static_cast<int16_t>(y + kDigits + 16);

  // One trough with the remaining time filled inside it, not ten free-standing
  // blocks: at ten the bar read as a barcode and at full it was a slab of ink.
  // A bounded outline with a shrinking fill says "this is emptying" in one look
  // from the other side of a table, and the black is what leaves rather than
  // what sits there -- which is also the cheaper half of the ink budget, since
  // an emptying bar only ever repaints its own leading edge.
  const fui::Rect trough = fui::makeRect(body.x, y, body.width, kBarH);
  screen.target().stroke(trough, fui::Paint::solid(fui::Color::Black), toybox::kFrame, 10);

  constexpr int kSegments = 10;
  const fui::Rect inner = trough.inset(fui::Insets{toybox::kFrame + 2, toybox::kFrame + 2, toybox::kFrame + 2,
                                                   toybox::kFrame + 2});
  const int left = model.secondsLeft <= 0
                       ? 0
                       : (model.secondsLeft * kSegments + insider::kQuestionSeconds - 1) / insider::kQuestionSeconds;
  if (left > 0) {
    const int16_t fillW = static_cast<int16_t>(inner.width * left / kSegments);
    screen.target().fill(fui::makeRect(inner.x, inner.y, fillW, inner.height), fui::Paint::solid(fui::Color::Black), 6);
    // Paper-coloured separators, so the fill is readable as a count of blocks
    // and not just a length somebody has to estimate.
    for (int i = 1; i < left; ++i) {
      const int16_t at = static_cast<int16_t>(inner.x + inner.width * i / kSegments);
      screen.target().fill(fui::makeRect(at, inner.y, 3, inner.height), fui::Paint::solid(fui::Color::White));
    }
  }
  y = static_cast<int16_t>(y + kBarH + 26);

  screen.target().text(fui::makeRect(body.x, y, body.width, kHint), "ASK ANYTHING. YES OR NO ONLY.",
                       styled(toybox::kTileFont, fui::TextAlign::Center));
}

// ---------------------------------------------------------------------------

void buildVote(toybox::Screen& screen, const VoteModel& model) {
  chrome(screen, "THE VOTE", nullptr);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  // The confirmation is a label you were going to read anyway. Nothing is
  // destroyed by choosing; the button is what settles the round.
  const bool ready = model.chosen != VoteModel::kNothingChosen;
  char confirm[28];
  if (!ready) {
    std::snprintf(confirm, sizeof(confirm), "CHOOSE SOMEBODY");
  } else if (model.chosen == kNoInsider) {
    std::snprintf(confirm, sizeof(confirm), "SAY NOBODY");
  } else {
    std::snprintf(confirm, sizeof(confirm), "ACCUSE PLAYER %d", model.chosen + 1);
  }
  fui::ButtonProps go;
  go.label = confirm;
  go.action = ready ? ActionConfirmVote : fui::NO_ACTION;
  go.text = styled(toybox::kUiFont, fui::TextAlign::Center, fui::Color::White);
  go.styles = ready ? toybox::invertedStyles() : toybox::disabledButtonStyles();
  go.radius = 10;
  screen.button(go, fui::LayoutAnchor::Bottom);

  const fui::Rect hint = screen.takeBottom(44, toybox::kGutter);
  screen.target().text(hint, "SOMETIMES THE ROLE WAS NEVER DEALT.",
                       styled(toybox::kTileFont, fui::TextAlign::Center));

  fui::ButtonProps nobody;
  nobody.label = "NOBODY WAS";
  nobody.action = ActionAccuse;
  nobody.value = static_cast<int16_t>(kNoInsider);
  nobody.text = styled(toybox::kUiFont, fui::TextAlign::Center,
                       model.chosen == kNoInsider ? fui::Color::White : fui::Color::Black);
  nobody.styles = model.chosen == kNoInsider ? toybox::invertedStyles() : toybox::rowStyles();
  nobody.radius = 10;
  screen.button(nobody, fui::LayoutAnchor::Bottom);

  const fui::Rect body = screen.body();
  screen.target().text(fui::makeRect(body.x, body.y, body.width, 30), "WHO WAS THE INSIDER?",
                       styled(toybox::kUiFont, fui::TextAlign::Center));

  // Four across, two down, and the second row's space is reserved whether or
  // not it is used: a grid that grows a row between five players and six would
  // move every control under it, and a layout that reflows mid-evening is worse
  // than one with a little air in it.
  constexpr int kCols = 4;
  constexpr int16_t kChipGap = 14;
  const int16_t chip = static_cast<int16_t>((body.width - (kCols - 1) * kChipGap) / kCols);
  const int16_t gridY = static_cast<int16_t>(body.y + 52);
  for (int i = 0; i < model.players; ++i) {
    const fui::Rect at = fui::makeRect(static_cast<int16_t>(body.x + (i % kCols) * (chip + kChipGap)),
                                       static_cast<int16_t>(gridY + (i / kCols) * (chip + kChipGap)), chip, chip);
    if (i == model.masterSeat) {
      // The Master cannot be the Insider, so their seat is shown and cannot be
      // chosen. Dimmed rather than absent: a hole in the grid would read as a
      // drawing bug, and the label is what says why.
      seat(screen, at, i, SeatLook::Dimmed);
      screen.target().text(fui::makeRect(at.x, static_cast<int16_t>(at.bottom() - 24), at.width, 20), "MASTER",
                           styled(toybox::kTileFont, fui::TextAlign::Center));
      continue;
    }
    seat(screen, at, i, i == model.chosen ? SeatLook::Filled : SeatLook::Plain);
    screen.frame().hit(at, ActionAccuse, static_cast<int16_t>(i));
  }
}

// ---------------------------------------------------------------------------

void buildReveal(toybox::Screen& screen, const RevealModel& model) {
  chrome(screen, "INSIDER", nullptr);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  const fui::Rect body = screen.body();

  const bool hadInsider = model.insiderSeat != kNoInsider;
  const char* verdict = "TIME UP";
  if (model.outcome == Outcome::Won) {
    verdict = hadInsider ? "CAUGHT" : "NOBODY, AND YOU KNEW IT";
  } else if (model.outcome == Outcome::Lost) {
    verdict = hadInsider ? "THEY GOT AWAY" : "THERE WAS NOBODY";
  }

  // Solid, at full width, once, on the frame the activity spends a full refresh
  // on. This is the payoff and the flash is the punctuation.
  const fui::Rect capsule = fui::makeRect(body.x, body.y, body.width, 84);
  screen.target().fill(capsule, fui::Paint::solid(fui::Color::Black), toybox::kPillRadius);
  const int16_t line = screen.target().lineHeight(toybox::kDisplayFont);
  screen.target().text(fui::makeRect(static_cast<int16_t>(capsule.x + 12),
                                     static_cast<int16_t>(capsule.y + (capsule.height - line) / 2),
                                     static_cast<int16_t>(capsule.width - 24), line),
                       verdict, styled(toybox::kDisplayFont, fui::TextAlign::Center, fui::Color::White));

  const int16_t seatsY = static_cast<int16_t>(body.y + 130);
  if (hadInsider) {
    screen.target().text(fui::makeRect(body.x, seatsY, body.width, 24), "THE INSIDER WAS",
                         styled(toybox::kTileFont, fui::TextAlign::Center));
    const fui::Rect chip = fui::makeRect(static_cast<int16_t>(body.x + (body.width - 96) / 2),
                                         static_cast<int16_t>(seatsY + 34), 96, 96);
    seat(screen, chip, model.insiderSeat, SeatLook::Filled);
  } else {
    fui::TextStyle style = styled(toybox::kUiFont, fui::TextAlign::Center);
    style.maxLines = 2;
    screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(seatsY + 30), body.width, 70),
                         "THERE WAS NO INSIDER.\nTHE ROLE WAS NEVER DEALT.", style);
  }

  // What the accusation was, when it was not the answer. Without this the
  // screen says who it was but never says what the table decided, and the
  // argument that follows has nothing to point at.
  if (model.outcome == Outcome::Lost) {
    char said[40];
    if (model.accused == kNoInsider) {
      std::snprintf(said, sizeof(said), "YOU SAID NOBODY");
    } else {
      std::snprintf(said, sizeof(said), "YOU ACCUSED PLAYER %d", model.accused + 1);
    }
    screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(seatsY + 150), body.width, 24), said,
                         styled(toybox::kTileFont, fui::TextAlign::Center));
  }

  const int16_t wordY = static_cast<int16_t>(body.y + 400);
  screen.target().text(fui::makeRect(body.x, wordY, body.width, 24), "THE WORD WAS",
                       styled(toybox::kTileFont, fui::TextAlign::Center));
  screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(wordY + 30), body.width, 46), model.word,
                       styled(toybox::kDisplayFont, fui::TextAlign::Center));

  fui::ButtonProps done;
  done.label = "BACK TO THE MENU";
  done.action = ActionDone;
  done.text = styled(toybox::kUiFont, fui::TextAlign::Center);
  done.styles = toybox::rowStyles();
  done.radius = 10;
  screen.button(done, fui::LayoutAnchor::Bottom);

  fui::ButtonProps again;
  again.label = "DEAL AGAIN";
  again.action = ActionPlayAgain;
  again.text = styled(toybox::kDisplayFont, fui::TextAlign::Center, fui::Color::White);
  again.styles = toybox::invertedStyles();
  again.radius = 10;
  screen.button(again, fui::LayoutAnchor::Bottom);
}

// ---------------------------------------------------------------------------

void buildRules(toybox::Screen& screen) {
  chrome(screen, "HOW TO PLAY", nullptr);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  const fui::Rect body = screen.body();

  // Short on purpose. The card each player is handed already tells them how to
  // play their own role, which is the only rules text anybody reads at a table.
  static const char* kLines[] = {
      "ONE OF YOU IS THE MASTER AND KNOWS A",
      "SECRET WORD. ONE OF YOU IS THE INSIDER",
      "AND KNOWS IT TOO.",
      "",
      "EVERYONE ASKS THE MASTER YES-OR-NO",
      "QUESTIONS UNTIL SOMEBODY SAYS THE WORD",
      "OUT LOUD. YOU HAVE FIVE MINUTES.",
      "",
      "THE INSIDER WANTS THE WORD FOUND, BUT",
      "NOT TO BE THE ONE WHO FOUND IT.",
      "",
      "THEN YOU VOTE. NAME THE INSIDER AND THE",
      "TABLE WINS. NAME THE WRONG PERSON AND",
      "THE INSIDER WINS.",
      "",
      "ONE ROLE IS THROWN AWAY BEFORE DEALING,",
      "SO SOMETIMES THERE IS NO INSIDER AT ALL",
      "AND THE RIGHT ANSWER IS NOBODY.",
  };
  constexpr int kCount = static_cast<int>(sizeof(kLines) / sizeof(kLines[0]));
  const auto style = styled(toybox::kUiFont, fui::TextAlign::Left);
  const int16_t line = screen.target().lineHeight(toybox::kUiFont);
  for (int i = 0; i < kCount; ++i) {
    screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(body.y + i * line), body.width, line), kLines[i],
                         style);
  }

  fui::ButtonProps back;
  back.label = "BACK";
  back.action = ActionBack;
  back.text = styled(toybox::kUiFont, fui::TextAlign::Center, fui::Color::White);
  back.styles = toybox::invertedStyles();
  back.radius = 10;
  screen.button(back, fui::LayoutAnchor::Bottom);
}

}  // namespace insiderui
