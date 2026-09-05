// Every board of the published archive, drawn through the real Connections
// screen against the real Instrument Serif cuts, asserting that not one line of
// type is ever shortened.
//
// The rule this pins is Mario's, and it has no exceptions: a tile shows the
// whole word, a solved row shows the whole category and the whole list of what
// was in it. Nothing elides, ever.
//
// Why this suite exists apart from host-tests/ui: that one builds screens
// against a draw target answering a flat ten pixels a character, which cannot
// see a truncation. This one links lib/EpdFont and the generated font headers
// and measures with the same call GfxRendererTarget::measureText makes, so the
// widths here are the widths on the panel.
//
// What it checks, per drawn run:
//
//   1  The run measures inside the rect it was given. That IS the renderer's
//      own truncation test -- GfxRendererTarget::text() draws a string
//      untouched when getTextWidth <= rect.width and reaches for U+2026 only
//      when it does not -- so a run that passes here cannot come back short.
//   2  A solved row's two blocks stay inside the row and clear of each other.
//      The list used to draw its second line past the black fill, in white, on
//      white paper, which reads as the sentence stopping.
//
// It is checked against the two rules that came before it: reverting
// chooseTileCut to the shipped one-line test fails on 3 boards, and to the
// laid-out-at-all test fails on 48.

#include <EpdFontFamily.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/apps_local/connections/ConnectionsPack.h"
#include "../../src/apps_local/connections/ConnectionsScreens.h"
#include "../../src/apps_local/ui/fonts/instrument_10.h"
#include "../../src/apps_local/ui/fonts/instrument_13.h"
#include "../../src/apps_local/ui/fonts/toybox_20.h"
#include "../../src/apps_local/ui/fonts/toybox_30.h"

namespace fui = freeink::ui;

namespace {

int checksRun = 0;
int checksFailed = 0;
// Kept apart so a flood of one kind cannot hide the other: the two faults have
// different causes and the sample is the first thing anyone reads.
std::vector<std::string> tileFailures;
std::vector<std::string> rowFailures;

void fail(std::vector<std::string>& into, const std::string& what) {
  ++checksFailed;
  if (into.size() < 6) into.push_back(what);
}

// The faces the board really binds. ConnectionsActivity takes serifBoardFaces()
// for the chrome pass and then rebinds FONT_BODY to the smaller serif cut for
// the tile pass (View::Board in ConnectionsActivity.cpp); `bodyFamily` below is
// switched at the same point for the same reason.
EpdFont serifSmall(&instrument_10);
EpdFont serifTile(&instrument_13);
EpdFont ui20(&toybox_20);
EpdFont display30(&toybox_30);
EpdFontFamily serifSmallFamily(&serifSmall);
EpdFontFamily serifTileFamily(&serifTile);
EpdFontFamily uiFamily(&ui20);
EpdFontFamily displayFamily(&display30);

// Records what was drawn and measures it the way the panel would.
class FontTarget final : public fui::DrawTarget {
 public:
  struct Run {
    fui::Rect rect;
    std::string text;
    int measured;
    uint8_t maxLines;
    fui::FontId font;
  };
  std::vector<Run> texts;
  const EpdFontFamily* bodyFamily = &uiFamily;

  const EpdFontFamily* familyFor(const fui::FontId font) const {
    if (font == fui::FONT_SLOT_SMALL) return &serifTileFamily;
    if (font == fui::FONT_SLOT_BODY) return bodyFamily;
    return &displayFamily;
  }

  int widthOf(const fui::FontId font, const std::string& text) const {
    if (text.empty()) return 0;
    int w = 0;
    int h = 0;
    familyFor(font)->getTextDimensions(text.c_str(), &w, &h);
    return w;
  }

