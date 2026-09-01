#include "TriviaScreens.h"

#include <cstdio>

namespace triviaui {
namespace {

constexpr int16_t kMargin = 16;
constexpr int16_t kFooterHeight = 96;

fui::TextStyle textStyle(const fui::FontId font, const fui::TextAlign align,
                         const fui::Color colour = fui::Color::Black) {
  fui::TextStyle style;
  style.font = font;  // named even when it is the slot the component defaults to
  style.align = align;
  style.color = colour;
  return style;
}

// All-caps chrome: safe to ink-centre, because every glyph sits on the baseline.
void drawLabel(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
               const fui::TextAlign align, const toybox::CutMetrics& cut, const fui::Color colour = fui::Color::Black) {
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
    const fui::Rect box{static_cast<int16_t>(screen.body().x), bandTop,
                        static_cast<int16_t>(screen.body().width - kMargin), toybox::kHeaderHeight};
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
  drawLabel(screen, box, label, toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut, fui::Color::White);
  screen.frame().hit(box, action);
}

constexpr int16_t kAsideWidth = 132;
constexpr int16_t kAsideGap = 12;

// The narrow outlined box at the right of the footer. Outlined rather than
// filled because it is the rarer choice, and the black you can afford is
// inversely proportional to how often you want it pressed.
//
// Split out so it can be drawn WITHOUT a primary beside it, and so both callers
// derive the same x. A way out that moves under the finger when the question is
// answered would be its own bug.
void drawAsideAction(toybox::Screen& screen, const char* label, const fui::ActionId action) {
  const fui::Rect body = screen.body();
  const int16_t full = static_cast<int16_t>(body.width - kMargin * 2);
  const fui::Rect aside{static_cast<int16_t>(body.x + kMargin + full - kAsideWidth),
                        static_cast<int16_t>(footerTop(screen) + 16), kAsideWidth, 64};
  screen.target().stroke(aside, fui::Paint::solid(fui::Color::Black), 2);
  drawLabel(screen, aside, label, toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut);
  screen.frame().hit(aside, action);
}

// The primary action, plus the narrower outlined one beside it.
void drawActionPair(toybox::Screen& screen, const char* primary, const fui::ActionId primaryAction,
                    const char* secondary, const fui::ActionId secondaryAction) {
  const fui::Rect body = screen.body();
  const int16_t full = static_cast<int16_t>(body.width - kMargin * 2);
  const int16_t wide = static_cast<int16_t>(full - kAsideWidth - kAsideGap);

  const fui::Rect main{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(footerTop(screen) + 16), wide, 64};
  screen.target().fill(main, fui::Paint::solid(fui::Color::Black));
  drawLabel(screen, main, primary, toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut, fui::Color::White);
  screen.frame().hit(main, primaryAction);

  drawAsideAction(screen, secondary, secondaryAction);
}

// ---------------------------------------------------------------------------
// The question. Chrome at the top, the clue as the card's face, one action
// under it -- so it reads as the same device as the rest of the shelf.
//
// Chosen from three arrangements rendered side by side (the others set the clue
// as a left-ragged column, and as the whole panel with no header at all). This
// one won on looking like the fork; see docs/apps/trivia.md.
void questionCard(toybox::Screen& screen, const QuestionModel& model) {
  chrome(screen, "TRIVIA", model.answer != nullptr ? "ANSWER" : "QUESTION");
  const fui::Rect body = screen.body();

  drawDifficulty(screen, static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.y + 20), model.difficulty);

  const int16_t top = static_cast<int16_t>(body.y + 56);
  const int16_t height = static_cast<int16_t>(footerTop(screen) - top - 24);
  drawProse(
      screen,
      fui::Rect{static_cast<int16_t>(body.x + kMargin), top, static_cast<int16_t>(body.width - kMargin * 2), height},
      model.clue, fui::TextAlign::Center);

  if (model.answer != nullptr) {
    hairline(screen, static_cast<int16_t>(footerTop(screen) - 96));
    drawLabel(screen, fui::Rect{body.x, static_cast<int16_t>(footerTop(screen) - 84), body.width, 64}, model.answer,
              toybox::kDisplayFont, fui::TextAlign::Center, toybox::kLargeCut);
    // FLAG is only offered once the answer is showing: you cannot judge a
    // question bad until you have seen what it claims the answer is.
    drawActionPair(screen, "NEXT", ActionNext, "FLAG", ActionFlag);
  } else {
    drawAction(screen, "REVEAL", ActionReveal);
  }
}

}  // namespace

void buildQuestion(toybox::Screen& screen, const QuestionModel& model) { questionCard(screen, model); }

