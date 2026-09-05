#include "TriviaScreens.h"

#include <cstdio>

#include "../ui/ToyboxFormat.h"

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
    // From the band's VISIBLE top, matching headerBand()'s own centring. Boxed
    // over the whole band instead, the label centres partly in rows the bezel
    // covers and rides above the title it is meant to sit beside.
    const int16_t bandTop = static_cast<int16_t>(screen.body().y - toybox::kHeaderHeight);
    const int16_t visibleTop = screen.frame().safeRect().y;
    const fui::Rect box{static_cast<int16_t>(screen.body().x), static_cast<int16_t>(bandTop + visibleTop),
                        static_cast<int16_t>(screen.body().width - kMargin),
                        static_cast<int16_t>(toybox::kHeaderHeight - visibleTop)};
    drawLabel(screen, box, rightLabel, toybox::kSmallFont, fui::TextAlign::Right, toybox::kButtonCut,
              fui::Color::White);
  }
}

// Difficulty as five pips, filled to the level. A number would need a legend;
// five marks need nothing, and they are the same idiom the shelf pages use.
void drawDifficulty(toybox::Screen& screen, const int16_t x, const int16_t y, const int level) {
  // No question, no meter. Drawing five empty pips beside a "no question at
  // this difficulty" message described a question that was not there, and the
  // filled count came from a default rather than from anything the player set.
  if (level <= 0) return;
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

  // Say what the pips ARE. Unlabelled, five of them filled to N reads as
  // "question N of 5" -- a cold tester was certain of that reading and chased a
  // phantom auto-advance bug for two rounds before doubting it. Filled-of-five
  // is the universal shape for progress, so the meter has to name itself.
  const fui::Rect box{static_cast<int16_t>(x + trivia::kDifficulties * (kPip + kGap) + 6), static_cast<int16_t>(y - 5),
                      220, static_cast<int16_t>(kPip + 10)};
  drawLabel(screen, box, "DIFFICULTY", toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut);
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

// Up to two outlined controls on their OWN ROW, immediately above the footer.
//
// This exists so a screen can gain a control without moving the one that is
// already there. The alternative -- widening drawAction into drawActionPair --
// shrinks the primary from full width to `full - kAsideWidth - kAsideGap` and
// moves its centre, so a player who has learned where the big bar is would hit
// the new control instead. This fork has a memory about exactly that
// (same-pixel-different-action), and the HIDDEN notice is the worst place to
// spend it: its primary is what continues the round.
//
// Sized and placed off footerTop, so it tracks the footer rather than being
// pinned to a number that a font change would strand.
void drawSecondRow(toybox::Screen& screen, const char* left, const fui::ActionId leftAction, const char* right,
                   const fui::ActionId rightAction) {
  if (left == nullptr && right == nullptr) return;
  const fui::Rect body = screen.body();
  const int16_t full = static_cast<int16_t>(body.width - kMargin * 2);
  const int16_t top = static_cast<int16_t>(footerTop(screen) - 60);
  const int16_t width = right != nullptr ? static_cast<int16_t>((full - kAsideGap) / 2) : full;

  if (left != nullptr) {
    const fui::Rect box{static_cast<int16_t>(body.x + kMargin), top, width, 52};
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 2);
    drawLabel(screen, box, left, toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut);
    screen.frame().hit(box, leftAction);
  }
  if (right != nullptr) {
    const fui::Rect box{static_cast<int16_t>(body.x + kMargin + full - width), top, width, 52};
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 2);
    drawLabel(screen, box, right, toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut);
    screen.frame().hit(box, rightAction);
  }
}

