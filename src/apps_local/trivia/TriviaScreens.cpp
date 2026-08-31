#include "TriviaScreens.h"

namespace triviaui {
namespace {

constexpr int16_t kMargin = 16;
constexpr int16_t kFooterHeight = 96;

fui::TextStyle textStyle(const fui::FontId font, const fui::TextAlign align,
                         const fui::Color colour = fui::Color::Black) {
  fui::TextStyle style;
  style.font = font;      // named even when it is the slot the component defaults to
  style.align = align;
  style.color = colour;
  return style;
}

// All-caps chrome: safe to ink-centre, because every glyph sits on the baseline.
void drawLabel(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
               const fui::TextAlign align, const toybox::CutMetrics& cut,
               const fui::Color colour = fui::Color::Black) {
  screen.target().text(toybox::inkCentred(box, cut), text, textStyle(font, align, colour));
}

// Mixed-case prose: placed by its box and NEVER ink-centred. inkCentred solves
// for the cap band, so a clue set with it hangs every descender below the box.
//
// TextStyle::maxLines defaults to ONE. A clue handed to text() without raising
// it draws a single line and truncates the rest with U+2026, which Jersey has
// no glyph for -- so the sentence simply stops, the screenshot looks plausible,
// and the half with the answer in it is gone. The line count is derived from
// the box rather than picked, so a taller box wraps further with no second
// number to keep in step. The SDK caps at 16 lines.
void drawProse(toybox::Screen& screen, const fui::Rect& box, const char* text,
               const fui::TextAlign align = fui::TextAlign::Left) {
  fui::TextStyle style = textStyle(toybox::kBodyFont, align);
  const int16_t lineHeight = screen.target().lineHeight(style.font);
  const int lines = lineHeight > 0 ? box.height / lineHeight : 1;
  style.maxLines = static_cast<uint8_t>(lines < 1 ? 1 : (lines > 16 ? 16 : lines));
  screen.target().text(box, text, style);
}

// A hairline across the body. toybox::rule() takes a GfxRenderer, which a
// freestanding builder deliberately does not have, so screens draw their own.
void hairline(toybox::Screen& screen, const int16_t y) {
  const fui::Rect body = screen.body();
  screen.target().fill(fui::makeRect(static_cast<int16_t>(body.x + kMargin), y,
                                     static_cast<int16_t>(body.width - kMargin * 2), toybox::kHairline),
                       fui::Paint::solid(fui::Color::Black));
}

void chrome(toybox::Screen& screen, const char* title, const char* rightLabel) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  // Drawn here rather than handed to header.rightLabel: the component centres
  // each run on its own line box, and the title's cut and the label's differ,
  // so sharing the band leaves their ink visibly apart. Every app in this fork
  // that passes a right label has this.
  if (rightLabel != nullptr && *rightLabel != '\0') {
    const int16_t bandTop = static_cast<int16_t>(screen.body().y - toybox::kHeaderHeight);
    const fui::Rect box{static_cast<int16_t>(screen.body().x),
                        bandTop,
                        static_cast<int16_t>(screen.body().width - kMargin),
                        toybox::kHeaderHeight};
    drawLabel(screen, box, rightLabel, toybox::kSmallFont, fui::TextAlign::Right, toybox::kButtonCut,
              fui::Color::White);
  }
}

// Difficulty as five pips, filled to the level. A number would need a legend;
// five marks need nothing, and they are the same idiom the shelf pages use.
void drawDifficulty(toybox::Screen& screen, const int16_t x, const int16_t y, const int level) {
  constexpr int16_t kPip = 9;
  constexpr int16_t kGap = 7;
  for (int i = 0; i < trivia::kDifficulties; ++i) {
    const fui::Rect pip{static_cast<int16_t>(x + i * (kPip + kGap)), y, kPip, kPip};
    if (i < level) {
      screen.target().fill(pip, fui::Paint::solid(fui::Color::Black));
    } else {
      screen.target().stroke(pip, fui::Paint::solid(fui::Color::Black), static_cast<uint8_t>(1));
    }
  }
}

int16_t footerTop(const toybox::Screen& screen) {
  return static_cast<int16_t>(screen.body().y + screen.body().height - kFooterHeight);
}

// One wide action across the bottom, where a thumb rests.
void drawAction(toybox::Screen& screen, const char* label, const fui::ActionId action) {
  const fui::Rect body = screen.body();
  const fui::Rect box{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(footerTop(screen) + 16),
                      static_cast<int16_t>(body.width - kMargin * 2), 64};
  screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
  drawLabel(screen, box, label, toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut,
            fui::Color::White);
  screen.frame().hit(box, action);
}

// ---------------------------------------------------------------------------
// The three arrangements. Same model, same content, different composition.

// A -- CARD. Chrome at the top, the clue as the card's face, one action under
// it. Closest to the rest of the fork, so it looks like the same device.
void questionCard(toybox::Screen& screen, const QuestionModel& model) {
  chrome(screen, "TRIVIA", model.answer != nullptr ? "ANSWER" : "QUESTION");
  const fui::Rect body = screen.body();

  drawDifficulty(screen, static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.y + 20),
                 model.difficulty);