void buildNotice(toybox::Screen& screen, const NoticeModel& model) {
  chrome(screen, "TRIVIA", nullptr);
  const fui::Rect body = screen.body();

  const int16_t top = static_cast<int16_t>(body.y + 96);
  drawLabel(screen,
            fui::Rect{static_cast<int16_t>(body.x + kMargin), top, static_cast<int16_t>(body.width - kMargin * 2), 64},
            model.headline, toybox::kDisplayFont, fui::TextAlign::Center, toybox::kLargeCut);
  drawProse(screen,
            fui::Rect{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(top + 88),
                      static_cast<int16_t>(body.width - kMargin * 2), 260},
            model.body, fui::TextAlign::Center);
  if (model.actionLabel != nullptr) drawAction(screen, model.actionLabel, model.action);
}

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  chrome(screen, "TRIVIA", nullptr);
  const fui::Rect body = screen.body();

  fui::ListProps list;
  list.action = ActionMenuRow;
  list.selectedIndex = model.selected;

  fui::ListItem items[static_cast<int>(MenuRow::Count)];
  items[0].label = "QUIZMASTER";
  // Measured, not guessed: at the 20px UI cut these run off the panel and are
  // truncated with an ellipsis Jersey has no glyph for, so the line just stops.
  items[0].subtitle = "Read it out, argue, reveal";
  items[0].actionValue = 0;
  items[1].label = "SOLO";
  items[1].subtitle = "Four options, scored";
  items[1].actionValue = 1;
  items[2].label = "DIFFICULTY";
  items[2].subtitle = model.difficulty == 0 ? "Any difficulty" : "One level only";
  items[2].actionValue = 2;

  list.items = items;
  list.count = static_cast<uint16_t>(MenuRow::Count);
  screen.list(list);

  // A record line at the foot, so the front door is a page rather than three
  // rows with a hole under them.
  if (model.packCount > 0) {
    char line[48];
    std::snprintf(line, sizeof(line), "%u QUESTIONS ON THE CARD", static_cast<unsigned>(model.packCount));
    const int16_t footY = static_cast<int16_t>(body.y + body.height - 72);
    hairline(screen, footY);
    drawLabel(screen, fui::Rect{body.x, static_cast<int16_t>(footY + 14), body.width, 44}, line, toybox::kSmallFont,
              fui::TextAlign::Center, toybox::kButtonCut);
  }
}

void buildChoice(toybox::Screen& screen, const ChoiceModel& model) {
  char score[16] = {};
  if (model.asked > 0) {
    std::snprintf(score, sizeof(score), "%d/%d", model.right, model.asked);
  }
  chrome(screen, "TRIVIA", model.asked > 0 ? score : nullptr);
  const fui::Rect body = screen.body();

  drawDifficulty(screen, static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.y + 18), model.difficulty);

  // The four options are anchored to the bottom and the clue takes what is
  // left, so the screen has no hole under it whatever the clue's length. A
  // fixed clue box leaves a short clue floating above a gap, and dead space at
  // the bottom is a real defect on a panel that holds its image for hours.
  const int16_t optionsHeight = static_cast<int16_t>(trivia::kOptions * 70);
  const int16_t clueTop = static_cast<int16_t>(body.y + 54);
  const int16_t clueHeight = static_cast<int16_t>(footerTop(screen) - clueTop - optionsHeight - 12);
  drawProse(screen,
            fui::Rect{static_cast<int16_t>(body.x + kMargin), clueTop, static_cast<int16_t>(body.width - kMargin * 2),
                      clueHeight},
            model.clue, fui::TextAlign::Left);

  int16_t y = static_cast<int16_t>(footerTop(screen) - optionsHeight);
  for (int i = 0; i < trivia::kOptions; ++i) {
    const fui::Rect box{static_cast<int16_t>(body.x + kMargin), y, static_cast<int16_t>(body.width - kMargin * 2), 62};
    const bool isCorrect = model.chosen >= 0 && i == model.correct;
    const bool isWrongPick = model.chosen == i && i != model.correct;

    if (isCorrect) {
      screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
    } else {
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), static_cast<uint8_t>(isWrongPick ? 3 : 1));
    }
    drawLabel(screen, box, model.option[i] != nullptr ? model.option[i] : "", toybox::kSmallFont,
              fui::TextAlign::Center, toybox::kButtonCut, isCorrect ? fui::Color::White : fui::Color::Black);

      // The player's own pick, marked so it cannot be missed. Until now the only
      // difference was a 3px stroke against a 1px one, and a cold tester read
      // that as a leftover focus ring and trained themselves to ignore it -- so
      // when the solid box was not where they had tapped, they could not tell
      // whether they had mis-tapped, misremembered, or been scored wrongly.
      // That ambiguity is what makes someone quietly conclude they are bad at
      // trivia instead of reporting a bug. White on the correct box, which is
      // already solid black; black on any other.
      if (model.chosen == i) {
        const fui::Rect tab{static_cast<int16_t>(box.x + 5), static_cast<int16_t>(box.y + 5), 7,
                            static_cast<int16_t>(box.height - 10)};
        screen.target().fill(tab, fui::Paint::solid(isCorrect ? fui::Color::White : fui::Color::Black));
      }

      // The index MUST be passed. Frame::hit's value parameter defaults to 0,
      // so all four options registered as option 1 -- and the handler, which
      // reads value correctly, scored the top slot wherever the finger landed.
      // Solo play was decided entirely by whether the answer happened to be
      // first: 3/12 measured, which is chance. Buttons were unaffected, which
      // is why every check passed.
    if (model.chosen < 0) screen.frame().hit(box, ActionOption, static_cast<int16_t>(i));
    y = static_cast<int16_t>(y + 70);
  }

  // END is present BEFORE an answer too. Solo had no way out at all: no footer
  // action, no header target, and this app is touch-only, so Back did nothing.
  // A cold tester could escape only with the HOME key, which also meant there
  // was no way to finish deliberately and see a score. One omission, not two.
  if (model.chosen >= 0) {
    drawActionPair(screen, "NEXT", ActionNext, "END", ActionQuit);
  } else {
    drawAsideAction(screen, "END", ActionQuit);
  }
}

}  // namespace triviaui
