#include "WordleScreens.h"

#include <FreeInkUIIcon.h>

#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxMetrics.h"
#include "../ui/ToyboxTokens.h"

// TEMPORARY: which of the three proposed layouts builds. The two that lose are
// deleted with this macro in the commit that builds the winner.
//   1  classic: big tiles, QWERTY with ENTER and delete on the bottom row
//   2  big keys: smaller tiles, taller QWERTY, ENTER and delete as a footer bar,
//      absent letters dithered
//   3  alphabet: A-Z in four rows of seven, absent letters dithered
#ifndef WORDLE_VARIANT
#define WORDLE_VARIANT 1
#endif

namespace wordleui {
namespace {

const fui::Paint kInk = fui::Paint::solid(fui::Color::Black);

void chrome(toybox::Screen& screen, const GameModel& model) {
  fui::HeaderProps header;
  header.title = model.title;
  header.titleText = screen.theme().titleText;
  header.titleText.font = toybox::kDisplayFont;
  header.rightLabel = model.rightLabel;
  header.borderEdges = fui::EdgesNone;
  if (model.rightLabel != nullptr) {
    header.subtitleText = screen.theme().smallText;
    header.subtitleText.font = toybox::kTileFont;
    header.subtitleText.color = fui::Color::White;
    header.subtitleText.align = fui::TextAlign::Right;
  }
  toybox::headerBand(screen, header);
}

void centredText(fui::DrawTarget& target, const fui::Rect& box, const char* text, const fui::FontId font,
                 const fui::Color color) {
  fui::TextStyle style;
  style.font = font;
  style.align = fui::TextAlign::Center;
  style.color = color;
  const int16_t line = target.lineHeight(font);
  target.text(fui::makeRect(box.x, static_cast<int16_t>(box.y + (box.height - line) / 2), box.width, line), text,
              style);
}

// One tile or key. The marks are the agreed ones: right place is solid black,
// in the word is a heavy border with a dot, not in the word is a thin border
// (or, in variant 3, a dithered ground).
void cell(fui::DrawTarget& target, const fui::Rect& box, const char letter, const Mark mark, const fui::FontId font,
          const bool absentDither) {
  const char text[2] = {letter, '\0'};
  switch (mark) {
    case Mark::Empty:
      target.stroke(box, kInk, 1);
      return;
    case Mark::Typed:
      target.stroke(box, kInk, 2);
      centredText(target, box, text, font, fui::Color::Black);
      return;
    case Mark::Absent:
      if (absentDither) {
        target.fill(box, fui::Paint::dither(fui::Color::LightGray));
      } else {
        target.stroke(box, kInk, 1);
      }
      centredText(target, box, text, font, fui::Color::Black);
      return;
    case Mark::Present: {
      target.stroke(box, kInk, 3);
      const int16_t dot = static_cast<int16_t>(box.width >= 48 ? 10 : 7);
      target.fill(
          fui::makeRect(static_cast<int16_t>(box.x + box.width - dot - 4), static_cast<int16_t>(box.y + 4), dot, dot),
          kInk, static_cast<uint8_t>(dot / 2));
      centredText(target, box, text, font, fui::Color::Black);
      return;
    }
    case Mark::Correct:
      target.fill(box, kInk);
      centredText(target, box, text, font, fui::Color::White);
      return;
  }
}

// The six guesses, centred in the body, tiles of `tile` pixels.
int16_t grid(toybox::Screen& screen, const GameModel& model, const int16_t top, const int16_t tile, const int16_t gap,
             const bool absentDither) {
  fui::DrawTarget& target = screen.target();
  const fui::Rect safe = screen.frame().safeRect();
  const int16_t width = static_cast<int16_t>(kLetters * tile + (kLetters - 1) * gap);
  const int16_t left = static_cast<int16_t>(safe.x + (safe.width - width) / 2);
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kLetters; ++c) {
      const fui::Rect box = fui::makeRect(static_cast<int16_t>(left + c * (tile + gap)),
                                          static_cast<int16_t>(top + r * (tile + gap)), tile, tile);
      cell(target, box, model.tiles[r][c].letter, model.tiles[r][c].mark, toybox::kDisplayFont, absentDither);
    }
  }
  return static_cast<int16_t>(top + kRows * tile + (kRows - 1) * gap);
}

void iconKey(fui::DrawTarget& target, const fui::Rect& box, const freeink::Icon& icon, const bool filled) {
  if (filled) {
    target.fill(box, kInk, 8);
  } else {
    target.stroke(box, kInk, 2, 8);
  }
  const int16_t size = 24;
  const fui::Rect where = fui::makeRect(static_cast<int16_t>(box.x + (box.width - size) / 2),
                                        static_cast<int16_t>(box.y + (box.height - size) / 2), size, size);
  target.bitmap(where, fui::bitmapFromIcon(icon), fui::BitmapMode::Contain,
                fui::Paint::solid(filled ? fui::Color::White : fui::Color::Black));
}