// Primary, plus TWO narrower outlined ones. Quizmaster needs three: advance,
// reject the question, and leave. Widths derive from the same constants as the
// pair so the right-hand control lines up across every screen in the app.
void drawActionTrio(toybox::Screen& screen, const char* primary, const fui::ActionId primaryAction, const char* second,
                    const fui::ActionId secondAction, const char* third, const fui::ActionId thirdAction) {
  const fui::Rect body = screen.body();
  const int16_t top = static_cast<int16_t>(footerTop(screen) + 16);
  const int16_t full = static_cast<int16_t>(body.width - kMargin * 2);
  const int16_t wide = static_cast<int16_t>(full - 2 * kAsideWidth - 2 * kAsideGap);

  const fui::Rect main{static_cast<int16_t>(body.x + kMargin), top, wide, 64};
  screen.target().fill(main, fui::Paint::solid(fui::Color::Black));
  drawLabel(screen, main, primary, toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut, fui::Color::White);
  screen.frame().hit(main, primaryAction);

  const fui::Rect middle{static_cast<int16_t>(main.x + wide + kAsideGap), top, kAsideWidth, 64};
  screen.target().stroke(middle, fui::Paint::solid(fui::Color::Black), 2);
  drawLabel(screen, middle, second, toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut);
  screen.frame().hit(middle, secondAction);

  drawAsideAction(screen, third, thirdAction);
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
    // END in both states, and in the same place as solo's. Quizmaster had no
    // exit at all: a stranger read questions aloud to a room and the only way
    // out was the HOME key, which drops out of the app. On a handheld with a
    // physical Back button, pressing it and getting nothing is the worst
    // available outcome -- and this app is touch-only, so Back gets nothing.
    drawActionTrio(screen, "NEXT", ActionNext, "HIDE", ActionFlag, "END", ActionQuit);
  } else {
    drawActionPair(screen, "REVEAL", ActionReveal, "END", ActionQuit);
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
  drawSecondRow(screen, model.secondLabel, model.secondAction, model.thirdLabel, model.thirdAction);
  if (model.actionLabel != nullptr) drawAction(screen, model.actionLabel, model.action);
}

// The front door. Three arrangements were rendered side by side before this one
// was chosen: the shipped list plus a hole, an "on this card" panel filling that
// hole with the pack's size, and a stated door with a paragraph of introduction.
//
// This won because the two modes ARE the decision, so they should be the screen.
// The panel version duplicated itself -- the difficulty row and the panel both
// said "Any difficulty" -- and the stated door spent a paragraph saying what the
// two buttons already say, which you would read every time you opened the app.
// See docs/apps/trivia.md.
// "SEEN" rather than "ANSWERED", deliberately: the flag is set when a question
// is SERVED, and Quizmaster serves questions that a room answers out loud and
// the device never scores. Calling that "answered" would be a number the app
// cannot actually stand behind.
//
// A count rather than a bar. At a few hundred of fifty thousand a bar is an
// empty rectangle, which reads as broken rather than as early.
void questionCount(char* out, const size_t n, const uint32_t count, const uint32_t seen) {
  if (count == 0) {
    out[0] = '\0';
    return;
  }
  if (seen == 0) {
    // "1 QUESTIONS ON THE CARD" was on the panel until now, and the
    // one-question seed pack used for screenshots is exactly the case that
    // exposes it.
    std::snprintf(out, n, count == 1 ? "%u QUESTION ON THE CARD" : "%u QUESTIONS ON THE CARD",
                  static_cast<unsigned>(count));
    return;
  }
  std::snprintf(out, n, "%u OF %u SEEN", static_cast<unsigned>(seen), static_cast<unsigned>(count));
}

void difficultyLine(char* out, const size_t n, const int difficulty) {
  if (difficulty == 0) {
    std::snprintf(out, n, "%s", "Any difficulty");
  } else {
    // Derived: "of 5" as a literal would rot the day kDifficulties changes and
    // would still compile.
    std::snprintf(out, n, "Level %d of %d", difficulty, trivia::kDifficulties);
  }
}