  fui::Size measureText(const fui::FontId font, const char* text, const fui::TextStyle) const override {
    return fui::Size{static_cast<int16_t>(widthOf(font, text == nullptr ? "" : text)), lineHeight(font)};
  }
  int16_t lineHeight(const fui::FontId font) const override {
    return static_cast<int16_t>(familyFor(font)->getData(EpdFontFamily::REGULAR)->advanceY);
  }
  void fill(fui::Rect, fui::Paint, uint8_t = 0, uint8_t = 0xFF) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t = 0, uint8_t = 0xFF) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(const fui::Rect rect, const char* text, const fui::TextStyle style) override {
    if (text == nullptr) return;
    texts.push_back(
        Run{rect, text, widthOf(style.font, text), style.maxLines == 0 ? uint8_t{1} : style.maxLines, style.font});
  }
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint = {},
              fui::Rotation = fui::Rotation::None) override {}

  // What the renderer would make of one text() call, by its own rules:
  // GfxRendererTarget::text() draws the string untouched when it already fits,
  // otherwise hands it to GfxRenderer::wrappedText, which breaks at SPACES
  // ONLY, up to maxLines, and ellipsises whatever is left over. Mirrored here
  // because the question this suite asks -- did the player see the whole
  // string -- can only be answered against what the renderer actually does
  // with the call, not against what the caller hoped.
  struct Placed {
    int lines = 1;
    bool elided = false;
    int widest = 0;
  };

  Placed place(const Run& run) const {
    Placed out;
    out.widest = run.measured;
    if (run.measured <= run.rect.width) return out;
    if (run.maxLines <= 1) {
      out.elided = true;
      return out;
    }
    // Greedy, breaking at spaces only.
    out.lines = 0;
    out.widest = 0;
    size_t at = 0;
    while (at < run.text.size()) {
      while (at < run.text.size() && run.text[at] == ' ') ++at;
      if (at >= run.text.size()) break;
      if (out.lines >= run.maxLines) {
        out.elided = true;
        return out;
      }
      size_t take = run.text.size() - at;
      if (widthOf(run.font, run.text.substr(at, take)) > run.rect.width) {
        size_t space = std::string::npos;
        for (size_t i = 1; i < take; ++i) {
          if (run.text[at + i] != ' ') continue;
          if (widthOf(run.font, run.text.substr(at, i)) <= run.rect.width) space = i;
        }
        // A single word wider than the box: the renderer cannot break it and
        // ellipsises instead.
        if (space == std::string::npos) {
          out.elided = true;
          return out;
        }
        take = space;
      }
      const int w = widthOf(run.font, run.text.substr(at, take));
      if (w > out.widest) out.widest = w;
      ++out.lines;
      at += take;
    }
    if (out.lines == 0) out.lines = 1;
    return out;
  }
};

fui::DeviceContext device() {
  fui::DeviceContext ctx;
  ctx.width = 480;
  ctx.height = 800;
  ctx.hasTouch = true;
  ctx.hasButtons = true;
  return ctx;
}

// --- the pack, read with the app's own reader -------------------------------

struct Blob {
  std::vector<uint8_t> bytes;
};

bool readBlob(void* ctx, const uint32_t offset, void* dst, const uint32_t len) {
  const Blob* blob = static_cast<const Blob*>(ctx);
  if (static_cast<size_t>(offset) + len > blob->bytes.size()) return false;
  std::memcpy(dst, blob->bytes.data() + offset, len);
  return true;
}

bool loadBlob(const char* path, Blob& out) {
  std::FILE* fh = std::fopen(path, "rb");
  if (fh == nullptr) return false;
  std::fseek(fh, 0, SEEK_END);
  const long size = std::ftell(fh);
  std::fseek(fh, 0, SEEK_SET);
  out.bytes.resize(static_cast<size_t>(size));
  const size_t read = std::fread(out.bytes.data(), 1, out.bytes.size(), fh);
  std::fclose(fh);
  return read == out.bytes.size();
}

// --- what one rendered board is asked --------------------------------------

// The gap between block rows, which ConnectionsScreens spells kTileGap. Named
// here so the row bands this suite reconstructs cannot drift from the ones the
// screen drew.
constexpr int kRowGap = connectionsui::kTileGap;

struct Tally {
  int tileFaults = 0;
  int rowFaults = 0;
  int badBoards = 0;
  // Distinct boards, which is the number that says how often a player meets
  // this: a fault on one board is met once however many renders it spans.
  int tileBoards = 0;
  int rowBoards = 0;
  bool tileHere = false;
  bool rowHere = false;
  int tightestRowSlack = 1 << 30;
  std::string tightestRow;
};

