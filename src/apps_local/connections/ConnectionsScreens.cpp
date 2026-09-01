#include "ConnectionsScreens.h"

#include <cstdio>
#include <cstring>

namespace connectionsui {

namespace {

// Left over from before a solved row took over the tile row it replaced, which
// is what makes the block height conserved. Row height is derived now.
constexpr int kStatusHeight = 34;

// `doorAction` makes the header's right side a control, the way Murdle's does.
// Left at NO_ACTION the header is inert, which is every other caller.
void toyboxChrome(toybox::Screen& screen, const char* title, const char* rightLabel,
                  const fui::ActionId doorAction = fui::NO_ACTION) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  // Same trap the title fell into: header() uses these styles as given rather
  // than resolving them against the band, so the default black renders black on
  // black and the label is simply not there. Screen substitutes smallText for an
  // unset subtitleText, and smallText is black.
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  const fui::Rect panel = screen.device().screen();
  const int16_t bandTop = static_cast<int16_t>(screen.body().y - toybox::kHeaderHeight);
  toybox::headerRule(screen);
  if (doorAction != fui::NO_ACTION) {
    // Registered after the header drew, so the hit rect and the label come from
    // the same band and cannot drift apart.
    screen.frame().hit(fui::makeRect(static_cast<int16_t>(panel.width * 3 / 5), bandTop,
                                     static_cast<int16_t>(panel.width * 2 / 5), toybox::kHeaderHeight),
                       doorAction, 0);
  }
}

// The word's share of a tile. Both the sizing pass and the draw pass have to
// inset identically or the board is sized against a width it is not drawn in,
// so the insets are written once.
constexpr fui::Insets kTileTextInsets{2, kTilePad, 2, kTilePad};

// The cuts a tile's word may be set in.
//
// NOT "largest first": which of these two slots holds the bigger face is a
// property of the caller's binding, not of the slots. On the board they are the
// 20px and 15px serif cuts in that order only because ConnectionsActivity
// rebinds FONT_SLOT_BODY to kSerifSmallFontId immediately before the tile pass
// (View::Board in ConnectionsActivity.cpp). Every other Connections screen
// takes serifMenuFaces(), where the two slots are the other way round -- small
// holds the 15px cut and body the 20px -- so an order-dependent ladder would
// step UP there. chooseTileCut() therefore asks each face for its line height
// instead of trusting this array's order, and the rebind above can change
// without silently inverting the result.
constexpr fui::FontId kTileCuts[2] = {toybox::kTileFont, toybox::kBodyFont};

// One size for all sixteen tiles: the largest cut whose WIDEST word still fits
// a tile, or the smallest cut when none of them does.
//
// Sizing each tile against its own word made a long word set a quarter smaller
// than the fifteen beside it, and on a board whose whole premise is sixteen
// interchangeable candidates a size difference reads as significance that is
// not there. Measured at draw time against the real face, for the reason
// murdleui's drawLegend gives (MurdleScreens.cpp): the host tests' draw
// target answers a flat ten pixels a character and would call any of this fine.
fui::FontId chooseTileCut(toybox::Screen& screen, const char* const* words, const int count, const int innerWidth) {
  fui::TextStyle probe;
  probe.align = fui::TextAlign::Center;
  probe.maxLines = 1;

  fui::FontId smallest = kTileCuts[0];
  int16_t smallestHeight = 0;
  fui::FontId fitting = kTileCuts[0];
  int16_t fittingHeight = -1;

  for (const fui::FontId cut : kTileCuts) {
    const int16_t height = screen.target().lineHeight(cut);
    if (smallestHeight == 0 || height < smallestHeight) {
      smallestHeight = height;
      smallest = cut;
    }
    bool fits = true;
    for (int i = 0; i < count && fits; ++i) {
      fits = screen.target().measureText(cut, words[i], probe).width <= innerWidth;
    }
    if (fits && height > fittingHeight) {
      fittingHeight = height;
      fitting = cut;
    }
  }
  return fittingHeight >= 0 ? fitting : smallest;
}

// Lays a tile's word out over up to three centred lines, breaking inside a word
// when it has to. The cut is decided for the whole board by chooseTileCut() and
// handed in, so the split below is the only per-tile decision left.
//
// Not the target's own multi-line text(): that delegates to the renderer's
// wrappedText(), which only ever breaks at a space and then ellipsises. A tile
// holds a single word most of the time, so "ACTUALLY" came out as "ACTUA..." --
// a different word, with no way for the player to tell. Breaking mid-word is
// uglier than wrapping at a space and much better than lying about the word.
//
// Each emitted line is drawn as its own single-line run, so it is measured
// before it is drawn and can never be the thing that overflows.
void drawTileText(toybox::Screen& screen, const fui::Rect& box, const char* word, const bool inverted,
                  const fui::FontId cut) {
  const fui::Rect inner = box.inset(kTileTextInsets);

  fui::TextStyle style;
  style.align = fui::TextAlign::Center;
  style.maxLines = 1;
  style.color = inverted ? fui::Color::White : fui::Color::Black;
  style.font = cut;

  const int16_t lineHeight = screen.target().lineHeight(style.font);
  if (screen.target().measureText(style.font, word, style).width > inner.width) {
    // Too wide even at the board's cut, which chooseTileCut() has already
    // dropped to the smallest available: JOHANNESBURGER is fourteen letters
    // with nowhere to break. Only here is the word split, and it is split into
    // halves rather than filled greedily, because "JOHANNESBURGE / R" orphans a
    // letter and reads as a bug. The renderer's own wrapper is no use for this:
    // it breaks at spaces only and then ellipsises, which silently shows a
    // different word.
    const int length = static_cast<int>(std::strlen(word));
    // Two parts for a long single word, three for a long phrase. Chosen by
    // measuring rather than by counting characters, because SMILING FACE WITH
    // SUNGLASSES is 28 characters and no two halves of it fit.
    int parts = 2;
    while (parts < 3 && screen.target().measureText(style.font, word, style).width / parts > inner.width) ++parts;
    if (parts == 3) {
      int bound[4] = {0, length / 3, (2 * length) / 3, length};
      for (int i = 1; i < 3; ++i) {
        for (int slack = 0; slack <= 4; ++slack) {
          if (bound[i] - slack > bound[i - 1] && word[bound[i] - slack] == ' ') {
            bound[i] -= slack;
            break;
          }
          if (bound[i] + slack < bound[i + 1] && word[bound[i] + slack] == ' ') {
            bound[i] += slack;
            break;
          }
        }
      }
      const int top3 = inner.y + (inner.height - lineHeight * 3) / 2;
      for (int i = 0; i < 3; ++i) {
        char part[connections::kMaxWordLen + 2] = {};
        const int span = bound[i + 1] - bound[i];
        std::memcpy(part, word + bound[i], static_cast<size_t>(span));
        part[span] = '\0';
        const char* text = part[0] == ' ' ? part + 1 : part;
        screen.target().text(fui::makeRect(inner.x, top3 + i * lineHeight, inner.width, lineHeight), text, style);
      }
      return;
    }
    char first[connections::kMaxWordLen + 2] = {};
    char second[connections::kMaxWordLen + 2] = {};
    int split = length / 2;
    // Prefer a nearby space or hyphen so a two-word tile still breaks naturally.
    for (int slack = 0; slack <= 3; ++slack) {
      if (split - slack > 0 && (word[split - slack] == ' ' || word[split - slack] == '-')) {
        split -= slack;
        break;
      }
      if (split + slack < length && (word[split + slack] == ' ' || word[split + slack] == '-')) {
        split += slack;
        break;
      }
    }
    std::memcpy(first, word, static_cast<size_t>(split));
    first[split] = '\0';
    std::memcpy(second, word + split, static_cast<size_t>(length - split));
    second[length - split] = '\0';
    const char* tail = second[0] == ' ' ? second + 1 : second;
    const int top = inner.y + (inner.height - lineHeight * 2) / 2;
    screen.target().text(fui::makeRect(inner.x, top, inner.width, lineHeight), first, style);
    screen.target().text(fui::makeRect(inner.x, top + lineHeight, inner.width, lineHeight), tail, style);
    return;
  }
  screen.target().text(fui::makeRect(inner.x, inner.y + (inner.height - lineHeight) / 2, inner.width, lineHeight), word,
                       style);
}

}  // namespace