// One mode as a large tappable panel. The whole box is the hit region, so the
// target is the thing you can see rather than the words inside it.
void modeCard(toybox::Screen& screen, const fui::Rect& box, const char* title, const char* under,
              const fui::ActionId action, const int16_t value, const bool filled) {
  if (filled) {
    screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
  } else {
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 2);
  }
  const fui::Color ink = filled ? fui::Color::White : fui::Color::Black;
  // Title and line CENTRED AS A GROUP, not pinned to the box's edges. Pinned, a
  // tall card puts the name at the top and the line at the bottom with a hole
  // between them, and reads as an empty panel with two captions.
  const int16_t mid = static_cast<int16_t>(box.y + box.height / 2);
  drawLabel(screen, fui::Rect{box.x, static_cast<int16_t>(mid - 44), box.width, 46}, title, toybox::kDisplayFont,
            fui::TextAlign::Center, toybox::kLargeCut, ink);
  drawLabel(screen, fui::Rect{box.x, static_cast<int16_t>(mid + 8), box.width, 34}, under, toybox::kSmallFont,
            fui::TextAlign::Center, toybox::kButtonCut, ink);
  screen.frame().hit(box, action, value);
}

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  chrome(screen, "TRIVIA", nullptr);
  const fui::Rect body = screen.body();
  char diff[28];
  difficultyLine(diff, sizeof(diff), model.difficulty);

  const int16_t left = static_cast<int16_t>(body.x + kMargin);
  const int16_t wide = static_cast<int16_t>(body.width - kMargin * 2);

  // The cards take the room between the header and the two rows, which sit
  // just above the footer rule. Derived from the panel rather than fixed, so
  // the layout does not need re-tuning if the chrome changes height or a third
  // row ever arrives.
  const int16_t top = static_cast<int16_t>(body.y + 24);
  const int16_t rowH = 62;
  const int16_t rowGap = 12;
  const int16_t rowsH = static_cast<int16_t>(rowH * 2 + rowGap);
  const int16_t rowsY = static_cast<int16_t>(footerTop(screen) - rowsH - 18);
  const int16_t cardH = static_cast<int16_t>((rowsY - top - 16 - 20) / 2);

  modeCard(screen, fui::Rect{left, top, wide, cardH}, "QUIZMASTER", "Read it out, argue, reveal", ActionMenuRow, 0,
           true);
  modeCard(screen, fui::Rect{left, static_cast<int16_t>(top + cardH + 16), wide, cardH}, "SOLO", "Four options, scored",
           ActionMenuRow, 1, false);

  // DIFFICULTY stays on the front door, and the value stays beside it. It is a
  // per-session mood -- easy tonight, hard tomorrow -- changed about as often
  // as the mode is, so it costs no taps and it is readable without opening
  // anything. The app's persistent preference lives one row below, behind a
  // door; see SettingRow in TriviaScreens.h for why the two are not the same
  // kind of thing.
  const fui::Rect diffBox{left, rowsY, wide, rowH};
  screen.target().stroke(diffBox, fui::Paint::solid(fui::Color::Black), 1);
  drawLabel(screen, fui::Rect{static_cast<int16_t>(left + 16), diffBox.y, 200, rowH}, "DIFFICULTY", toybox::kSmallFont,
            fui::TextAlign::Left, toybox::kButtonCut);
  drawLabel(screen, fui::Rect{left, diffBox.y, static_cast<int16_t>(wide - 16), rowH}, diff, toybox::kSmallFont,
            fui::TextAlign::Right, toybox::kButtonCut);
  screen.frame().hit(diffBox, ActionMenuRow, static_cast<int16_t>(MenuRow::Difficulty));

  // The door to the app's own settings. No value on the right: this row is a
  // door, not a setting, and a value column beside SETTINGS would read as one
  // thing the tap changes rather than a screen of several. Chess's start menu
  // does exactly this.
  const fui::Rect settingsBox{left, static_cast<int16_t>(rowsY + rowH + rowGap), wide, rowH};
  screen.target().stroke(settingsBox, fui::Paint::solid(fui::Color::Black), 1);
  drawLabel(screen, fui::Rect{static_cast<int16_t>(left + 16), settingsBox.y, 240, rowH}, "SETTINGS",
            toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut);
  screen.frame().hit(settingsBox, ActionMenuRow, static_cast<int16_t>(MenuRow::Settings));

  if (model.packCount > 0) {
    char count[48];
    questionCount(count, sizeof(count), model.packCount, model.seenCount);
    drawLabel(screen, fui::Rect{body.x, static_cast<int16_t>(footerTop(screen) + 20), body.width, 40}, count,
              toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut);
  }
}