// Every run this screen draws, judged as the renderer would place it.
//
// Two ways for text not to reach the player, and the suite has to know both:
//
//   cut short   the renderer could not fit the string and swapped the tail for
//               U+2026, so a different phrase is on the panel;
//   off its row the block was placed past the end of the block row it belongs
//               to. On a solved row that means white type on white paper below
//               a black fill, which reads as the sentence simply stopping --
//               and is what the app did to every two-line word list.
//
// The box a run is GIVEN is not the test for the second one. This codebase
// positions centre-aligned labels with boxes deliberately shorter than their
// own line box (see toybox::inkCentred), so a run taller than its rect is
// normal. What is never normal is a run outside the row it was drawn for.
void inspect(const FontTarget& target, const uint32_t date, const int solved, const connectionsui::BoardLayout& layout,
             Tally& tally, bool& boardBad) {
  for (const FontTarget::Run& run : target.texts) {
    ++checksRun;
    const FontTarget::Placed placed = target.place(run);
    const int lineHeight = target.lineHeight(run.font);
    const int block = placed.lines * lineHeight;
    const int top = run.rect.y + (run.rect.height - block > 0 ? (run.rect.height - block) / 2 : 0);

    // Which of the four block rows this run was drawn into.
    int band = -1;
    for (int r = 0; r < connections::kGroups; ++r) {
      const int bandTop = layout.grid.y + r * (layout.rowHeight + kRowGap);
      if (run.rect.y >= bandTop && run.rect.y < bandTop + layout.rowHeight) band = r;
    }
    const int bandTop = layout.grid.y + band * (layout.rowHeight + kRowGap);
    const bool offRow = band >= 0 && (top < bandTop || top + block > bandTop + layout.rowHeight);
    if (!placed.elided && !offRow) continue;

    boardBad = true;
    const bool inSolvedRow = band >= 0 && band < solved;
    char line[320];
    std::snprintf(line, sizeof(line), "%u (%d solved) %s: '%s' %s", date, solved, inSolvedRow ? "solved row" : "tile",
                  run.text.c_str(), placed.elided ? "is cut short" : "is drawn off its row");
    if (inSolvedRow) {
      ++tally.rowFaults;
      tally.rowHere = true;
      fail(rowFailures, line);
    } else {
      ++tally.tileFaults;
      tally.tileHere = true;
      fail(tileFailures, line);
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  char idxPath[512];
  char datPath[512];
  std::snprintf(idxPath, sizeof(idxPath), "%s/archive.idx", dir);
  std::snprintf(datPath, sizeof(datPath), "%s/archive.dat", dir);

  Blob idx;
  Blob dat;
  if (!loadBlob(idxPath, idx) || !loadBlob(datPath, dat)) {
    std::printf("FAIL cannot read %s / %s\n", idxPath, datPath);
    return 1;
  }

  connections::PackReader pack;
  if (!pack.open(readBlob, &idx, &dat, static_cast<uint32_t>(idx.bytes.size()))) {
    std::printf("FAIL the fixture pack does not open\n");
    return 1;
  }

  Tally tally;
  int boards = 0;
  int rendersLarge = 0;
  int rendersSmall = 0;
  int rowsRendered = 0;

  for (int index = 0; index < pack.count(); ++index) {
    connections::Puzzle puzzle;
    if (!pack.readPuzzle(index, puzzle)) {
      std::printf("FAIL puzzle %d does not read\n", index);
      return 1;
    }
    ++boards;

    tally.tileHere = false;
    tally.rowHere = false;
    connections::Game game;
    game.start(puzzle, 0x9E3779B9u ^ (static_cast<uint32_t>(puzzle.id) * 2654435761u));

    bool boardBad = false;
    // Every solve state, because a solved group both takes a row and hands the
    // tiles that are left a larger cut. Five renders: the fresh board, one
    // after each group falls, and the win where all four are rows and no tile
    // is left.
    for (int solved = 0; solved <= connections::kGroups; ++solved) {
      if (solved > 0) {
        // Solve the lowest group still on the board.
        int target = -1;
        for (int i = 0; i < game.tileCount() && target < 0; ++i) target = game.tileGroup(i);
        game.deselectAll();
        for (int i = 0; i < game.tileCount(); ++i) {
          if (game.tileGroup(i) == target) game.toggleTile(i);
        }
        if (game.submit() != connections::Guess::Solved) {
          std::printf("FAIL could not solve group on puzzle %u\n", puzzle.date);
          return 1;
        }
      }

      connectionsui::BoardModel model;
      model.game = &game;
      model.date = puzzle.date;

      FontTarget target;
      toybox::Interactions interactions;
      const fui::DeviceContext ctx = device();
      const fui::InputSnapshot noInput{};
      toybox::Frame frame(target, ctx, noInput, interactions);
      toybox::Screen screen(frame, toybox::themeTokens());
      const connectionsui::BoardLayout layout = connectionsui::buildBoardChrome(screen, model);
      target.texts.clear();
      // The tile pass draws with FONT_BODY rebound to the smaller serif cut,
      // exactly as ConnectionsActivity does between the two passes.
      target.bodyFamily = &serifSmallFamily;
      connectionsui::buildBoardTiles(screen, model, layout);

      inspect(target, puzzle.date, solved, layout, tally, boardBad);

      // A solved row's two blocks share the row: they must both land inside it
      // and never touch. Runs are grouped by the black fill they sit on.
      for (int r = 0; r < solved; ++r) {
        const fui::Rect row = fui::makeRect(layout.grid.x, layout.grid.y + r * (layout.rowHeight + kRowGap),
                                            layout.grid.width, static_cast<int16_t>(layout.rowHeight));
        int top = 1 << 30;
        int bottom = -(1 << 30);
        int lines = 0;
        for (const FontTarget::Run& run : target.texts) {
          if (run.rect.y < row.y || run.rect.y >= row.y + row.height) continue;
          ++lines;
          if (run.rect.y < top) top = run.rect.y;
          if (run.rect.y + run.rect.height > bottom) bottom = run.rect.y + run.rect.height;
        }
        if (lines == 0) continue;
        ++rowsRendered;
        ++checksRun;
        const int slack = row.height - (bottom - top);
        if (slack < 0) {
          ++tally.rowFaults;
          tally.rowHere = true;
          boardBad = true;
          char line[256];
          std::snprintf(line, sizeof(line), "%u: a solved row's text is %dpx tall in a %dpx row", puzzle.date,
                        bottom - top, row.height);
          fail(rowFailures, line);
        }
        if (slack < tally.tightestRowSlack) {
          tally.tightestRowSlack = slack;
          char line[256];
          std::snprintf(line, sizeof(line), "%u group %d: %dpx of text in a %dpx row", puzzle.date, r, bottom - top,
                        row.height);
          tally.tightestRow = line;
        }
      }

      if (solved == 0 && !target.texts.empty()) {
        // Which cut the fresh board took, for the report.
        const int16_t small = target.lineHeight(fui::FONT_SLOT_BODY);
        const int16_t large = target.lineHeight(fui::FONT_SLOT_SMALL);
        int tallest = 0;
        for (const FontTarget::Run& run : target.texts) {
          if (run.rect.height > tallest) tallest = run.rect.height;
        }
        if (tallest >= large && large > small) {
          ++rendersLarge;
        } else {
          ++rendersSmall;
        }
      }
    }
    if (boardBad) ++tally.badBoards;
    if (tally.tileHere) ++tally.tileBoards;
    if (tally.rowHere) ++tally.rowBoards;
  }

  std::printf("boards %d, renders %d\n", boards, boards * (connections::kGroups + 1));
  std::printf("fresh boards at the large cut %d, at the small cut %d\n", rendersLarge, rendersSmall);
  std::printf("solved rows rendered %d, tightest %s\n", rowsRendered, tally.tightestRow.c_str());
  if (tally.tileFaults > 0 || tally.rowFaults > 0) {
    std::printf("TEXT THE PLAYER NEVER SEES: %d on tiles (%d of %d boards), %d on solved rows (%d of %d boards)\n",
                tally.tileFaults, tally.tileBoards, boards, tally.rowFaults, tally.rowBoards, boards);
    for (const std::string& line : tileFailures) std::printf("  %s\n", line.c_str());
    if (static_cast<int>(tileFailures.size()) < tally.tileFaults) std::printf("  ...\n");
    for (const std::string& line : rowFailures) std::printf("  %s\n", line.c_str());
    if (static_cast<int>(rowFailures.size()) < tally.rowFaults) std::printf("  ...\n");
  }
  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