  const int16_t top = static_cast<int16_t>(body.y + 56);
  const int16_t height = static_cast<int16_t>(footerTop(screen) - top - 24);
  drawProse(screen,
            fui::Rect{static_cast<int16_t>(body.x + kMargin), top,
                      static_cast<int16_t>(body.width - kMargin * 2), height},
            model.clue, fui::TextAlign::Center);

  if (model.answer != nullptr) {
    hairline(screen, static_cast<int16_t>(footerTop(screen) - 96));
    drawLabel(screen,
              fui::Rect{body.x, static_cast<int16_t>(footerTop(screen) - 84), body.width, 64},
              model.answer, toybox::kDisplayFont, fui::TextAlign::Center, toybox::kLargeCut);
    drawAction(screen, "NEXT", ActionNext);
  } else {
    drawAction(screen, "REVEAL", ActionReveal);
  }
}

// B -- PAGE. Editorial. A rule under the chrome, the clue set ragged-right like
// a paragraph rather than centred, the answer arriving in the same column.
void questionPage(toybox::Screen& screen, const QuestionModel& model) {
  chrome(screen, "TRIVIA", nullptr);
  const fui::Rect body = screen.body();

  const int16_t railTop = static_cast<int16_t>(body.y + 18);
  drawDifficulty(screen, static_cast<int16_t>(body.x + kMargin), railTop, model.difficulty);
  hairline(screen, static_cast<int16_t>(railTop + 26));

  const int16_t top = static_cast<int16_t>(railTop + 48);
  const int16_t height = static_cast<int16_t>(footerTop(screen) - top - 16);
  drawProse(screen,
            fui::Rect{static_cast<int16_t>(body.x + kMargin), top,
                      static_cast<int16_t>(body.width - kMargin * 2), height},
            model.clue, fui::TextAlign::Left);

  if (model.answer != nullptr) {
    const int16_t answerTop = static_cast<int16_t>(footerTop(screen) - 92);
    hairline(screen, answerTop);
    drawLabel(screen,
              fui::Rect{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(answerTop + 12),
                        static_cast<int16_t>(body.width - kMargin * 2), 60},
              model.answer, toybox::kDisplayFont, fui::TextAlign::Left, toybox::kLargeCut);
    drawAction(screen, "NEXT", ActionNext);
  } else {
    drawAction(screen, "REVEAL", ActionReveal);
  }
}

// C -- STAGE. No header band. The clue is the whole screen, vertically centred,
// with the instruction as a hairline footnote. The most readable across a table
// and the least like the rest of the fork -- which is the trade.
void stageHairline(toybox::Screen& screen, const int16_t y, const int16_t w) {
  screen.target().fill(fui::makeRect(kMargin, y, static_cast<int16_t>(w - kMargin * 2), toybox::kHairline),
                       fui::Paint::solid(fui::Color::Black));
}