void formatDate(const uint32_t date, char* out, const int cap) {
  static const char* kMonths[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  const int year = static_cast<int>(date / 10000);
  const int month = static_cast<int>((date / 100) % 100);
  const int day = static_cast<int>(date % 100);
  if (month < 1 || month > 12) {
    std::snprintf(out, static_cast<size_t>(cap), "%u", date);
    return;
  }
  std::snprintf(out, static_cast<size_t>(cap), "%d %s %d", day, kMonths[month - 1], year);
}

namespace {
void buildBoardStatus(toybox::Screen& screen, const BoardModel& model, const fui::Rect& status,
                      const fui::Rect& actions);
}  // namespace

BoardLayout buildBoardChrome(toybox::Screen& screen, const BoardModel& model) {
  // The date IS the title. "CONNECTIONS" in the display cut is 11 characters
  // wide and truncates to "CONNECTI", and the app's name is already on the row
  // you tapped to get here; what identifies this screen is which puzzle it is.
  char dateText[16];
  formatDate(model.date, dateText, sizeof(dateText));
  toyboxChrome(screen, dateText, nullptr);
  screen.insetContent(fui::Insets{toybox::kGutter, kGridMargin, toybox::kMargin, kGridMargin});

  if (model.game == nullptr) return BoardLayout{screen.body(), 0};

  // The action bar is reserved from the bottom first so the grid never grows
  // into it, whatever the solved rows do above.
  const fui::Rect actions = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  const fui::Rect status = screen.takeBottom(kStatusHeight, toybox::spaceBetween);

  // One block of four rows, always. A solved group takes over the row its tiles
  // occupied, so the block height is conserved and nothing below it ever moves:
  // solving a group looks like the row turning over in place rather than like
  // the board jumping. This is the design language's reserve-space rule, and
  // here it costs nothing because the arithmetic happens to be exact.
  const fui::Rect body = screen.body();
  int rowHeight = (body.height - (connections::kGroups - 1) * kTileGap) / connections::kGroups;
  if (rowHeight > kTileMaxHeight) rowHeight = kTileMaxHeight;
  const int blockHeight = connections::kGroups * rowHeight + (connections::kGroups - 1) * kTileGap;
  const int blockTop = body.y + (body.height - blockHeight) / 2;
  const fui::Rect grid = fui::makeRect(body.x, blockTop, body.width, blockHeight);

  // Status and actions belong to this pass: they are the fork's controls, not
  // the game's content, so they speak Jersey like every other app's buttons.
  buildBoardStatus(screen, model, status, actions);
  return BoardLayout{grid, rowHeight};
}

void buildBoardTiles(toybox::Screen& screen, const BoardModel& model, const BoardLayout& layout) {
  if (model.game == nullptr) return;
  const connections::Game& game = *model.game;
  const fui::Rect grid = layout.grid;
  const int rowHeight = layout.rowHeight;
  const int blockTop = grid.y;
  const int revealed = game.revealedCount();
  for (int i = 0; i < revealed; ++i) {
    const connections::Group& group = game.solvedGroup(i);
    const fui::Rect row = fui::makeRect(grid.x, blockTop + i * (rowHeight + kTileGap), grid.width, rowHeight);
    // Solid black: a solved row never repaints again, so by the ink budget rule
    // its ink is free, and it makes the shrinking board read as progress.
    screen.target().fill(row, fui::Paint::solid(fui::Color::Black));

    fui::TextStyle name;
    name.font = toybox::kUiFont;
    name.align = fui::TextAlign::Center;
    name.color = fui::Color::White;
    screen.target().text(
        toybox::inkCentred(fui::makeRect(row.x, row.y + rowHeight / 2 - 26, row.width, 26), toybox::kUiCut), group.name,
        name);

    char words[connections::kMaxWordLen * 4 + 8];
    std::snprintf(words, sizeof(words), "%s, %s, %s, %s", group.members[0], group.members[1], group.members[2],
                  group.members[3]);
    fui::TextStyle list;
    list.font = toybox::kTileFont;
    list.align = fui::TextAlign::Center;
    list.maxLines = 2;
    list.color = fui::Color::White;
    screen.target().text(fui::makeRect(row.x + kTilePad, row.y + rowHeight / 2 + 2, row.width - 2 * kTilePad, 30),
                         words, list);
  }

  // A loss reveals every group as a row, so there is no board left to draw. The
  // core keeps the unsolved tiles (nothing removed them, since they were never
  // guessed correctly) and revealedCount() jumps to four, so drawing both put
  // the answers in the four row slots AND the leftover tiles underneath them,
  // spilling past the block.
  if (game.result() == connections::Result::Lost) return;

  // Tiles fill the rows the solved groups have not taken.
  const int tiles = game.tileCount();
  if (tiles <= 0) return;

  // Every tile on the board is set in one cut, chosen against the widest word
  // still on it. Solving a group can therefore hand the rest of the board a
  // larger cut, which is right: the tiles you are still comparing stay
  // interchangeable, and the set only ever gets easier to read.
  const char* words[connections::kTiles];
  for (int i = 0; i < tiles; ++i) words[i] = game.tileWord(i);
  const int16_t innerWidth = fui::makeRect(0, 0, kTileWidth, rowHeight).inset(kTileTextInsets).width;
  const fui::FontId cut = chooseTileCut(screen, words, tiles, innerWidth);

  for (int i = 0; i < tiles; ++i) {
    const int col = i % 4;
    const int row = revealed + i / 4;
    const fui::Rect box = fui::makeRect(grid.x + col * (kTileWidth + kTileGap), blockTop + row * (rowHeight + kTileGap),
                                        kTileWidth, rowHeight);
    const bool chosen = game.isSelected(i);
    screen.frame().hit(box, ActionTile, static_cast<int16_t>(i));

    if (chosen) {
      screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
    } else {
      screen.target().fill(box, fui::Paint::solid(fui::Color::White));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kHairline);
    }
    drawTileText(screen, box, game.tileWord(i), chosen, cut);
  }
}

