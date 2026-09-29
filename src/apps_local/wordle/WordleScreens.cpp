#include "WordleScreens.h"

#include <FreeInkUIIcon.h>

#include <cstdio>

#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxMetrics.h"
#include "../ui/ToyboxTokens.h"

namespace wordleui {
namespace {

const fui::Paint kInk = fui::Paint::solid(fui::Color::Black);
// One dot in four: the lighter of the renderer's two greys.
const fui::Paint kGrey = fui::Paint::dither(fui::Color::LightGray);

constexpr int16_t kMessageH = 22;
constexpr int16_t kTile = 60;
constexpr int16_t kTileGap = 8;
constexpr int16_t kKeyGap = 6;
constexpr int16_t kKeyH = 74;
constexpr int16_t kKeysBelowGrid = 16;
const char* const kKeyRows[3] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};

void chrome(toybox::Screen& screen, const char* title, const char* rightLabel) {
  fui::HeaderProps header;
  header.title = title;
  header.titleText = screen.theme().titleText;
  header.titleText.font = toybox::kDisplayFont;
  header.rightLabel = rightLabel;
  header.borderEdges = fui::EdgesNone;
  if (rightLabel != nullptr) {
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

// One tile or key. Right place: solid black. In the word: a heavy border and a
// dot. Not in the word: grey. Empty: a thin border; typed: a firmer one.
void cell(fui::DrawTarget& target, const fui::Rect& box, const char letter, const Mark mark, const fui::FontId font) {
  const char text[2] = {letter, '\0'};
  switch (mark) {
    case Mark::Empty:
      target.stroke(box, kInk, 1);
      if (letter != '\0') centredText(target, box, text, font, fui::Color::Black);
      return;
    case Mark::Typed:
      target.stroke(box, kInk, 2);
      centredText(target, box, text, font, fui::Color::Black);
      return;
    case Mark::Absent:
      target.fill(box, kGrey);
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

void iconKey(fui::DrawTarget& target, const fui::Rect& box, const freeink::Icon& icon, const bool filled,
             const bool enabled) {
  if (!enabled) {
    target.fill(box, kGrey, 8);
    target.stroke(box, kInk, 1, 8);
  } else if (filled) {
    target.fill(box, kInk, 8);
  } else {
    target.stroke(box, kInk, 2, 8);
  }
  const int16_t size = 24;
  const fui::Rect where = fui::makeRect(static_cast<int16_t>(box.x + (box.width - size) / 2),
                                        static_cast<int16_t>(box.y + (box.height - size) / 2), size, size);
  target.bitmap(where, fui::bitmapFromIcon(icon), fui::BitmapMode::Contain,
                fui::Paint::solid(filled && enabled ? fui::Color::White : fui::Color::Black));
}

int rowLength(const int row) {
  int n = 0;
  while (kKeyRows[row][n] != '\0') ++n;
  return n;
}

fui::Rect keyRect(const KeyboardLayout& k, const int row, const int index) {
  // Row 3 opens with ENTER, so its letters start one wide key in.
  const int16_t start = static_cast<int16_t>(k.rowLeft[row] + (row == 2 ? k.wide + k.gap : 0));
  return fui::makeRect(static_cast<int16_t>(start + index * (k.keyW + k.gap)),
                       static_cast<int16_t>(k.top + row * (k.keyH + k.gap)), k.keyW, k.keyH);
}

fui::Rect enterRect(const KeyboardLayout& k) {
  return fui::makeRect(k.rowLeft[2], static_cast<int16_t>(k.top + 2 * (k.keyH + k.gap)), k.wide, k.keyH);
}

fui::Rect eraseRect(const KeyboardLayout& k) {
  const fui::Rect last = keyRect(k, 2, rowLength(2) - 1);
  return fui::makeRect(static_cast<int16_t>(last.x + last.width + k.gap), last.y, k.wide, k.keyH);
}

bool inside(const fui::Rect& r, const int x, const int y) {
  return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}

}  // namespace

KeyboardLayout keyboardLayout(const fui::Rect& safe, const int16_t top) {
  KeyboardLayout k;
  k.top = top;
  k.gap = kKeyGap;
  k.keyH = kKeyH;
  const int16_t inner = static_cast<int16_t>(safe.width - 2 * toybox::kMargin);
  k.keyW = static_cast<int16_t>((inner - 9 * kKeyGap) / 10);
  k.wide = static_cast<int16_t>(k.keyW * 3 / 2 + kKeyGap / 2);
  for (int row = 0; row < 3; ++row) {
    const int n = rowLength(row);
    const int width = n * k.keyW + (n - 1) * k.gap + (row == 2 ? 2 * (k.wide + k.gap) : 0);
    k.rowLeft[row] = static_cast<int16_t>(safe.x + (safe.width - width) / 2);
  }
  return k;
}

char keyAt(const KeyboardLayout& k, const int x, const int y) {
  if (k.keyW <= 0) return 0;
  if (inside(enterRect(k), x, y)) return kEnter;
  if (inside(eraseRect(k), x, y)) return kErase;
  for (int row = 0; row < 3; ++row) {
    for (int i = 0; i < rowLength(row); ++i) {
      if (inside(keyRect(k, row, i), x, y)) return kKeyRows[row][i];
    }
  }
  return 0;
}

KeyboardLayout buildGame(toybox::Screen& screen, const GameModel& model) {
  const wordle::Game& game = *model.game;
  fui::DrawTarget& target = screen.target();
  const fui::Rect safe = screen.frame().safeRect();

  // A finished game puts the answer in the header and says how it went; any
  // tap then goes back to the menu.
  char answer[wordle::kLetters + 1] = {};
  char result[28] = {};
  if (game.over()) {
    for (int i = 0; i < wordle::kLetters; ++i) answer[i] = game.answer()[i];
    if (game.status() == wordle::Game::Status::Won) {
      std::snprintf(result, sizeof(result), "%d / %d", game.guesses(), wordle::kRows);
    } else {
      std::snprintf(result, sizeof(result), "X / %d", wordle::kRows);
    }
    chrome(screen, answer, result);
  } else {
    chrome(screen, "WORDLE", model.date);
  }

  // The first row this screen owns is the body's top, which the header band
  // reserves; the message line sits a gutter below it and the grid under that.
  const int16_t messageTop = static_cast<int16_t>(screen.body().y + toybox::kGutter);
  const int16_t gridTop = static_cast<int16_t>(messageTop + kMessageH + 4);
  if (model.message != nullptr) {
    centredText(target, fui::makeRect(safe.x, messageTop, safe.width, kMessageH), model.message, toybox::kTileFont,
                fui::Color::Black);
  }

  const int16_t gridW = static_cast<int16_t>(wordle::kLetters * kTile + (wordle::kLetters - 1) * kTileGap);
  const int16_t left = static_cast<int16_t>(safe.x + (safe.width - gridW) / 2);
  for (int r = 0; r < wordle::kRows; ++r) {
    for (int c = 0; c < wordle::kLetters; ++c) {
      char letter = '\0';
      Mark mark = Mark::Empty;
      if (r < game.guesses()) {
        letter = game.guess(r)[c];
        mark = game.marks(r)[c];
      } else if (r == game.guesses() && !game.over() && c < game.typed()) {
        letter = game.typing()[c];
        mark = Mark::Typed;
      }
      const fui::Rect box = fui::makeRect(static_cast<int16_t>(left + c * (kTile + kTileGap)),
                                          static_cast<int16_t>(gridTop + r * (kTile + kTileGap)), kTile, kTile);
      cell(target, box, letter, mark, toybox::kDisplayFont);
    }
  }

  const int16_t gridBottom = static_cast<int16_t>(gridTop + wordle::kRows * kTile + (wordle::kRows - 1) * kTileGap);
  const KeyboardLayout keys = keyboardLayout(safe, static_cast<int16_t>(gridBottom + kKeysBelowGrid));
  for (int row = 0; row < 3; ++row) {
    for (int i = 0; i < rowLength(row); ++i) {
      const char letter = kKeyRows[row][i];
      const Mark mark = game.key(letter);
      const fui::Rect box = keyRect(keys, row, i);
      if (mark == Mark::Empty) {
        target.stroke(box, kInk, 1);
        const char text[2] = {letter, '\0'};
        centredText(target, box, text, toybox::kBodyFont, fui::Color::Black);
      } else {
        cell(target, box, letter, mark, toybox::kBodyFont);
      }
    }
  }
  iconKey(target, enterRect(keys), icon_tick_24, true, !game.over() && game.typed() == wordle::kLetters);
  iconKey(target, eraseRect(keys), icon_wordle_delete_24, false, true);

  if (game.over()) {
    screen.frame().hit(safe, ActionAnywhere, 0);
  } else {
    const fui::Rect block =
        fui::makeRect(safe.x, keys.top, safe.width, static_cast<int16_t>(3 * keys.keyH + 2 * keys.gap));
    screen.frame().hit(block, ActionKeyboard, 0);
  }
  return keys;
}

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  chrome(screen, "WORDLE", nullptr);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  fui::DrawTarget& target = screen.target();
  const fui::Rect body = screen.body();

  // Today, in the biggest type, is also the way into today's game.
  fui::TextStyle hero;
  hero.font = toybox::kDisplayFont;
  hero.align = fui::TextAlign::Left;
  target.text(fui::makeRect(body.x, body.y, body.width, 60), model.date != nullptr ? model.date : "NONE YET", hero);
  fui::TextStyle sub;
  sub.font = toybox::kBodyFont;
  sub.align = fui::TextAlign::Left;
  target.text(fui::makeRect(body.x, static_cast<int16_t>(body.y + 60), body.width, 30), model.state, sub);
  if (model.date != nullptr) screen.frame().hit(fui::makeRect(body.x, body.y, body.width, 96), ActionMenu, 0);

  target.fill(fui::makeRect(body.x, static_cast<int16_t>(body.y + 104), body.width, toybox::kRule), kInk);

  char stats[64];
  std::snprintf(stats, sizeof(stats), "%d PLAYED   %d WON   STREAK %d", model.stats.played, model.stats.won,
                model.stats.streak);
  fui::TextStyle small;
  small.font = toybox::kTileFont;
  small.align = fui::TextAlign::Left;
  target.text(fui::makeRect(body.x, static_cast<int16_t>(body.y + 118), body.width, 24), stats, small);

  // The one piece of decor is the player's own record: how many guesses each
  // win took, 1 to 6, as bars.
  int most = 1;
  for (const int n : model.stats.wins) most = n > most ? n : most;
  const int16_t rowH = 30;
  const int16_t chartTop = static_cast<int16_t>(body.y + 166);
  const int16_t labelW = 28;
  const int16_t countW = 44;
  const int16_t barMax = static_cast<int16_t>(body.width - labelW - countW - 16);
  for (int i = 0; i < wordle::kRows; ++i) {
    const int16_t y = static_cast<int16_t>(chartTop + i * (rowH + 8));
    char label[12];
    std::snprintf(label, sizeof(label), "%d", i + 1);
    centredText(target, fui::makeRect(body.x, y, labelW, rowH), label, toybox::kBodyFont, fui::Color::Black);
    const int n = model.stats.wins[i];
    const int16_t w = static_cast<int16_t>(n == 0 ? 0 : (barMax * n + most - 1) / most);
    const fui::Rect track = fui::makeRect(static_cast<int16_t>(body.x + labelW + 8), y, barMax, rowH);
    target.stroke(track, kInk, 1);
    if (w > 0) target.fill(fui::makeRect(track.x, track.y, w, rowH), kInk);
    char count[12];
    std::snprintf(count, sizeof(count), "%d", n);
    centredText(target, fui::makeRect(static_cast<int16_t>(track.x + barMax + 8), y, countW, rowH), count,
                toybox::kBodyFont, fui::Color::Black);
  }
  fui::TextStyle caption = small;
  caption.align = fui::TextAlign::Center;
  target.text(fui::makeRect(body.x, static_cast<int16_t>(chartTop + 6 * (rowH + 8) + 4), body.width, 24),
              "GUESSES PER WIN", caption);

  fui::ListItem rows[3];
  for (auto& r : rows) r = fui::ListItem{};
  char total[16] = "";
  std::snprintf(total, sizeof(total), "%d", model.puzzles);
  rows[0].label = "HOW TO PLAY";
  rows[0].actionValue = 1;
  rows[1].label = "ARCHIVE";
  rows[1].value = total;
  rows[1].actionValue = 2;
  rows[2].label = model.upToDate ? "ALL CAUGHT UP" : "GET PUZZLES";
  rows[2].value = model.upToDate ? "" : "WI-FI";
  rows[2].enabled = !model.upToDate;
  rows[2].actionValue = 3;
  fui::ListProps list;
  list.items = rows;
  list.count = 3;
  list.selectedIndex = -1;
  list.action = ActionMenu;
  screen.list(list, 3 * (toybox::kRowHeight + 4), fui::LayoutAnchor::Bottom);
}

void buildHowTo(toybox::Screen& screen) {
  chrome(screen, "HOW TO PLAY", nullptr);
  screen.insetContent(fui::Insets{toybox::kGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  fui::DrawTarget& target = screen.target();
  const fui::Rect body = screen.body();

  fui::TextStyle heading;
  heading.font = toybox::kBodyFont;
  heading.align = fui::TextAlign::Left;
  fui::TextStyle prose;
  prose.font = toybox::kTileFont;
  prose.align = fui::TextAlign::Left;
  prose.maxLines = 2;
  const int16_t headH = target.lineHeight(toybox::kBodyFont);
  const int16_t proseH = target.lineHeight(toybox::kTileFont);

  int16_t y = body.y;
  target.text(fui::makeRect(body.x, y, body.width, headH), "SIX GUESSES FOR ONE WORD", heading);
  y = static_cast<int16_t>(y + headH + 6);
  target.text(fui::makeRect(body.x, y, body.width, static_cast<int16_t>(proseH * 2)),
              "Type a five-letter word and tap the tick. Each letter is then marked.", prose);
  y = static_cast<int16_t>(y + proseH * 2 + 24);

  struct Example {
    char letter;
    Mark mark;
    const char* meaning;
  };
  const Example examples[] = {
      {'P', Mark::Correct, "In the word, in this spot"},
      {'L', Mark::Present, "In the word, somewhere else"},
      {'C', Mark::Absent, "Not in the word"},
  };
  for (const Example& example : examples) {
    cell(target, fui::makeRect(body.x, y, kTile, kTile), example.letter, example.mark, toybox::kDisplayFont);
    fui::TextStyle meaning = prose;
    meaning.maxLines = 1;
    target.text(fui::makeRect(static_cast<int16_t>(body.x + kTile + 16), static_cast<int16_t>(y + (kTile - proseH) / 2),
                              static_cast<int16_t>(body.width - kTile - 16), proseH),
                example.meaning, meaning);
    y = static_cast<int16_t>(y + kTile + 16);
  }
  y = static_cast<int16_t>(y + 8);
  target.text(fui::makeRect(body.x, y, body.width, static_cast<int16_t>(proseH * 2)),
              "A new word every day. Past days are in the archive.", prose);
}

}  // namespace wordleui