void questionStage(toybox::Screen& screen, const QuestionModel& model) {
  toybox::absoluteChrome(screen);
  const fui::Rect body = screen.body();
  const int16_t w = screen.device().screen().width;
  const int16_t h = screen.device().screen().height;

  drawDifficulty(screen, static_cast<int16_t>((w - (trivia::kDifficulties * 16 - 7)) / 2), 28,
                 model.difficulty);

  const int16_t top = 72;
  const int16_t bottom = static_cast<int16_t>(h - 108);
  drawProse(screen,
            fui::Rect{kMargin, top, static_cast<int16_t>(w - kMargin * 2),
                      static_cast<int16_t>(bottom - top)},
            model.clue, fui::TextAlign::Center);

  if (model.answer != nullptr) {
    stageHairline(screen, static_cast<int16_t>(bottom - 76), w);
    drawLabel(screen, fui::Rect{0, static_cast<int16_t>(bottom - 62), w, 64}, model.answer,
              toybox::kDisplayFont, fui::TextAlign::Center, toybox::kLargeCut);
  }

  stageHairline(screen, static_cast<int16_t>(h - 72), w);
  drawLabel(screen, fui::Rect{0, static_cast<int16_t>(h - 60), w, 44},
            model.answer != nullptr ? "TAP FOR THE NEXT ONE" : "TAP TO REVEAL", toybox::kSmallFont,
            fui::TextAlign::Center, toybox::kButtonCut);

  // The whole screen is the target. A bar game is passed hand to hand and a
  // 64px button is a thing to aim at; the panel is not.
  screen.frame().hit(fui::Rect{0, 0, w, static_cast<int16_t>(h - 40)},
                     model.answer != nullptr ? ActionNext : ActionReveal);
  (void)body;
}

}  // namespace

void buildQuestion(toybox::Screen& screen, const QuestionModel& model) {
  switch (model.layout) {
    case QuestionLayout::Page:
      questionPage(screen, model);
      break;
    case QuestionLayout::Stage:
      questionStage(screen, model);
      break;
    case QuestionLayout::Card:
    default:
      questionCard(screen, model);
      break;
  }
}

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  chrome(screen, "TRIVIA", nullptr);
  const fui::Rect body = screen.body();

  if (model.packMissing) {
    drawProse(screen,
              fui::Rect{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.y + 60),
                        static_cast<int16_t>(body.width - kMargin * 2), 220},
              "No question pack on the card yet. Connect to WiFi and this app will "
              "fetch one, or copy pack.dat into /trivia yourself.",
              fui::TextAlign::Center);
    return;
  }

  fui::ListProps list;
  list.action = ActionMenuRow;
  list.selectedIndex = model.selected;

  fui::ListItem items[static_cast<int>(MenuRow::Count)];
  items[0].label = "QUIZMASTER";
  items[0].subtitle = "One question, everyone argues, tap to reveal";
  items[0].actionValue = 0;
  items[1].label = "SOLO";
  items[1].subtitle = "Four options, the device keeps score";
  items[1].actionValue = 1;
  items[2].label = "DIFFICULTY";
  items[2].subtitle = model.difficulty == 0 ? "Any" : "Set";
  items[2].actionValue = 2;

  list.items = items;
  list.count = static_cast<uint16_t>(MenuRow::Count);
  screen.list(list);
}

void buildChoice(toybox::Screen& screen, const ChoiceModel& model) {
  chrome(screen, "TRIVIA", nullptr);
  const fui::Rect body = screen.body();

  drawDifficulty(screen, static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.y + 18),
                 model.difficulty);

  const int16_t clueTop = static_cast<int16_t>(body.y + 54);
  const int16_t clueHeight = 200;
  drawProse(screen,
            fui::Rect{static_cast<int16_t>(body.x + kMargin), clueTop,
                      static_cast<int16_t>(body.width - kMargin * 2), clueHeight},
            model.clue, fui::TextAlign::Left);

  int16_t y = static_cast<int16_t>(clueTop + clueHeight + 12);
  for (int i = 0; i < trivia::kOptions; ++i) {
    const fui::Rect box{static_cast<int16_t>(body.x + kMargin), y,
                        static_cast<int16_t>(body.width - kMargin * 2), 62};
    const bool isCorrect = model.chosen >= 0 && i == model.correct;
    const bool isWrongPick = model.chosen == i && i != model.correct;

    if (isCorrect) {
      screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
    } else {
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), static_cast<uint8_t>(isWrongPick ? 3 : 1));
    }
    drawLabel(screen, box, model.option[i] != nullptr ? model.option[i] : "", toybox::kSmallFont,
              fui::TextAlign::Center, toybox::kButtonCut,
              isCorrect ? fui::Color::White : fui::Color::Black);
    if (model.chosen < 0) screen.frame().hit(box, ActionOption);
    y = static_cast<int16_t>(y + 70);
  }

  if (model.chosen >= 0) drawAction(screen, "NEXT", ActionNext);
}

}  // namespace triviaui