// TRIVIA's own settings. One screen, two rows, and it writes nothing: the
// activity owns both values and this is a picture of them.
//
// A list rather than the hand-drawn boxes the front door uses. Screen::list()
// substitutes the theme into every row (height, gap, side padding, minimum
// touch size) where a stack of stroked rects only does layout, and it is the
// shape chess, forehead and toybattle already settled on -- including the value
// column, where a boolean is the word ON or OFF rather than fui::ListItem's
// `toggle` switch. The switch is not unused in this fork (OpdsFilterActivity
// draws one); it is the wrong pick HERE, because at arm's length on e-ink a
// knob's position is a guess and a word is not, and every settings row this
// screen sits beside in chess and toybattle spells it out.
void buildSettings(toybox::Screen& screen, const SettingsModel& model) {
  chrome(screen, "SETTINGS", nullptr);
  // The list needs a content rect; Trivia's own chrome() leaves the raw body
  // because every other screen here draws straight to the target.
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  // Anchored to the bottom margin BEFORE the rows take the rest, so the list
  // can never grow into it.
  fui::ButtonProps back;
  back.label = "BACK TO MENU";
  back.action = ActionCloseSettings;
  back.borderEdges = fui::EdgesNone;
  screen.button(back, screen.takeBottom(toybox::kPillHeight));

  char hiddenValue[24] = {};
  char hiddenSub[64] = {};

  fui::ListItem rows[static_cast<int>(SettingRow::Count)] = {};
  rows[static_cast<int>(SettingRow::UsCentric)].label = "US QUESTIONS";
  rows[static_cast<int>(SettingRow::UsCentric)].value = model.usCentric ? "ON" : "OFF";
  // Says what OFF costs you, not just what the row is. The pack ships
  // international by default and a player who never turns this on never learns
  // the marked questions were held back at all.
  rows[static_cast<int>(SettingRow::UsCentric)].subtitle = "OFF HIDES CLUES ONLY A US PLAYER WOULD KNOW";

  rows[static_cast<int>(SettingRow::Sync)].label = "SYNC";
  rows[static_cast<int>(SettingRow::Sync)].value = "";
  // Says what the card holds rather than only offering the button, so the row
  // can be read without pressing it. A sync that only spins is the thing card
  // #253 calls "honest and useless".
  rows[static_cast<int>(SettingRow::Sync)].subtitle = model.packLine;

  rows[static_cast<int>(SettingRow::Hidden)].label = "HIDDEN QUESTIONS";
  std::snprintf(hiddenValue, sizeof(hiddenValue), "%u", static_cast<unsigned>(model.hidden));
  rows[static_cast<int>(SettingRow::Hidden)].value = hiddenValue;
  // A total and a way back. HIDE was permanent and silent before this: a
  // mis-tap could not be undone and nothing anywhere said how many had gone.
  if (model.hidden == 0) {
    std::snprintf(hiddenSub, sizeof(hiddenSub), "NOTHING IS HIDDEN ON THIS CARD");
  } else if (model.pending > 0) {
    std::snprintf(hiddenSub, sizeof(hiddenSub), "TAP TO SHOW THEM ALL AGAIN. %u NOT YET SENT",
                  static_cast<unsigned>(model.pending));
  } else {
    std::snprintf(hiddenSub, sizeof(hiddenSub), "TAP TO SHOW THEM ALL AGAIN");
  }
  rows[static_cast<int>(SettingRow::Hidden)].subtitle = hiddenSub;

  for (int i = 0; i < static_cast<int>(SettingRow::Count); ++i) {
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(SettingRow::Count);
  // Touch-only, like the rest of this app: nothing is "selected".
  list.selectedIndex = -1;
  list.action = ActionSettingsRow;
  // Set EXPLICITLY. FONT_SLOT_SMALL is 0, so a style naming only the font is
  // indistinguishable from a default-constructed one and Screen::list quietly
  // substitutes the theme's 20px body cut -- which is how FOREHEAD shipped a
  // subtitle drawn as large as the label it was subtitling, truncated with an
  // ellipsis its cut has no glyph for. maxLines makes the style caller-owned
  // and gives a long sentence a real second line to wrap into.
  list.subtitleText.font = toybox::kSmallFont;
  list.subtitleText.maxLines = 2;
  screen.list(list);
}

// The WHY? list. Its own screen, so the play screens gain nothing and the rows
// get room to be legible -- and so this list can grow without any question
// screen changing shape.
//
// Reached only from the HIDDEN notice, and only after the report is already
// filed. That ordering is the design: one tap reports, and the reason is an
// optional second. A player who walks away here has still reported the question.
void buildReasons(toybox::Screen& screen, const ReasonModel& model) {
  chrome(screen, "WHY?", nullptr);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::ButtonProps back;
  back.label = "NO REASON";
  back.action = ActionCloseReason;
  back.borderEdges = fui::EdgesNone;
  screen.button(back, screen.takeBottom(toybox::kPillHeight));

  fui::ListItem rows[ReasonModel::kMax] = {};
  const int count = model.count < ReasonModel::kMax ? model.count : ReasonModel::kMax;
  for (int i = 0; i < count; ++i) {
    rows[i].label = model.label[i];
    // The row carries the reason's WIRE value, not its position. Ordering the
    // list differently, or hiding a row that does not apply, then cannot change
    // what a report means -- which it would if the handler read the index.
    rows[i].actionValue = static_cast<int16_t>(model.value[i]);
  }

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(count);
  list.selectedIndex = -1;
  list.action = ActionReasonRow;
  // SET EXPLICITLY, and this is the row count's whole margin.
  //
  // The list is virtualised: list.h says only rows that fully fit are "laid
  // out, drawn, and registered for interaction" -- so a row past the bottom is
  // not clipped, it does not exist. No hit region, nothing on screen, and a
  // reason a player can never choose.
  //
  // At the theme's 62px row and a 4px gap this band takes NINE rows, and the
  // list is TEN in solo with US questions off (both conditional rows show at
  // once). Exactly one reason -- TOO EASY, the last -- would have vanished, in
  // one combination, silently. That is the screens-overflow-silently failure
  // with a one-row margin, which is the hardest size to notice.
  //
  // 52 is right on its own terms rather than as a squeeze: these rows carry a
  // label and nothing else -- no subtitle, no value, no icon -- and the theme's
  // 62 is sized for rows that do.
  //
  // MEASURED CAPACITY AT THIS HEIGHT IS kMaxReasonRows, and that constant is
  // the thing to re-derive if this number changes. The band's own height is
  // deliberately not written here: an earlier draft quoted it and was wrong by
  // 17px, which is the derived-facts-written-as-literals shape in a comment
  // rather than in code.
  list.rowHeight = 52;
  screen.list(list);
}

void buildChoice(toybox::Screen& screen, const ChoiceModel& model) {
  char score[toybox::kSlashCounterChars] = {};
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

  // No question at this difficulty: the clue carries the message and there are
  // no options. Four empty boxes were still drawn under it, and they still
  // registered taps -- a hit region does not care that its label is empty, so
  // the screen offered four blank controls that scored a question that was not
  // there. Keyed on the option itself, like `answer` in the model, rather than
  // on a second flag that can disagree with it.
  if (model.option[0] != nullptr) {
    int16_t y = static_cast<int16_t>(footerTop(screen) - optionsHeight);
    for (int i = 0; i < trivia::kOptions; ++i) {
      const fui::Rect box{static_cast<int16_t>(body.x + kMargin), y, static_cast<int16_t>(body.width - kMargin * 2),
                          62};
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
  }

  // END is present BEFORE an answer too. Solo had no way out at all: no footer
  // action, no header target, and this app is touch-only, so Back did nothing.
  // A cold tester could escape only with the HOME key, which also meant there
  // was no way to finish deliberately and see a score. One omission, not two.
  if (model.chosen >= 0) {
    // The SAME trio quizmaster draws, in the same places. Solo had no way to
    // report a question at all, which is the mode where "the options give it
    // away" and "wrong answer" are the two faults a player can actually see.
    //
    // This does move NEXT's centre in solo, which is the cost: the footer goes
    // from a pair to a trio the first time a player meets it after updating.
    // Taken deliberately, because the alternative is two different footers for
    // the same three choices, and because HIDE announces itself and is now
    // undoable from the notice it raises. END, the destructive one, does not
    // move: drawAsideAction derives its x the same way for pair and trio.
    drawActionTrio(screen, "NEXT", ActionNext, "HIDE", ActionFlag, "END", ActionQuit);
  } else {
    drawAsideAction(screen, "END", ActionQuit);
  }
}

}  // namespace triviaui