namespace {

void buildBoardStatus(toybox::Screen& screen, const BoardModel& model, const fui::Rect& status,
                      const fui::Rect& actions) {
  const connections::Game& game = *model.game;
  // Mistakes, as four pips that empty out. A count would need reading; pips are
  // countable at a glance from across a table.
  fui::TextStyle statusText;
  statusText.font = toybox::kUiFont;
  statusText.align = fui::TextAlign::Left;
  const char* label = model.toast != nullptr ? model.toast : "MISTAKES";
  const int pip = 16;
  // Everything the pips do not need. Half the width truncated "ALREADY
  // GUESSED" to "ALREADY GUE", which is the kind of thing only a render shows.
  const int pipBand = connections::kMaxMistakes * (pip + 8);
  screen.target().text(fui::makeRect(status.x, status.y, status.width - pipBand - toybox::kGutter, status.height),
                       label, statusText);
  for (int i = 0; i < connections::kMaxMistakes; ++i) {
    const fui::Rect dot =
        fui::makeRect(status.right() - (connections::kMaxMistakes - i) * (pip + 8), status.y + 8, pip, pip);
    if (i < game.mistakesLeft()) {
      screen.target().fill(dot, fui::Paint::solid(fui::Color::Black), static_cast<uint8_t>(pip / 2));
    } else {
      screen.target().stroke(dot, fui::Paint::solid(fui::Color::Black), toybox::kHairline,
                             static_cast<uint8_t>(pip / 2));
    }
  }

  // One row of three actions while playing; one wide button once it is over.
  if (game.result() == connections::Result::Playing) {
    const int width = (actions.width - 2 * toybox::kGutter) / 3;
    const char* labels[3] = {"SHUFFLE", "CLEAR", "SUBMIT"};
    const fui::ActionId ids[3] = {ActionShuffle, ActionDeselect, ActionSubmit};
    for (int i = 0; i < 3; ++i) {
      fui::ButtonProps button;
      button.label = labels[i];
      button.action = ids[i];
      button.borderEdges = fui::EdgesNone;
      // Submit is the only one that is ever unavailable, and it is unavailable
      // most of the time: without this it would be a button that silently does
      // nothing on three taps out of four.
      // Off only when there is nothing to submit. A repeat deliberately stays
      // pressable: disabling it would be a dead button with no explanation,
      // and being told "ALREADY GUESSED" beats being left to wonder why the
      // button stopped working.
      button.enabled = ids[i] != ActionSubmit || game.canSubmit();
      if (!button.enabled) button.styles = toybox::disabledButtonStyles();
      screen.button(button, fui::makeRect(actions.x + i * (width + toybox::kGutter), actions.y, width, actions.height));
    }
  } else {
    fui::ButtonProps done;
    done.label = game.result() == connections::Result::Won ? "SOLVED - BACK TO LIST" : "BACK TO LIST";
    done.action = ActionOpenMenu;
    done.borderEdges = fui::EdgesNone;
    screen.button(done, actions);
  }
}

}  // namespace