// A row of letter keys of one width, centred, optionally flanked by two wide
// keys (ENTER on the left, delete on the right, as the original has them).
void keyRow(toybox::Screen& screen, const GameModel& model, const char* letters, const int16_t y, const int16_t keyW,
            const int16_t keyH, const int16_t gap, const bool flanked, const bool absentDither,
            const int16_t startX = -1) {
  fui::DrawTarget& target = screen.target();
  const fui::Rect safe = screen.frame().safeRect();
  int count = 0;
  while (letters[count] != '\0') ++count;
  const int16_t wide = static_cast<int16_t>(keyW * 3 / 2 + gap / 2);
  const int16_t width = static_cast<int16_t>(count * keyW + (count - 1) * gap + (flanked ? 2 * (wide + gap) : 0));
  int16_t x = startX >= 0 ? startX : static_cast<int16_t>(safe.x + (safe.width - width) / 2);
  if (flanked) {
    iconKey(target, fui::makeRect(x, y, wide, keyH), icon_tick_24, true);
    x = static_cast<int16_t>(x + wide + gap);
  }
  for (int i = 0; i < count; ++i) {
    const char letter = letters[i];
    const Mark mark = model.keys[letter - 'A'];
    const fui::Rect box = fui::makeRect(x, y, keyW, keyH);
    if (mark == Mark::Empty) {
      target.stroke(box, kInk, 1);
      const char text[2] = {letter, '\0'};
      centredText(target, box, text, toybox::kBodyFont, fui::Color::Black);
    } else {
      cell(target, box, letter, mark, toybox::kBodyFont, absentDither);
    }
    x = static_cast<int16_t>(x + keyW + gap);
  }
  if (flanked) iconKey(target, fui::makeRect(x, y, wide, keyH), icon_wiki_back_24, false);
}

}  // namespace

void buildGame(toybox::Screen& screen, const GameModel& model) {
  chrome(screen, model);
  const fui::Rect safe = screen.frame().safeRect();
  const int16_t top = static_cast<int16_t>(toybox::kHeaderHeight + toybox::kMargin);
  const int16_t inner = static_cast<int16_t>(safe.width - 2 * toybox::kMargin);

#if WORDLE_VARIANT == 1
  const int16_t below = grid(screen, model, top, 62, 8, false);
  const int16_t gap = 6;
  const int16_t keyW = static_cast<int16_t>((inner - 9 * gap) / 10);
  const int16_t keyH = 78;
  int16_t y = static_cast<int16_t>(below + 24);
  keyRow(screen, model, "QWERTYUIOP", y, keyW, keyH, gap, false, false);
  y = static_cast<int16_t>(y + keyH + gap);
  keyRow(screen, model, "ASDFGHJKL", y, keyW, keyH, gap, false, false);
  y = static_cast<int16_t>(y + keyH + gap);
  keyRow(screen, model, "ZXCVBNM", y, keyW, keyH, gap, true, false);
#elif WORDLE_VARIANT == 2
  const int16_t below = grid(screen, model, top, 54, 6, true);
  const int16_t gap = 6;
  const int16_t keyW = static_cast<int16_t>((inner - 9 * gap) / 10);
  const int16_t keyH = 76;
  int16_t y = static_cast<int16_t>(below + 20);
  keyRow(screen, model, "QWERTYUIOP", y, keyW, keyH, gap, false, true);
  y = static_cast<int16_t>(y + keyH + gap);
  keyRow(screen, model, "ASDFGHJKL", y, keyW, keyH, gap, false, true);
  y = static_cast<int16_t>(y + keyH + gap);
  keyRow(screen, model, "ZXCVBNM", y, keyW, keyH, gap, false, true);
  // ENTER and delete as the fork's footer pair, the way Notes has ADD and
  // CLEAR DONE: the two actions that are not letters, apart from the letters.
  const int16_t barH = 52;
  const int16_t barY = static_cast<int16_t>(safe.y + safe.height - toybox::kMargin - barH);
  const int16_t half = static_cast<int16_t>((inner - 12) / 2);
  const int16_t barX = static_cast<int16_t>(safe.x + toybox::kMargin);
  iconKey(screen.target(), fui::makeRect(barX, barY, half, barH), icon_wiki_back_24, false);
  iconKey(screen.target(), fui::makeRect(static_cast<int16_t>(barX + half + 12), barY, half, barH), icon_tick_24, true);
#else
  const int16_t below = grid(screen, model, top, 58, 6, true);
  const int16_t gap = 6;
  const int16_t keyW = static_cast<int16_t>((inner - 6 * gap) / 7);
  const int16_t keyH = 58;
  int16_t y = static_cast<int16_t>(below + 20);
  keyRow(screen, model, "ABCDEFG", y, keyW, keyH, gap, false, true);
  y = static_cast<int16_t>(y + keyH + gap);
  keyRow(screen, model, "HIJKLMN", y, keyW, keyH, gap, false, true);
  y = static_cast<int16_t>(y + keyH + gap);
  keyRow(screen, model, "OPQRSTU", y, keyW, keyH, gap, false, true);
  y = static_cast<int16_t>(y + keyH + gap);
  // The last row holds five letters and the two actions in the same seven
  // columns, so the grid of keys stays square.
  const int16_t left = static_cast<int16_t>(safe.x + (safe.width - (7 * keyW + 6 * gap)) / 2);
  keyRow(screen, model, "VWXYZ", y, keyW, keyH, gap, false, true, left);
  iconKey(screen.target(), fui::makeRect(static_cast<int16_t>(left + 5 * (keyW + gap)), y, keyW, keyH),
          icon_wiki_back_24, false);
  iconKey(screen.target(), fui::makeRect(static_cast<int16_t>(left + 6 * (keyW + gap)), y, keyW, keyH), icon_tick_24,
          true);
#endif
}

}  // namespace wordleui