void buildArchive(toybox::Screen& screen, const ArchiveModel& model) {
  // "17-32 OF 1141" rather than a bare total: with sixteen rows on screen out of
  // eleven hundred, the useful fact is where you are, not how many there are.
  char count[24];
  if (model.count > 0) {
    std::snprintf(count, sizeof(count), "%d-%d OF %d", model.topIndex + 1, model.topIndex + model.count, model.total);
  } else {
    std::snprintf(count, sizeof(count), "%d", model.total);
  }
  toyboxChrome(screen, "ARCHIVE", count);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::ListItem items[16];
  const int visible = model.count < 16 ? model.count : 16;
  for (int i = 0; i < visible; ++i) {
    items[i] = fui::ListItem{};
    items[i].label = model.rows[i].label;
    // The score is drawn as pips below rather than set as a value, so the value
    // slot carries only what pips cannot say.
    items[i].value = model.rows[i].lost ? "FAILED" : "";
    items[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps list;
  list.items = items;
  list.count = static_cast<uint16_t>(visible);
  list.topIndex = static_cast<uint16_t>(model.topIndex);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionArchiveRow;
  screen.list(list);

  // Mistake pips, drawn over the rows the list just laid out. The same four
  // pips the board uses, so a glance down the column reads without a legend --
  // and drawn in a second pass because they sit inside a row the component owns.
  const fui::Rect band = screen.body();
  const int rowStep = toybox::kRowHeight + 4;
  const int pip = 12;
  for (int i = 0; i < visible; ++i) {
    const ArchiveRow& row = model.rows[i];
    if (!row.played || row.lost) continue;
    const int top = band.y + i * rowStep + (toybox::kRowHeight - pip) / 2;
    const bool selected = i == model.selected;
    for (int m = 0; m < connections::kMaxMistakes; ++m) {
      const int x = band.right() - toybox::kGutter - (connections::kMaxMistakes - m) * (pip + 6);
      const fui::Rect dot = fui::makeRect(x, top, pip, pip);
      const fui::Paint ink = fui::Paint::solid(selected ? fui::Color::White : fui::Color::Black);
      // Filled pips are the mistakes still unspent, matching the board exactly.
      if (m < connections::kMaxMistakes - row.mistakes) {
        screen.target().fill(dot, ink, static_cast<uint8_t>(pip / 2));
      } else {
        screen.target().stroke(dot, ink, toybox::kHairline, static_cast<uint8_t>(pip / 2));
      }
    }
    // The hardest group first, where the source told us which one that was.
    if (row.hardestFirst) {
      const int x = band.right() - toybox::kGutter - connections::kMaxMistakes * (pip + 6) - 22;
      screen.target().fill(fui::makeRect(x, top, pip, pip), fui::Paint::dither(fui::Color::DarkGray),
                           static_cast<uint8_t>(pip / 2));
    }
  }
}

void buildImport(toybox::Screen& screen, const ImportModel& model) {
  toyboxChrome(screen, "GET PUZZLES", nullptr);
  screen.insetContent(fui::Insets{toybox::kGutter * 2, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::ButtonProps close;
  close.label = model.done ? "PLAY" : (model.failed ? "BACK" : "WORKING");
  close.enabled = model.done || model.failed;
  if (!close.enabled) close.styles = toybox::disabledButtonStyles();
  close.borderEdges = fui::EdgesNone;
  screen.button(close, screen.takeBottom(toybox::kPillHeight));

  // The number's panel. Outline while the count is still moving, solid black
  // once it has stopped: the inversion is the "done", and it costs nothing
  // because a finished panel never repaints again.
  const fui::Rect body = screen.body();
  const int panelHeight = 190;
  const fui::Rect panel =
      fui::makeRect(body.x, body.y + (body.height - panelHeight) / 2 - toybox::kGutter, body.width, panelHeight);
  if (model.done) {
    screen.target().fill(panel, fui::Paint::solid(fui::Color::Black));
  } else {
    screen.target().stroke(panel, fui::Paint::solid(fui::Color::Black), toybox::kFrame);
  }
  const fui::Color ink = model.done ? fui::Color::White : fui::Color::Black;

  char hero[16];
  std::snprintf(hero, sizeof(hero), "%d", model.puzzles);
  fui::TextStyle number;
  number.font = toybox::kDisplayFont;
  number.align = fui::TextAlign::Center;
  number.color = ink;
  screen.target().text(toybox::inkCentred(fui::makeRect(panel.x, panel.y + 28, panel.width, 52), toybox::kDisplayCut),
                       model.failed ? "--" : hero, number);

  // The doubled line, the header band's own motif, so the panel belongs to the
  // same family rather than being a box that happens to be here.
  screen.target().fill(fui::makeRect(panel.x + 70, panel.y + 96, panel.width - 140, toybox::kRule),
                       fui::Paint::solid(ink));
  screen.target().fill(fui::makeRect(panel.x + 70, panel.y + 104, panel.width - 140, toybox::kHairline),
                       fui::Paint::solid(ink));

  // While working this is the year the download has reached, so the wait reads
  // as sweeping forward through the archive rather than as nothing happening.
  char caption[64];
  if (model.failed) {
    std::snprintf(caption, sizeof(caption), "%s", model.detail);
  } else if (model.done) {
    std::snprintf(caption, sizeof(caption), "PUZZLES ON THE CARD");
  } else if (model.reachedDate > 0) {
    std::snprintf(caption, sizeof(caption), "REACHED %u", model.reachedDate / 10000);
  } else if (model.wifiDone) {
    // The fetch blocks, so this screen will sit perfectly still for about half a
    // minute. Saying so costs one line and turns "is it broken" into "it is
    // working" -- which is the whole benefit a progress bar would have bought,
    // for none of the cost of getting one onto the screen.
    std::snprintf(caption, sizeof(caption), "THIS TAKES A MINUTE.\nTHE SCREEN WILL SIT STILL.");
  } else {
    std::snprintf(caption, sizeof(caption), "%s", model.detail);
  }
  fui::TextStyle text;
  text.font = toybox::kUiFont;
  text.align = fui::TextAlign::Center;
  text.color = ink;
  text.maxLines = 2;
  screen.target().text(fui::makeRect(panel.x + toybox::kGutter, panel.y + 124, panel.width - 2 * toybox::kGutter, 60),
                       caption, text);
}

namespace {

// The marks. Drawn rather than set as glyphs: the tile font has no star and no
// tick, and four small procedural shapes cost less than another font cut on a
// device where a cut is 100KB.
//
// A four-point sparkle for a flawless solve, a digit for one to three mistakes,
// a cross for a failure, a hollow ring for a puzzle opened and left. The
// hierarchy is deliberate -- solid ink for the best result, an outline for the
// unfinished one -- so a month reads at a glance before any number does.
void drawSparkle(toybox::Screen& screen, const fui::Rect& box, const fui::Paint& ink) {
  const int cx = box.x + box.width / 2;
  const int cy = box.y + box.height / 2;
  const int r = box.width / 2;
  const int w = r / 3;
  // Four tapered arms, each a pair of triangles meeting at the centre.
  screen.target().triangle(fui::Point{static_cast<int16_t>(cx), static_cast<int16_t>(cy - r)},
                           fui::Point{static_cast<int16_t>(cx - w), static_cast<int16_t>(cy)},
                           fui::Point{static_cast<int16_t>(cx + w), static_cast<int16_t>(cy)}, ink);
  screen.target().triangle(fui::Point{static_cast<int16_t>(cx), static_cast<int16_t>(cy + r)},
                           fui::Point{static_cast<int16_t>(cx - w), static_cast<int16_t>(cy)},
                           fui::Point{static_cast<int16_t>(cx + w), static_cast<int16_t>(cy)}, ink);
  screen.target().triangle(fui::Point{static_cast<int16_t>(cx - r), static_cast<int16_t>(cy)},
                           fui::Point{static_cast<int16_t>(cx), static_cast<int16_t>(cy - w)},
                           fui::Point{static_cast<int16_t>(cx), static_cast<int16_t>(cy + w)}, ink);
  screen.target().triangle(fui::Point{static_cast<int16_t>(cx + r), static_cast<int16_t>(cy)},
                           fui::Point{static_cast<int16_t>(cx), static_cast<int16_t>(cy - w)},
                           fui::Point{static_cast<int16_t>(cx), static_cast<int16_t>(cy + w)}, ink);
}

void drawCross(toybox::Screen& screen, const fui::Rect& box, const fui::Paint& ink) {
  const int inset = box.width / 5;
  const fui::Rect b = box.inset(fui::makeInsets(inset));
  // right()/bottom() return int, and Point holds int16_t: braced init refuses
  // the narrowing, which the device build's warning set happened not to.
  const auto pt = [](const int x, const int y) { return fui::Point{static_cast<int16_t>(x), static_cast<int16_t>(y)}; };
  screen.target().line(pt(b.x, b.y), pt(b.right(), b.bottom()), toybox::kRule, ink);
  screen.target().line(pt(b.right(), b.y), pt(b.x, b.bottom()), toybox::kRule, ink);
}

void drawDayMark(toybox::Screen& screen, const fui::Rect& box, const CalendarDay& day, const fui::Color color) {
  const fui::Paint ink = fui::Paint::solid(color);
  if (day.lost) {
    drawCross(screen, box, ink);
    return;
  }
  if (!day.finished) {
    // Opened and left: an outline, so an abandoned day is visibly lighter than
    // any finished one.
    screen.target().stroke(box.inset(fui::Insets{3, 3, 3, 3}), ink, toybox::kHairline,
                           static_cast<uint8_t>(box.width / 2));
    return;
  }
  if (day.mistakes == 0) {
    drawSparkle(screen, box, ink);
    return;
  }
  // Pips, not a digit. A digit under the date read as one number: 2 mistakes on
  // the 2nd came out "2 / 2". Dots cannot be confused with a date, and they are
  // the same count the board and the archive list already use.
  const int pip = box.width / 4;
  const int span = day.mistakes * pip + (day.mistakes - 1) * 3;
  const int left = box.x + (box.width - span) / 2;
  for (int i = 0; i < day.mistakes; ++i) {
    screen.target().fill(fui::makeRect(left + i * (pip + 3), box.y + (box.height - pip) / 2, pip, pip), ink,
                         static_cast<uint8_t>(pip / 2));
  }
}

}  // namespace

bool dayCellAt(const CalendarLayout& layout, const int x, const int y, int& cell) {
  if (!layout.valid || layout.cell <= 0) return false;
  const int step = layout.cell + layout.gap;
  const int dx = x - layout.originX;
  const int dy = y - layout.originY;
  if (dx < 0 || dy < 0) return false;
  const int col = dx / step;
  const int row = dy / step;
  if (col >= layout.cols || row >= layout.rows) return false;
  // The gaps between cells belong to nobody. Rounding them into the cell on
  // their left would make a 3px seam act as a date, which on a grid of numbers
  // is a tap that opens the wrong day.
  if (dx % step >= layout.cell || dy % step >= layout.cell) return false;
  cell = row * layout.cols + col;
  return true;
}

CalendarLayout buildCalendar(toybox::Screen& screen, const CalendarModel& model) {
  static const char* kMonthNames[12] = {"JANUARY", "FEBRUARY", "MARCH",     "APRIL",   "MAY",      "JUNE",
                                        "JULY",    "AUGUST",   "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
  const int m = model.month >= 1 && model.month <= 12 ? model.month : 1;
  char played[16];
  std::snprintf(played, sizeof(played), "%d PLAYED", model.playedThisMonth);
  toyboxChrome(screen, "ARCHIVE", played);
  screen.insetContent(fui::Insets{toybox::kGutter, kGridMargin, toybox::kMargin, kGridMargin});

  fui::ButtonProps today;
  today.label = "TODAY";
  today.action = ActionCalendarToday;
  today.borderEdges = fui::EdgesNone;
  screen.button(today, screen.takeBottom(toybox::kPillHeight));

  // Two steppers, not a cursor that walks. Reaching June 2023 a week at a time
  // is forty presses; picking the year and the month is two rows that say
  // exactly where you are and go anywhere in the archive in a few taps.
  // stepperRow is the SDK's own control for this, so the arrows register their
  // own hit rects and the value slot is sized to its widest string rather than
  // shifting as the number changes.
  char yearText[8];
  std::snprintf(yearText, sizeof(yearText), "%d", model.year);
  const char* monthText = kMonthNames[m - 1];

  // Drawn here rather than with stepperRow, which takes a single StyleSet for
  // both arrows and so cannot show one end spent while the other still works.
  // The rects are computed once and used for drawing and for hit registration
  // alike, so the two cannot drift.
  for (int i = 0; i < 2; ++i) {
    const bool isYear = i == 0;
    const fui::Rect row = screen.takeTop(toybox::kRowHeight, 4);
    screen.target().stroke(row, fui::Paint::solid(fui::Color::Black), toybox::kHairline);

    fui::TextStyle label;
    label.font = toybox::kUiFont;
    label.align = fui::TextAlign::Left;
    screen.target().text(row.inset(fui::Insets{0, 0, 0, toybox::kGutter}), isYear ? "YEAR" : "MONTH", label);

    const int buttonW = 52;
    const int valueW = 150;
    const int right = row.right() - toybox::kGutter;
    const fui::Rect plusBox = fui::makeRect(right - buttonW, row.y + 6, buttonW, row.height - 12);
    const fui::Rect valueBox = fui::makeRect(plusBox.x - 6 - valueW, row.y, valueW, row.height);
    const fui::Rect minusBox = fui::makeRect(valueBox.x - 6 - buttonW, row.y + 6, buttonW, row.height - 12);

    fui::TextStyle value;
    value.font = toybox::kUiFont;
    value.align = fui::TextAlign::Center;
    screen.target().text(valueBox, isYear ? yearText : monthText, value);

    const bool canDown = isYear ? model.canPrevYear : model.canPrevMonth;
    const bool canUp = isYear ? model.canNextYear : model.canNextMonth;
    const fui::ActionId action = isYear ? ActionCalendarYear : ActionCalendarMonth;
    for (int side = 0; side < 2; ++side) {
      const bool isPlus = side == 1;
      const fui::Rect box = isPlus ? plusBox : minusBox;
      const bool live = isPlus ? canUp : canDown;
      fui::ButtonProps arrow;
      arrow.label = isPlus ? "+" : "-";
      arrow.text = value;
      arrow.action = live ? action : fui::NO_ACTION;
      arrow.value = isPlus ? 1 : -1;
      arrow.enabled = live;
      // Spent, not missing: dithered so the arrow still reads as a control and
      // says which end of the archive you have reached.
      arrow.styles = live ? toybox::rowStyles() : toybox::disabledStepperStyles();
      screen.button(arrow, box);
    }
  }
  screen.spacer(toybox::kGutter);

  // Square cells, and the whole block centred in what is left. Anchored to the
  // top it stranded five weeks of calendar above a third of a screen of nothing;
  // a month is one object and should sit like one.
  const fui::Rect avail = screen.body();
  const int cols = 7;
  const int gap = 3;
  const int cell = (avail.width - (cols - 1) * gap) / cols;
  const int rows = 6;
  const int blockHeight = 28 + rows * cell + (rows - 1) * gap;
  const fui::Rect body = fui::makeRect(avail.x, avail.y + (avail.height - blockHeight) / 2, avail.width, blockHeight);

  // Weekday initials, so the grid reads as a calendar before any date does.
  static const char* kInitials[7] = {"S", "M", "T", "W", "T", "F", "S"};
  fui::TextStyle head;
  head.font = toybox::kTileFont;
  head.align = fui::TextAlign::Center;
  for (int c = 0; c < cols; ++c) {
    screen.target().text(fui::makeRect(body.x + c * (cell + gap), body.y, cell, 22), kInitials[c], head);
  }

  const int gridTop = body.y + 28;

  // One region for the month, resolved by dayCellAt against these very numbers.
  // A slot per date overflowed the 24-slot buffer and killed every day from the
  // 20th on; see CalendarLayout in the header.
  CalendarLayout layout;
  layout.valid = true;
  layout.originX = static_cast<int16_t>(body.x);
  layout.originY = static_cast<int16_t>(gridTop);
  layout.cell = static_cast<int16_t>(cell);
  layout.gap = static_cast<int16_t>(gap);
  layout.cols = static_cast<int8_t>(cols);
  layout.rows = static_cast<int8_t>(rows);
  screen.frame().hit(fui::makeRect(body.x, gridTop, static_cast<int16_t>(cols * cell + (cols - 1) * gap),
                                   static_cast<int16_t>(rows * cell + (rows - 1) * gap)),
                     ActionCalendarDay, 0);

  for (int i = 0; i < 42; ++i) {
    const CalendarDay& day = model.cells[i];
    if (day.day == 0) continue;
    const fui::Rect box =
        fui::makeRect(body.x + (i % cols) * (cell + gap), gridTop + (i / cols) * (cell + gap), cell, cell);

    if (day.inArchive) {
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kHairline);
    }
    const fui::Color ink = fui::Color::Black;

    // Today gets a second, heavier frame: on a screen of dates the one that
    // matters most should not need reading to find.
    if (i == model.todayCell) {
      screen.target().stroke(box, fui::Paint::solid(ink), toybox::kFrame);
    }

    char label[4];
    std::snprintf(label, sizeof(label), "%d", day.day);
    fui::TextStyle number;
    number.font = toybox::kTileFont;
    number.align = fui::TextAlign::Center;
    number.color = ink;
    // A date with no puzzle is dithered rather than hidden, so the month keeps
    // its shape and the gaps read as "nothing here" instead of as a bug.
    if (!day.inArchive) number.color = fui::Color::DarkGray;
    screen.target().text(fui::makeRect(box.x, box.y + 6, box.width, 22), label, number);

    if (day.played) {
      const int markSize = cell / 2;
      drawDayMark(screen, fui::makeRect(box.x + (cell - markSize) / 2, box.y + cell - markSize - 5, markSize, markSize),
                  day, ink);
    }
  }
  return layout;
}

namespace {

// The sixteen-day grid: one cell per day, oldest top-left, newest bottom-right.
//
// It is the same shape as a Connections board on purpose, but nothing about it
// is decorative filler -- every cell is a day you did or did not play, so the
// picture in the middle of the menu is your own record and changes daily.
//
//   solid     finished clean
//   dithered  finished with mistakes
//   crossed   ran out of guesses
//   empty     never opened
//
// The last cell wears a heavy border: that one is today.
void drawRecentGrid(toybox::Screen& screen, const fui::Rect& box, const MenuModel& model) {
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  const int cell = box.width / 4;
  const int size = cell - 6;
  for (int i = 0; i < 16; ++i) {
    const fui::Rect c = fui::makeRect(box.x + (i % 4) * cell, box.y + (i / 4) * cell, size, size);
    const bool isToday = i == 15;
    const uint8_t border = isToday ? toybox::kRule : toybox::kHairline;
    switch (model.recent[i]) {
      case DayResult::Perfect:
        screen.target().fill(c, ink);
        break;
      case DayResult::Won:
        screen.target().fill(c, fui::Paint::dither(fui::Color::LightGray));
        screen.target().stroke(c, ink, border);
        break;
      case DayResult::Lost:
        screen.target().stroke(c, ink, border);
        drawCross(screen, fui::makeRect(c.x + 10, c.y + 10, c.width - 20, c.height - 20), ink);
        break;
      case DayResult::Unplayed:
      default:
        screen.target().stroke(c, ink, border);
        break;
    }
  }
}

// Corner brackets, the same shape the chess board wears, so the two games read
// as the same device. Drawn through the DrawTarget rather than through the
// Toybox helper because that one takes a GfxRenderer and screens stay
// freestanding.
void drawBrackets(toybox::Screen& screen, const fui::Rect& box, const int arm) {
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  const int w = toybox::kFrame;
  for (int cx = 0; cx < 2; ++cx) {
    for (int cy = 0; cy < 2; ++cy) {
      const int x = cx == 0 ? box.x : box.right() - arm;
      const int y = cy == 0 ? box.y : box.bottom() - w;
      screen.target().fill(fui::makeRect(x, y, arm, w), ink);
      const int vx = cx == 0 ? box.x : box.right() - w;
      const int vy = cy == 0 ? box.y : box.bottom() - arm;
      screen.target().fill(fui::makeRect(vx, vy, w, arm), ink);
    }
  }
}

const char* todayState(const MenuModel& model) {
  if (model.todayDone) return "SOLVED";
  if (model.inProgress) return "IN PROGRESS";
  return "NOT STARTED";
}

}  // namespace

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  toyboxChrome(screen, "CONNECTIONS", nullptr);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  char date[16] = "NONE YET";
  if (model.hasPuzzles) formatDate(model.newestDate, date, sizeof(date));
  const fui::Rect body = screen.body();

  // The masthead. Today's date is the headline and the whole block is the hit
  // target, so playing today is one tap on the largest thing on the screen.
  fui::TextStyle hero;
  hero.font = toybox::kDisplayFont;
  hero.align = fui::TextAlign::Left;
  screen.target().text(fui::makeRect(body.x, body.y, body.width, 60), date, hero);

  fui::TextStyle sub;
  sub.font = toybox::kUiFont;
  sub.align = fui::TextAlign::Left;
  // The menu binds the body slot to the 35px serif cut, so this 26px band is
  // one more box shorter than its own line box.
  screen.target().text(toybox::inkCentred(fui::makeRect(body.x, body.y + 62, body.width, 26), toybox::kSerifTileCut),
                       todayState(model), sub);
  screen.frame().hit(fui::makeRect(body.x, body.y, body.width, 96), ActionNewest, 0);

  screen.target().fill(fui::makeRect(body.x, body.y + 108, body.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  char stats[64];
  std::snprintf(stats, sizeof(stats), "%d PLAYED   %d PERFECT   STREAK %d", model.played, model.perfect, model.streak);
  fui::TextStyle statsStyle;
  statsStyle.font = toybox::kTileFont;
  statsStyle.align = fui::TextAlign::Left;
  screen.target().text(fui::makeRect(body.x, body.y + 122, body.width, 24), stats, statsStyle);

  // 196 and 200, not 216 and 228. The third door below costs a row, and it is
  // paid for out of the ornament rather than out of the masthead or the stats,
  // both of which are text that would have to shrink to give it up. Chosen
  // against two alternatives rendered side by side: one where the ornament kept
  // its size and swallowed the ARCHIVE row, and one where the how-to moved into
  // the header. Both of the tester's complaints here were discoverability, and
  // this is the only arrangement that answers each with a label rather than
  // with an inference.
  const int grid = 196;
  const fui::Rect gridBox = fui::makeRect(body.x + (body.width - grid) / 2, body.y + 200, grid, grid);
  drawBrackets(screen, fui::makeRect(gridBox.x - 22, gridBox.y - 22, grid + 38, grid + 38), 34);
  drawRecentGrid(screen, gridBox, model);

  // The bracketed block is a control now, and it always looked like one.
  //
  // It is the largest object on the screen and it wears the same corner
  // brackets as the chess board, so a tester tapped it and got nothing: "not
  // sure what this is? ... touching the middle squares ... don't do anything".
  // Sixteen days of your own record is a picture of the archive, so the archive
  // is where it goes. No new control, no change to what is drawn -- the thing
  // that looked tappable now is.
  screen.frame().hit(gridBox, ActionNewest, 1);

  fui::TextStyle caption;
  caption.font = toybox::kTileFont;
  caption.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(body.x, gridBox.bottom() + 26, body.width, 24), "LAST 16 DAYS", caption);

  fui::ListItem rows[3];
  for (auto& r : rows) r = fui::ListItem{};
  char total[16] = "";
  std::snprintf(total, sizeof(total), "%d", model.puzzleCount);
  rows[0].label = "HOW TO PLAY";
  rows[0].actionValue = 3;
  rows[1].label = "ARCHIVE";
  rows[1].value = total;
  rows[1].actionValue = 1;
  rows[2].label = model.upToDate ? "ALL CAUGHT UP" : "GET PUZZLES";
  rows[2].value = model.upToDate ? "" : "WI-FI";
  rows[2].enabled = !model.upToDate;
  rows[2].actionValue = 2;
  fui::ListProps list;
  list.items = rows;
  list.count = 3;
  list.selectedIndex = -1;
  list.action = ActionNewest;
  screen.list(list, 3 * (toybox::kRowHeight + 4), fui::LayoutAnchor::Bottom);
}

// One page, and it is mostly a board.
//
// Chosen against a page of prose and a numbered rules card, rendered side by
// side. The problem this screen exists to solve is recognition -- a tester who
// plays this shelf's other games could not tell what Connections was ("not sure
// what this is?") -- and a picture of a real board with one group already taken
// answers that faster than any paragraph. The prose that remains is only the
// things the picture cannot say.
void buildHowTo(toybox::Screen& screen) {
  toyboxChrome(screen, "HOW TO PLAY", nullptr);
  screen.insetContent(fui::Insets{toybox::kGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  auto& target = screen.target();
  const fui::Rect body = screen.body();
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);

  fui::TextStyle heading;
  heading.font = toybox::kUiFont;
  heading.align = fui::TextAlign::Left;
  const int16_t headH = target.lineHeight(toybox::kUiFont);
  const int16_t proseH = target.lineHeight(toybox::kTileFont);
  const int16_t demoW = static_cast<int16_t>((body.width - 3 * kTileGap) / 4);

  fui::TextStyle lead;
  lead.font = toybox::kTileFont;
  lead.align = fui::TextAlign::Left;
  lead.maxLines = 2;

  int16_t y = body.y;
  target.text(fui::makeRect(body.x, y, body.width, headH), "SIXTEEN WORDS, FOUR GROUPS", heading);
  y = static_cast<int16_t>(y + headH + 6);
  target.text(fui::makeRect(body.x, y, body.width, static_cast<int16_t>(proseH * 2)),
              "Tap four that share something, then SUBMIT. Four wrong guesses and it is over.", lead);
  y = static_cast<int16_t>(y + proseH * 2 + 20);

  const int16_t tileH = 54;
  // The solved row carries a group name AND its four words, so it needs the
  // height of both. At the plain tile height the name printed straight through
  // the words -- which is exactly what the real board reserves a taller row for.
  const int16_t solvedH = static_cast<int16_t>(proseH * 2 + 20);
  static const char* kBoard[4][4] = {
      {"HAIL", "SLEET", "RAIN", "SNOW"},
      {"BOB", "OTTO", "ANNA", "EWE"},
      {"REED", "PIPE", "DRUM", "HORN"},
      {"MARS", "VENUS", "PLUTO", "CERES"},
  };
  fui::TextStyle tileText;
  tileText.font = toybox::kTileFont;
  tileText.align = fui::TextAlign::Center;
  fui::TextStyle solvedText = tileText;
  solvedText.color = fui::Color::White;
  for (int r = 0; r < 4; ++r) {
    const int16_t rowY = static_cast<int16_t>(r == 0 ? y : y + solvedH + kTileGap + (r - 1) * (tileH + kTileGap));
    if (r == 0) {
      // The taken group: one filled row, the way the board does it -- name over
      // members, both knocked out white.
      target.fill(fui::makeRect(body.x, rowY, body.width, solvedH), ink);
      target.text(fui::makeRect(body.x, static_cast<int16_t>(rowY + 6), body.width, proseH), "WET WEATHER", solvedText);
      for (int c = 0; c < 4; ++c) {
        target.text(fui::makeRect(static_cast<int16_t>(body.x + c * (demoW + kTileGap)),
                                  static_cast<int16_t>(rowY + proseH + 12), demoW, proseH),
                    kBoard[r][c], solvedText);
      }
      continue;
    }
    for (int c = 0; c < 4; ++c) {
      const fui::Rect tile = fui::makeRect(static_cast<int16_t>(body.x + c * (demoW + kTileGap)), rowY, demoW, tileH);
      target.stroke(tile, ink, toybox::kHairline);
      target.text(fui::makeRect(tile.x, static_cast<int16_t>(rowY + (tileH - proseH) / 2), demoW, proseH), kBoard[r][c],
                  tileText);
    }
  }
  y = static_cast<int16_t>(y + solvedH + 4 * kTileGap + 3 * tileH + 16);

  fui::TextStyle note;
  note.font = toybox::kTileFont;
  note.align = fui::TextAlign::Left;
  note.maxLines = 2;
  target.text(fui::makeRect(body.x, y, body.width, static_cast<int16_t>(proseH * 2)),
              "WET WEATHER is solved and has closed up. ONE AWAY means three of your four were right.", note);
  y = static_cast<int16_t>(y + proseH * 2 + 14);

  // The pips are drawn, not described, for the same reason the board above is:
  // this is the variant that shows things. Four filled dots is what a fresh
  // puzzle's mistake budget actually looks like at the foot of the board, so
  // the reader recognises it there rather than having to decode a sentence.
  const int16_t pip = 16;
  for (int i = 0; i < connections::kMaxMistakes; ++i) {
    target.fill(fui::makeRect(static_cast<int16_t>(body.x + i * (pip + 8)), y, pip, pip), ink,
                static_cast<uint8_t>(pip / 2));
  }
  fui::TextStyle pipLabel;
  pipLabel.font = toybox::kTileFont;
  pipLabel.align = fui::TextAlign::Left;
  const int16_t pipBand = static_cast<int16_t>(connections::kMaxMistakes * (pip + 8) + 8);
  target.text(fui::makeRect(static_cast<int16_t>(body.x + pipBand), static_cast<int16_t>(y - 4),
                            static_cast<int16_t>(body.width - pipBand), proseH),
              "wrong guesses left", pipLabel);
  y = static_cast<int16_t>(y + pip + 16);

  target.text(fui::makeRect(body.x, y, body.width, static_cast<int16_t>(proseH * 2)),
              "SHUFFLE only moves the tiles. It never changes the answer.", note);

  // Tap anywhere to leave, the same contract Murdle's how-to uses. Registered
  // last so it sits under everything above rather than over it.
  screen.frame().hit(screen.device().screen(), ActionHowTo, 0);
}

}  // namespace connectionsui
