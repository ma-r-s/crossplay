// Host tests for this fork's screens. No device, no PlatformIO, no renderer:
// FreeInkUI is freestanding C++17 and the screen builders were written to stay
// that way, so a laptop can build a screen against a fake draw target and ask
// what it drew and what it made tappable.
//
// This is the half of the app that used to be untestable. The chess *rules* had
// 2940 assertions and the screens had none, which was backwards: the rules are
// stable and the screens change every time Mario asks for something. Two real
// bugs this file would have caught the day they were written are pinned below.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/apps_local/ShelfScreen.h"
#include "../../src/apps_local/battleship/BattleshipScreens.h"
#include "../../src/apps_local/checkers/CheckersScreens.h"
#include "../../src/apps_local/chess/ChessScreens.h"
#include "../../src/apps_local/connectfour/ConnectFourScreens.h"
#include "../../src/apps_local/connections/ConnectionsScreens.h"
#include "../../src/apps_local/dungeon/DungeonScreens.h"
#include "../../src/apps_local/forehead/ForeheadScreens.h"
#include "../../src/apps_local/hackernews/HackerNewsScreens.h"
#include "../../src/apps_local/insider/InsiderScreens.h"
#include "../../src/apps_local/instapaper/InstapaperScreens.h"
#include "../../src/apps_local/knucklebones/KnucklebonesScreens.h"
#include "../../src/apps_local/link/LinkScreens.h"
#include "../../src/apps_local/minesweeper/MinesweeperScreens.h"
#include "../../src/apps_local/murdle/MurdleScreens.h"
#include "../../src/apps_local/murdle/MurdleText.h"
#include "../../src/apps_local/player/PlayerAvatar.h"
#include "../../src/apps_local/player/PlayerScreen.h"
#include "../../src/apps_local/seasalt/SeaSaltScreens.h"
#include "../../src/apps_local/study/StudyScreens.h"
#include "../../src/apps_local/sudoku/SudokuScreens.h"
#include "../../src/apps_local/toybattle/ToyBattleMenus.h"
#include "../../src/apps_local/toybattle/ToyBattleScreens.h"
#include "../../src/apps_local/trivia/TriviaScreens.h"
#include "../../src/apps_local/ui/ToyboxIcons.h"
#include "../../src/apps_local/ui/ToyboxText.h"
#include "../../src/apps_local/wavelength/WavelengthScreens.h"

namespace fui = freeink::ui;

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL %s:%d  %s\n", "test_ui.cpp", line, what);
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// Records what was drawn instead of drawing it. Text is what the assertions
// mostly care about; the rects matter for the hit-testing checks.
class FakeTarget final : public fui::DrawTarget {
 public:
  struct TextRun {
    fui::Rect rect;
    std::string text;
    fui::Color color;
    // The whole style, not just the ink. A single-line run wider than its rect
    // is a silent truncation -- the SDK ellipsizes and logs nothing -- and that
    // is what put "IN THE BAR: TILE = TROOPS IN HAND, TRIANGLE = LEFT ..." on
    // the terrain card for as long as the card existed. It is only assertable
    // if the assertion can see maxLines.
    fui::TextStyle style;
  };

  // An avatar is four stacked 1-bpp masks and no text at all, so without
  // recording these there is nothing to assert about a face: it would draw, or
  // not draw, or draw in the same colour as the bar behind it, and every one of
  // those would look identical from here.
  struct Blit {
    fui::Rect rect;
    const uint8_t* data;
    fui::Color color;
  };

  std::vector<TextRun> texts;
  std::vector<fui::Rect> fills;
  // The paint too, not just the rect. A state expressed only as a different
  // ground -- Battlefield freezing a card -- is otherwise untestable, and it is
  // exactly the kind of state a screenshot will not happen to contain.
  std::vector<fui::Paint> fillPaints;
  std::vector<Blit> blits;
  // Outlines and marks, which used to be dropped on the floor. "Does this look
  // like a button" is a question about a BORDER, so a target that records only
  // text and fills cannot be asked it -- and that is why the forehead start
  // control shipped as a bare headline that three rounds of tests called fine.
  struct Stroke {
    fui::Rect rect;
    uint8_t width;
  };
  std::vector<Stroke> strokes;
  struct Triangle {
    fui::Point a, b, c;
    fui::Color color;
  };
  std::vector<Triangle> triangles;

  // A fixed cell, but not a fixed LINE. A layout that reserves a constant
  // number of pixels for wrapped text is correct at one metric and wrong at
  // every other, and 20 happens to be small enough to hide it: the rules
  // caption was given a flat 132px, which fits four fake lines and not four
  // real ones (the display cut is 45). Tests that care re-run at a taller
  // line, where a hardcoded box overflows exactly as it does on the device.
  int16_t lineH = 20;

  fui::Size measureText(const fui::FontId, const char* text, const fui::TextStyle) const override {
    return fui::Size{static_cast<int16_t>(text ? std::strlen(text) * 10 : 0), lineH};
  }
  int16_t lineHeight(const fui::FontId) const override { return lineH; }

  void fill(const fui::Rect rect, const fui::Paint paint, const uint8_t = 0, const uint8_t = 0xFF) override {
    if (paint.kind != fui::PaintKind::None) {
      fills.push_back(rect);
      fillPaints.push_back(paint);
    }
  }
  void stroke(const fui::Rect rect, const fui::Paint paint, const uint8_t width, const uint8_t = 0,
              const uint8_t = 0xFF) override {
    if (paint.kind != fui::PaintKind::None) strokes.push_back(Stroke{rect, width});
  }
  void line(const fui::Point, const fui::Point, const uint8_t, const fui::Paint) override {}
  void triangle(const fui::Point a, const fui::Point b, const fui::Point c, const fui::Paint paint) override {
    triangles.push_back(Triangle{a, b, c, paint.color});
  }
  void text(const fui::Rect rect, const char* text, const fui::TextStyle style) override {
    if (text != nullptr) texts.push_back(TextRun{rect, text, style.color, style});
  }
  void bitmap(const fui::Rect rect, const fui::BitmapRef bitmap, const fui::BitmapMode, const fui::Paint paint = {},
              const fui::Rotation = fui::Rotation::None) override {
    blits.push_back(Blit{rect, bitmap.data, paint.color});
  }

  // Where this exact face was painted, and in what colour. Returns a zero rect
  // unless every one of its layers landed on the *same* rect in the *same*
  // colour, which is the property that matters: the layers are separate
  // bitmaps of one drawing, so a face out of register is a mouth on a forehead.
  //
  // Asked this way rather than "was something drawn near here" because the
  // pointers come from player::avatarFor, so a pass means this name's face and
  // no other.
  fui::Rect faceRect(const player::Avatar& avatar, const fui::Color color) const {
    fui::Rect agreed{};
    bool first = true;
    for (int i = 0; i < player::Avatar::kLayerCount; ++i) {
      if (avatar.layer[i] == nullptr) continue;
      bool found = false;
      for (const auto& blit : blits) {
        if (blit.data != avatar.layer[i]->bits || blit.color != color) continue;
        if (first) {
          agreed = blit.rect;
          first = false;
          found = true;
          break;
        }
        if (blit.rect.x == agreed.x && blit.rect.y == agreed.y && blit.rect.width == agreed.width &&
            blit.rect.height == agreed.height) {
          found = true;
          break;
        }
      }
      if (!found) return fui::Rect{};
    }
    return agreed;
  }

  int layersOf(const player::Avatar& avatar) const {
    int count = 0;
    for (int i = 0; i < player::Avatar::kLayerCount; ++i) {
      if (avatar.layer[i] != nullptr) count++;
    }
    return count;
  }

  bool drew(const char* needle) const {
    for (const auto& run : texts) {
      // cppcheck-suppress useStlAlgorithm
      if (run.text == needle) return true;
    }
    return false;
  }

  bool outlined(const fui::Rect rect) const {
    for (const auto& s : strokes) {
      if (s.width == 0) continue;  // a zero-width stroke draws nothing
      if (s.rect.x == rect.x && s.rect.y == rect.y && s.rect.width == rect.width && s.rect.height == rect.height) {
        return true;
      }
    }
    return false;
  }

  bool triangleInside(const fui::Rect rect, const fui::Color color = fui::Color::Black) const {
    for (const auto& tri : triangles) {
      if (tri.color != color) continue;
      const fui::Point pts[3] = {tri.a, tri.b, tri.c};
      bool all = true;
      for (const auto& p : pts) {
        if (p.x < rect.x || p.x > rect.x + rect.width || p.y < rect.y || p.y > rect.y + rect.height) all = false;
      }
      if (all) return true;
    }
    return false;
  }

  const TextRun* find(const char* needle) const {
    for (const auto& run : texts) {
      if (run.text == needle) return &run;
    }
    return nullptr;
  }
};

// Where the ink actually lands, which is not where the rect is.
//
// GfxRendererTarget places a single-line run at
// `rect.y + max(0, (rect.height - lineHeight) / 2)` and draws from there with
// the y as the top of the ascender box. That rule is restated here rather than
// assumed, so a change to the target's arithmetic fails these checks instead of
// being silently agreed with.
int inkTopIn(const fui::Rect& given, const toybox::CutMetrics& cut) {
  const int offset = given.height - cut.lineHeight > 0 ? (given.height - cut.lineHeight) / 2 : 0;
  return given.y + offset + cut.ascender - cut.inkHeight;
}

// Sea Salt, and every other game on the default faces, binds the three slots to
// the Jersey cuts. Which cut a recorded run was set in is therefore knowable
// from its slot, and that is what turns a rect back into the ink inside it.
const toybox::CutMetrics& cutForSlot(const fui::FontId slot) {
  if (slot == toybox::kDisplayFont) return toybox::kDisplayCut;
  if (slot == toybox::kUiFont) return toybox::kUiCut;
  return toybox::kTileCut;
}

// The band a recorded run puts ink in. Wrapped runs keep their rect: the target
// lays those out by the block, and this rule is the single-line one.
fui::Rect inkBandOf(const FakeTarget::TextRun& run) {
  if (run.style.maxLines > 1) return run.rect;
  const toybox::CutMetrics& cut = cutForSlot(run.style.font);
  return fui::makeRect(run.rect.x, static_cast<int16_t>(inkTopIn(run.rect, cut)), run.rect.width, cut.inkHeight);
}

// The X4 Pro's logical frame.
fui::DeviceContext device() {
  fui::DeviceContext ctx;
  ctx.width = 480;
  ctx.height = 800;
  ctx.hasTouch = true;
  ctx.hasButtons = true;
  return ctx;
}

// A default Draft as an lvalue. GCC 14 ICEs gimplifying `d = toybattle::Draft{}`
// inside the answerability walk (gimple_add_tmp_var, gimplify.cc:774) while
// Apple clang compiles it happily -- so the ui suite was green here and red on
// CI for two pushes. There is no temporary to gimplify when the right-hand side
// has a name. See the ci-gcc-clang-gap note: a green local suite is not a green
// CI, and this is the second time that gap has been a compiler and not a test.
const toybattle::Draft kFreshDraft{};

// One rendered screen, with everything the assertions need to inspect.
struct Rendered {
  FakeTarget target;
  toybox::Interactions interactions;

  // Routes a tap at logical (x, y) against what was just drawn, which is the
  // whole point: the table under test is the one the paint produced.
  fui::ActionEvent tap(const int x, const int y) {
    fui::InputSnapshot input;
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(x);
    input.touchY = static_cast<int16_t>(y);
    return interactions.route(input);
  }
};

void buildSettings(Rendered& out, const chessui::SettingsModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  chessui::buildSettings(screen, model);
}

void buildBoard(Rendered& out, const chessui::BoardModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  chessui::buildBoardChrome(screen, model);
}

void buildChoice(Rendered& out, const triviaui::ChoiceModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  triviaui::buildChoice(screen, model);
}

void buildLink(Rendered& out, const linkui::LinkModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  linkui::buildLink(screen, model);
}

// --- the shared multiplayer screen -----------------------------------------

linkui::LinkModel searchingModel() {
  linkui::LinkModel model;
  model.gameTitle = "CHESS";
  model.headline = "LOOKING FOR A PLAYER";
  model.yourName = "MARIO";
  model.you = linkui::SeatState::Ready;
  model.them = linkui::SeatState::Looking;
  return model;
}

void testSearchingAsksNothing() {
  // The whole claim: tap MULTIPLAYER and the only thing on screen is that it is
  // looking. No device list, no host/join, no pairing code, no retry.
  Rendered out;
  buildLink(out, searchingModel());

  CHECK(out.target.drew("CHESS"));
  CHECK(out.target.drew("LOOKING FOR A PLAYER"));
  CHECK(out.target.drew("MARIO"));
  // The empty seat shows the shape of the absence rather than a blank row.
  CHECK(out.target.drew("- - - -"));
  CHECK(out.target.drew("LOOKING"));

  // One control, and it is the way out rather than a choice. PLAY AGAIN has no
  // business here: there is no game yet to play again.
  CHECK(out.target.drew("BACK"));
  CHECK(!out.target.drew("PLAY AGAIN"));
  const FakeTarget::TextRun* back = out.target.find("BACK");
  CHECK(back != nullptr);
  if (back != nullptr) {
    const fui::ActionEvent event = out.tap(back->rect.x + back->rect.width / 2, back->rect.y + back->rect.height / 2);
    CHECK(event.action == linkui::ActionLeaveLink);
  }
  // The seats are not buttons: a tap on one must do nothing, or a mis-drawn hit
  // region would hand the player a control that cannot work.
  const FakeTarget::TextRun* seat = out.target.find("MARIO");
  CHECK(seat != nullptr);
  if (seat != nullptr) {
    const fui::ActionEvent event = out.tap(seat->rect.x + seat->rect.width / 2, seat->rect.y + seat->rect.height / 2);
    CHECK(event.action == fui::NO_ACTION);
  }
}

void testSeatsSayWhatEachPlayerHasDecided() {
  // The rematch used to be a guess: you tapped PLAY AGAIN and stared at a
  // screen that could not tell you whether they had. Each seat now reads out.
  CHECK(strcmp(linkui::seatValue(linkui::SeatState::Looking, false), "LOOKING") == 0);
  // Once linked, an empty-handed seat is waiting on a person, not on a search.
  CHECK(strcmp(linkui::seatValue(linkui::SeatState::Looking, true), "WAITING") == 0);
  CHECK(strcmp(linkui::seatValue(linkui::SeatState::Deciding, true), "DECIDING") == 0);
  CHECK(strcmp(linkui::seatValue(linkui::SeatState::Ready, true), "READY") == 0);
  CHECK(strcmp(linkui::seatValue(linkui::SeatState::Left, true), "LEFT") == 0);
  CHECK(strcmp(linkui::seatValue(linkui::SeatState::Lost, true), "LOST") == 0);
}

void testTheRematchShowsBothAnswers() {
  Rendered out;
  linkui::LinkModel model;
  model.gameTitle = "CHESS";
  model.headline = "CHECKMATE";
  model.yourName = "MARIO";
  model.theirName = "LUIGI";
  model.you = linkui::SeatState::Ready;
  model.them = linkui::SeatState::Deciding;
  model.linked = true;
  model.offerPlayAgain = true;
  buildLink(out, model);

  CHECK(out.target.drew("CHECKMATE"));
  CHECK(out.target.drew("MARIO"));
  CHECK(out.target.drew("LUIGI"));
  // You have answered and they have not, and the screen says exactly that.
  CHECK(out.target.drew("READY"));
  CHECK(out.target.drew("DECIDING"));

  CHECK(out.target.drew("PLAY AGAIN"));
  const FakeTarget::TextRun* again = out.target.find("PLAY AGAIN");
  CHECK(again != nullptr);
  if (again != nullptr) {
    const fui::ActionEvent event =
        out.tap(again->rect.x + again->rect.width / 2, again->rect.y + again->rect.height / 2);
    CHECK(event.action == linkui::ActionPlayAgain);
  }
  // Stacked pills must not share a hit band.
  const FakeTarget::TextRun* back = out.target.find("BACK");
  CHECK(back != nullptr);
  if (back != nullptr) {
    const fui::ActionEvent event = out.tap(back->rect.x + back->rect.width / 2, back->rect.y + back->rect.height / 2);
    CHECK(event.action == linkui::ActionLeaveLink);
  }
}

// The bottom band is not a style choice, and this is the assertion that says so.
//
// y = 800 - kMargin - kPillHeight = 732 is where every link game's board puts
// the status capsule that becomes PLAY AGAIN at game over. This screen replaces
// that board in the same pass that ends the game, with no announcement and no
// settle, so whatever occupies 732 is what a thumb already on its way there
// hits. LEAVE used to be it: the rematch tap killed the radio instead.
//
// Asserted as "the destructive action is nowhere in the band" rather than as a
// literal rect, so a layout change that moves BACK back down fails here even if
// it moves it by a different arithmetic.
void testTheRematchBandIsNotTheWayOut() {
  Rendered out;
  linkui::LinkModel model;
  model.gameTitle = "CHESS";
  model.headline = "CHECKMATE";
  model.yourName = "YOU";
  model.theirName = "LUIGI";
  model.you = linkui::SeatState::Deciding;
  model.them = linkui::SeatState::Deciding;
  model.linked = true;
  model.offerPlayAgain = true;
  buildLink(out, model);

  // The band the boards hand over: the full pill, at the full content width.
  const int bandTop = 800 - toybox::kMargin - toybox::kPillHeight;
  const int bandBottom = 800 - toybox::kMargin;
  for (int y = bandTop; y < bandBottom; y += 4) {
    for (int x = toybox::kMargin; x < 480 - toybox::kMargin; x += 16) {
      const fui::ActionEvent event = out.tap(x, y);
      CHECK(event.action != linkui::ActionLeaveLink);
      CHECK(event.action == linkui::ActionPlayAgain);
    }
  }
  // Battleship's capsule is inset by the opponent face, so its own game-over
  // PLAY AGAIN starts at x=76. That exact pixel must not leave the match.
  CHECK(out.tap(76 + 4, bandTop + toybox::kPillHeight / 2).action == linkui::ActionPlayAgain);

  // And BACK is still reachable, one row up, where no board draws a control.
  const FakeTarget::TextRun* back = out.target.find("BACK");
  CHECK(back != nullptr);
  if (back != nullptr) {
    CHECK(back->rect.y < bandTop);
    const fui::ActionEvent event = out.tap(back->rect.x + back->rect.width / 2, back->rect.y + back->rect.height / 2);
    CHECK(event.action == linkui::ActionLeaveLink);
  }

  // The two pills must not share a pixel: a leave that overlaps the rematch by
  // one row is the same bug wearing a smaller number.
  const FakeTarget::TextRun* again = out.target.find("PLAY AGAIN");
  CHECK(again != nullptr);
  if (again != nullptr && back != nullptr) {
    CHECK(back->rect.y + back->rect.height <= again->rect.y);
  }
}

// Alone, BACK keeps the bottom band. Nothing is at risk there -- the only
// screens that reach this state are the search (which replaces a menu) and an
// opponent who has already gone -- and a single pill floating one row up over
// an empty margin reads as a layout that lost something.
void testTheLoneWayOutKeepsTheBottomBand() {
  Rendered out;
  buildLink(out, searchingModel());
  const int bandMid = 800 - toybox::kMargin - toybox::kPillHeight / 2;
  CHECK(out.tap(240, bandMid).action == linkui::ActionLeaveLink);
}

void testAnOpponentWhoHasGoneTakesTheButtonWithThem() {
  // A button that cannot work is worse than one that is not there.
  Rendered out;
  linkui::LinkModel model;
  model.gameTitle = "CHESS";
  model.headline = "LUIGI LEFT";
  model.yourName = "MARIO";
  model.theirName = "LUIGI";
  model.you = linkui::SeatState::Deciding;
  model.them = linkui::SeatState::Left;
  model.linked = true;
  model.offerPlayAgain = false;
  buildLink(out, model);

  CHECK(out.target.drew("LUIGI LEFT"));
  CHECK(out.target.drew("LEFT"));
  CHECK(!out.target.drew("PLAY AGAIN"));
  CHECK(out.target.drew("BACK"));
}

// --- the settings menu's row model ----------------------------------------

void testRowModel() {
  chessui::SettingsModel vsComputer;
  vsComputer.opponent = chessui::Opponent::Computer;
  CHECK(chessui::visibleRows(vsComputer) == 6);
  CHECK(chessui::rowAt(vsComputer, 0) == chessui::MenuRow::NewGame);
  CHECK(chessui::rowAt(vsComputer, 3) == chessui::MenuRow::Level);
  CHECK(chessui::rowAt(vsComputer, 4) == chessui::MenuRow::PlayAs);
  CHECK(chessui::rowAt(vsComputer, 5) == chessui::MenuRow::Hints);

  // Two people sharing the device: Level and Play As mean nothing, so they are
  // not shown and the remaining rows close the gap rather than leaving holes.
  chessui::SettingsModel vsFriend;
  vsFriend.opponent = chessui::Opponent::PassAndPlay;
  CHECK(chessui::visibleRows(vsFriend) == 4);
  CHECK(chessui::rowAt(vsFriend, 0) == chessui::MenuRow::NewGame);
  CHECK(chessui::rowAt(vsFriend, 1) == chessui::MenuRow::TakeBack);
  CHECK(chessui::rowAt(vsFriend, 2) == chessui::MenuRow::Opponent);
  CHECK(chessui::rowAt(vsFriend, 3) == chessui::MenuRow::Hints);

  // Out-of-range indices clamp instead of reading past the table. Reachable
  // whenever the row set shrinks under a selection that was already lower.
  CHECK(chessui::rowAt(vsFriend, 99) == chessui::MenuRow::Hints);
  CHECK(chessui::rowAt(vsFriend, -1) == chessui::MenuRow::NewGame);
  CHECK(chessui::rowAt(vsComputer, 99) == chessui::MenuRow::Hints);

  chessui::SettingsModel model;
  model.level = 0;
  CHECK(std::string(chessui::rowValue(model, chessui::MenuRow::Level)) == "EASY");
  model.level = 2;
  CHECK(std::string(chessui::rowValue(model, chessui::MenuRow::Level)) == "HARD");
  model.humanPlaysWhite = false;
  CHECK(std::string(chessui::rowValue(model, chessui::MenuRow::PlayAs)) == "BLACK");
  model.opponent = chessui::Opponent::PassAndPlay;
  CHECK(std::string(chessui::rowValue(model, chessui::MenuRow::Opponent)) == "P&P");
  model.canTakeBack = false;
  CHECK(std::string(chessui::rowValue(model, chessui::MenuRow::TakeBack)) == "NONE");
  model.canTakeBack = true;
  CHECK(std::string(chessui::rowValue(model, chessui::MenuRow::TakeBack)) == "");
}

// --- the settings screen ---------------------------------------------------

void testSettingsOpenedFromTheMenuOffersOnlyPreferences() {
  // Settings has two doors and they are not the same screen. NEW GAME and TAKE
  // BACK act on a board you are looking at; reached from the start menu there
  // is no such board, so offering them is a row that drops you somewhere you
  // were never going. The close button has to say where it lands, too.
  chessui::SettingsModel fromMenu;
  fromMenu.fromMenu = true;
  fromMenu.opponent = chessui::Opponent::Computer;
  CHECK(chessui::visibleRows(fromMenu) == 4);
  CHECK(chessui::rowAt(fromMenu, 0) == chessui::MenuRow::Opponent);
  CHECK(chessui::rowAt(fromMenu, 3) == chessui::MenuRow::Hints);
  // Neither game action is reachable at any index.
  for (int i = 0; i < chessui::visibleRows(fromMenu); ++i) {
    const chessui::MenuRow row = chessui::rowAt(fromMenu, i);
    CHECK(row != chessui::MenuRow::NewGame && row != chessui::MenuRow::TakeBack);
  }

  fromMenu.opponent = chessui::Opponent::PassAndPlay;
  CHECK(chessui::visibleRows(fromMenu) == 2);
  CHECK(chessui::rowAt(fromMenu, 0) == chessui::MenuRow::Opponent);
  CHECK(chessui::rowAt(fromMenu, 1) == chessui::MenuRow::Hints);

  Rendered out;
  fromMenu.opponent = chessui::Opponent::Computer;
  buildSettings(out, fromMenu);
  CHECK(!out.target.drew("NEW GAME"));
  CHECK(!out.target.drew("TAKE BACK"));
  CHECK(out.target.drew("BACK TO MENU"));
  CHECK(!out.target.drew("BACK TO BOARD"));

  // From the board it is the screen it always was.
  Rendered board;
  chessui::SettingsModel fromBoard;
  fromBoard.opponent = chessui::Opponent::Computer;
  buildSettings(board, fromBoard);
  CHECK(board.target.drew("NEW GAME"));
  CHECK(board.target.drew("BACK TO BOARD"));
  CHECK(!board.target.drew("BACK TO MENU"));

  // And a pending restart outranks both, because that is what leaving will do.
  Rendered pending;
  fromMenu.restartPending = true;
  buildSettings(pending, fromMenu);
  CHECK(pending.target.drew("START NEW GAME"));
  CHECK(!pending.target.drew("BACK TO MENU"));
}

void testSettingsScreen() {
  chessui::SettingsModel model;
  model.opponent = chessui::Opponent::Computer;
  Rendered vsComputer;
  buildSettings(vsComputer, model);

  CHECK(vsComputer.target.drew("SETTINGS"));
  CHECK(vsComputer.target.drew("NEW GAME"));
  CHECK(vsComputer.target.drew("LEVEL"));
  CHECK(vsComputer.target.drew("PLAY AS"));
  CHECK(!vsComputer.interactions.overflowed());

  // The header title is knocked out of a solid black band. Drawn in the default
  // black it is invisible, which is exactly how it shipped once.
  const FakeTarget::TextRun* title = vsComputer.target.find("SETTINGS");
  CHECK(title != nullptr && title->color == fui::Color::White);

  // Header text lines up with the rows beneath it. Left to inherit, the raw
  // component falls back to 6px and the title sits 10px adrift.
  const FakeTarget::TextRun* row = vsComputer.target.find("NEW GAME");
  CHECK(title != nullptr && row != nullptr && title->rect.x == toybox::kMargin);

  // Rows span the page margins exactly once. The content rect already carries
  // the margin, so a non-zero listInset on top of it indents them twice, which
  // is a look regression no assertion about text or actions would notice.
  CHECK(row != nullptr && row->rect.x == toybox::kMargin + toybox::kGutter);

  // Friend mode hides the two engine-only rows.
  model.opponent = chessui::Opponent::PassAndPlay;
  Rendered vsFriend;
  buildSettings(vsFriend, model);
  CHECK(vsFriend.target.drew("OPPONENT"));
  CHECK(!vsFriend.target.drew("LEVEL"));
  CHECK(!vsFriend.target.drew("PLAY AS"));

  // The close button's label is the confirmation that a pending change will
  // cost you the current game.
  CHECK(vsFriend.target.drew("BACK TO BOARD"));
  model.restartPending = true;
  Rendered pending;
  buildSettings(pending, model);
  CHECK(pending.target.drew("START NEW GAME"));
  CHECK(!pending.target.drew("BACK TO BOARD"));
}

void testSettingsRouting() {
  chessui::SettingsModel model;
  model.opponent = chessui::Opponent::Computer;
  Rendered screen;
  buildSettings(screen, model);

  // A tap on the first row activates the first row. The interesting part is
  // that the rect being tested is the one the paint produced, so this fails if
  // the layout moves and the hit-testing does not.
  const int firstRowY = toybox::kHeaderHeight + toybox::kGutter * 3 + toybox::kRowHeight / 2;
  const fui::ActionEvent first = screen.tap(240, firstRowY);
  CHECK(first.action == chessui::ActionMenuRow);
  CHECK(first.value == 0);

  const fui::ActionEvent third = screen.tap(240, firstRowY + 2 * (toybox::kRowHeight + 4));
  CHECK(third.action == chessui::ActionMenuRow);
  CHECK(third.value == 2);

  // The close button spans the full content width, so its far edges respond.
  const int closeY = 800 - toybox::kMargin - toybox::kPillHeight / 2;
  CHECK(screen.tap(240, closeY).action == chessui::ActionCloseSettings);
  CHECK(screen.tap(toybox::kMargin + 4, closeY).action == chessui::ActionCloseSettings);
  CHECK(screen.tap(480 - toybox::kMargin - 4, closeY).action == chessui::ActionCloseSettings);
}

// --- the board's chrome ----------------------------------------------------

void testBoardChrome() {
  // Mid-game the status capsule is a label, not a button. It said "YOUR MOVE"
  // and a tap on it must do nothing at all.
  chessui::BoardModel playing;
  playing.status = "YOUR MOVE";
  playing.gameOver = false;
  Rendered mid;
  const fui::Rect body = [&] {
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(mid.target, ctx, noInput, mid.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    return chessui::buildBoardChrome(screen, playing);
  }();

  CHECK(mid.target.drew("CHESS"));
  CHECK(mid.target.drew("YOUR MOVE"));
  const int statusY = 800 - toybox::kMargin - toybox::kPillHeight / 2;
  CHECK(mid.tap(240, statusY).action == fui::NO_ACTION);

  // The body left for the board sits under the header and above the capsule.
  CHECK(body.y >= toybox::kHeaderHeight);
  CHECK(body.bottom() <= 800 - toybox::kMargin - toybox::kPillHeight);
  CHECK(body.width == 480 - 2 * toybox::kMargin);

  // Game over turns the same capsule into a button, and the whole width of it
  // responds. It did not: the old code painted the capsule full width while
  // hit-testing a narrower centred rect, so its outer thirds were dead.
  chessui::BoardModel finished;
  finished.status = "PLAY AGAIN";
  finished.gameOver = true;
  Rendered over;
  buildBoard(over, finished);
  CHECK(over.target.drew("PLAY AGAIN"));
  CHECK(over.tap(240, statusY).action == chessui::ActionPlayAgain);
  CHECK(over.tap(toybox::kMargin + 4, statusY).action == chessui::ActionPlayAgain);
  CHECK(over.tap(480 - toybox::kMargin - 4, statusY).action == chessui::ActionPlayAgain);

  // A tap on the board area is the board's business, not the chrome's.
  CHECK(over.tap(240, 400).action == fui::NO_ACTION);
}

// --- connections: a finished board ------------------------------------------

connections::Puzzle connectionsPuzzle() {
  connections::Puzzle p;
  p.id = 1;
  p.date = 20230612;
  const char* names[4] = {"WET WEATHER", "NBA TEAMS", "KEYBOARD KEYS", "PALINDROMES"};
  const char* words[4][4] = {{"HAIL", "RAIN", "SLEET", "SNOW"},
                             {"BUCKS", "HEAT", "JAZZ", "NETS"},
                             {"OPTION", "RETURN", "SHIFT", "TAB"},
                             {"KAYAK", "LEVEL", "MOM", "RACECAR"}};
  for (int g = 0; g < 4; ++g) {
    std::snprintf(p.groups[g].name, sizeof(p.groups[g].name), "%s", names[g]);
    for (int m = 0; m < 4; ++m) {
      std::snprintf(p.groups[g].members[m], sizeof(p.groups[g].members[m]), "%s", words[g][m]);
    }
  }
  return p;
}

void renderConnectionsBoard(Rendered& out, const connections::Game& game) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  connectionsui::BoardModel model;
  model.game = &game;
  model.date = game.puzzle().date;
  const connectionsui::BoardLayout layout = connectionsui::buildBoardChrome(screen, model);
  connectionsui::buildBoardTiles(screen, model, layout);
}

void testConnectionsLostBoard() {
  // Losing reveals all four groups as rows. The tiles that were never guessed
  // are still on the board as far as the core is concerned, so drawing both put
  // the answers in the four slots and the leftover words underneath them.
  connections::Game game;
  game.start(connectionsPuzzle(), 5);
  connections::Game::Save lost;
  lost.seed = 5;
  lost.mistakes = connections::kMaxMistakes;
  CHECK(game.restore(lost));
  CHECK(game.result() == connections::Result::Lost);
  CHECK(game.revealedCount() == 4);
  // The core still holds them; it is the screen's job not to draw them.
  CHECK(game.tileCount() == 16);

  Rendered screen;
  renderConnectionsBoard(screen, game);
  CHECK(screen.target.drew("WET WEATHER"));
  CHECK(screen.target.drew("PALINDROMES"));
  // No loose tile words anywhere on a finished board.
  CHECK(!screen.target.drew("HAIL"));
  CHECK(!screen.target.drew("RACECAR"));
  // And no tile is tappable once the game is over.
  CHECK(screen.tap(67, 434).action == fui::NO_ACTION);
}

void testConnectionsWonBoard() {
  connections::Game game;
  game.start(connectionsPuzzle(), 5);
  for (int g = 0; g < 4; ++g) {
    game.deselectAll();
    for (int i = 0; i < game.tileCount(); ++i) {
      if (game.tileGroup(i) == g) game.toggleTile(i);
    }
    game.submit();
  }
  CHECK(game.result() == connections::Result::Won);
  CHECK(game.tileCount() == 0);

  Rendered screen;
  renderConnectionsBoard(screen, game);
  CHECK(screen.target.drew("WET WEATHER"));
  CHECK(!screen.target.drew("HAIL"));
}

// Sixteen tiles, one size.
//
// A word too long for its tile used to be set a quarter smaller than the
// fifteen beside it, each tile sized against its own word. On a board whose
// premise is sixteen interchangeable candidates a size difference reads as
// significance that is not there, and roughly three boards in five of the
// published archive contain such a word.
//
// This target answers a flat ten pixels a character for every cut, so it cannot
// say WHICH cut a board should pick -- only the real face at draw time can, and
// that is what the simulator shots are for. What it can say is the invariant
// that regressed: whatever cut the board picks, every tile is set in it.
void testConnectionsTilesShareOneSize() {
  connections::Puzzle puzzle = connectionsPuzzle();
  // Eleven characters is 110 against the 105px a tile gives its word, so this
  // one cannot fit and the other fifteen can. Under per-tile sizing that was
  // enough to set it in a different cut.
  std::snprintf(puzzle.groups[0].members[0], sizeof(puzzle.groups[0].members[0]), "%s", "DECORATIONS");
  connections::Game game;
  game.start(puzzle, 5);
  CHECK(game.tileCount() == 16);

  Rendered screen;
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(screen.target, ctx, noInput, screen.interactions);
  toybox::Screen built(frame, toybox::themeTokens());
  connectionsui::BoardModel model;
  model.game = &game;
  model.date = game.puzzle().date;
  const connectionsui::BoardLayout layout = connectionsui::buildBoardChrome(built, model);
  // Everything after this index is the tile pass, which is the only text the
  // board sizes for itself; the chrome above speaks the fork's voice.
  const size_t chromeRuns = screen.target.texts.size();
  connectionsui::buildBoardTiles(built, model, layout);
  CHECK(screen.target.texts.size() > chromeRuns);

  const fui::FontId cut = screen.target.texts[chromeRuns].style.font;
  bool oneSize = true;
  for (size_t i = chromeRuns; i < screen.target.texts.size(); ++i) {
    if (screen.target.texts[i].style.font != cut) oneSize = false;
  }
  CHECK(oneSize);
  // And the fifteen that fit are still whole words, not casualties of the
  // sixteenth: shrinking the board must not start breaking them across lines.
  CHECK(screen.target.drew("RACECAR"));
  CHECK(screen.target.drew("OPTION"));
  CHECK(screen.target.drew("SLEET"));
}

// Every date in the fullest possible month has to be reachable.
//
// This is the test that was missing. The calendar took an interaction slot per
// playable date -- 31 of them, plus TODAY and four steppers, against a 24-slot
// buffer -- so the buffer filled partway through the month and every date from
// the 20th on was dead. It shipped in v1.2.1 and a tester found it. The board
// was covered here from the start; the calendar never was.
//
// A 31-day month starting on a Saturday is the worst case: six week rows and
// the most days that can be live at once.
void testConnectionsCalendarEveryDayIsReachable() {
  connectionsui::CalendarDay cells[42] = {};
  constexpr int kLead = 6;   // 1st falls on a Saturday
  constexpr int kDays = 31;  // the longest month
  for (int d = 1; d <= kDays; ++d) {
    cells[kLead + d - 1].day = static_cast<uint8_t>(d);
    cells[kLead + d - 1].inArchive = true;
  }
  connectionsui::CalendarModel model;
  model.cells = cells;
  model.todayCell = kLead + kDays - 1;

  Rendered out;
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  const connectionsui::CalendarLayout layout = connectionsui::buildCalendar(screen, model);

  // The whole month in one region, so a fuller month cannot push anything out.
  CHECK(!out.interactions.overflowed());
  CHECK(out.interactions.count() <= toybox::kMaxInteractions);
  CHECK(layout.valid);

  // Tap the centre of every live date and confirm it routes to that date and no
  // other. Routing, not just registration: the failure this replaces was a cell
  // that existed on screen and had no entry in the table behind it.
  const int step = layout.cell + layout.gap;
  for (int i = 0; i < 42; ++i) {
    if (cells[i].day == 0) continue;
    const int x = layout.originX + (i % layout.cols) * step + layout.cell / 2;
    const int y = layout.originY + (i / layout.cols) * step + layout.cell / 2;
    const fui::ActionEvent hit = out.tap(x, y);
    CHECK(hit.action == connectionsui::ActionCalendarDay);
    int cell = -1;
    CHECK(connectionsui::dayCellAt(layout, x, y, cell));
    CHECK(cell == i);
  }

  // Outside the block is not a date. The steppers and TODAY sit above and below
  // it, and swallowing their taps would trade one dead control for three.
  int spill = -1;
  CHECK(!connectionsui::dayCellAt(layout, layout.originX - 4, layout.originY + 4, spill));
  CHECK(!connectionsui::dayCellAt(layout, layout.originX + 4, layout.originY - 4, spill));
  CHECK(!connectionsui::dayCellAt(layout, layout.originX + layout.cols * step + 4, layout.originY + 4, spill));

  // TODAY and both steppers still answer, which is the whole risk of collapsing
  // the month into one region that sits between them.
  bool sawToday = false;
  bool sawYear = false;
  bool sawMonth = false;
  for (size_t s = 0; s < out.interactions.count(); ++s) {
    const fui::ActionId action = out.interactions.data()[s].action;
    if (action == connectionsui::ActionCalendarToday) sawToday = true;
    if (action == connectionsui::ActionCalendarYear) sawYear = true;
    if (action == connectionsui::ActionCalendarMonth) sawMonth = true;
  }
  CHECK(sawToday);
  CHECK(sawYear);
  CHECK(sawMonth);
}

// The ornament is a control, and the how-to exists.
//
// The bracketed 4x4 in the middle of the menu is the largest object on the
// screen and wears the chess board's corner brackets; it drew for a year and
// answered nothing, and a tester tapped it and reported the app as broken.
// It routes to the archive now, which is what a picture of your last sixteen
// days should open.
void testConnectionsMenuOrnamentOpensArchive() {
  connectionsui::MenuModel model;
  model.hasPuzzles = true;
  model.newestDate = 20260812;
  model.puzzleCount = 1150;
  model.played = 12;
  model.perfect = 4;
  model.streak = 3;

  Rendered out;
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  connectionsui::buildMenu(screen, model);

  CHECK(!out.interactions.overflowed());
  CHECK(out.target.drew("HOW TO PLAY"));
  CHECK(out.target.drew("LAST 16 DAYS"));

  // Dead centre of the block. Its geometry is body-relative, so this asserts
  // through the caption rather than against a hardcoded rect: whatever the
  // block's own bounds are, the middle of the screen between the stats rule and
  // the doors belongs to it.
  const FakeTarget::TextRun* caption = out.target.find("LAST 16 DAYS");
  CHECK(caption != nullptr);
  if (caption != nullptr) {
    const fui::ActionEvent hit = out.tap(ctx.width / 2, caption->rect.y - 60);
    CHECK(hit.action == connectionsui::ActionNewest);
    CHECK(hit.value == 1);  // 1 = archive, the same value the ARCHIVE row sends
  }

  // All three doors still answer. The third row was bought by shrinking the
  // ornament, so this is the assertion that catches it being bought by pushing
  // a door off the bottom instead.
  int archive = 0;
  int puzzles = 0;
  int howTo = 0;
  for (size_t i = 0; i < out.interactions.count(); ++i) {
    const fui::Interaction& it = out.interactions.data()[i];
    if (it.action != connectionsui::ActionNewest) continue;
    if (it.value == 1) ++archive;
    if (it.value == 2) ++puzzles;
    if (it.value == 3) ++howTo;
  }
  CHECK(archive == 2);  // the ARCHIVE row and the ornament
  CHECK(puzzles == 1);
  CHECK(howTo == 1);
}

void testConnectionsHowToFitsOnePage() {
  Rendered out;
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  connectionsui::buildHowTo(screen);

  CHECK(!out.interactions.overflowed());
  CHECK(out.target.drew("SIXTEEN WORDS, FOUR GROUPS"));
  // The board is the page. All sixteen words, and the taken group's name.
  CHECK(out.target.drew("WET WEATHER"));
  CHECK(out.target.drew("HAIL"));
  CHECK(out.target.drew("CERES"));
  // The two facts the picture cannot say, which the chosen variant was missing
  // until they were added: what the pips mean and what SHUFFLE does.
  CHECK(out.target.drew("wrong guesses left"));
  CHECK(out.target.drew("SHUFFLE only moves the tiles. It never changes the answer."));

  // One page means one page: nothing may be drawn below the fold. A how-to that
  // runs off the bottom is worse than none, because the part that falls off is
  // the part the reader has not read yet and there is no scrollbar to say so.
  for (const auto& run : out.target.texts) {
    CHECK(run.rect.y + run.rect.height <= ctx.height);
  }

  // Tap anywhere leaves.
  const fui::ActionEvent hit = out.tap(ctx.width / 2, ctx.height / 2);
  CHECK(hit.action == connectionsui::ActionHowTo);
}

// --- battleship -------------------------------------------------------------

void buildBattleshipStart(Rendered& out, const bshipui::StartModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  bshipui::buildStartMenu(screen, model);
}

void buildBattleshipBoard(Rendered& out, const bshipui::BoardModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  bshipui::buildBoardChrome(screen, model);
}

void buildBattleshipPlace(Rendered& out, const bshipui::PlaceModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  bshipui::buildPlaceChrome(screen, model);
}

// The paint clock is one global counter, and every other test in this file
// runs with it at zero -- which is exactly the "nothing has been shown yet"
// state that leaves the gate open. A test that advances it therefore has to
// put it back, or it silently gates the ~600 tests that come after it.
struct PaintClockGuard {
  uint32_t saved = paintclock::counter();
  ~PaintClockGuard() { paintclock::counter() = saved; }
};

// The one this whole mechanism exists for.
//
// BattleshipScreens.cpp:160 registers the bottom capsule as
//     gameOver ? ActionPlayAgain : (canFire ? ActionFire : NO_ACTION)
// so one rect means FIRE for the whole game and becomes PLAY AGAIN the instant
// the last shot lands. FIRE is tapped dozens of times a game; the player's
// thumb lives on that pixel. The rebuild happens BEFORE displayBuffer(), which
// blocks 0.3-2s, so without a gate the capsule is already PLAY AGAIN while the
// panel still reads FIRE.
//
// This drives the real builder through that exact sequence, and it is written
// so that "the capsule is live over a frame that still says FIRE" cannot pass.
void testACapsuleThatChangedMeaningWaitsForThePanel() {
  PaintClockGuard clock;
  Rendered out;

  bshipui::BoardModel firing;
  firing.status = "FIRE";
  firing.canFire = true;
  firing.theirName = "LUIGI";

  // Mid-game: the board is built and the panel has shown it.
  buildBattleshipBoard(out, firing);
  paintclock::notePainted();

  // The capsule, in the bottom band. x=300 is inside it whether or not the
  // opponent's face has taken the left strip.
  const int capsuleY = 800 - toybox::kMargin - toybox::kPillHeight / 2;
  CHECK(out.tap(300, capsuleY).action == bshipui::ActionFire);

  // The last shot lands. The activity rebuilds and has NOT painted yet: this
  // is the window, and the panel is still showing FIRE.
  bshipui::BoardModel over = firing;
  over.canFire = false;
  over.gameOver = true;
  over.status = "PLAY AGAIN";
  buildBattleshipBoard(out, over);

  // The rect now says PLAY AGAIN in the table. A thumb already travelling to
  // FIRE must get nothing at all -- not FIRE (the game is over) and above all
  // not PLAY AGAIN (a rematch nobody asked for).
  const fui::ActionEvent duringPaint = out.tap(300, capsuleY);
  CHECK(duringPaint.action != bshipui::ActionPlayAgain);
  CHECK(duringPaint.action != bshipui::ActionFire);
  CHECK(duringPaint.action == fui::NO_ACTION);
  CHECK(!out.interactions.routable());

  // The panel catches up. From here the control is real and answers.
  paintclock::notePainted();
  CHECK(out.interactions.routable());
  CHECK(out.tap(300, capsuleY).action == bshipui::ActionPlayAgain);
}

// The other half, and the one that decides whether this fix is worth having:
// input must NOT go dead. MappedInputManager::rowTouch() reports Down after
// 90ms of contact, apps repaint to highlight the row, and then act on the
// RELEASE of that same contact. That repaint rebuilds an identical table while
// the finger is still down, so if an ordinary repaint gated the release, every
// list in the fork would highlight and then do nothing.
void testARepaintThatChangedNothingStillAnswers() {
  PaintClockGuard clock;
  Rendered out;

  bshipui::BoardModel firing;
  firing.status = "FIRE";
  firing.canFire = true;

  buildBattleshipBoard(out, firing);
  paintclock::notePainted();
  const int capsuleY = 800 - toybox::kMargin - toybox::kPillHeight / 2;
  CHECK(out.tap(300, capsuleY).action == bshipui::ActionFire);

  // Rebuilt with the same model, mid-contact, with no paint since. Same table,
  // same meaning: the release still has to land.
  buildBattleshipBoard(out, firing);
  CHECK(out.interactions.routable());
  CHECK(out.tap(300, capsuleY).action == bshipui::ActionFire);

  // And repeatedly, because a highlight can repaint several times before the
  // finger lifts. Nothing here may accumulate into a closed gate.
  for (int repaint = 0; repaint < 5; ++repaint) {
    buildBattleshipBoard(out, firing);
    CHECK(out.tap(300, capsuleY).action == bshipui::ActionFire);
  }
}

// A changed screen that is rebuilt again before it is ever painted must stay
// gated. The panel is still showing the table from two builds ago, so adopting
// the intermediate one as "shown" would reopen the gate on a frame nobody saw.
void testAnUnshownRebuildDoesNotCountAsShown() {
  PaintClockGuard clock;
  Rendered out;

  bshipui::BoardModel firing;
  firing.status = "FIRE";
  firing.canFire = true;
  buildBattleshipBoard(out, firing);
  paintclock::notePainted();
  const int capsuleY = 800 - toybox::kMargin - toybox::kPillHeight / 2;
  CHECK(out.tap(300, capsuleY).action == bshipui::ActionFire);

  bshipui::BoardModel over = firing;
  over.canFire = false;
  over.gameOver = true;
  buildBattleshipBoard(out, over);
  CHECK(!out.interactions.routable());

  // Rebuilt again, still unpainted. FIRE is what the panel shows and PLAY
  // AGAIN is what the table says; the gate stays shut.
  buildBattleshipBoard(out, over);
  CHECK(!out.interactions.routable());
  CHECK(out.tap(300, capsuleY).action == fui::NO_ACTION);

  // One paint is all it takes to open, and it opens fully.
  paintclock::notePainted();
  CHECK(out.tap(300, capsuleY).action == bshipui::ActionPlayAgain);
}

// Chess and Sea Salt share the shape through a different door: the capsule is
// NO_ACTION mid-game, so the table has no entry there at all, and at game over
// one appears. Making the action safe does not survive this window -- it is
// precisely where the two tables disagree.
void buildWin(Rendered& out, const dungeonui::WinModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  dungeonui::buildWin(screen, model);
}

// StateDisabled is not a cosmetic bit, and the digest has to know that.
//
// InteractionBuffer::findTouch skips disabled entries, so flipping
// StateDisabled changes what a tap DOES. DungeonScreens.cpp:832 registers
// NEXT with an identical rect, action, value and inputMask and flips only
// that bit on model.moreToPlay. A dead control becoming live under a
// stationary finger is the same defect as FIRE becoming PLAY AGAIN, and it
// would slip a digest that treated state as decoration.
void testAControlComingBackToLifeAlsoWaits() {
  PaintClockGuard clock;
  Rendered out;

  dungeonui::WinModel done;
  done.dungeonName = "THE CELLAR";
  done.solvedCount = 8;
  done.total = 8;
  done.moreToPlay = false;  // NEXT is registered, and disabled.

  buildWin(out, done);
  paintclock::notePainted();

  // Find NEXT by its label so this does not hard-code the footer arithmetic.
  const FakeTarget::TextRun* next = out.target.find("NEXT");
  CHECK(next != nullptr);
  if (next == nullptr) return;
  const int nextX = next->rect.x + next->rect.width / 2;
  const int nextY = next->rect.y + next->rect.height / 2;

  // Disabled: the tap finds nothing, which is the point of the state.
  CHECK(out.tap(nextX, nextY).action == fui::NO_ACTION);

  // More dungeons arrive and the same pixel comes alive, with every other
  // field of the interaction identical. Not yet painted, so not yet live.
  dungeonui::WinModel more = done;
  more.moreToPlay = true;
  buildWin(out, more);
  CHECK(!out.interactions.routable());
  CHECK(out.tap(nextX, nextY).action == fui::NO_ACTION);

  // Shown: now it answers.
  paintclock::notePainted();
  CHECK(out.tap(nextX, nextY).action == dungeonui::ActionButton);
}

// paintclock::RevealGate is the same decision UiAppHost makes, lifted out so
// it can be tested: UiAppHost needs a GfxRenderer and an Arduino and cannot be
// built here, and a restatement of its logic in a test would only ever agree
// with itself. This exercises the object the firmware actually uses.
void testTheRevealGateWaitsForOnePaintAndThenLatches() {
  PaintClockGuard clock;
  paintclock::RevealGate gate;

  // Unarmed: never in the way.
  CHECK(gate.revealed());

  // A screen entry. Built, but the panel still shows the previous screen.
  gate.arm();
  gate.markBuilt();
  CHECK(!gate.revealed());

  // A render that rebuilds several times before its single paint (which is
  // what UiListActivity does, up to 8 passes) must measure from the LAST
  // build, or the gate opens on a paint that predates the table.
  paintclock::notePainted();
  gate.markBuilt();
  CHECK(!gate.revealed());

  // One paint from any source releases it.
  paintclock::notePainted();
  CHECK(gate.revealed());

  // And it latches: a later build with no arm() must not re-close it, or an
  // ordinary repaint would start eating input.
  gate.markBuilt();
  CHECK(gate.revealed());
  CHECK(gate.revealed());

  // Only a fresh screen entry closes it again.
  gate.arm();
  gate.markBuilt();
  CHECK(!gate.revealed());
  paintclock::notePainted();
  CHECK(gate.revealed());
}

// The eight games that hit-test a board against GEOMETRY never reach route(),
// so no table digest can see their taps -- and what such a tap MEANS is not in
// the table either. MinesweeperScreens.cpp registers the FLAG capsule with an
// identical rect, action, value and inputMask and flips only StateSelected,
// which the digest ignores as paint, while that same mode bit decides whether
// a grid tap digs or flags. paintclock::SurfaceGate is the decision those apps
// make instead; this drives the object the firmware uses, not a restatement.
void testTheSurfaceGateHoldsAChangedMeaningAndPassesAnUnchangedOne() {
  PaintClockGuard clock;
  paintclock::SurfaceGate gate;

  // Before the first paint of all there is no shown frame to disagree with,
  // so nothing is gated -- the boot splash window, and every other test here.
  CHECK(gate.routable(0));
  CHECK(gate.routable(12345));

  // Minesweeper, DIG mode, on the panel.
  const uint32_t dig = 0;
  const uint32_t flag = 1;
  gate.noteBuilt(dig);
  paintclock::notePainted();
  CHECK(gate.routable(dig));

  // The FLAG capsule is tapped: flagMode flips and the board is rebuilt. The
  // panel still reads DIG for the length of the refresh, so a grid tap in this
  // window must NOT flag.
  gate.noteBuilt(flag);
  CHECK(!gate.routable(flag));

  // The refresh completes. The panel now reads FLAG and the board is live.
  paintclock::notePainted();
  CHECK(gate.routable(flag));

  // THE safety property, and the reason this is a digest rather than a
  // suppression: a repaint that changed nothing still answers. Minesweeper
  // holds a finger on a cell, requestUpdate() repaints the outline, and the
  // LIFT of that same contact is what digs. Gating it would eat the move and
  // read as a frozen device.
  gate.noteBuilt(flag);
  CHECK(gate.routable(flag));
  gate.noteBuilt(flag);
  CHECK(gate.routable(flag));

  // Back to DIG on the panel, so the next block measures from a known frame.
  paintclock::notePainted();
  gate.noteBuilt(dig);
  CHECK(!gate.routable(dig));
  paintclock::notePainted();
  CHECK(gate.routable(dig));

  // A render that rebuilds several times before its single paint must measure
  // from the frame the panel last SHOWED, not from an intermediate build the
  // user never saw. Two builds, no paint between: the gate stays shut against
  // the meaning that ends up built...
  const uint32_t pencil = 2;
  gate.noteBuilt(flag);
  gate.noteBuilt(pencil);
  CHECK(!gate.routable(pencil));
  // ...and open against the one still on the glass, which is DIG and not the
  // intermediate FLAG build. Taking the intermediate as "shown" is the bug
  // this check exists to catch.
  CHECK(gate.routable(dig));
  CHECK(!gate.routable(flag));

  paintclock::notePainted();
  CHECK(gate.routable(pencil));
}

// Several small values fold into one meaning, and they must not collide when
// they swap places: "selected e2, white to move" is not "selected d4, black to
// move".
void testMeaningsMixPositionally() {
  const uint32_t a = paintclock::mixMeaning(paintclock::mixMeaning(paintclock::kMeaningSeed, 4), 7);
  const uint32_t b = paintclock::mixMeaning(paintclock::mixMeaning(paintclock::kMeaningSeed, 7), 4);
  CHECK(a != b);
  const uint32_t again = paintclock::mixMeaning(paintclock::mixMeaning(paintclock::kMeaningSeed, 4), 7);
  CHECK(a == again);
}

// OptionPopup and KeyboardEntryActivity hold their own buffers at their own
// capacities (17 and 48) and opt into the SDK's double-buffered publish cycle,
// which the 24-slot toybox screens do not. beginBuild() therefore has to
// digest the PUBLISHED generation: by the time a publishing caller builds,
// building_ has already flipped and data() is a rebuild from two generations
// ago, which would be compared against as though the panel had shown it.
void testAPublishingBufferDigestsWhatThePanelIsShowing() {
  PaintClockGuard clock;
  paintclock::RevealedInteractions<17> iact;
  freeink::ui::InteractionBuffer<17>& raw = iact;

  const auto slot = [](const freeink::ui::ActionId action, const int16_t value) {
    freeink::ui::Interaction hit{};
    hit.rect = fui::Rect{0, 0, 100, 40};
    hit.action = action;
    hit.value = value;
    hit.inputMask = fui::InputTouch;
    return hit;
  };
  const auto tap = [&iact]() {
    fui::InputSnapshot in{};
    in.touchReleased = true;
    in.touchX = 10;
    in.touchY = 10;
    return iact.routePublished(in);
  };

  // A popup is shown and the panel catches up.
  iact.beginBuild();
  iact.beginPublishCycle();
  raw.clear();
  raw.addInteraction(slot(1, 3));
  iact.publish();
  paintclock::notePainted();
  CHECK(iact.publishedRoutable());
  CHECK(tap().value == 3);

  // A second popup replaces it on the same object. Published, not yet painted:
  // a finger resting where the first popup's row was must not select the
  // second popup's row under it.
  iact.beginBuild();
  iact.beginPublishCycle();
  raw.clear();
  raw.addInteraction(slot(1, 9));
  iact.publish();
  CHECK(!iact.publishedRoutable());
  CHECK(!tap());

  paintclock::notePainted();
  CHECK(iact.publishedRoutable());
  CHECK(tap().value == 9);

  // The touch-down highlight repaint: same options, only StateFocused moves,
  // which the digest ignores. It must still answer, or every popup would
  // highlight a row and then do nothing.
  iact.beginBuild();
  iact.beginPublishCycle();
  raw.clear();
  freeink::ui::Interaction focused = slot(1, 9);
  focused.state = fui::StateFocused;
  raw.addInteraction(focused);
  iact.publish();
  CHECK(iact.publishedRoutable());
  CHECK(tap().value == 9);
}

// beginBuild() digests the PUBLISHED generation, not the one being built into.
// The two are the same array for a caller that never publishes, and for one
// that calls beginBuild() before beginPublishCycle() (which is what
// OptionPopup does). They diverge for a caller that flips generations FIRST,
// and then data() is the table from two generations ago -- compared against as
// though the panel had shown it. This drives that order deliberately, because
// nothing else in the suite can tell the two apart.
void testBeginBuildDigestsThePublishedGenerationNotTheBuildingOne() {
  PaintClockGuard clock;
  paintclock::RevealedInteractions<17> iact;
  freeink::ui::InteractionBuffer<17>& raw = iact;

  const auto put = [&raw](const int16_t value) {
    freeink::ui::Interaction hit{};
    hit.rect = fui::Rect{0, 0, 100, 40};
    hit.action = 1;
    hit.value = value;
    hit.inputMask = fui::InputTouch;
    raw.clear();
    raw.addInteraction(hit);
  };

  // Generation 1 ends up holding table A, generation 0 holding table B, and B
  // is what the panel is showing.
  iact.beginBuild();
  iact.beginPublishCycle();
  put(1);
  iact.publish();
  paintclock::notePainted();

  iact.beginBuild();
  iact.beginPublishCycle();
  put(2);
  iact.publish();
  paintclock::notePainted();
  CHECK(iact.publishedRoutable());

  // Now the order that matters: flip generations FIRST, so data() is the stale
  // A from two renders ago while publishedData() is still the B on the glass.
  iact.beginPublishCycle();
  iact.beginBuild();
  put(1);
  iact.publish();

  // The panel shows B and the table is A, so this tap must be held. Digesting
  // data() instead would have adopted the stale A as "shown", found the new
  // table identical to it, and let the tap straight through.
  CHECK(!iact.publishedRoutable());
  paintclock::notePainted();
  CHECK(iact.publishedRoutable());
}

// OptionPopup's real render sequence, through the SDK component it actually
// calls. The hand-built test above proves the gate; this proves the thing a
// hand-built table cannot -- that the touch-down HIGHLIGHT repaint produces a
// byte-identical table. Get that wrong and every popup in the firmware lights
// a row up and then refuses it, which is the frozen-device failure this whole
// mechanism is shaped around, and no assertion on the gate alone would notice.
void testAnOptionPopupHighlightRepaintStillAnswers() {
  PaintClockGuard clock;
  FakeTarget target;
  paintclock::RevealedInteractions<17> interactions;

  static const char* const kLabels[3] = {"ONE", "TWO", "THREE"};

  // Mirrors OptionPopup::render(): beginBuild() before the publish cycle, the
  // chrome guard rect first, the dialog after, publish() last.
  const auto build = [&](const int selectedIndex, const uint8_t count) {
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    interactions.beginBuild();
    interactions.beginPublishCycle();
    fui::Frame<17> frame(target, ctx, noInput, interactions);

    fui::DialogOption options[3];
    for (uint8_t i = 0; i < count; ++i) {
      options[i].label = kLabels[i];
      options[i].action = 1;
      options[i].value = static_cast<int16_t>(i);
      options[i].state = (i == selectedIndex) ? fui::StateFocused : fui::StateNormal;
    }

    fui::OptionDialogProps props;
    props.title = "PICK";
    props.options = options;
    props.optionCount = count;
    props.verticalOptions = true;
    props.inputMask = fui::InputTouch;
    props.buttonHeight = 40;

    const fui::Rect dialog = fui::centeredRect(ctx.screen(), fui::Size{300, 300});
    frame.hit(dialog, 2, 0, fui::InputTouch);
    fui::optionDialog(frame, dialog, props);
    interactions.publish();
  };

  build(0, 3);
  paintclock::notePainted();
  CHECK(interactions.publishedRoutable());
  const size_t slots = interactions.publishedCount();
  CHECK(slots > 1);  // the guard plus at least one option, or this proves nothing

  // The highlight moving is the ONLY change. optionDialog derives each option
  // rect from geometry and the state only reaches Interaction::state, which the
  // digest reads for StateDisabled and nothing else -- so the release of the
  // contact that caused this repaint must still route.
  build(1, 3);
  CHECK(interactions.publishedRoutable());
  CHECK(interactions.publishedCount() == slots);
  build(2, 3);
  CHECK(interactions.publishedRoutable());

  // A different popup on the same object is a different table, and waits.
  build(0, 2);
  CHECK(!interactions.publishedRoutable());
  paintclock::notePainted();
  CHECK(interactions.publishedRoutable());
}

void testACapsuleThatWasDeadMidGameAlsoWaits() {
  PaintClockGuard clock;
  Rendered out;

  chessui::BoardModel playing;
  playing.status = "THEIR MOVE";
  playing.gameOver = false;
  buildBoard(out, playing);
  paintclock::notePainted();

  const int capsuleY = 800 - toybox::kMargin - toybox::kPillHeight / 2;
  CHECK(out.tap(300, capsuleY).action == fui::NO_ACTION);

  chessui::BoardModel finished = playing;
  finished.gameOver = true;
  finished.status = "CHECKMATE";
  buildBoard(out, finished);
  CHECK(!out.interactions.routable());
  CHECK(out.tap(300, capsuleY).action != chessui::ActionPlayAgain);

  paintclock::notePainted();
  CHECK(out.tap(300, capsuleY).action == chessui::ActionPlayAgain);
}

void testBattleshipStartMenu() {
  // A row that would do nothing is not drawn, exactly as in chess: with no
  // saved game there is nothing to continue, so the first row is NEW GAME.
  bshipui::StartModel fresh;
  fresh.played = 0;
  CHECK(bshipui::startRows(fresh) == 2);
  CHECK(bshipui::startRowAt(fresh, 0) == bshipui::StartRow::NewGame);
  CHECK(bshipui::startRowAt(fresh, 1) == bshipui::StartRow::PlayNearby);
  // Out of range clamps rather than reading past the end.
  CHECK(bshipui::startRowAt(fresh, 9) == bshipui::StartRow::PlayNearby);
  CHECK(bshipui::startRowAt(fresh, -1) == bshipui::StartRow::NewGame);

  bshipui::StartModel saved;
  saved.hasSavedGame = true;
  saved.played = 12;
  saved.won = 7;
  saved.streak = 3;
  CHECK(bshipui::startRows(saved) == 3);
  CHECK(bshipui::startRowAt(saved, 0) == bshipui::StartRow::Continue);

  Rendered out;
  buildBattleshipStart(out, saved);
  CHECK(out.target.drew("BATTLESHIP"));
  CHECK(out.target.drew("CONTINUE"));
  // No receipt beside the word: how the game stands is drawn in the slot this
  // builder returns, in the same marks the board uses.
  CHECK(!out.target.drew("14 SHOTS, 2 SUNK"));
  CHECK(out.target.drew("PLAY NEARBY"));
  // The record is one line, not three rows.
  CHECK(out.target.drew("12 PLAYED   7 WON   STREAK 3"));

  const FakeTarget::TextRun* nearby = out.target.find("PLAY NEARBY");
  CHECK(nearby != nullptr);
  if (nearby != nullptr) {
    const fui::ActionEvent event =
        out.tap(nearby->rect.x + nearby->rect.width / 2, nearby->rect.y + nearby->rect.height / 2);
    CHECK(event.action == bshipui::ActionStartRow);
    CHECK(bshipui::startRowAt(saved, event.value) == bshipui::StartRow::PlayNearby);
  }
}

void testBattleshipCapsuleIsOnlyATriggerWhenItSaysSo() {
  // The capsule does three jobs and the hit table has to agree with the label
  // every time. Chess shipped a PLAY AGAIN that was dead on its edges; these
  // assertions are that bug pinned for this app.
  Rendered reporting;
  bshipui::BoardModel model;
  model.status = "MARIO FIRED AT C4";
  buildBattleshipBoard(reporting, model);
  const FakeTarget::TextRun* label = reporting.target.find("MARIO FIRED AT C4");
  CHECK(label != nullptr);
  if (label != nullptr) {
    const fui::ActionEvent event =
        reporting.tap(label->rect.x + label->rect.width / 2, label->rect.y + label->rect.height / 2);
    CHECK(event.action == fui::NO_ACTION);
  }

  Rendered armed;
  bshipui::BoardModel aiming;
  aiming.status = "FIRE AT C4";
  aiming.canFire = true;
  buildBattleshipBoard(armed, aiming);
  const FakeTarget::TextRun* fire = armed.target.find("FIRE AT C4");
  CHECK(fire != nullptr);
  if (fire != nullptr) {
    CHECK(armed.tap(fire->rect.x + fire->rect.width / 2, fire->rect.y + fire->rect.height / 2).action ==
          bshipui::ActionFire);
    // Both edges, because a capsule painted wider than it hit-tests is exactly
    // how this went wrong before.
    CHECK(armed.tap(fire->rect.x + 2, fire->rect.y + fire->rect.height / 2).action == bshipui::ActionFire);
    CHECK(armed.tap(fire->rect.right() - 2, fire->rect.y + fire->rect.height / 2).action == bshipui::ActionFire);
  }

  Rendered finished;
  bshipui::BoardModel over;
  over.status = "PLAY AGAIN";
  over.gameOver = true;
  buildBattleshipBoard(finished, over);
  const FakeTarget::TextRun* again = finished.target.find("PLAY AGAIN");
  CHECK(again != nullptr);
  if (again != nullptr) {
    CHECK(finished.tap(again->rect.x + again->rect.width / 2, again->rect.y + again->rect.height / 2).action ==
          bshipui::ActionPlayAgain);
  }
}

void testBattleshipPlacementControls() {
  Rendered out;
  bshipui::PlaceModel model;
  model.status = "TAP A SHIP TO MOVE IT";
  buildBattleshipPlace(out, model);
  // "PLACE YOUR FLEET" came out of the band as "PLACE YOUR FLEE" on the device:
  // the display cut is wide and the header does not shrink to fit.
  CHECK(out.target.drew("YOUR FLEET"));
  CHECK(out.target.drew("TAP A SHIP TO MOVE IT"));
  CHECK(out.target.drew("SHUFFLE"));
  CHECK(out.target.drew("READY"));

  const FakeTarget::TextRun* shuffle = out.target.find("SHUFFLE");
  const FakeTarget::TextRun* ready = out.target.find("READY");
  CHECK(shuffle != nullptr && ready != nullptr);
  if (shuffle != nullptr && ready != nullptr) {
    // Two controls side by side, so the risk is one swallowing the other's
    // half of the footer. Each is checked at both its edges.
    CHECK(out.tap(shuffle->rect.x + 2, shuffle->rect.y + shuffle->rect.height / 2).action == bshipui::ActionShuffle);
    CHECK(out.tap(shuffle->rect.right() - 2, shuffle->rect.y + shuffle->rect.height / 2).action ==
          bshipui::ActionShuffle);
    CHECK(out.tap(ready->rect.x + 2, ready->rect.y + ready->rect.height / 2).action == bshipui::ActionReady);
    CHECK(out.tap(ready->rect.right() - 2, ready->rect.y + ready->rect.height / 2).action == bshipui::ActionReady);
    CHECK(shuffle->rect.right() < ready->rect.x);
  }

  // Waiting for the other device: the buttons stay where they are and stop
  // working, rather than vanishing and moving the grid.
  Rendered waiting;
  bshipui::PlaceModel sent;
  sent.status = "WAITING FOR MARIO";
  sent.canEdit = false;
  buildBattleshipPlace(waiting, sent);
  CHECK(waiting.target.drew("SHUFFLE"));
  CHECK(waiting.target.drew("READY"));
  const FakeTarget::TextRun* inert = waiting.target.find("READY");
  CHECK(inert != nullptr);
  if (inert != nullptr) {
    CHECK(waiting.tap(inert->rect.x + inert->rect.width / 2, inert->rect.y + inert->rect.height / 2).action ==
          fui::NO_ACTION);
  }
}

// --- a shelf folder --------------------------------------------------------

void buildShelf(Rendered& out, const shelfui::MenuModel& model) {
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, device(), noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  shelfui::buildMenu(screen, model);
}

void testShelfFolderDrawsItsOwnNameAndRows() {
  fui::ListItem items[4] = {};
  const char* titles[4] = {"CHESS", "BATTLESHIP", "CONNECTIONS", "SOLITAIRE"};
  for (int i = 0; i < 4; ++i) {
    items[i].label = titles[i];
    items[i].actionValue = static_cast<int16_t>(i);
  }

  shelfui::MenuModel model;
  // One builder draws every folder, so the title is data, not a literal. If it
  // were hardcoded again the APPS folder would call itself GAMES.
  model.title = "GAMES";
  model.items = items;
  model.count = 4;
  model.playerName = "SPIKY GRIM BEARD";

  Rendered menu;
  buildShelf(menu, model);
  CHECK(menu.target.drew("GAMES"));
  CHECK(menu.target.drew("CHESS"));
  CHECK(menu.target.drew("SOLITAIRE"));
  CHECK(menu.target.drew("SPIKY GRIM BEARD"));
  CHECK(!menu.interactions.overflowed());

  const int firstRowY = toybox::kHeaderHeight + toybox::kGutter * 3 + toybox::kRowHeight / 2;
  const fui::ActionEvent first = menu.tap(240, firstRowY);
  CHECK(first.action == shelfui::ActionOpen);
  CHECK(first.value == 0);

  // The same builder, a different folder. Asserting the name changed is the
  // only thing standing between one builder and a hardcoded header.
  shelfui::MenuModel apps = model;
  apps.title = "APPS";
  Rendered other;
  buildShelf(other, apps);
  CHECK(other.target.drew("APPS"));
  CHECK(!other.target.drew("GAMES"));
}

// A folder with more rows than fit, which is every GAMES folder from the tenth
// game onward.
//
// The row icons are drawn by this fork rather than by the list component, so
// they carry their own idea of where a row is, and it used to be the absolute
// item index. That is the same thing as the row only while nothing scrolls. At
// ten items the tenth icon painted below the band in black, on top of the black
// player footer; once scrolled, every icon sat a row away from its label. The
// three shelf tests that already existed all used lists short enough to fit, so
// none of them could see it.
//
// Asserted as "each visible label has its own icon on its own row" rather than
// as a count, because the count was right the whole time the positions were
// wrong. A distinct icon per row is what makes an off-by-N detectable at all.
//
// Driven at both pages, because they fail differently and an earlier draft of
// this test only had the second. On page one the rows past the fold must simply
// not be drawn, which is the tenth-icon-on-the-footer case. On page two the
// drawn ones must have moved up with their labels.
void checkShelfIconsSitOnTheirRows(const int page) {
  constexpr int kCount = 12;
  const freeink::Icon* const palette[kCount] = {&icon_chess_32,     &icon_battleship_32, &icon_connections_32,
                                                &icon_solitaire_32, &icon_nearby_32,     &icon_games_32,
                                                &icon_apps_32,      &icon_hackernews_32, &icon_unreadable_32,
                                                &icon_study_32,     &icon_dungeon_32,    &icon_insider_32};

  fui::ListItem items[kCount] = {};
  char labels[kCount][8] = {};
  for (int i = 0; i < kCount; ++i) {
    std::snprintf(labels[i], sizeof(labels[i]), "GAME%02d", i);
    items[i].label = labels[i];
    items[i].actionValue = static_cast<int16_t>(i);
  }

  const fui::ThemeTokens tokens = toybox::themeTokens();
  const shelfui::Paging paging = shelfui::pagingFor(device(), tokens, true, kCount);
  // The list has to overflow one page or neither case under test exists.
  CHECK(paging.pageCount > 1);
  const fui::Rect band = shelfui::listBand(device(), true, true);

  shelfui::MenuModel model;
  model.title = "GAMES";
  model.playerName = "SPIKY GRIM BEARD";
  model.page = page;
  model.pageCount = paging.pageCount;

  // The screen is handed one page, sliced, exactly as the activity hands it one.
  // The last page is short, so this is not always rowsPerPage.
  const int first = page * paging.rowsPerPage;
  const int onThisPage = kCount - first < paging.rowsPerPage ? kCount - first : paging.rowsPerPage;
  model.items = items + first;
  model.icons = palette + first;
  model.count = onThisPage;

  Rendered menu;
  buildShelf(menu, model);

  // Half a row: an icon one row out of place is a whole rowHeight + gap away,
  // so this is generous about text metrics and still exact about rows.
  const int tolerance = tokens.rowHeight / 2;
  int paired = 0;
  for (int i = 0; i < kCount; ++i) {
    const fui::Rect* icon = nullptr;
    for (const auto& blit : menu.target.blits) {
      if (blit.data == palette[i]->bits) {
        icon = &blit.rect;
        break;
      }
    }
    const fui::Rect* label = nullptr;
    for (const auto& run : menu.target.texts) {
      if (run.text == labels[i]) {
        label = &run.rect;
        break;
      }
    }

    // Scrolled off the top, or below the fold. The icon must be gone too: this
    // is the half that used to paint onto the player footer.
    if (label == nullptr) {
      CHECK(icon == nullptr);
      continue;
    }

    // Guarded rather than asserted-and-continued: a missing icon here used to
    // segfault the rest of the loop, which is a worse failure report than the
    // one line that is actually wrong.
    CHECK(icon != nullptr);
    if (icon == nullptr) continue;

    CHECK(icon->y >= band.y);
    CHECK(icon->y + icon->height <= band.y + band.height);
    const int iconMid = icon->y + icon->height / 2;
    const int labelMid = label->y + label->height / 2;
    CHECK(iconMid >= labelMid - tolerance && iconMid <= labelMid + tolerance);
    ++paired;
  }

  CHECK(paired == onThisPage);
}

// No row of a shelf folder is ever marked.
//
// The X4 Pro has two physical keys, both of which PAGE, and `frontButtonConfirm`
// resolves to an unassigned pin -- so an inverted row is a cursor that nothing
// can move and nothing can act on. It shipped as a landmark explaining why a
// restored folder did not open on page one, and it was read as a cursor
// instead: the row it marked was the last app opened, so APPS wore a permanent
// highlight on whichever app was used most.
//
// Asserted as ink rather than as a field so it survives the field: a selected
// row draws its label paper-on-black, so every label being ink is the property
// that actually matters, whatever the model grows later. The icons are checked
// the same way, because they are drawn by this fork rather than by the list
// component and used to invert on their own.
void testShelfFolderMarksNoRow() {
  constexpr int kCount = 5;
  const freeink::Icon* const palette[kCount] = {&icon_study_32, &icon_hackernews_32, &icon_xkcd_32, &icon_games_32,
                                                &icon_apps_32};
  const char* titles[kCount] = {"STUDY", "HACKER NEWS", "XKCD", "GET BOOKS", "INSTAPAPER"};
  fui::ListItem items[kCount] = {};
  for (int i = 0; i < kCount; ++i) {
    items[i].label = titles[i];
    items[i].actionValue = static_cast<int16_t>(i);
  }

  shelfui::MenuModel model;
  model.title = "APPS";
  model.items = items;
  model.icons = palette;
  model.count = kCount;

  Rendered menu;
  buildShelf(menu, model);

  // Every row label present, and every one of them ink. White here would be a
  // row drawn inverted, which is the mark under test.
  int checked = 0;
  for (int i = 0; i < kCount; ++i) {
    const FakeTarget::TextRun* row = menu.target.find(titles[i]);
    CHECK(row != nullptr);
    if (row == nullptr) continue;
    CHECK(row->color == fui::Color::Black);
    ++checked;
  }
  CHECK(checked == kCount);

  // And the icons, which invert separately from the label.
  for (int i = 0; i < kCount; ++i) {
    for (const auto& blit : menu.target.blits) {
      if (blit.data == palette[i]->bits) CHECK(blit.color == fui::Color::Black);
    }
  }
}

void testShelfIconsFollowTheRowsWhenTheListScrolls() {
  // Page one of a folder that overflows: the rows past the fold are the ones
  // that used to paint their icons onto the player footer.
  checkShelfIconsSitOnTheirRows(0);
  // And page two, where every drawn icon has moved up by a page and the ones
  // above the band must be gone.
  checkShelfIconsSitOnTheirRows(1);
}

// The shelf pages rather than scrolls, which is what makes a folder of forty
// games reachable on a panel whose only gesture is a tap: there is no swipe
// anywhere in this fork, and the list component's 3px overflow track is drawn
// but not tappable, so before this every row past the ninth could be reached
// only with the physical buttons.
void testTheShelfPagesWhenAFolderOverflows() {
  constexpr int kCount = 12;
  fui::ListItem items[kCount] = {};
  char labels[kCount][8] = {};
  for (int i = 0; i < kCount; ++i) {
    std::snprintf(labels[i], sizeof(labels[i]), "GAME%02d", i);
    items[i].label = labels[i];
    items[i].actionValue = static_cast<int16_t>(i);
  }

  const fui::ThemeTokens tokens = toybox::themeTokens();

  // A folder that fits pays nothing for paging: no bar, and every row it could
  // hold before it is still there.
  const shelfui::Paging small = shelfui::pagingFor(device(), tokens, true, 3);
  CHECK(small.pageCount == 1);
  CHECK(small.rowsPerPage ==
        fui::listVisibleRows(shelfui::listBand(device(), true, false), tokens.rowHeight, tokens.listRowGap));

  const shelfui::Paging paging = shelfui::pagingFor(device(), tokens, true, kCount);
  CHECK(paging.pageCount == 2);
  // The bar costs a row, so a paged folder holds fewer than an unpaged one.
  CHECK(paging.rowsPerPage < small.rowsPerPage);
  CHECK(paging.rowsPerPage * paging.pageCount >= kCount);

  // Every item is on exactly one page. This is the assertion that catches the
  // list component clamping topIndex to count - visible so its last screen is
  // full (list.h:164): under that rule page two of twelve showed items four to
  // eleven, repeating half of page one. It is why the screen is handed a slice.
  for (int page = 0; page < paging.pageCount; ++page) {
    const int first = page * paging.rowsPerPage;
    const int onThisPage = kCount - first < paging.rowsPerPage ? kCount - first : paging.rowsPerPage;

    shelfui::MenuModel model;
    model.title = "GAMES";
    model.playerName = "SPIKY GRIM BEARD";
    model.items = items + first;
    model.count = onThisPage;
    model.page = page;
    model.pageCount = paging.pageCount;

    Rendered menu;
    buildShelf(menu, model);
    for (int i = 0; i < kCount; ++i) {
      const bool belongsHere = i >= first && i < first + onThisPage;
      CHECK(menu.target.drew(labels[i]) == belongsHere);
    }
  }

  // And the pips are reachable. Rendered page one, tapping the bar must offer
  // every other page, because being able to leave page one is the entire point.
  shelfui::MenuModel model;
  model.title = "GAMES";
  model.playerName = "SPIKY GRIM BEARD";
  model.items = items;
  model.count = paging.rowsPerPage;
  model.page = 0;
  model.pageCount = paging.pageCount;

  Rendered menu;
  buildShelf(menu, model);
  const fui::Rect band = shelfui::listBand(device(), true, true);

  // Found by probing rather than by recomputing the layout, so the test cannot
  // agree with the builder by making the same arithmetic mistake twice.
  int barY = -1;
  for (int y = band.y + band.height; y < 800 && barY < 0; ++y) {
    if (menu.tap(device().width / 2, y).action == shelfui::ActionGoToPage) barY = y;
  }
  CHECK(barY > 0);
  CHECK(barY > band.y + band.height);

  // Every page is one tap away, and the targets are contiguous *within the
  // cluster*: a sweep hits pages in ascending order with no dead pixel between
  // the first target and the last. Outside the cluster there is deliberately
  // nothing, because the marks are a position indicator with air around them
  // rather than a bar of buttons -- so this asserts no gap rather than no miss.
  // A gap between adjacent pages is a strip the thumb finds and the eye cannot.
  int reached[8] = {};
  int firstHit = -1;
  int lastHit = -1;
  int gaps = 0;
  int previous = -1;
  for (int x = toybox::kMargin; x < device().width - toybox::kMargin; ++x) {
    const fui::ActionEvent hit = menu.tap(x, barY);
    if (hit.action != shelfui::ActionGoToPage) {
      if (firstHit >= 0 && lastHit == x - 1) continue;  // past the cluster's end
      continue;
    }
    CHECK(hit.value >= 0 && hit.value < paging.pageCount);
    if (firstHit < 0) firstHit = x;
    if (lastHit >= 0 && x != lastHit + 1) ++gaps;
    // Ascending left to right: page one is on the left, as it reads.
    CHECK(hit.value >= previous);
    previous = hit.value;
    lastHit = x;
    if (hit.value < 8) ++reached[hit.value];
  }
  CHECK(firstHit > 0);
  CHECK(gaps == 0);
  for (int p = 0; p < paging.pageCount; ++p) CHECK(reached[p] > 0);
  // A cluster, not the whole bar: it must leave the edges alone or it is the
  // control this was rewritten to stop being.
  CHECK(lastHit - firstHit < band.width - 2 * toybox::kMargin);
}

// One input, one page, and the same page whichever input it was.
//
// The shelf pages from three places -- the two side keys, a horizontal swipe and
// a tap on a page mark -- and they used to do their own modular arithmetic each.
// Asserted as arithmetic because arithmetic is the half a cold tester cannot
// see: three of them reported a single press advancing two pages, and the press
// was never the variable. Where the folder had OPENED was.
void testAPageStepMovesExactlyOnePage() {
  CHECK(shelfui::pageStep(0, 3, 1) == 1);
  CHECK(shelfui::pageStep(1, 3, 1) == 2);
  // Wraps, because there is no cursor to run off the end of.
  CHECK(shelfui::pageStep(2, 3, 1) == 0);
  CHECK(shelfui::pageStep(0, 3, -1) == 2);
  CHECK(shelfui::pageStep(2, 3, -1) == 1);
  CHECK(shelfui::pageStep(1, 3, -1) == 0);
  // A folder that fits has nowhere to step to, and a key that quietly moved the
  // resumed row to the top instead would be a step that changed something
  // without going anywhere.
  CHECK(shelfui::pageStep(0, 1, 1) == 0);
  CHECK(shelfui::pageStep(0, 1, -1) == 0);

  // The property, not three examples of it: from any page of any folder, a step
  // moves by exactly one page and the opposite step undoes it. A guard that
  // fixed a double advance by making the key dead passes every example above
  // and fails the second line here.
  for (int pages = 2; pages <= 6; ++pages) {
    for (int from = 0; from < pages; ++from) {
      const int forward = shelfui::pageStep(from, pages, 1);
      const int back = shelfui::pageStep(from, pages, -1);
      CHECK((forward - from + pages) % pages == 1);
      CHECK((from - back + pages) % pages == 1);
      CHECK(shelfui::pageStep(forward, pages, -1) == from);
      CHECK(shelfui::pageStep(back, pages, 1) == from);
    }
  }
}

// The shelf's own step STOPS at both ends, and that is the fix for a wrong game
// being launched twice by two different testers.
//
// Every page of a folder draws its rows at the same eight screen positions, so
// a page arrived at by accident is indistinguishable from the page that was
// wanted until something opens. Walking forward off the last page is the step
// nobody ever means; with a wrap it silently rehomes you two pages back, and the
// next tap opens the game that happens to sit in that row instead.
void testTheShelfStepStopsAtBothEnds() {
  CHECK(shelfui::pageStepClamped(0, 3, 1) == 1);
  CHECK(shelfui::pageStepClamped(1, 3, 1) == 2);
  CHECK(shelfui::pageStepClamped(1, 3, -1) == 0);
  // The two that a wrap gets wrong, and the whole reason this exists.
  CHECK(shelfui::pageStepClamped(2, 3, 1) == 2);
  CHECK(shelfui::pageStepClamped(0, 3, -1) == 0);
  // A folder that fits has nowhere to step to at all.
  CHECK(shelfui::pageStepClamped(0, 1, 1) == 0);
  CHECK(shelfui::pageStepClamped(0, 1, -1) == 0);

  // The property, not five examples of it: a step lands on a real page, moves by
  // at most one, and moves by exactly one unless it was already at that end.
  // Written as a property because the failure it guards is arithmetic that only
  // misbehaves at the two rows nobody writes an example for.
  for (int pages = 2; pages <= 6; ++pages) {
    for (int from = 0; from < pages; ++from) {
      const int forward = shelfui::pageStepClamped(from, pages, 1);
      const int back = shelfui::pageStepClamped(from, pages, -1);
      CHECK(forward >= 0 && forward < pages);
      CHECK(back >= 0 && back < pages);
      CHECK(forward == (from == pages - 1 ? from : from + 1));
      CHECK(back == (from == 0 ? from : from - 1));
      // Never around the horn. A wrap satisfies every line above except these.
      CHECK(forward >= from);
      CHECK(back <= from);
    }
  }

  // A stored row that outlived its folder still lands on a page that exists, so
  // a step from it cannot walk off either end.
  CHECK(shelfui::pageStepClamped(9, 3, 1) == 2);
  CHECK(shelfui::pageStepClamped(-4, 3, -1) == 0);
}

// A folder comes back to the page it was left on, and it is a ROW that carries
// that across the reboot.
//
// Mario, on the device, after the restored page had been made visible: "if I
// navigate to page two and then leave to read a book and then come back, I
// should still be taken to page two." What was stored was the page holding the
// game he last LAUNCHED, which is the same page right up until he browses and
// walks away, and browsing and walking away is most of what a shelf is for.
//
// Asserted as arithmetic because the activity that writes the row cannot be
// built here -- it needs the ActivityManager. What can be pinned down here is
// the pair of rules that make the stored row mean a page at all: that a page
// round-trips through the row that stands for it, and what happens when the page
// it stood for is gone.
void testAFolderComesBackToThePageItWasLeftOn() {
  // A page is stored as its first row, and comes back as the same page. Every
  // page of every plausible folder, not three examples: a stored row that
  // reopened one page out is the original bug wearing different clothes.
  for (int rows = 1; rows <= 12; ++rows) {
    for (int page = 0; page < 9; ++page) {
      CHECK(shelfui::pageFor(shelfui::rowForPage(page, rows), rows) == page);
    }
  }
  // The first row of page one is the top of the list, which is where a folder
  // nobody has left anywhere opens: an unvisited folder needs no stored value to
  // behave, and page zero must not be a special case anywhere else either.
  CHECK(shelfui::rowForPage(0, 9) == 0);

  // A row inside the folder is where it says it is.
  CHECK(shelfui::resumeRowFor(0, 19) == 0);
  CHECK(shelfui::resumeRowFor(13, 19) == 13);
  CHECK(shelfui::resumeRowFor(18, 19) == 18);

  // A row past the end lands on the LAST page, never back at the top. This is
  // the removed-game case: the card outlives the firmware that wrote it, so the
  // folder can be shorter than it was, and page one throws away the one thing
  // that was remembered.
  for (int count = 1; count <= 24; ++count) {
    for (int rows = 1; rows <= 10; ++rows) {
      const int last = shelfui::pageCountFor(count, rows) - 1;
      for (int stored = count; stored < count + 30; ++stored) {
        const int row = shelfui::resumeRowFor(stored, count);
        CHECK(row == count - 1);
        CHECK(shelfui::pageFor(row, rows) == last);
      }
    }
  }

  // And the shrink is a real one, not a folder that collapsed to a single page:
  // nineteen games remembered at the end, two removed, still the last page and
  // still not page one. A "fix" that reset an out-of-range row to the top passes
  // every check above this one and fails these two.
  constexpr int kWas = 19;
  constexpr int kNow = 17;
  const shelfui::Paging paging = shelfui::pagingFor(device(), toybox::themeTokens(), true, kNow);
  CHECK(paging.pageCount > 1);
  const int resumed = shelfui::pageFor(shelfui::resumeRowFor(kWas - 1, kNow), paging.rowsPerPage);
  CHECK(resumed == paging.pageCount - 1);
  CHECK(resumed != 0);

  // An empty folder has one page and it is page one. There is no such folder in
  // the registry today, and the arithmetic must not divide by it if there ever
  // is: a folder that shrank to nothing is the limit of the case above.
  CHECK(shelfui::resumeRowFor(7, 0) == 0);
  CHECK(shelfui::pageFor(shelfui::resumeRowFor(7, 0), 9) == 0);
  CHECK(shelfui::pageCountFor(0, 9) == 1);

  // A corrupt or negative row is the top, which is also what an unwritten file
  // gives. Nothing here may go below zero and index off the front of a page.
  CHECK(shelfui::resumeRowFor(-4, 19) == 0);
  CHECK(shelfui::rowForPage(-1, 9) == 0);
  CHECK(shelfui::resumeRowFor(5, -1) == 0);
}

// The marks are a control, and a control has to look like one.
//
// They were always tappable and always the reliable way to page; two cold
// testers found them by accident and a third never tried them, because ten
// pixels of ink with air around them read as decoration. The frame is the
// smallest thing here that reads as touchable, and it has to sit on exactly the
// strip the taps land in or it promises a hit where there is none.
void testThePageMarksReadAsAControl() {
  constexpr int kCount = 20;
  fui::ListItem items[kCount] = {};
  for (int i = 0; i < kCount; ++i) {
    items[i].label = "GAME";
    items[i].actionValue = static_cast<int16_t>(i);
  }

  const fui::ThemeTokens& tokens = toybox::themeTokens();
  const shelfui::Paging paging = shelfui::pagingFor(device(), tokens, true, kCount);
  CHECK(paging.pageCount > 1);

  shelfui::MenuModel model;
  model.title = "GAMES";
  model.playerName = "SPIKY GRIM BEARD";
  model.items = items;
  model.count = paging.rowsPerPage;
  model.page = 0;
  model.pageCount = paging.pageCount;

  Rendered menu;
  buildShelf(menu, model);
  const fui::Rect band = shelfui::listBand(device(), true, true);

  // Probed, not recomputed, so the test cannot make the builder's arithmetic
  // mistake twice. Both edges of the strip, because the ink has to sit ON the
  // strip the taps land in: ink outside it promises a hit where there is none,
  // and that is the half a screenshot cannot show.
  int barY = -1;
  int barBottom = -1;
  for (int y = band.y + band.height; y < 800; ++y) {
    if (menu.tap(device().width / 2, y).action != shelfui::ActionGoToPage) continue;
    if (barY < 0) barY = y;
    barBottom = y;
  }
  CHECK(barY > 0);
  CHECK(barBottom > barY);

  int firstHit = -1;
  int lastHit = -1;
  for (int x = 0; x < device().width; ++x) {
    if (menu.tap(x, barY).action != shelfui::ActionGoToPage) continue;
    if (firstHit < 0) firstHit = x;
    lastHit = x;
  }
  CHECK(firstHit > 0);

  // It stays a cluster: ink as wide as the list is the bar of slabs the marks
  // were deliberately rewritten not to be.
  CHECK(lastHit - firstHit < band.width);

  const int pitch = (lastHit - firstHit + 1) / model.pageCount;
  CHECK(pitch > 20);

  // Every page carries a box of ink filling most of its own cell, and the
  // current one is FILLED where the others are outlined. Ten pixels of ink in a
  // forty-four pixel cell -- what this replaced, and what a cold tester called
  // "the size of a full stop" -- passes "something was drawn down there" and
  // fails the width check here.
  for (int p = 0; p < model.pageCount; ++p) {
    const int left = firstHit + p * pitch;
    const int right = left + pitch - 1;
    const auto ownCell = [&](const fui::Rect& r) {
      if (r.y < barY || r.y + r.height - 1 > barBottom) return false;
      if (r.x < left || r.x + r.width - 1 > right) return false;
      return r.width * 2 >= pitch;
    };
    int filled = 0;
    int outlined = 0;
    for (const auto& r : menu.target.fills) {
      if (ownCell(r)) ++filled;
    }
    for (const auto& s : menu.target.strokes) {
      if (s.width > 0 && ownCell(s.rect)) ++outlined;
    }
    // Asserted as a pair, both ways round: a mutant that filled every cell says
    // you are on all three pages, and one that outlined every cell says you are
    // on none. Either reads as a control and answers nothing.
    CHECK(filled == (p == model.page ? 1 : 0));
    CHECK(outlined == (p == model.page ? 0 : 1));

    // And it says which page it is, in words. This is the whole reason the
    // marks changed: the folder resumes on the page it was left on, so the row
    // in position two is a different game on each visit, and "which page is
    // this" has to be answerable before any tap is safe.
    char number[8];
    std::snprintf(number, sizeof(number), "%d", p + 1);
    CHECK(menu.target.drew(number));
  }

  // Said twice, and the second time in the header, where the eye already is
  // while it is on the rows. The bar sits at the bottom of an 800px panel; a
  // cold tester did not misread it, they never looked at it.
  //
  // Composed rather than written out, so the strings cannot go stale the first
  // time a game is added and the folder gains a page.
  char onFirst[12];
  char onSecond[12];
  std::snprintf(onFirst, sizeof(onFirst), "1/%d", model.pageCount);
  std::snprintf(onSecond, sizeof(onSecond), "2/%d", model.pageCount);
  CHECK(menu.target.drew(onFirst));

  // The count moves with the page. A header that always says 1/N is worse than
  // no header at all.
  shelfui::MenuModel second = model;
  second.page = 1;
  Rendered later;
  buildShelf(later, second);
  CHECK(later.target.drew(onSecond));
  CHECK(!later.target.drew(onFirst));

  // A folder that fits draws no bar and no counter: "1/1" is furniture.
  shelfui::MenuModel lone = model;
  lone.count = 3;
  lone.page = 0;
  lone.pageCount = 1;
  Rendered single;
  buildShelf(single, lone);
  CHECK(!single.target.drew("1/1"));
}

// A row on a restored page opens ITS OWN game, not the game at that position on
// page one.
//
// The screen is handed one page as a slice, so the row a tap lands on is
// page-relative while the game it stands for is absolute. Kept as its own test
// because every other shelf tap test runs on page one, where the two are the
// same number and an off-by-a-page cannot show.
void testARowOnARestoredPageOpensItsOwnGame() {
  fui::ListItem items[3] = {};
  const char* titles[3] = {"MURDLE", "CHECKERS", "CONNECT FOUR"};
  for (int i = 0; i < 3; ++i) {
    items[i].label = titles[i];
    items[i].actionValue = static_cast<int16_t>(8 + i);
  }

  shelfui::MenuModel model;
  model.title = "GAMES";
  model.playerName = "SPIKY GRIM BEARD";
  model.items = items;
  model.count = 3;
  model.page = 1;
  model.pageCount = 3;

  Rendered menu;
  buildShelf(menu, model);

  const int rowY = toybox::kHeaderHeight + toybox::kGutter * 3 + toybox::kRowHeight + toybox::kRowHeight / 2;
  const fui::ActionEvent hit = menu.tap(240, rowY);
  CHECK(hit.action == shelfui::ActionOpen);
  CHECK(hit.value == 9);

  // And nothing on a restored page is marked. This is the page the mark used to
  // live on -- it existed to explain why the list had not opened at the top --
  // so it is the page where a reintroduced cursor would show first.
  for (const auto& run : menu.target.texts) {
    for (const char* title : titles) {
      if (run.text == title) CHECK(run.color == fui::Color::Black);
    }
  }
}

void testAFolderWithoutADeviceNameHasNoFooter() {
  fui::ListItem items[1] = {};
  items[0].label = "STUDY";

  shelfui::MenuModel model;
  model.title = "APPS";
  model.items = items;
  model.count = 1;
  // APPS does not show the device name: it exists for playing against somebody
  // in the room, and here it would be a word with no job.
  model.playerName = nullptr;

  Rendered menu;
  buildShelf(menu, model);
  CHECK(menu.target.drew("STUDY"));
  CHECK(!menu.target.drew("SPIKY GRIM BEARD"));

  // Not drawing the name is not enough: the control must not be there at all.
  // A footer built from a null label draws nothing visible, so an assertion on
  // the text alone passes while an invisible door to PLAYER sits at the bottom
  // of the screen waiting to be pressed. Tap where it would be.
  const int footerY = 800 - toybox::kMargin - toybox::kRowHeight / 2;
  CHECK(menu.tap(240, footerY).action != shelfui::ActionOpenPlayer);
  // And nothing painted a face there either. The bar is gone, not blanked.
  CHECK(menu.target.blits.empty());

  // The footer is not just hidden, its space is returned to the list. A folder
  // that reserved room for a control it never draws is dead space, and the list
  // would think it had one row less than it does.
  const fui::Rect withName = shelfui::listBand(device(), true, false);
  const fui::Rect without = shelfui::listBand(device(), false, false);
  CHECK(without.height > withName.height);
  CHECK(without.height - withName.height == toybox::kRowHeight + toybox::kGutter);
}

void testTheShelfFooterIsADoorWithAFaceOnIt() {
  fui::ListItem items[1] = {};
  items[0].label = "CHESS";

  shelfui::MenuModel model;
  model.title = "GAMES";
  model.items = items;
  model.count = 1;
  model.playerName = "PUNK SLY GOATEE";

  Rendered menu;
  buildShelf(menu, model);

  const FakeTarget::TextRun* bar = menu.target.find("PUNK SLY GOATEE");
  CHECK(bar != nullptr);
  if (bar == nullptr) return;

  // It opens PLAYER. It used to reroll in place, which meant the only way to
  // look at your name was also the only way to lose it.
  const fui::ActionEvent event = menu.tap(240, bar->rect.y + bar->rect.height / 2);
  CHECK(event.action == shelfui::ActionOpenPlayer);
  // Both edges, because a bar this wide is exactly where a hit region computed
  // separately from the paint goes dead at the ends -- which is how PLAY AGAIN
  // shipped with dead outer thirds.
  CHECK(menu.tap(toybox::kMargin + 2, bar->rect.y + bar->rect.height / 2).action == shelfui::ActionOpenPlayer);
  CHECK(menu.tap(480 - toybox::kMargin - 2, bar->rect.y + bar->rect.height / 2).action == shelfui::ActionOpenPlayer);

  // The face is the name's face, drawn in paper. This bar is filled solid
  // black, so a face in ink would be perfectly invisible and nothing would say
  // so -- the multiplayer mark went black-on-black once for exactly this
  // reason, and then white-on-white when it moved.
  const player::Avatar face = player::avatarFor("PUNK SLY GOATEE", player::AvatarSize::Row);
  const int16_t size = player::avatarPixels(player::AvatarSize::Row);
  const fui::Rect paper = menu.target.faceRect(face, fui::Color::White);
  CHECK(paper.width == size && paper.height == size);
  CHECK(menu.target.faceRect(face, fui::Color::Black).width == 0);
  // Inside the bar, and at its left.
  CHECK(paper.x >= toybox::kMargin);
  CHECK(paper.bottom() <= 800 - toybox::kMargin);

  // The name gets a band of its own that touches neither the face nor the
  // chevron. This is asserted as geometry rather than as "the face is in the
  // left quarter", which is what the previous version checked and why it passed
  // while the widest name ran straight through both marks: the label was handed
  // to the button, the button centred it across the whole bar, and the fake
  // font here is narrower than the real one so nothing collided in the test.
  //
  // Three things cannot share one centre line. Comparing the rects compares
  // what was actually drawn, at any font.
  const fui::Rect chevron = menu.target.blits.back().rect;
  CHECK(chevron.x > bar->rect.x);
  CHECK(bar->rect.x >= paper.right());
  CHECK(bar->rect.right() <= chevron.x);
  // ...and with air, not merely abutting.
  CHECK(bar->rect.x - paper.right() >= toybox::kGutter);
  CHECK(chevron.x - bar->rect.right() >= toybox::kGutter);
}

// --- PLAYER ----------------------------------------------------------------

void buildPlayer(Rendered& out, const playerui::PlayerModel& model) {
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, device(), noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  playerui::buildPlayer(screen, model);
}

playerui::PlayerModel playerModel() {
  playerui::PlayerModel model;
  model.name = "SPIKY GRIM BEARD";
  model.words[0] = "SPIKY";
  model.words[1] = "GRIM";
  model.words[2] = "BEARD";
  return model;
}

void testPlayerOffersThreeSeparateWords() {
  Rendered out;
  buildPlayer(out, playerModel());

  CHECK(out.target.drew("PLAYER"));
  CHECK(out.target.drew("SPIKY"));
  CHECK(out.target.drew("GRIM"));
  CHECK(out.target.drew("BEARD"));
  CHECK(out.target.drew("BACK"));
  CHECK(!out.interactions.overflowed());

  // The name is not spelled out a second time. Two copies of one string are two
  // things that can disagree, and the words already read as the name.
  CHECK(!out.target.drew("SPIKY GRIM BEARD"));

  // Each word rolls its own slot and nothing else. One action carrying the slot
  // as its value, so a fourth slot would need no new branch -- but the values
  // have to actually differ, or all three buttons roll the hair.
  const char* words[3] = {"SPIKY", "GRIM", "BEARD"};
  for (int slot = 0; slot < 3; ++slot) {
    const FakeTarget::TextRun* run = out.target.find(words[slot]);
    CHECK(run != nullptr);
    if (run == nullptr) continue;
    const fui::ActionEvent event = out.tap(run->rect.x + run->rect.width / 2, run->rect.y + run->rect.height / 2);
    CHECK(event.action == playerui::ActionStepSlot);
    CHECK(event.value == slot);
  }
}

void testPlayerWordsTileTheRowWithoutGapsOrOverlap() {
  Rendered out;
  buildPlayer(out, playerModel());

  const char* words[3] = {"SPIKY", "GRIM", "BEARD"};
  const FakeTarget::TextRun* runs[3] = {};
  for (int slot = 0; slot < 3; ++slot) runs[slot] = out.target.find(words[slot]);
  CHECK(runs[0] != nullptr && runs[1] != nullptr && runs[2] != nullptr);
  if (runs[0] == nullptr || runs[1] == nullptr || runs[2] == nullptr) return;

  // Left to right in slot order, which is the whole reading of the name.
  CHECK(runs[0]->rect.x < runs[1]->rect.x);
  CHECK(runs[1]->rect.x < runs[2]->rect.x);
  CHECK(runs[0]->rect.y == runs[1]->rect.y && runs[1]->rect.y == runs[2]->rect.y);

  // Sweep the whole band a pixel at a time and ask what each column does. This
  // is the assertion, rather than comparing rect edges, because what a player
  // hits is the routed action and the label's rect is inset from the control
  // that owns it. Three across a fixed band is where integer division shows up:
  // the last one ends short of the margin, or two overlap and one swallows the
  // other's taps.
  const int y = runs[0]->rect.y + runs[0]->rect.height / 2;
  // Right() is exclusive, so the last column inside the band is one short of
  // the margin.
  const int lastColumn = 480 - toybox::kMargin - 1;
  int owner[481];
  for (int x = toybox::kMargin; x <= lastColumn; ++x) {
    const fui::ActionEvent event = out.tap(x, y);
    owner[x] = event.action == playerui::ActionStepSlot ? event.value : -1;
  }

  // Both outer edges of the band belong to the outer words: no dead margin.
  CHECK(owner[toybox::kMargin] == 0);
  CHECK(owner[lastColumn] == 2);
  // Every slot owns a contiguous run, in order, and nothing owns two runs.
  int transitions = 0;
  int deadColumns = 0;
  int outOfOrder = 0;
  int lastOwner = 0;
  for (int x = toybox::kMargin; x <= lastColumn; ++x) {
    if (owner[x] < 0) {
      deadColumns++;
      continue;
    }
    if (owner[x] != lastOwner) {
      transitions++;
      if (owner[x] < lastOwner) outOfOrder++;
      lastOwner = owner[x];
    }
  }
  CHECK(transitions == 2);
  CHECK(outOfOrder == 0);
  // Only the two gutters may be untappable, and only if the controls do not
  // already cover them.
  CHECK(deadColumns <= 2 * toybox::kGutter);
}

void testPlayerDrawsTheFaceItsNameDescribes() {
  Rendered out;
  buildPlayer(out, playerModel());

  const player::Avatar face = player::avatarFor("SPIKY GRIM BEARD", player::AvatarSize::Portrait);
  const fui::Rect drawn = out.target.faceRect(face, fui::Color::Black);

  // Every layer on one rect, exactly kFaceSize, horizontally centred in the
  // content band. The sampler is nearest-neighbour, so an integer multiple of
  // the 120px asset doubles every pixel evenly and anything else leaves some
  // strokes a pixel fatter than their neighbours.
  CHECK(drawn.width == playerui::kFaceSize && drawn.height == playerui::kFaceSize);
  CHECK(drawn.x == toybox::kMargin + (480 - 2 * toybox::kMargin - playerui::kFaceSize) / 2);
  CHECK(drawn.y > toybox::kHeaderHeight);
  CHECK(playerui::kFaceSize % player::avatarPixels(player::AvatarSize::Portrait) == 0);

  // A different name is a different face. Without this the whole feature could
  // be one static drawing and every assertion above would still pass.
  Rendered other;
  playerui::PlayerModel changed = playerModel();
  changed.name = "BALD GLAD GRIN";
  changed.words[0] = "BALD";
  changed.words[1] = "GLAD";
  changed.words[2] = "GRIN";
  buildPlayer(other, changed);
  const player::Avatar theirs = player::avatarFor("BALD GLAD GRIN", player::AvatarSize::Portrait);
  CHECK(other.target.faceRect(theirs, fui::Color::Black).width == playerui::kFaceSize);
  CHECK(face.layer[1] != theirs.layer[1]);
  CHECK(face.layer[2] != theirs.layer[2]);
  CHECK(face.layer[3] != theirs.layer[3]);
  // The first face is not on this screen at all: the eyes and mouth it named
  // are gone, not merely overdrawn.
  CHECK(other.target.faceRect(face, fui::Color::Black).width == 0);
}

void testPlayerBackLeaves() {
  Rendered out;
  buildPlayer(out, playerModel());
  const FakeTarget::TextRun* back = out.target.find("BACK");
  CHECK(back != nullptr);
  if (back == nullptr) return;
  CHECK(out.tap(back->rect.x + back->rect.width / 2, back->rect.y + back->rect.height / 2).action ==
        playerui::ActionLeavePlayer);
  // The face is not a button. It is the biggest thing on the screen, so a
  // stray hit region over it would swallow most taps aimed at nothing.
  CHECK(out.tap(240, toybox::kHeaderHeight + toybox::kGutter * 4 + playerui::kFaceSize / 2).action == fui::NO_ACTION);
}

// --- the artwork and the vocabulary ----------------------------------------

void testEveryWordHasTheArtworkItNames() {
  // Two hand-maintained lists in two files: the words in PlayerName.cpp and the
  // bitmaps in PlayerAvatar.cpp. A static_assert pins their lengths. Nothing
  // but this pins their ORDER, and getting that wrong is silent -- swap two
  // hair words and every device quietly grows different hair, with no build
  // error and no visible defect until somebody who knows their own name looks
  // at their own face.
  int mismatched = 0;
  for (int slot = 0; slot < player::kSlotCount; ++slot) {
    for (uint8_t index = 0; index < player::wordCount(slot); ++index) {
      const char* word = player::word(slot, index);
      const char* art = player::artWord(slot, index);
      if (word == nullptr || art == nullptr || std::strcmp(word, art) != 0) mismatched++;
    }
  }
  CHECK(mismatched == 0);

  // Every triple resolves to a full face at both sizes, so no combination has a
  // hole in it.
  int incomplete = 0;
  for (uint8_t hair = 0; hair < player::wordCount(player::SlotHair); ++hair) {
    for (uint8_t eyes = 0; eyes < player::wordCount(player::SlotEyes); ++eyes) {
      for (uint8_t mouth = 0; mouth < player::wordCount(player::SlotMouth); ++mouth) {
        player::Name name;
        name.word[player::SlotHair] = hair;
        name.word[player::SlotEyes] = eyes;
        name.word[player::SlotMouth] = mouth;
        for (const player::AvatarSize size : {player::AvatarSize::Row, player::AvatarSize::Portrait}) {
          const player::Avatar avatar = player::avatarFor(name, size);
          // Four layers for every triple, including BALD -- its drawing is
          // deliberately empty, but it is a drawing, so the table has no holes
          // and the draw loop has no special case.
          for (int layer = 0; layer < player::Avatar::kLayerCount; ++layer) {
            if (avatar.layer[layer] == nullptr) incomplete++;
          }
        }
      }
    }
  }
  CHECK(incomplete == 0);
}

void testAnUnreadableNameDrawsThePlainHead() {
  // What a device running a different word list sends. It must come out as the
  // portrait everyone starts from, not as the wrong person and not as nothing.
  // PEERING is seven letters, so no future list can contain it -- the 20-char
  // name budget caps a word at six. A sample built from a word that happens not
  // to exist yet stops testing anything the day somebody adds it, which is what
  // happened to the previous one when CROSS became a real pair of eyes.
  const player::Avatar stranger = player::avatarFor("MOHAWK PEERING BEARD", player::AvatarSize::Row);
  CHECK(stranger.layer[0] != nullptr);
  CHECK(stranger.layer[1] == nullptr);
  CHECK(stranger.layer[2] == nullptr);
  // The third word IS one of ours, and a name we can half read draws the half
  // we understand rather than being thrown away whole.
  CHECK(stranger.layer[3] != nullptr);

  const player::Avatar nobody = player::avatarFor("", player::AvatarSize::Row);
  CHECK(nobody.layer[0] != nullptr);
  for (int i = 1; i < player::Avatar::kLayerCount; ++i) CHECK(nobody.layer[i] == nullptr);
}

void testABoardShowsWhoYouArePlaying() {
  // Faces used to appear only while pairing, so the person you were playing
  // vanished the moment you started playing them. Both link games put the
  // opponent beside the status capsule now, through one shared helper, so they
  // cannot place it differently.
  const char* them = "BALD SPECS GRIN";
  const player::Avatar face = player::avatarFor(them, player::AvatarSize::Row);

  Rendered match;
  chessui::BoardModel playing;
  playing.status = "BALD'S MOVE";
  playing.theirName = them;
  const fui::Rect matchBody = [&] {
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(match.target, device(), noInput, match.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    return chessui::buildBoardChrome(screen, playing);
  }();

  // Their face, at row size, down at the status band rather than up in the
  // board's rect.
  const fui::Rect drawn = match.target.faceRect(face, fui::Color::Black);
  CHECK(drawn.width == player::avatarPixels(player::AvatarSize::Row));
  CHECK(drawn.x == toybox::kMargin);
  CHECK(drawn.y >= matchBody.bottom());

  // The capsule moved over rather than being drawn under the face. Comparing
  // the drawn rects is the check that matters: the label is centred in whatever
  // rect it gets, so a helper that returned the band unshortened would overlap
  // the face and no text assertion would notice.
  const FakeTarget::TextRun* label = match.target.find("BALD'S MOVE");
  CHECK(label != nullptr);
  if (label != nullptr) CHECK(label->rect.x >= drawn.right());

  // Solo against the engine: nobody to show, and the capsule keeps the full
  // width it has always had. A face that appeared from nowhere would move the
  // capsule between modes for no reason the player could name.
  Rendered solo;
  chessui::BoardModel alone;
  alone.status = "YOUR MOVE";
  alone.theirName = nullptr;
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(solo.target, device(), noInput, solo.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  chessui::buildBoardChrome(screen, alone);
  CHECK(solo.target.blits.empty());
  const FakeTarget::TextRun* soloLabel = solo.target.find("YOUR MOVE");
  CHECK(soloLabel != nullptr);
  if (soloLabel != nullptr && label != nullptr) {
    // Wider, and starting further left, which is the whole difference between
    // them. Not compared against kMargin: the button insets its own label, and
    // how much is the component's business rather than this test's.
    CHECK(soloLabel->rect.width > label->rect.width);
    CHECK(soloLabel->rect.x < label->rect.x);
    CHECK(soloLabel->rect.width - label->rect.width == player::avatarPixels(player::AvatarSize::Row) + toybox::kGutter);
  }

  // Battleship takes the identical treatment from the identical helper.
  Rendered bship;
  bshipui::BoardModel fleet;
  fleet.report = "BALD SANK YOUR CRUISER";
  fleet.status = "THEIR MOVE";
  fleet.theirName = them;
  const fui::InputSnapshot none{};
  toybox::Frame bframe(bship.target, device(), none, bship.interactions);
  toybox::Screen bscreen(bframe, toybox::themeTokens());
  bshipui::buildBoardChrome(bscreen, fleet);
  const fui::Rect bdrawn = bship.target.faceRect(face, fui::Color::Black);
  CHECK(bdrawn.width == drawn.width);
  CHECK(bdrawn.x == drawn.x);
  CHECK(bdrawn.y == drawn.y);
}

void testBothSeatsWearTheirOwnFace() {
  // The payoff, and the reason the avatar is derived rather than stored: their
  // name already crossed the radio, so their face costs no wire bytes and
  // cannot arrive stale.
  Rendered out;
  linkui::LinkModel model = searchingModel();
  // Your seat is LABELLED "YOU" and drawn from your NAME. Those are two fields
  // on purpose, and this is the case that proves it: the first version derived
  // the face from the label, so every player saw a blank head in their own seat
  // -- "YOU" parses to no words at all. Nothing failed, nothing logged, and the
  // test passed because it had helpfully put a real name in the label.
  model.yourName = "YOU";
  model.yourFaceName = "SPIKY GRIM BEARD";
  model.theirName = "BALD SPECS GRIN";
  model.them = linkui::SeatState::Ready;
  model.linked = true;
  buildLink(out, model);

  CHECK(out.target.drew("YOU"));
  CHECK(!out.target.drew("SPIKY GRIM BEARD"));

  const player::Avatar mine = player::avatarFor("SPIKY GRIM BEARD", player::AvatarSize::Row);
  const player::Avatar theirs = player::avatarFor("BALD SPECS GRIN", player::AvatarSize::Row);
  // Different names, so at least one layer differs -- otherwise this test would
  // pass on a screen that drew the same face twice.
  CHECK(mine.layer[1] != theirs.layer[1]);

  int mineDrawn = 0;
  int theirsDrawn = 0;
  for (const auto& blit : out.target.blits) {
    for (int i = 0; i < player::Avatar::kLayerCount; ++i) {
      if (mine.layer[i] != nullptr && blit.data == mine.layer[i]->bits) mineDrawn++;
      if (theirs.layer[i] != nullptr && blit.data == theirs.layer[i]->bits) theirsDrawn++;
    }
  }
  // The base is shared, so it lands twice; each face's own layers land once.
  CHECK(mineDrawn == out.target.layersOf(mine) + 1);
  CHECK(theirsDrawn == out.target.layersOf(theirs) + 1);

  // An empty seat still gets a head: "somebody will be here" is what LOOKING
  // means, and the plain portrait says it without a special case.
  Rendered searching;
  buildLink(searching, searchingModel());
  const player::Avatar vacant = player::avatarFor("", player::AvatarSize::Row);
  CHECK(searching.target.layersOf(vacant) == 1);
  int vacantDrawn = 0;
  for (const auto& blit : searching.target.blits) {
    if (blit.data == vacant.layer[0]->bits) vacantDrawn++;
  }
  // Both seats: yours (MARIO, which parses to nothing) and the empty one.
  CHECK(vacantDrawn == 2);
}

// --- Hacker News -----------------------------------------------------------

void buildHnReader(Rendered& out, const hnui::ReaderModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  hnui::buildReader(screen, model);
}

void buildHnNotice(Rendered& out, const hnui::NoticeModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  hnui::buildNotice(screen, model);
}

hnui::ReaderModel articleModel() {
  hnui::ReaderModel model;
  model.title = "A tiny e-ink game console";
  model.text = "Some words that go on for a while and wrap onto more than one line of the panel.";
  model.pageLabel = "1/3";
  model.showingComments = false;
  model.swapAvailable = true;
  model.canPagePrev = false;
  model.canPageNext = true;
  return model;
}

bool drewText(const Rendered& out, const char* needle) {
  for (const auto& run : out.target.texts) {
    if (run.text.find(needle) != std::string::npos) return true;
  }
  return false;
}

// Present is not the same as legible. drewText() sees the string the builder
// HANDED the renderer, and the renderer is what shortens it -- so a button
// whose box is too narrow for its own label passes every "did it draw?" check
// while the panel says "UNDO A...". This asks the target to measure the run it
// recorded against the rect it was given, which is the one comparison the
// truncation is decided by.
bool drewLabelWhole(const Rendered& out, const char* needle) {
  bool found = false;
  for (const auto& run : out.target.texts) {
    if (run.text != needle) continue;
    found = true;
    if (out.target.measureText(run.style.font, run.text.c_str(), run.style).width > run.rect.width) return false;
  }
  return found;
}

// The height this text needs with the LINE CAP LIFTED, against the width it was
// drawn into.
//
// Measuring with the run's own style is a tautology wherever the builder sized
// the rect from that same call: the check restates the line it is guarding and
// can only fail if that line disappears entirely. Worse, it is blind to the
// mechanism it exists to catch. layoutText clamps to style.maxLines and
// ellipsizes whatever is left over, so a wording that needs five lines under a
// four-line cap is silently cut, the capped measure dutifully reports four, and
// the reserved rect matches it exactly.
//
// style.maxLines saturates at layoutText's own MAX_LINES (16), so asking for 16
// is asking for as many lines as the sentence takes. Comparing THAT against the
// reserved rect is the comparison the truncation is actually decided by.
int16_t uncappedWrappedHeight(const FakeTarget& target, const FakeTarget::TextRun& run) {
  fui::TextStyle uncapped = run.style;
  uncapped.maxLines = 16;
  return fui::measureWrappedText(target, run.text.c_str(), uncapped, run.rect.width).height;
}

void testHnReaderFooter() {
  Rendered out;
  hnui::ReaderModel model = articleModel();
  model.canPagePrev = true;
  buildHnReader(out, model);

  // The middle button says where it goes, and it is the wide one because it is
  // the only control here that changes what is being read.
  CHECK(drewText(out, "COMMENTS"));
  CHECK(drewText(out, "1/3"));

  // Find the footer row and tap the far edges of each control. This is the
  // PLAY AGAIN bug class: a button whose painted width and hit rect disagree is
  // dead on its edges and looks perfectly fine in a screenshot.
  const fui::Rect body = hnui::readerBody(device());
  const int footerY = body.y + body.height + 24;

  bool sawPrev = false;
  bool sawNext = false;
  bool sawSwap = false;
  for (int x = 0; x < 480; ++x) {
    const fui::ActionEvent event = out.tap(x, footerY);
    if (event.action == hnui::ActionPagePrev) sawPrev = true;
    if (event.action == hnui::ActionPageNext) sawNext = true;
    if (event.action == hnui::ActionSwapView) sawSwap = true;
  }
  CHECK(sawPrev);
  CHECK(sawNext);
  CHECK(sawSwap);
}

void testHnReaderDisabledControls() {
  Rendered out;
  hnui::ReaderModel model = articleModel();
  model.canPagePrev = false;  // page one: there is nowhere back to go
  model.canPageNext = false;
  buildHnReader(out, model);

  const fui::Rect body = hnui::readerBody(device());
  const int footerY = body.y + body.height + 24;
  for (int x = 0; x < 480; ++x) {
    const fui::ActionEvent event = out.tap(x, footerY);
    // A dimmed control keeps its place in the bar so nothing moves, but it must
    // not fire. Dimming is drawn in the fill, because there is no grey text on
    // this panel and a coloured label would just draw solid black.
    CHECK(event.action != hnui::ActionPagePrev);
    CHECK(event.action != hnui::ActionPageNext);
  }
}

void testHnReaderSwapLabelFollowsMode() {
  Rendered article;
  buildHnReader(article, articleModel());
  CHECK(drewText(article, "COMMENTS"));
  CHECK(!drewText(article, "ARTICLE  "));

  Rendered comments;
  hnui::ReaderModel model = articleModel();
  model.showingComments = true;
  buildHnReader(comments, model);
  // One action, and the model decides which way it points, so the label and the
  // effect cannot disagree.
  CHECK(drewText(comments, "ARTICLE"));
}

void testHnReaderTextStaysInItsRect() {
  Rendered out;
  buildHnReader(out, articleModel());

  // The Activity pages by counting the lines that fit in readerBody(). If the
  // text were drawn anywhere else, a page turn would skip or repeat lines and
  // nothing would report it.
  const fui::Rect body = hnui::readerBody(device());
  bool sawBodyText = false;
  for (const auto& run : out.target.texts) {
    if (run.text.find("Some words") == std::string::npos) continue;
    sawBodyText = true;
    CHECK(run.rect.y >= body.y);
    CHECK(run.rect.y < body.y + body.height);
    CHECK(run.rect.x >= body.x);
  }
  CHECK(sawBodyText);
}

void testHnNotice() {
  Rendered unreadable;
  hnui::NoticeModel model;
  model.headline = "NOT READABLE HERE";
  model.message = "This link is not a page of text.";
  model.mark = &icon_unreadable_32;
  // Both halves of the control, because buildNotice now draws it only when both
  // are set. A label with no action is a button that answers nothing.
  model.actionLabel = "READ THE COMMENTS";
  model.action = hnui::ActionNotice;
  buildHnNotice(unreadable, model);

  CHECK(drewText(unreadable, "NOT READABLE HERE"));
  CHECK(drewText(unreadable, "READ THE COMMENTS"));

  // The mark is a 1-bpp mask painted in one colour, so it is invisible on a
  // background of that colour and nothing warns you. This one sits on paper, so
  // it has to be black; drawn white it would be a blank square nobody notices.
  bool markDrawnInInk = false;
  for (const auto& blit : unreadable.target.blits) {
    if (blit.color == fui::Color::Black) markDrawnInInk = true;
  }
  CHECK(markDrawnInInk);

  // Comments are always reachable, which is the promise this screen exists to
  // keep: the only button on it leads there.
  bool foundWayOut = false;
  for (int y = 0; y < 800; y += 4) {
    for (int x = 0; x < 480; x += 8) {
      if (unreadable.tap(x, y).action == hnui::ActionNotice) foundWayOut = true;
    }
  }
  CHECK(foundWayOut);

  // A busy notice has nothing to decide yet, so it offers no button at all.
  Rendered busy;
  hnui::NoticeModel loading;
  loading.headline = "HACKER NEWS";
  loading.message = "FETCHING THE FRONT PAGE";
  buildHnNotice(busy, loading);
  CHECK(drewText(busy, "FETCHING THE FRONT PAGE"));
  for (int y = 0; y < 800; y += 4) {
    CHECK(busy.tap(240, y).action != hnui::ActionNotice);
  }
}

// EVERY notice has a way off it, and the notice that is not about an unreadable
// link is the one that did not.
//
// This screen has no segment strip and no list under it, so a notice with no
// control is a full-screen dead end whose only exit is a left-edge swipe that
// nothing on it mentions -- with the SAVED shelf, the half of this app that
// needs no network, on the far side of it. A failed ARTICLE or THREAD fetch
// showed exactly that, and it is the common failure: on a train every tap on a
// cached front page lands there. The fix for a failed FRONT PAGE went into one
// arm of the same `if` and not into its twin.
void testHnEveryNoticeCarriesAWayOff() {
  // The rule itself, asked directly. It cannot answer "no control": that is the
  // whole reason it is a function rather than a ternary at the call site, where
  // the nullptr half quietly covered four different failures.
  for (const bool unreadable : {false, true}) {
    const hnui::NoticeControl control = hnui::noticeControl(unreadable);
    CHECK(control.label != nullptr);
    CHECK(control.action != fui::NO_ACTION);
  }
  // And the two are DIFFERENT doors. A failure screen must not offer to fetch a
  // thread over the network it has just reported down.
  CHECK(hnui::noticeControl(false).action != hnui::noticeControl(true).action);

  // Drawn, live, and legible. The failure notice as the Activity builds it: no
  // mark, the same sentence the list's own failure shows, and the control the
  // rule above hands out.
  Rendered failure;
  hnui::NoticeModel model;
  model.headline = "NO LUCK";
  model.message = "Could not reach Hacker News. Saved articles still work.";
  const hnui::NoticeControl control = hnui::noticeControl(false);
  model.actionLabel = control.label;
  model.action = control.action;
  buildHnNotice(failure, model);

  // Two questions, and the first one is the one the bug was about: does ANY
  // pixel on this screen answer a finger. Asked separately from "is it the
  // right door" because a dead end fails the first and a mis-wired control
  // fails only the second.
  //
  // The door is named by its literal id, never by control.action. Comparing a
  // tap against control.action would make a revert that answers NO_ACTION pass
  // vacuously: every blank pixel on the panel returns NO_ACTION, so the sweep
  // would find its "door" in the margin. A test derived from the value under
  // test cannot falsify it.
  bool answersAFinger = false;
  bool foundTheDoor = false;
  for (int y = 0; y < 800; y += 4) {
    for (int x = 0; x < 480; x += 8) {
      const fui::ActionId action = failure.tap(x, y).action;
      if (action != fui::NO_ACTION) answersAFinger = true;
      if (action == hnui::ActionNoticeBack) foundTheDoor = true;
    }
  }
  CHECK(answersAFinger);
  CHECK(foundTheDoor);
  // Present is not legible: a label wider than its pill is ellipsized by the
  // renderer and drewText would still find it. Guarded so that a regression
  // answering nullptr here reports as the named CHECKs above rather than as a
  // segfault, which names nothing and cannot be counted.
  if (control.label != nullptr) CHECK(drewLabelWhole(failure, control.label));

  // The pairing rule, from the side that makes the control invisible rather
  // than dead. A label with no action used to be drawable; it would paint a
  // pill that answers nothing, which is worse than no pill at all because the
  // reader tries it and concludes the screen is frozen.
  Rendered orphan;
  hnui::NoticeModel unpaired;
  unpaired.headline = "NO LUCK";
  unpaired.message = "Could not reach Hacker News. Saved articles still work.";
  unpaired.actionLabel = "BACK TO THE LIST";
  buildHnNotice(orphan, unpaired);
  CHECK(!drewText(orphan, "BACK TO THE LIST"));
}

// The save mark, identified by being the only bitmap the reader draws and NOT
// by its pointer: ToyboxIcons.h declares every icon `static` at namespace
// scope, so this file's `icon_saved_32.bits` is a different array from the
// screen builder's and a pointer comparison silently never matches.
const FakeTarget::Blit* saveMarkIn(const Rendered& out) {
  return out.target.blits.size() == 1 ? &out.target.blits[0] : nullptr;
}

// Whether a solid paper fill sits under `rect`. The chip is that fill, and
// nothing else on this screen paints one.
bool paperChipUnder(const Rendered& out, const fui::Rect& rect) {
  for (size_t i = 0; i < out.target.fills.size(); ++i) {
    const fui::Paint& paint = out.target.fillPaints[i];
    if (paint.kind != fui::PaintKind::Solid || paint.color != fui::Color::White) continue;
    const fui::Rect& fill = out.target.fills[i];
    if (fill.x <= rect.x && fill.y <= rect.y && fill.x + fill.width >= rect.x + rect.width &&
        fill.y + fill.height >= rect.y + rect.height) {
      return true;
    }
  }
  return false;
}

fui::ActionEvent tapTheMark(Rendered& out, const fui::Rect& mark) {
  return out.tap(mark.x + mark.width / 2, mark.y + mark.height / 2);
}

// The thing about this mark that a screenshot cannot tell you: the header band
// is SOLID BLACK, so the two ordinary style sets swap weights on it. A black
// fill IS the band and disappears; a white fill is the loudest thing on the
// screen. Styled "filled means saved" out of those, the mark reads backwards --
// which is exactly how two cold testers read it, one of them removing an
// article they believed they had just kept.
//
// So the claim under test is about WEIGHT, not about which style was passed:
// the state carrying the paper-coloured chip has to be the saved one.
void testHnSaveMarkIsLoudestWhenSaved() {
  Rendered kept;
  hnui::ReaderModel model = articleModel();
  model.canSave = true;
  model.saved = true;
  buildHnReader(kept, model);

  const FakeTarget::Blit* keptMark = saveMarkIn(kept);
  CHECK(keptMark != nullptr);
  if (keptMark != nullptr) {
    // On the device: a paper chip with the bookmark knocked out of it.
    CHECK(paperChipUnder(kept, keptMark->rect));
    CHECK(keptMark->color == fui::Color::Black);
    CHECK(tapTheMark(kept, keptMark->rect).action == hnui::ActionUnsave);
  }
  // The glyph is one 1-bpp mask and never fills, so the chip was the only thing
  // that ever changed and nothing said what a tap had just done. A word does.
  CHECK(kept.target.drew("SAVED"));
  CHECK(!kept.target.drew("SAVE"));

  Rendered offer;
  model.saved = false;
  buildHnReader(offer, model);

  const FakeTarget::Blit* offerMark = saveMarkIn(offer);
  CHECK(offerMark != nullptr);
  if (offerMark != nullptr) {
    // The quiet state. A paper chip here is the bug: it outshouts the kept one.
    CHECK(!paperChipUnder(offer, offerMark->rect));
    // Drawn in paper so it is visible AT ALL on a black band -- the same trap
    // that made the page label invisible for two renders.
    CHECK(offerMark->color == fui::Color::White);
    CHECK(tapTheMark(offer, offerMark->rect).action == hnui::ActionSave);
  }
  CHECK(offer.target.drew("SAVE"));
  CHECK(!offer.target.drew("SAVED"));
}

// A thread carries the mark too. The stories worth keeping for a train are the
// ones whose page will not render here, and for those the conversation is the
// only thing there is to keep.
void testHnAThreadCanBeKept() {
  Rendered out;
  hnui::ReaderModel model = articleModel();
  model.showingComments = true;
  model.canSave = true;
  model.saved = false;
  buildHnReader(out, model);

  const FakeTarget::Blit* mark = saveMarkIn(out);
  CHECK(mark != nullptr);
  if (mark != nullptr) CHECK(tapTheMark(out, mark->rect).action == hnui::ActionSave);

  // And a reader with nothing to key an entry by draws no mark at all, rather
  // than offering a control that cannot work.
  Rendered none;
  hnui::ReaderModel unkeyed = articleModel();
  unkeyed.canSave = false;
  buildHnReader(none, unkeyed);
  CHECK(saveMarkIn(none) == nullptr);
  CHECK(!none.target.drew("SAVE"));
  CHECK(!none.target.drew("SAVED"));
}

void testHnReaderShowsWhereYouAre() {
  Rendered out;
  hnui::ReaderModel model = articleModel();
  model.pageLabel = "3/12";
  buildHnReader(out, model);

  // The page indicator has to be drawn in paper. The band is solid black and
  // the component takes rightLabel's style from the theme's subtitle, whose
  // colour is Black -- so a label left at the default is painted black on black
  // and is indistinguishable from never having been set. It went missing
  // through two renders exactly that way.
  bool paperOnTheBand = false;
  for (const auto& run : out.target.texts) {
    if (run.text == "3/12" && run.color == fui::Color::White) paperOnTheBand = true;
  }
  CHECK(paperOnTheBand);

  // The band carries the story's own headline, in paper for the same reason,
  // and in its own case: a title is content, not chrome. The mode word the
  // band used to shout belongs to the footer's swap button alone.
  bool headlineOnTheBand = false;
  for (const auto& run : out.target.texts) {
    if (run.text == "A tiny e-ink game console" && run.color == fui::Color::White) headlineOnTheBand = true;
  }
  CHECK(headlineOnTheBand);
  CHECK(!drewText(out, "ARTICLE"));
}

void testHnFitLines() {
  // The fake target bills every character at 10px, so the arithmetic here is
  // exact: a 200px line holds 20 characters.
  FakeTarget target;
  fui::TextStyle style;

  const auto fit = [&](const char* text, int16_t width, int lines) {
    return hnui::fitLines(target, text, width, lines, style);
  };

  // Fits outright: returned untouched, with no ellipsis bolted on.
  CHECK(fit("Waymo in Dallas", 200, 2) == "Waymo in Dallas");
  CHECK(fit("Waymo in Dallas", 150, 1) == "Waymo in Dallas");

  // Wraps across two lines and still fits: also untouched. This is the case the
  // first implementation got wrong -- it appended the ellipsis to the whole
  // string and measured that against ONE line, so anything that wrapped was
  // trimmed back to a single line and the front page read "In Memory of My...".
  CHECK(fit("There Will Come Soft Rains", 150, 2) == "There Will Come Soft Rains");
  CHECK(fit("There Will Come Soft Rains", 150, 1) != "There Will Come Soft Rains");

  // Genuinely too long: cut on a space, never inside a word, and marked.
  const std::string cut = fit("In Memory of My Wife Elise Cawley with Thanks for Many Years", 200, 2);
  CHECK(cut.size() > 3);
  CHECK(cut.rfind("...") == cut.size() - 3);
  const std::string body = cut.substr(0, cut.size() - 3);
  // Every word kept is a whole word from the original.
  CHECK(std::string("In Memory of My Wife Elise Cawley with Thanks for Many Years").rfind(body, 0) == 0);
  CHECK(!body.empty() && body.back() != ' ');

  // Two lines really do hold more than one.
  CHECK(fit("In Memory of My Wife Elise Cawley with Thanks", 200, 2).size() >
        fit("In Memory of My Wife Elise Cawley with Thanks", 200, 1).size());

  // A single word wider than the whole line cannot be broken on a space, so it
  // is allowed through rather than looping forever hunting for a break.
  const std::string huge = fit("Supercalifragilisticexpialidocious", 100, 2);
  CHECK(!huge.empty());

  // Degenerate inputs return something drawable rather than misbehaving.
  CHECK(fit(nullptr, 200, 2).empty());
  CHECK(fit("anything", 0, 2).empty());
  CHECK(fit("anything", 200, 0).empty());
  CHECK(fit("", 200, 2).empty());
}

void testHnList() {
  Rendered out;
  fui::ListItem items[3];
  items[0].label = "First story";
  items[0].subtitle = "412 points, 88 comments";
  items[0].actionValue = 0;
  items[1].label = "Second story";
  items[1].subtitle = "12 points, 3 comments";
  items[1].actionValue = 1;
  items[2].label = "Third story";
  items[2].subtitle = "9 points, 0 comments";
  items[2].actionValue = 2;

  hnui::ListModel model;
  model.items = items;
  model.count = 3;
  model.selected = 1;

  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  hnui::buildList(screen, model);

  CHECK(drewText(out, "First story"));
  CHECK(drewText(out, "412 points, 88 comments"));

  // Every row opens, and each carries its own index: a row that routes the
  // wrong value opens somebody else's story.
  bool opened[3] = {false, false, false};
  const fui::Rect band = hnui::listBand(ctx);
  for (int y = band.y; y < band.y + band.height; ++y) {
    const fui::ActionEvent event = out.tap(240, y);
    if (event.action == hnui::ActionOpenStory && event.value >= 0 && event.value < 3) opened[event.value] = true;
  }
  CHECK(opened[0]);
  CHECK(opened[1]);
  CHECK(opened[2]);

  // An empty front page says so rather than drawing a blank panel.
  Rendered empty;
  hnui::ListModel none;
  toybox::Frame emptyFrame(empty.target, ctx, noInput, empty.interactions);
  toybox::Screen emptyScreen(emptyFrame, toybox::themeTokens());
  hnui::buildList(emptyScreen, none);
  CHECK(drewText(empty, "NOTHING TO READ"));

  // An empty SAVED shelf is the ordinary state of a new device, and both lines
  // of it have to be IN INK. The display cut's token colour is paper because it
  // is otherwise only ever set on the black band, so a headline taken straight
  // from the theme lands white on white paper and the shelf answers with one
  // small sentence and an expanse of nothing.
  Rendered shelf;
  hnui::ListModel nothingSaved;
  nothingSaved.title = "SAVED";
  nothingSaved.showingSaved = true;
  nothingSaved.emptyHeadline = "NOTHING SAVED YET";
  nothingSaved.emptyMessage = "Tap SAVE while you read.";
  toybox::Frame shelfFrame(shelf.target, ctx, noInput, shelf.interactions);
  toybox::Screen shelfScreen(shelfFrame, toybox::themeTokens());
  hnui::buildList(shelfScreen, nothingSaved);
  const FakeTarget::TextRun* headline = shelf.target.find("NOTHING SAVED YET");
  CHECK(headline != nullptr);
  if (headline != nullptr) CHECK(headline->color == fui::Color::Black);
  const FakeTarget::TextRun* line = shelf.target.find("Tap SAVE while you read.");
  CHECK(line != nullptr);
  if (line != nullptr) CHECK(line->color == fui::Color::Black);
  // And they must not be drawn ON each other. centeredText centres in the
  // content rect and consumes nothing, so two calls land on the same y: the
  // headline was painted over the sentence for as long as it was invisible.
  if (headline != nullptr && line != nullptr) {
    CHECK(headline->rect.y + headline->rect.height <= line->rect.y);
  }
}

// The empty front page is the screen a device that has never joined a network
// opens on, so it is the one that has to carry a way onward. Text alone will
// not do: an empty shelf and an unloaded front page are the same expanse of
// paper, and a live control drawn like a dead one is one nobody tries.
void testHnEmptyFrontPageOffersAWayOnward() {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};

  Rendered cold;
  hnui::ListModel model;
  model.emptyHeadline = "NOT LOADED YET";
  model.emptyMessage = "The front page needs a connection. Saved articles do not.";
  model.emptyActionLabel = "LOAD";
  model.emptyAction = hnui::ActionLoadFrontPage;
  toybox::Frame coldFrame(cold.target, ctx, noInput, cold.interactions);
  toybox::Screen coldScreen(coldFrame, toybox::themeTokens());
  hnui::buildList(coldScreen, model);

  CHECK(drewText(cold, "NOT LOADED YET"));
  // Whole, not merely present. drewText sees the string the builder HANDED the
  // renderer and the renderer is what shortens it, so a pill too narrow for its
  // own label passes every "did it draw?" check while the panel says "LO...".
  // This control is the only way off the screen a device with no network opens
  // on, so it is the last label in the app that can afford to be a guess.
  CHECK(drewLabelWhole(cold, "LOAD"));

  // The control is reachable by a finger, which is the only thing that makes it
  // a control. Swept rather than tapped at one guessed point.
  bool foundLoad = false;
  for (int y = 0; y < ctx.height; ++y) {
    if (cold.tap(240, y).action == hnui::ActionLoadFrontPage) foundLoad = true;
  }
  CHECK(foundLoad);

  // It must not sit on the segment strip. The segments are the map between the
  // two shelves, and a control stealing their taps would strand the reader on
  // the half that needs the network. Swept across the panel rather than down one
  // column, because the segments are half-width and the control is not.
  int loadBottom = -1;
  int savedTop = ctx.height;
  bool foundSaved = false;
  for (const int x : {60, 240, 380}) {
    for (int y = 0; y < ctx.height; ++y) {
      const fui::ActionEvent event = cold.tap(static_cast<int16_t>(x), static_cast<int16_t>(y));
      if (event.action == hnui::ActionLoadFrontPage && y > loadBottom) loadBottom = y;
      if (event.action == hnui::ActionShowSaved) {
        foundSaved = true;
        if (y < savedTop) savedTop = y;
      }
    }
  }
  CHECK(foundSaved);
  CHECK(loadBottom >= 0);
  CHECK(loadBottom < savedTop);
  CHECK(!cold.interactions.overflowed());

  // The same screen after a failed fetch is a DIFFERENT screen and still
  // carries the control. This is where the app used to put a full-screen notice
  // with no segments and no buttons, so a failed front page was a dead end with
  // the offline shelf on the other side of it.
  Rendered failed;
  hnui::ListModel retry;
  retry.emptyHeadline = "NO LUCK";
  retry.emptyMessage = "Could not reach Hacker News. Saved articles still work.";
  retry.emptyActionLabel = "TRY AGAIN";
  retry.emptyAction = hnui::ActionLoadFrontPage;
  toybox::Frame failedFrame(failed.target, ctx, noInput, failed.interactions);
  toybox::Screen failedScreen(failedFrame, toybox::themeTokens());
  hnui::buildList(failedScreen, retry);
  CHECK(drewText(failed, "NO LUCK"));
  CHECK(drewLabelWhole(failed, "TRY AGAIN"));
  bool foundRetry = false;
  for (int y = 0; y < ctx.height; ++y) {
    if (failed.tap(240, y).action == hnui::ActionLoadFrontPage) foundRetry = true;
  }
  CHECK(foundRetry);

  // And the empty SAVED shelf carries NO such control: it is the half that
  // needs no network, and the only thing a button there could do is fetch the
  // other half.
  Rendered shelf;
  hnui::ListModel nothingSaved;
  nothingSaved.title = "SAVED";
  nothingSaved.showingSaved = true;
  nothingSaved.emptyHeadline = "NOTHING SAVED YET";
  nothingSaved.emptyMessage = "Tap SAVE while you read.";
  toybox::Frame shelfFrame(shelf.target, ctx, noInput, shelf.interactions);
  toybox::Screen shelfScreen(shelfFrame, toybox::themeTokens());
  hnui::buildList(shelfScreen, nothingSaved);
  for (int y = 0; y < ctx.height; ++y) {
    CHECK(shelf.tap(240, y).action != hnui::ActionLoadFrontPage);
  }
}

// The block stacks: headline, sentence, control, none of them on each other.
// The sentence used to reserve one line however long it was, so the line that
// has to explain what still works with no network was ellipsised at the panel
// edge with nothing to say it had been.
void testHnEmptyStateStacksWithoutOverlap() {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};

  Rendered out;
  hnui::ListModel model;
  model.emptyHeadline = "NOT LOADED YET";
  // Long enough to need two lines at every plausible cut, which is the case the
  // single reserved line got wrong.
  model.emptyMessage = "The front page needs a connection. Saved articles do not.";
  model.emptyActionLabel = "LOAD";
  model.emptyAction = hnui::ActionLoadFrontPage;
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  hnui::buildList(screen, model);

  const FakeTarget::TextRun* headline = out.target.find("NOT LOADED YET");
  const FakeTarget::TextRun* message = out.target.find(model.emptyMessage);
  const FakeTarget::TextRun* label = out.target.find("LOAD");
  CHECK(headline != nullptr);
  CHECK(message != nullptr);
  CHECK(label != nullptr);
  if (headline != nullptr && message != nullptr) {
    CHECK(headline->rect.y + headline->rect.height <= message->rect.y);
  }
  if (message != nullptr && label != nullptr) {
    CHECK(message->rect.y + message->rect.height <= label->rect.y);
  }
  // The sentence is given the lines the SDK's own wrap will emit into it. A
  // rect one line tall for a two-line sentence is a silent truncation: the SDK
  // ellipsizes and logs nothing, which is how "Tap the mark on an article to
  // ke" shipped -- and estimating the count from a single-line width divided by
  // the column is one short whenever the wrap cannot fill a line, which is how
  // "Saved articles do ..." reached a render on this very screen.
  //
  // Measured with the CAP LIFTED, which is the only version of this check that
  // can fail. See uncappedWrappedHeight: the builder reserves
  // measureWrappedText(style) and this used to assert against
  // measureWrappedText(style), so it restated the production expression and
  // went green on the exact case it names -- a wording longer than maxLines,
  // clipped and ellipsized, with the reserved rect matching the clipped
  // measurement perfectly.
  if (message != nullptr) {
    CHECK(message->rect.height >= uncappedWrappedHeight(out.target, *message));
  }
  if (headline != nullptr) {
    CHECK(headline->rect.height >= uncappedWrappedHeight(out.target, *headline));
  }

  // And with no sentence between them, the control still clears the headline by
  // the gap it is supposed to sit below by. The stack used to step over the
  // message's height whether or not a message had been drawn, so the button
  // landed a bare gutter below the TOP of the headline -- the same compositing
  // bug that already put this headline on top of its own sentence.
  //
  // Measured against the BUTTON'S HIT RECT and against the full intended
  // clearance, not against "does it overlap". Overlap is not expressible here:
  // FakeTarget's line height is smaller than the gutter, so the broken layout
  // draws them apart on this target and on top of each other on the panel,
  // where the display cut is more than twice as tall.
  Rendered bare;
  hnui::ListModel terse;
  terse.emptyHeadline = "NOT LOADED YET";
  terse.emptyMessage = nullptr;
  terse.emptyActionLabel = "LOAD";
  terse.emptyAction = hnui::ActionLoadFrontPage;
  toybox::Frame bareFrame(bare.target, ctx, noInput, bare.interactions);
  toybox::Screen bareScreen(bareFrame, toybox::themeTokens());
  hnui::buildList(bareScreen, terse);
  const FakeTarget::TextRun* bareHeadline = bare.target.find("NOT LOADED YET");
  CHECK(bareHeadline != nullptr);
  int buttonTop = ctx.height;
  for (int y = 0; y < ctx.height; ++y) {
    if (bare.tap(240, static_cast<int16_t>(y)).action == hnui::ActionLoadFrontPage && y < buttonTop) buttonTop = y;
  }
  CHECK(buttonTop < ctx.height);
  if (bareHeadline != nullptr) {
    CHECK(buttonTop >= bareHeadline->rect.y + bareHeadline->rect.height + toybox::kGutter * 2);
  }
}

// --- the study deck screen -------------------------------------------------

void buildStudyDeck(Rendered& out, const studyui::DeckModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  studyui::buildDeck(screen, model);
}

studyui::DeckModel deckWithWork(const int* forecast) {
  studyui::DeckModel model;
  model.name = "Mandarin: Vocabulary";
  model.due = 289;
  model.fresh = 4700;
  model.total = 5001;
  model.forecast = forecast;
  return model;
}

void testStudyDeckLeadsWithTheCount() {
  int forecast[studyui::kForecastDays] = {289, 4, 0, 12, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0};
  Rendered out;
  buildStudyDeck(out, deckWithWork(forecast));

  // The headline is the number, because that is the only question the screen
  // is answering when you open it.
  CHECK(out.target.drew("4989 TO GO"));
  CHECK(out.target.drew("289 DUE   4700 NEW"));
  CHECK(out.target.drew("Mandarin: Vocabulary   5001 CARDS"));
  CHECK(out.target.drew("START REVIEWING"));

  // The caption carries the number scheduled ahead. Without it an all-backlog
  // deck draws an empty panel that reads as a panel that failed.
  CHECK(out.target.drew("2 WEEKS BACK   TODAY   20 DUE AHEAD"));
}

void testStudyHeadlineIsTheHitTarget() {
  int forecast[studyui::kForecastDays] = {};
  forecast[0] = 5;
  Rendered out;
  buildStudyDeck(out, deckWithWork(forecast));

  // The most common action must be a tap on the largest thing on the screen,
  // not on a button beside it. Tapping the headline block starts the session.
  const auto* headline = out.target.find("4989 TO GO");
  CHECK(headline != nullptr);
  if (headline != nullptr) {
    const fui::ActionEvent onHeadline = out.tap(headline->rect.x + 20, headline->rect.y + 10);
    CHECK(onHeadline.action == studyui::ActionStudy);
  }

  // And the bottom door does the same thing, so the two cannot drift apart.
  const auto* door = out.target.find("START REVIEWING");
  CHECK(door != nullptr);
  if (door != nullptr) {
    const fui::ActionEvent onDoor = out.tap(door->rect.x + 20, door->rect.y + 10);
    CHECK(onDoor.action == studyui::ActionStudy);
  }
}

void testStudyDeckRowSwitchesOnlyWhenThereIsSomewhereToGo() {
  int forecast[studyui::kForecastDays] = {};
  forecast[0] = 5;

  // One deck: no switcher door. A control that cycles through one thing is a
  // control that does nothing, and drawing it would advertise a feature the
  // card does not have.
  {
    Rendered out;
    buildStudyDeck(out, deckWithWork(forecast));
    CHECK(!out.target.drew("CHANGE DECK"));
  }

  // More than one: a third door beside START REVIEWING and SYNC. It says the
  // position rather than the name (the name is the row above the ornament),
  // and tapping it is the switch -- value 3 on the shared study action.
  {
    Rendered out;
    studyui::DeckModel model = deckWithWork(forecast);
    model.deckIndex = 1;
    model.deckCount = 3;
    buildStudyDeck(out, model);
    const auto* row = out.target.find("CHANGE DECK");
    CHECK(row != nullptr);
    CHECK(out.target.drew("2 OF 3"));
    if (row != nullptr) {
      const fui::ActionEvent onRow = out.tap(row->rect.x + 20, row->rect.y + 10);
      CHECK(onRow.action == studyui::ActionStudy);
      CHECK(onRow.value == 3);
    }
  }
}

void testStudyOffersNothingWhenNothingIsDue() {
  int forecast[studyui::kForecastDays] = {};
  studyui::DeckModel model;
  model.name = "Mandarin";
  model.total = 5001;
  model.forecast = forecast;
  model.reviewed = 40;
  model.recalled = 34;
  model.sessionOver = true;

  Rendered out;
  buildStudyDeck(out, model);

  // Finishing is a state of the same screen, not a separate page: the session
  // result replaces the due counts and the door stops offering.
  CHECK(out.target.drew("DONE"));
  CHECK(out.target.drew("40 REVIEWED   85% RIGHT"));
  CHECK(out.target.drew("NOTHING TO REVIEW"));
  CHECK(!out.target.drew("START REVIEWING"));

  // A control that cannot act must not still be armed. Tapping where the
  // headline was, with nothing to study, must do nothing at all.
  const auto* headline = out.target.find("DONE");
  CHECK(headline != nullptr);
  if (headline != nullptr) {
    const fui::ActionEvent event = out.tap(headline->rect.x + 20, headline->rect.y + 10);
    CHECK(event.action == fui::NO_ACTION);
  }
}

void testStudyForecastBarsStayInsideTheirPanel() {
  // Everything overdue piles onto today, so today's bar is an order of
  // magnitude taller than the rest. Scaling to it flattened the forecast to
  // one column and thirteen empty slots; today clips instead. Either way no
  // bar may escape the panel it was given.
  int forecast[studyui::kForecastDays] = {4000, 3, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  Rendered out;
  buildStudyDeck(out, deckWithWork(forecast));

  const auto* caption = out.target.find("2 WEEKS BACK   TODAY   6 DUE AHEAD");
  CHECK(caption != nullptr);
  if (caption == nullptr) return;

  // Every fill must sit above the caption and below the header band: a bar
  // scaled off a 4000-card backlog would otherwise run up through the title.
  int bars = 0;
  for (const auto& rect : out.target.fills) {
    if (rect.width > 40) continue;  // rules and dividers, not bars
    ++bars;
    CHECK(rect.y >= toybox::kHeaderHeight);
    CHECK(rect.y + rect.height <= caption->rect.y);
  }
  // Today plus the three non-zero days ahead, each of which draws at least one
  // fill. If the scale ever silently drops a small day this count falls.
  CHECK(bars >= 4);
}

void testStudyRecordShowsTheStreak() {
  int forecast[studyui::kForecastDays] = {};
  int history[studyui::kHistoryDays] = {40, 22, 31};

  studyui::DeckModel model = deckWithWork(forecast);
  model.history = history;
  model.streak = 3;
  model.retention = 90;
  model.lifetimeReviews = 1204;
  Rendered out;
  buildStudyDeck(out, model);

  // The Record band is what you have done, which is the half of "stats" that
  // belongs on the front door rather than behind another tap.
  CHECK(out.target.drew("STREAK 3   90% RECALL   1204 REVIEWS"));

  // With no history at all it falls back to naming the deck rather than
  // printing a row of zeroes, which would read as a broken counter.
  Rendered fresh;
  studyui::DeckModel blank = deckWithWork(forecast);
  buildStudyDeck(fresh, blank);
  CHECK(fresh.target.drew("Mandarin: Vocabulary   5001 CARDS"));
  CHECK(!fresh.target.drew("STREAK 0   -1% RECALL   0 REVIEWS"));
}

void testStudyPanelSaysSoWhenItHasNothing() {
  // A fresh install with a backlog has no history and nothing scheduled ahead,
  // so every column is zero. An empty bracketed box reads as a panel that
  // failed to draw; this is the first thing a new deck shows.
  int forecast[studyui::kForecastDays] = {};
  forecast[0] = 289;  // all overdue, which lands on today and is not a column
  int history[studyui::kHistoryDays] = {};

  studyui::DeckModel model = deckWithWork(forecast);
  model.history = history;
  Rendered out;
  buildStudyDeck(out, model);
  CHECK(out.target.drew("NOTHING RECORDED YET"));

  // One day of history is enough to stop saying it.
  history[3] = 12;
  Rendered some;
  buildStudyDeck(some, model);
  CHECK(!some.target.drew("NOTHING RECORDED YET"));
}

void testStudyWarnsWhenAReviewDidNotSave() {
  int forecast[studyui::kForecastDays] = {};
  studyui::DeckModel model = deckWithWork(forecast);
  model.writeFailed = true;

  Rendered out;
  buildStudyDeck(out, model);
  // The one failure this app must never swallow.
  CHECK(out.target.drew("SOME REVIEWS DID NOT SAVE"));

  Rendered quiet;
  studyui::DeckModel ok = deckWithWork(forecast);
  buildStudyDeck(quiet, ok);
  CHECK(!quiet.target.drew("SOME REVIEWS DID NOT SAVE"));
}

// --- insider ---------------------------------------------------------------

// The whole game rests on one screen keeping one secret, so that is what these
// assert. A Citizen's card that leaked the word would look completely normal:
// the layout is the same, the icon is the same, and the only difference is a
// string that should not be there.

void buildInsiderPass(Rendered& out, const insiderui::PassModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  insiderui::buildPass(screen, model);
}

void buildInsiderVote(Rendered& out, const insiderui::VoteModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  insiderui::buildVote(screen, model);
}

// Substring rather than whole-run equality: the word can share a run with
// anything, and "did this string reach the panel at all" is the question.
bool anyTextContains(const Rendered& out, const char* needle) {
  for (const auto& run : out.target.texts) {
    if (run.text.find(needle) != std::string::npos) return true;
  }
  return false;
}

// Taps the middle of whatever run drew `label`, so the tap follows the drawing
// instead of a second copy of the layout maths.
fui::ActionEvent tapTextCentre(Rendered& out, const char* label) {
  const auto* run = out.target.find(label);
  if (run == nullptr) return fui::ActionEvent{};
  return out.tap(run->rect.x + run->rect.width / 2, run->rect.y + run->rect.height / 2);
}

void testInsiderCitizenIsNeverToldTheWord() {
  insiderui::PassModel model;
  model.seat = 2;
  model.players = 5;
  model.revealed = true;
  model.role = insider::Role::Citizen;
  model.word = "PEACOCK";
  Rendered out;
  buildInsiderPass(out, model);

  CHECK(out.target.drew("CITIZEN"));
  // The one assertion this whole app exists to keep true.
  CHECK(!anyTextContains(out, "PEACOCK"));

  // And the mirror: the two roles that are supposed to know it, do.
  for (const insider::Role role : {insider::Role::Master, insider::Role::Insider}) {
    insiderui::PassModel knows = model;
    knows.role = role;
    Rendered told;
    buildInsiderPass(told, knows);
    CHECK(told.target.drew("PEACOCK"));
    CHECK(told.target.drew("THE WORD IS"));
  }
}

void testInsiderFaceDownCardShowsNothingAtAll() {
  // The state the device is in while it is being handed over. If the role or
  // the word reached the panel here, the person passing it would see it.
  insiderui::PassModel model;
  model.seat = 1;
  model.players = 5;
  model.revealed = false;
  model.role = insider::Role::Insider;
  model.word = "PEACOCK";
  Rendered out;
  buildInsiderPass(out, model);

  CHECK(!anyTextContains(out, "PEACOCK"));
  CHECK(!anyTextContains(out, "INSIDER"));
  CHECK(out.target.drew("PLAYER 2"));
  CHECK(out.target.drew("TAP TO SEE YOUR ROLE"));
}

void testInsiderFaceDownCardTakesATapAnywhere() {
  insiderui::PassModel model;
  model.seat = 0;
  model.players = 4;
  Rendered out;
  buildInsiderPass(out, model);
  // Deliberately away from any label: the body is the target because the
  // device is being put into somebody's hand as they tap it.
  CHECK(out.tap(40, 700).action == insiderui::ActionAdvance);
  CHECK(out.tap(440, 200).action == insiderui::ActionAdvance);
}

void testInsiderMasterCannotBeAccused() {
  insiderui::VoteModel model;
  model.players = 5;
  model.masterSeat = 2;
  Rendered out;
  buildInsiderVote(out, model);

  // The Master's seat is drawn -- it dims rather than disappearing -- and is
  // not tappable. A hole in the grid and a live-but-wrong chip look the same
  // from the code; only the routed action tells them apart.
  CHECK(out.target.drew("MASTER"));
  CHECK(tapTextCentre(out, "3").action == fui::NO_ACTION);

  // Every other seat accuses itself and nobody else.
  const char* labels[5] = {"1", "2", "3", "4", "5"};
  for (int i = 0; i < 5; ++i) {
    if (i == model.masterSeat) continue;
    Rendered each;
    buildInsiderVote(each, model);
    const fui::ActionEvent event = tapTextCentre(each, labels[i]);
    CHECK(event.action == insiderui::ActionAccuse);
    CHECK(event.value == i);
  }
}

void testInsiderVoteWaitsForAChoice() {
  insiderui::VoteModel model;
  model.players = 5;
  model.masterSeat = 0;
  Rendered idle;
  buildInsiderVote(idle, model);
  CHECK(idle.target.drew("CHOOSE SOMEBODY"));
  // Dimmed, and genuinely inert: the label alone would be a lie the compiler
  // cannot catch.
  CHECK(tapTextCentre(idle, "CHOOSE SOMEBODY").action == fui::NO_ACTION);

  model.chosen = 3;
  Rendered ready;
  buildInsiderVote(ready, model);
  CHECK(ready.target.drew("ACCUSE PLAYER 4"));
  CHECK(tapTextCentre(ready, "ACCUSE PLAYER 4").action == insiderui::ActionConfirmVote);

  model.chosen = insider::kNoInsider;
  Rendered nobody;
  buildInsiderVote(nobody, model);
  CHECK(nobody.target.drew("SAY NOBODY"));
  CHECK(tapTextCentre(nobody, "SAY NOBODY").action == insiderui::ActionConfirmVote);
}

void testInsiderSteppersDieAtTheEnds() {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};

  insiderui::MenuModel model;
  insider::Record record;
  model.record = &record;

  model.players = insider::kMinPlayers;
  Rendered floor;
  {
    toybox::Frame frame(floor.target, ctx, noInput, floor.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    insiderui::buildMenu(screen, model);
  }
  CHECK(floor.target.drew("4 PLAYERS"));
  CHECK(tapTextCentre(floor, "-").action == fui::NO_ACTION);
  const fui::ActionEvent up = tapTextCentre(floor, "+");
  CHECK(up.action == insiderui::ActionPlayers);
  CHECK(up.value == 1);

  model.players = insider::kMaxPlayers;
  Rendered ceiling;
  {
    toybox::Frame frame(ceiling.target, ctx, noInput, ceiling.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    insiderui::buildMenu(screen, model);
  }
  CHECK(ceiling.target.drew("8 PLAYERS"));
  CHECK(tapTextCentre(ceiling, "+").action == fui::NO_ACTION);
  const fui::ActionEvent down = tapTextCentre(ceiling, "-");
  CHECK(down.action == insiderui::ActionPlayers);
  CHECK(down.value == -1);
}

void testInsiderRevealAlwaysSaysTheWord() {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};

  // Every ending, including the one nobody won, has to end with the word --
  // otherwise a round that times out leaves the table with no answer at all.
  const insider::Outcome endings[3] = {insider::Outcome::Won, insider::Outcome::Lost, insider::Outcome::OutOfTime};
  for (const insider::Outcome ending : endings) {
    insiderui::RevealModel model;
    model.outcome = ending;
    model.insiderSeat = 3;
    model.accused = 1;
    model.players = 5;
    model.word = "PEACOCK";
    Rendered out;
    {
      toybox::Frame frame(out.target, ctx, noInput, out.interactions);
      toybox::Screen screen(frame, toybox::themeTokens());
      insiderui::buildReveal(screen, model);
    }
    CHECK(out.target.drew("PEACOCK"));
    CHECK(out.target.drew("THE WORD WAS"));
    CHECK(out.target.drew("THE INSIDER WAS"));
  }

  // And the round where the role was never dealt says so in words, rather than
  // leaving the seat block empty and looking like a drawing bug.
  insiderui::RevealModel none;
  none.outcome = insider::Outcome::Won;
  none.insiderSeat = insider::kNoInsider;
  none.accused = insider::kNoInsider;
  none.word = "PEACOCK";
  Rendered out;
  {
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    insiderui::buildReveal(screen, none);
  }
  CHECK(!out.target.drew("THE INSIDER WAS"));
  CHECK(anyTextContains(out, "NO INSIDER"));
  CHECK(out.target.drew("PEACOCK"));
}

void testInsiderTutorialLosesNoWords() {
  // The bug this pins, from the page the tutorial replaced: text was drawn into
  // a rect it did not fit, the renderer ellipsised the tail into a glyph the
  // Toybox face does not have, and the sentence simply stopped -- on screen and
  // in no test. So the assertion is not "it looks right", it is "every word
  // survived, on every page".
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};

  CHECK(insiderui::tutorialPages() >= 3);
  for (int page = 0; page < insiderui::tutorialPages(); ++page) {
    Rendered out;
    insiderui::TutorialModel model;
    model.page = page;
    {
      toybox::Frame frame(out.target, ctx, noInput, out.interactions);
      toybox::Screen screen(frame, toybox::themeTokens());
      insiderui::buildTutorial(screen, model);
    }

    // Every page says something, is tappable end to end, and never ellipsises.
    CHECK(!out.target.texts.empty());
    CHECK(out.tap(240, 300).action == insiderui::ActionAdvance);
    for (const auto& run : out.target.texts) {
      if (run.text.find("\xE2\x80\xA6") != std::string::npos) {
        std::printf("  tutorial page %d ellipsised: %s\n", page, run.text.c_str());
        CHECK(false);
      }
      // Every line drawn into a rect wide enough for it. The fake target
      // records whatever it is handed and never truncates, so without this a
      // line drawn into half its width looks identical from here.
      // FakeTarget::measureText is ten pixels a character.
      if (run.text == "HOW TO PLAY") continue;
      const int needed = static_cast<int>(run.text.size()) * 10;
      if (needed > run.rect.width) {
        std::printf("  tutorial page %d needs %d in %d: %s\n", page, needed, run.rect.width, run.text.c_str());
        CHECK(false);
      }
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Murdle
//
// The grid is the thing worth testing here. It is 144 cells at four categories
// of four against an interaction buffer that holds 24, so it registers one hit
// region and resolves the cell itself -- which means the arithmetic that turns
// a tap into a square is app code rather than component code, and it is the
// kind of code that is wrong by one and looks fine.

murdle::Puzzle murdleCase(const murdle::Tier tier, const uint32_t seed) {
  static murdle::Scratch scratch;
  const murdle::Shape shape = murdle::shapeOf(tier);
  uint8_t cast[murdle::kMaxCats][murdle::kMaxItems];
  murdle::drawCast(seed, shape, cast);
  murdle::Puzzle puzzle;
  murdle::generate(tier, seed, cast, murdle::attrMasksFor(cast, shape), scratch, puzzle);
  return puzzle;
}

murdleui::GridLayout buildMurdleCase(Rendered& out, const murdleui::CaseModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  return murdleui::buildCase(screen, model).grid;
}

void testMurdleGridResolvesEveryCellItDrew() {
  // Walk every live square of the staircase, tap its centre, and demand the
  // pair back. An off-by-one in either axis marks somebody else's square, which
  // is invisible until a solved grid disagrees with the answer.
  for (const murdle::Tier tier : {murdle::Tier::Elementary, murdle::Tier::HardBoiled}) {
    murdle::Puzzle puzzle = murdleCase(tier, 4242u);
    murdle::Marks marks;
    marks.reset(puzzle.shape);

    Rendered out;
    murdleui::CaseModel model;
    model.puzzle = &puzzle;
    model.marks = &marks;
    model.face = murdleui::Face::Grid;
    const murdleui::GridLayout grid = buildMurdleCase(out, model);
    CHECK(grid.valid);

    int live = 0;
    for (int r = 0; r < grid.groups * grid.items; ++r) {
      for (int c = 0; c < grid.groups * grid.items; ++c) {
        const int x = grid.cellX(c) + grid.cell / 2;
        const int y = grid.cellY(r) + grid.cell / 2;
        murdleui::GridCell cell;
        const bool hit = murdleui::cellAt(grid, x, y, cell);
        if (!grid.blockLive(r / grid.items, c / grid.items)) {
          // The empty corner of the staircase is not a square.
          CHECK(!hit);
          continue;
        }
        ++live;
        CHECK(hit);
        if (!hit) continue;
        CHECK(cell.catA == grid.rowCat[r / grid.items]);
        CHECK(cell.catB == grid.colCat[c / grid.items]);
        CHECK(cell.itemA == r % grid.items);
        CHECK(cell.itemB == c % grid.items);
      }
    }
    // Three blocks at three categories, six at four; never a pair twice.
    CHECK(live == puzzle.shape.cats * (puzzle.shape.cats - 1) / 2 * grid.items * grid.items);
  }
}

void testMurdleGridEdgesAreLive() {
  // The dead-on-its-edges class of bug: a square that draws normally and only
  // answers taps in its middle. Checked at the far corners of the whole grid,
  // one pixel inside.
  murdle::Puzzle puzzle = murdleCase(murdle::Tier::HardBoiled, 77u);
  murdle::Marks marks;
  marks.reset(puzzle.shape);
  Rendered out;
  murdleui::CaseModel model;
  model.puzzle = &puzzle;
  model.marks = &marks;
  model.face = murdleui::Face::Grid;
  const murdleui::GridLayout grid = buildMurdleCase(out, model);

  murdleui::GridCell topLeft;
  CHECK(murdleui::cellAt(grid, grid.originX, grid.originY, topLeft));
  CHECK(topLeft.itemA == 0 && topLeft.itemB == 0);

  const int last = grid.groups * grid.items - 1;
  murdleui::GridCell bottomOfFirstColumn;
  CHECK(murdleui::cellAt(grid, grid.cellX(0) + grid.cell - 1, grid.cellY(last) + grid.cell - 1, bottomOfFirstColumn));
  CHECK(bottomOfFirstColumn.itemA == grid.items - 1);

  // And just outside is not a square.
  murdleui::GridCell outside;
  CHECK(!murdleui::cellAt(grid, grid.originX - 1, grid.originY, outside));
  CHECK(!murdleui::cellAt(grid, grid.originX, grid.originY - 1, outside));
}

void testMurdleGridDrawsMarksItIsGiven() {
  murdle::Puzzle puzzle = murdleCase(murdle::Tier::Elementary, 5u);
  murdle::Marks marks;
  marks.reset(puzzle.shape);
  Rendered out;
  murdleui::CaseModel model;
  model.puzzle = &puzzle;
  model.marks = &marks;
  model.face = murdleui::Face::Grid;
  buildMurdleCase(out, model);
  // The whole grid is one hit region, not one per cell, or the 24-slot buffer
  // would be gone before the chrome got a look in.
  CHECK(!out.interactions.overflowed());
}

void testMurdleClueFaceIsPagedAndNeverOverflows() {
  // The densest screen in the app: four categories, the longest clue list, and
  // a pager. A control past the buffer limit draws normally and cannot be
  // tapped, with no log line.
  murdle::Puzzle puzzle = murdleCase(murdle::Tier::Impossible, 31u);
  murdle::Marks marks;
  marks.reset(puzzle.shape);
  for (int page = 0; page < 6; ++page) {
    Rendered out;
    murdleui::CaseModel model;
    model.puzzle = &puzzle;
    model.marks = &marks;
    model.face = murdleui::Face::Clues;
    model.page = page;
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    const murdleui::CaseReport report = murdleui::buildCase(screen, model);
    CHECK(!out.interactions.overflowed());
    // One page, even at Impossible. The cast used to be paged into the front
    // of this same stream, which is what made a case three or four pages deep;
    // it has its own face now, so twelve clues fit once. If a future change
    // makes clues longer this goes above one and the pager at the foot of the
    // face becomes reachable again -- which is the thing worth noticing, so
    // assert the floor rather than a fixed count.
    CHECK(report.pages >= 1);
    // A page past the end is clamped rather than drawn blank.
    CHECK(report.page < report.pages);
    CHECK(out.target.drew("ACCUSE"));
  }
}

void testMurdleSettingsPicksAnAbsoluteTier() {
  Rendered out;
  murdleui::SettingsModel model;
  model.tier = murdle::Tier::Elementary;
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  murdleui::buildSettings(screen, model);

  CHECK(out.target.drew("ELEMENTARY"));
  CHECK(out.target.drew("IMPOSSIBLE"));
  const FakeTarget::TextRun* hard = out.target.find("HARD BOILED");
  CHECK(hard != nullptr);
  if (hard != nullptr) {
    const fui::ActionEvent event = out.tap(hard->rect.x + 10, hard->rect.y + hard->rect.height / 2);
    CHECK(event.action == murdleui::ActionTier);
    // Absolute, not a step: the screen shows all four, so there is nothing to
    // walk and a delta would depend on where you already were.
    CHECK(event.value == static_cast<int>(murdle::Tier::HardBoiled));
  }
}

void testMurdleAccusationIsInertUntilComplete() {
  murdle::Puzzle puzzle = murdleCase(murdle::Tier::HardBoiled, 9u);
  murdleui::AccuseModel model;
  model.puzzle = &puzzle;

  Rendered empty;
  {
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(empty.target, ctx, noInput, empty.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    murdleui::buildAccuse(screen, model);
  }
  CHECK(!model.complete());
  const FakeTarget::TextRun* confirm = empty.target.find("THAT IS MY ACCUSATION");
  CHECK(confirm != nullptr);
  if (confirm != nullptr) {
    // It draws, dimmed, rather than disappearing -- and it must not act.
    const fui::ActionEvent event =
        empty.tap(confirm->rect.x + confirm->rect.width / 2, confirm->rect.y + confirm->rect.height / 2);
    CHECK(event.action == fui::NO_ACTION);
  }

  for (int c = 0; c < puzzle.shape.cats; ++c) model.picks[c] = 0;
  CHECK(model.complete());
  Rendered full;
  {
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(full.target, ctx, noInput, full.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    murdleui::buildAccuse(screen, model);
  }
  const FakeTarget::TextRun* live = full.target.find("THAT IS MY ACCUSATION");
  CHECK(live != nullptr);
  if (live != nullptr) {
    const fui::ActionEvent event = full.tap(live->rect.x + live->rect.width / 2, live->rect.y + live->rect.height / 2);
    CHECK(event.action == murdleui::ActionConfirm);
  }
}

void testMurdleMenuHeadlineIsTheDoorAcrossItsWidth() {
  Rendered out;
  murdleui::MenuModel model;
  model.hasCase = true;
  model.caseNumber = 3;
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  murdleui::buildMenu(screen, model);

  const FakeTarget::TextRun* headline = out.target.find("THE CASE");
  CHECK(headline != nullptr);
  if (headline != nullptr) {
    // Both far edges, because a headline hit-tested narrower than it draws is
    // the PLAY AGAIN bug wearing a different label.
    const int y = headline->rect.y + headline->rect.height / 2;
    CHECK(out.tap(headline->rect.x + 2, y).action == murdleui::ActionPlay);
    CHECK(out.tap(headline->rect.x + headline->rect.width - 2, y).action == murdleui::ActionPlay);
  }
  CHECK(out.target.drew("NEW CASE"));
  CHECK(!out.interactions.overflowed());
}

// --- connect four ----------------------------------------------------------

template <typename Model, void (*Build)(toybox::Screen&, const Model&)>
void buildC4(Rendered& out, const Model& model) {
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, device(), noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  Build(screen, model);
}

// The load-bearing one. The whole column is the target, so every pixel of it
// must resolve to that column and nothing outside it may.
void testTheColumnYouTapIsTheColumnTheRulesGet() {
  for (int column = 0; column < connectfour::kColumns; ++column) {
    const fui::Rect slot = c4ui::slotRect(device(), column);
    const fui::Rect bottom = c4ui::cellRect(device(), column, 0);
    const int probes[6][2] = {
        {slot.x, slot.y},
        {slot.x + slot.width - 1, slot.y},
        {slot.x, slot.y + slot.height - 1},
        {bottom.x, bottom.y},
        {bottom.x + bottom.width - 1, bottom.y + bottom.height - 1},
        {slot.x + slot.width / 2, (slot.y + bottom.y) / 2},
    };
    for (const auto& probe : probes) {
      CHECK(c4ui::columnAt(device(), probe[0], probe[1]) == column);
    }
  }
  // Each column is a distinct strip: neighbours never share a pixel.
  for (int column = 0; column + 1 < connectfour::kColumns; ++column) {
    const fui::Rect a = c4ui::cellRect(device(), column, 0);
    const fui::Rect b = c4ui::cellRect(device(), column + 1, 0);
    CHECK(a.x + a.width == b.x);
  }
}

// Row 0 is the bottom in the rules. If that flip ever inverts, discs pile
// downward from the ceiling and nothing else in the app would notice.
void testRowZeroIsDrawnAtTheBottom() {
  const fui::Rect floorCell = c4ui::cellRect(device(), 3, 0);
  const fui::Rect topCell = c4ui::cellRect(device(), 3, connectfour::kRows - 1);
  CHECK(floorCell.y > topCell.y);
  CHECK(floorCell.y - topCell.y == (connectfour::kRows - 1) * floorCell.height);
  // And the slot sits above everything, because that is where a disc goes in.
  CHECK(c4ui::slotRect(device(), 3).y < topCell.y);
}

void testTheConnectFourGridKeepsOffTheChrome() {
  const int capsuleY = 800 - toybox::kMargin - toybox::kPillHeight / 2;
  CHECK(c4ui::columnAt(device(), 240, capsuleY) == connectfour::kNoColumn);
  CHECK(c4ui::columnAt(device(), 240, toybox::kHeaderHeight / 2) == connectfour::kNoColumn);
  // And off the sides, where there is no column at all.
  const fui::Rect first = c4ui::cellRect(device(), 0, 0);
  CHECK(c4ui::columnAt(device(), first.x - 1, first.y) == connectfour::kNoColumn);
  const fui::Rect last = c4ui::cellRect(device(), connectfour::kColumns - 1, 0);
  CHECK(c4ui::columnAt(device(), last.x + last.width, last.y) == connectfour::kNoColumn);
  // The board clears the capsule.
  CHECK(last.y + last.height + toybox::kBoardFrame + toybox::kPillHeight + toybox::kGutter * 2 <= 800);
  CHECK(c4ui::slotRect(device(), 0).y >= toybox::kHeaderHeight);
}

void testTheBoardSaysWhoseDrop() {
  c4ui::BoardModel model;
  connectfour::start(model.game);
  model.open = connectfour::openColumns(model.game);
  model.yourTurn = true;

  Rendered mine;
  buildC4<c4ui::BoardModel, c4ui::buildBoard>(mine, model);
  CHECK(mine.target.drew("YOUR DROP"));
  CHECK(!mine.interactions.overflowed());

  model.yourTurn = false;
  Rendered theirs;
  buildC4<c4ui::BoardModel, c4ui::buildBoard>(theirs, model);
  CHECK(theirs.target.drew("THEIR DROP"));
  CHECK(!theirs.target.drew("YOUR DROP"));
}

void testTheConnectFourResultNamesTheOutcomeFromYourSeat() {
  c4ui::ResultModel won;
  connectfour::start(won.game);
  won.outcome = connectfour::Outcome::LightWins;
  won.seat = connectfour::kLight;
  Rendered a;
  buildC4<c4ui::ResultModel, c4ui::buildResult>(a, won);
  CHECK(a.target.drew("YOU WIN"));

  c4ui::ResultModel lost = won;
  lost.seat = connectfour::kDark;
  Rendered b;
  buildC4<c4ui::ResultModel, c4ui::buildResult>(b, lost);
  CHECK(b.target.drew("THEY WIN"));

  c4ui::ResultModel drawn = won;
  drawn.outcome = connectfour::Outcome::Draw;
  Rendered c;
  buildC4<c4ui::ResultModel, c4ui::buildResult>(c, drawn);
  CHECK(c.target.drew("A DRAW"));
}

// A board full of discs is a lot of registered controls if anyone ever
// registers them. Forty-two cells plus seven slots is well past the
// twenty-four slot cap, so this asserts the arithmetic path is really being
// taken rather than the buffer silently dropping half the board.
void testAFullBoardDoesNotOverflowTheInteractionBuffer() {
  c4ui::BoardModel model;
  connectfour::start(model.game);
  uint32_t rng = 0x2468ACE0u;
  while (!connectfour::over(model.game)) {
    int legal[connectfour::kColumns];
    int count = 0;
    for (int c = 0; c < connectfour::kColumns; ++c) {
      if (connectfour::canDrop(model.game, c)) legal[count++] = c;
    }
    rng = rng * 1664525u + 1013904223u;
    connectfour::drop(model.game, legal[rng % static_cast<uint32_t>(count)]);
  }
  model.open = connectfour::openColumns(model.game);
  Rendered out;
  buildC4<c4ui::BoardModel, c4ui::buildBoard>(out, model);
  CHECK(!out.interactions.overflowed());
}

// --- checkers --------------------------------------------------------------

template <typename Model, void (*Build)(toybox::Screen&, const Model&)>
void buildCk(Rendered& out, const Model& model) {
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, device(), noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  Build(screen, model);
}

// The load-bearing one, and harder here than in Minesweeper: the board is drawn
// from the playing side's end, so the same square is in a different place for
// each seat. squareRect and squareAt must be exact inverses for BOTH.
void testTheSquareYouTapIsTheSquareTheRulesGet() {
  const uint8_t seats[] = {checkers::kLight, checkers::kDarkSeat};
  for (const uint8_t seat : seats) {
    for (int file = 0; file < checkers::kSize; ++file) {
      for (int rank = 0; rank < checkers::kSize; ++rank) {
        const fui::Rect box = checkui::squareRect(device(), file, rank, seat);
        const int probes[4][2] = {{box.x, box.y},
                                  {box.x + box.width - 1, box.y},
                                  {box.x, box.y + box.height - 1},
                                  {box.x + box.width - 1, box.y + box.height - 1}};
        for (const auto& probe : probes) {
          int gotFile = -1;
          int gotRank = -1;
          CHECK(checkui::squareAt(device(), probe[0], probe[1], seat, gotFile, gotRank));
          CHECK(gotFile == file);
          CHECK(gotRank == rank);
        }
      }
    }
    // And the two seats really do disagree about where a square is, or the
    // flip is not happening at all.
    const fui::Rect mine = checkui::squareRect(device(), 0, 0, seat);
    const fui::Rect theirs =
        checkui::squareRect(device(), 0, 0, seat == checkers::kLight ? checkers::kDarkSeat : checkers::kLight);
    CHECK(mine.x != theirs.x || mine.y != theirs.y);
  }
}

void testTheBoardKeepsOffTheChrome() {
  int f = -1;
  int r = -1;
  const int capsuleY = 800 - toybox::kMargin - toybox::kPillHeight / 2;
  CHECK(!checkui::squareAt(device(), 240, capsuleY, checkers::kLight, f, r));
  CHECK(!checkui::squareAt(device(), 240, toybox::kHeaderHeight / 2, checkers::kLight, f, r));

  const fui::Rect last = checkui::squareRect(device(), checkers::kSize - 1, checkers::kSize - 1, checkers::kLight);
  CHECK(last.y + last.height + toybox::kPillHeight + toybox::kGutter * 2 <= 800);
  const fui::Rect first = checkui::squareRect(device(), 0, 0, checkers::kLight);
  CHECK(first.y >= toybox::kHeaderHeight + toybox::kRule);
}

void testTheBoardSaysWhoseMoveAndWho() {
  checkui::BoardModel model;
  checkers::start(model.game);
  model.yourTurn = true;

  Rendered mine;
  buildCk<checkui::BoardModel, checkui::buildBoard>(mine, model);
  CHECK(mine.target.drew("YOUR MOVE"));

  model.yourTurn = false;
  Rendered theirs;
  buildCk<checkui::BoardModel, checkui::buildBoard>(theirs, model);
  CHECK(theirs.target.drew("THEIR MOVE"));
  CHECK(!theirs.target.drew("YOUR MOVE"));
}

void testTheResultNamesTheOutcomeFromYourSeat() {
  checkui::ResultModel won;
  won.outcome = checkers::Outcome::LightWins;
  won.seat = checkers::kLight;
  Rendered a;
  buildCk<checkui::ResultModel, checkui::buildResult>(a, won);
  CHECK(a.target.drew("YOU WIN"));

  // The same outcome, from the other seat, must read the other way.
  checkui::ResultModel lost = won;
  lost.seat = checkers::kDarkSeat;
  Rendered b;
  buildCk<checkui::ResultModel, checkui::buildResult>(b, lost);
  CHECK(b.target.drew("THEY WIN"));

  checkui::ResultModel drawn;
  drawn.outcome = checkers::Outcome::Draw;
  Rendered c;
  buildCk<checkui::ResultModel, checkui::buildResult>(c, drawn);
  CHECK(c.target.drew("A DRAW"));
  // And it says why, because a draw nobody understands reads as a bug.
  CHECK(c.target.drew("FORTY MOVES EACH WITH NOTHING TAKEN."));
}

void testTheCheckersHowToPagesAndEnds() {
  // The tutorial shape: the whole page is the button, the tap line says
  // whether another page follows, and the counter lives in the band.
  for (int page = 0; page < checkui::howToPages(); ++page) {
    checkui::HowToModel model;
    model.page = page;
    Rendered out;
    buildCk<checkui::HowToModel, checkui::buildHowTo>(out, model);
    CHECK(out.target.drew(page + 1 < checkui::howToPages() ? "TAP TO CONTINUE" : "TAP TO FINISH"));
    char progress[16];
    std::snprintf(progress, sizeof(progress), "%d OF %d", page + 1, checkui::howToPages());
    CHECK(out.target.drew(progress));
    CHECK(out.tap(240, 300).action == checkui::ActionHowToNext);
  }
}

// --- knucklebones ----------------------------------------------------------

template <typename Model, void (*Build)(toybox::Screen&, const Model&)>
void buildKb(Rendered& out, const Model& model) {
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, device(), noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  Build(screen, model);
}

void testKnucklebonesMenuOffersItsThreeRows() {
  knuckleui::MenuModel model;
  Rendered menu;
  buildKb<knuckleui::MenuModel, knuckleui::buildMenu>(menu, model);

  CHECK(menu.target.drew("KNUCKLEBONES"));
  CHECK(menu.target.drew("PLAY"));
  CHECK(menu.target.drew("PLAY NEARBY"));
  CHECK(menu.target.drew("HOW TO PLAY"));
  CHECK(!menu.interactions.overflowed());

  // The doors anchor to the bottom now, so the first row is found from the
  // content's floor rather than the header -- the same arithmetic the builder
  // uses, exercised from the other end.
  const int listHeight = 3 * toybox::kRowHeight + 2 * (toybox::kGutter / 2) + toybox::kGutter;
  const int firstRowY = 800 - toybox::kMargin - listHeight + toybox::kRowHeight / 2;
  const fui::ActionEvent first = menu.tap(240, firstRowY);
  CHECK(first.action == knuckleui::ActionMenuRow);
  CHECK(first.value == static_cast<int>(knuckleui::MenuRow::Play));

  // The nearby row says who when somebody is there, rather than promising into
  // an empty room a thing the device cannot deliver.
  knuckleui::MenuModel withPeer = model;
  withPeer.nearbyName = "MOP SPECS GRIN";
  Rendered peer;
  buildKb<knuckleui::MenuModel, knuckleui::buildMenu>(peer, withPeer);
  CHECK(peer.target.drew("MOP SPECS GRIN"));
  CHECK(!menu.target.drew("MOP SPECS GRIN"));
}

// The one that matters on a touch device: the column a thumb lands on has to be
// the column the rules receive. Asserted by tapping the drawn target rather
// than by recomputing the geometry, so the test cannot agree with the builder
// by repeating its mistake.
void testTappingAColumnReportsThatColumn() {
  knuckleui::BoardModel model;
  model.die = 4;
  model.yourTurn = true;

  Rendered board;
  buildKb<knuckleui::BoardModel, knuckleui::buildBoard>(board, model);

  for (int column = 0; column < knucklebones::kColumns; ++column) {
    const fui::Rect target = knuckleui::columnRect(device(), column, true);
    const fui::ActionEvent hit =
        board.tap(target.x + target.width / 2, static_cast<int16_t>(target.y + target.height / 2));
    CHECK(hit.action == knuckleui::ActionColumn);
    CHECK(hit.value == column);
  }
}

void testTheBoardOnlyAcceptsAColumnOnYourOwnTurn() {
  knuckleui::BoardModel model;
  model.die = 4;
  model.yourTurn = false;

  Rendered board;
  buildKb<knuckleui::BoardModel, knuckleui::buildBoard>(board, model);

  // Still drawn -- you watch them play, because a board that blanks on their
  // turn makes a slow panel look broken -- but nothing is live.
  CHECK(board.target.drew("THEIR ROLL"));
  for (int column = 0; column < knucklebones::kColumns; ++column) {
    const fui::Rect target = knuckleui::columnRect(device(), column, true);
    CHECK(board.tap(target.x + target.width / 2, static_cast<int16_t>(target.y + target.height / 2)).action !=
          knuckleui::ActionColumn);
  }

  // A full column offers no target either, on your own turn. A tap that does
  // nothing reads, on a panel this slow, as the device having missed it.
  knuckleui::BoardModel filled;
  filled.die = 4;
  filled.yourTurn = true;
  for (int row = 0; row < knucklebones::kRows; ++row) filled.yours.cell[1][row] = 2;

  Rendered some;
  buildKb<knuckleui::BoardModel, knuckleui::buildBoard>(some, filled);
  const fui::Rect fullColumn = knuckleui::columnRect(device(), 1, true);
  CHECK(some.tap(fullColumn.x + fullColumn.width / 2, static_cast<int16_t>(fullColumn.y + fullColumn.height / 2))
            .action != knuckleui::ActionColumn);
  const fui::Rect openColumn = knuckleui::columnRect(device(), 0, true);
  CHECK(some.tap(openColumn.x + openColumn.width / 2, static_cast<int16_t>(openColumn.y + openColumn.height / 2))
            .action == knuckleui::ActionColumn);
}

void testTheBoardFitsThePanel() {
  // The first layout was 12px too tall for the panel and nothing complained:
  // the opponent's column scores drew behind the header band and mine ran off
  // the bottom. Arithmetic that overflows silently is exactly what a test is
  // for, so the extremes of the drawn board are pinned to the screen.
  const fui::Rect theirs = knuckleui::columnRect(device(), 0, false);
  const fui::Rect mine = knuckleui::columnRect(device(), 0, true);
  // Clear of the header band and its rule, so no score can hide under it.
  CHECK(theirs.y >= toybox::kHeaderHeight + toybox::kRule);
  // And clear of the bottom edge. No room is reserved beneath it: both score
  // rows sit on the inner edges beside the strip, which is the fix -- outside,
  // one collided with the header and the other with the panel's bottom.
  CHECK(mine.y + mine.height <= 800);
  // The scores really are between the grids, not outside them.
  CHECK(theirs.y + theirs.height < mine.y);
}

void testTheTwoGridsDoNotOverlap() {
  // They face each other across the strip. If the arithmetic ever puts one on
  // top of the other the dice would draw over each other, and no assertion
  // about text would notice.
  for (int column = 0; column < knucklebones::kColumns; ++column) {
    const fui::Rect mine = knuckleui::columnRect(device(), column, true);
    const fui::Rect theirs = knuckleui::columnRect(device(), column, false);
    CHECK(theirs.y + theirs.height <= mine.y);
    CHECK(mine.y + mine.height <= 800);
    CHECK(theirs.y >= toybox::kHeaderHeight);
  }
}

void testTheResultNamesTheOutcome() {
  knuckleui::ResultModel won;
  won.yourScore = 40;
  won.theirScore = 12;
  Rendered a;
  buildKb<knuckleui::ResultModel, knuckleui::buildResult>(a, won);
  CHECK(a.target.drew("YOU WIN"));
  CHECK(a.target.drew("40 - 12"));

  knuckleui::ResultModel lost;
  lost.yourScore = 12;
  lost.theirScore = 40;
  Rendered b;
  buildKb<knuckleui::ResultModel, knuckleui::buildResult>(b, lost);
  CHECK(b.target.drew("THEY WIN"));

  knuckleui::ResultModel drew;
  drew.yourScore = 20;
  drew.theirScore = 20;
  Rendered c;
  buildKb<knuckleui::ResultModel, knuckleui::buildResult>(c, drew);
  CHECK(c.target.drew("A DRAW"));
}

void testTheHowToEndsOnGotIt() {
  for (int page = 0; page < knuckleui::howToPages(); ++page) {
    knuckleui::HowToModel model;
    model.page = page;
    Rendered out;
    buildKb<knuckleui::HowToModel, knuckleui::buildHowTo>(out, model);
    CHECK(out.target.drew("HOW TO PLAY"));
    // Where you are in the sequence. Without it the only cue is NEXT becoming
    // GOT IT, which arrives too late to be one.
    char progress[8];
    std::snprintf(progress, sizeof(progress), "%d/%d", page + 1, knuckleui::howToPages());
    CHECK(out.target.drew(progress));
    // The last page says so, or a player pages forever looking for the end.
    CHECK(out.target.drew(page + 1 < knuckleui::howToPages() ? "NEXT" : "GOT IT"));
  }
}

// --- minesweeper -----------------------------------------------------------

template <typename Model, void (*Build)(toybox::Screen&, const Model&)>
void buildMs(Rendered& out, const Model& model) {
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, device(), noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  Build(screen, model);
}

// The load-bearing one on a touch device: the cell a thumb lands on must be the
// cell the rules receive.
//
// The grid is hit-tested arithmetically rather than registered as eighty
// buttons -- the interaction buffer holds twenty-four, and a regular grid is
// not what that buffer is for. So this asserts the two directions against each
// other: every cell's drawn rect must map back to that same cell, from all four
// of its corners, and points outside the board must map to nothing.
void testTheCellYouTapIsTheCellTheRulesGet() {
  for (int column = 0; column < minesweeper::kColumns; ++column) {
    for (int row = 0; row < minesweeper::kRows; ++row) {
      const fui::Rect box = mineui::cellRect(device(), column, row);
      const int probes[4][2] = {{box.x, box.y},
                                {box.x + box.width - 1, box.y},
                                {box.x, box.y + box.height - 1},
                                {box.x + box.width - 1, box.y + box.height - 1}};
      for (const auto& probe : probes) {
        int gotColumn = -1;
        int gotRow = -1;
        CHECK(mineui::cellAt(device(), probe[0], probe[1], gotColumn, gotRow));
        CHECK(gotColumn == column);
        CHECK(gotRow == row);
      }
    }
  }

  // Off the board in every direction, including the capsule and the header,
  // which are other people's controls.
  const fui::Rect first = mineui::cellRect(device(), 0, 0);
  const fui::Rect last = mineui::cellRect(device(), minesweeper::kColumns - 1, minesweeper::kRows - 1);
  int c = -1;
  int r = -1;
  CHECK(!mineui::cellAt(device(), first.x - 1, first.y, c, r));
  CHECK(!mineui::cellAt(device(), first.x, first.y - 1, c, r));
  CHECK(!mineui::cellAt(device(), last.x + last.width, last.y, c, r));
  CHECK(!mineui::cellAt(device(), last.x, last.y + last.height, c, r));
  CHECK(!mineui::cellAt(device(), 240, 780, c, r));
}

void testTheMinesweeperBoardFitsThePanel() {
  // Eighty cells, a tool capsule and a counter on one 800px panel. Arithmetic
  // that overflows is silent, so the extremes are pinned.
  const fui::Rect first = mineui::cellRect(device(), 0, 0);
  const fui::Rect last = mineui::cellRect(device(), minesweeper::kColumns - 1, minesweeper::kRows - 1);
  CHECK(first.x >= 0);
  CHECK(first.y >= toybox::kHeaderHeight + toybox::kRule);
  CHECK(last.x + last.width <= 480);
  // Room left under the board for the counter and the tool capsule.
  CHECK(last.y + last.height + toybox::kPillHeight + toybox::kGutter * 2 <= 800);
}

void testTheCounterSaysWhatItCounts() {
  mineui::BoardModel model;
  minesweeper::start(model.game, 5u);
  model.game.status = minesweeper::Status::Playing;

  Rendered out;
  buildMs<mineui::BoardModel, mineui::buildBoard>(out, model);
  // A bare numeral could not say what it counted, and with no total on screen
  // the player could not recover the denominator.
  CHECK(out.target.drew("10 OF 10"));

  minesweeper::toggleFlag(model.game, 0, 0);
  minesweeper::toggleFlag(model.game, 1, 0);
  Rendered flagged;
  buildMs<mineui::BoardModel, mineui::buildBoard>(flagged, model);
  CHECK(flagged.target.drew("8 OF 10"));

  // The tool switch says what a tap will do: the resting mode reads DIG, and
  // only flag mode wears the other word.
  CHECK(out.target.drew("DIG"));
  CHECK(!out.target.drew("FLAG"));
  mineui::BoardModel flagging = model;
  flagging.flagMode = true;
  Rendered mode;
  buildMs<mineui::BoardModel, mineui::buildBoard>(mode, flagging);
  CHECK(mode.target.drew("FLAG"));
}

void testTheBoardStaysWithinItsOwnArea() {
  // The capsule at the bottom belongs to the tool switch, and the header to the
  // device. A grid hit-tested by arithmetic would happily claim both if its
  // bounds were wrong, and nothing on screen would show it.
  int c = -1;
  int r = -1;
  const int stripY = 800 - toybox::kMargin - toybox::kPillHeight / 2;
  CHECK(!mineui::cellAt(device(), 120, stripY, c, r));
  CHECK(!mineui::cellAt(device(), 360, stripY, c, r));
  CHECK(!mineui::cellAt(device(), 240, toybox::kHeaderHeight / 2, c, r));
}

void testTheMinesweeperResultNamesTheOutcome() {
  mineui::ResultModel won;
  won.won = true;
  Rendered a;
  buildMs<mineui::ResultModel, mineui::buildResult>(a, won);
  CHECK(a.target.drew("CLEARED"));
  // The verdict as a sentence, not just a band word: Mario read the old
  // result screen and could not tell whether he had won.
  CHECK(a.target.drew("YOU CLEARED THE FIELD"));

  mineui::ResultModel lost;
  Rendered b;
  buildMs<mineui::ResultModel, mineui::buildResult>(b, lost);
  CHECK(b.target.drew("BOOM"));
  CHECK(b.target.drew("YOU HIT A MINE"));
}

void testTheSettledBoardStaysAndWearsItsVerdict() {
  // The ending is the board: a settled game keeps the minefield on screen and
  // swaps the tool strip for a verdict capsule that doors to the stats. The
  // first version navigated away the tick the game settled, so the finished
  // field -- mines bared -- flashed for under a repaint.
  mineui::BoardModel model;
  minesweeper::start(model.game, 5u);
  model.game.status = minesweeper::Status::Won;
  model.showMines = true;

  Rendered won;
  buildMs<mineui::BoardModel, mineui::buildBoard>(won, model);
  CHECK(won.target.drew("CLEARED"));
  CHECK(!won.target.drew("DIG"));
  CHECK(!won.target.drew("OF 10"));

  // The capsule sits where the strip was, and is the door to the stats.
  const fui::ActionEvent door = won.tap(240, 800 - toybox::kMargin - toybox::kPillHeight / 2);
  CHECK(door.action == mineui::ActionSeeResult);

  model.game.status = minesweeper::Status::Lost;
  Rendered lost;
  buildMs<mineui::BoardModel, mineui::buildBoard>(lost, model);
  CHECK(lost.target.drew("BOOM"));
}

void testTheHowToPagesAndEndsOnGotIt() {
  // Four pages now: the win condition (flags are notes, none are needed) got
  // a page of its own in the art pass.
  CHECK(mineui::howToPages() == 4);
  for (int page = 0; page < mineui::howToPages(); ++page) {
    mineui::HowToModel model;
    model.page = page;
    Rendered out;
    buildMs<mineui::HowToModel, mineui::buildHowTo>(out, model);
    CHECK(out.target.drew("HOW TO PLAY"));
    CHECK(out.target.drew(page + 1 < mineui::howToPages() ? "NEXT" : "GOT IT"));
    // The counter lives in the black band, jaipur's way.
    char progress[16];
    std::snprintf(progress, sizeof(progress), "%d OF %d", page + 1, mineui::howToPages());
    CHECK(out.target.drew(progress));
  }
}

// --- sea salt & paper -------------------------------------------------------

template <typename Model>
fui::Rect buildSs(Rendered& out, fui::Rect (*build)(toybox::Screen&, const Model&), const Model& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  const fui::Rect grid = build(screen, model);
  CHECK(!out.interactions.overflowed());
  return grid;
}

// The card you tap is the card the rules get: every cell the grid draws
// resolves back to its own index, at the centre and at the awkward corner.
void testTheSeaSaltCardYouTapIsTheCardTheRulesGet() {
  for (const int count : {1, 4, 8, 12, 16}) {
    seasaltui::BoardModel model;
    model.tileCount = count;
    for (int i = 0; i < count; ++i) {
      model.tiles[i].kind = static_cast<uint8_t>(i % 14);
      model.tiles[i].colour = static_cast<uint8_t>(i % 11);
    }
    Rendered out;
    const fui::Rect grid = buildSs(out, seasaltui::buildBoard, model);
    for (int i = 0; i < count; ++i) {
      const fui::Rect cell = seasaltui::cardCellRect(grid, i, count);
      CHECK(seasaltui::cardIndexAt(grid, count, static_cast<int16_t>(cell.x + cell.width / 2),
                                   static_cast<int16_t>(cell.y + cell.height / 2)) == i);
      CHECK(seasaltui::cardIndexAt(grid, count, cell.x, cell.y) == i);
      CHECK(seasaltui::cardIndexAt(grid, count, static_cast<int16_t>(cell.x + cell.width - 1),
                                   static_cast<int16_t>(cell.y + cell.height - 1)) == i);
      // And the whole grid stays inside the rect the builder returned.
      CHECK(cell.y + cell.height <= grid.y + grid.height);
    }
    // The gap between cards belongs to nobody.
    if (count >= 2) {
      const fui::Rect first = seasaltui::cardCellRect(grid, 0, count);
      CHECK(seasaltui::cardIndexAt(grid, count, static_cast<int16_t>(first.x + first.width + 3), first.y) != 0);
    }
  }
}

void testTheSeaSaltChromeIsTappableAndTheCallPillIsEarned() {
  seasaltui::BoardModel model;
  model.tab = 0;
  model.canCall = false;
  model.primaryLabel = "END TURN";
  model.primaryEnabled = true;
  Rendered out;
  buildSs(out, seasaltui::buildBoard, model);

  // The three tabs, the deck and both piles all resolve to their actions.
  bool sawTab = false, sawDeck = false, sawPile = false, sawCall = false;
  for (int16_t y = 0; y < 800; y += 7) {
    for (int16_t x = 0; x < 480; x += 7) {
      const fui::ActionId a = out.tap(x, y).action;
      sawTab |= a == seasaltui::ActionTabYours;
      sawDeck |= a == seasaltui::ActionDeck;
      sawPile |= a == seasaltui::ActionPileA;
      sawCall |= a == seasaltui::ActionCall;
    }
  }
  CHECK(sawTab);
  CHECK(sawDeck);
  CHECK(sawPile);
  CHECK(!sawCall);  // no call pill below 7 points

  // With the call earned, the pill exists and says the points.
  model.canCall = true;
  model.callPoints = 10;
  Rendered earned;
  buildSs(earned, seasaltui::buildBoard, model);
  CHECK(earned.target.drew("10 - CALL IT"));
  bool callNow = false;
  for (int16_t x = 0; x < 480; x += 5) {
    callNow |= earned.tap(x, 780).action == seasaltui::ActionCall;
  }
  CHECK(callNow);
}

void testTheSeaSaltCallChoiceSaysWhatEachWordCosts() {
  seasaltui::CallModel model;
  model.yourPoints = 11;
  Rendered out;
  buildSs(out, seasaltui::buildCallChoice, model);
  CHECK(out.target.drew("STOP"));
  CHECK(out.target.drew("LAST CHANCE"));
  CHECK(out.target.drew("11 PTS"));
  bool stop = false, bet = false;
  for (int16_t y = 0; y < 800; y += 7) {
    const fui::ActionId a = out.tap(240, y).action;
    stop |= a == seasaltui::ActionStop;
    bet |= a == seasaltui::ActionLastChance;
  }
  CHECK(stop);
  CHECK(bet);
}

void testTheSeaSaltRoundOverNamesTheBet() {
  seasaltui::RoundModel model;
  model.wasLastChance = true;
  model.youCalled = true;
  model.betWon = true;
  model.yourCards = 12;
  model.yourBonus = 4;
  model.yourBanked = 16;
  model.theirBanked = 2;
  Rendered out;
  buildSs(out, seasaltui::buildRoundOver, model);
  CHECK(out.target.drew("YOUR BET CAME OFF."));
  CHECK(out.target.drew("NEXT ROUND"));

  seasaltui::RoundModel dry;
  dry.deckOut = true;
  Rendered out2;
  buildSs(out2, seasaltui::buildRoundOver, dry);
  CHECK(out2.target.drew("THE DECK RAN OUT. NOBODY SCORES."));
}

// Every hint must fit the hint box: the split lines run ~9.5 device px per
// character on the small face, and the box's inner width holds 46. This is
// the check that would have caught "PLAYING THEM BUYS ANOTHER TURN" running
// off the panel before Mario did.

// The card's bands must stay apart at EVERY height the grid can hand out, not
// just the 125 the constants were once tuned for. A three-row hand gets 121,
// and at 121 the old fixed offsets printed the name through the supply mark --
// which is what Mario caught on the shot that was about to become the site's.
void testTheSeaSaltCardBandsNeverCollide() {
  for (const int count : {1, 2, 4, 8, 12, 16}) {
    seasaltui::BoardModel model;
    model.tileCount = count;
    for (int i = 0; i < count; ++i) {
      model.tiles[i].kind = static_cast<uint8_t>(i % 14);
      model.tiles[i].colour = static_cast<uint8_t>(i % 11);
      model.tiles[i].supply = 9;
    }
    Rendered out;
    const fui::Rect grid = buildSs(out, seasaltui::buildBoard, model);
    const fui::Rect cell = seasaltui::cardCellRect(grid, 0, count);

    // Every text the card drew, top to bottom, must be disjoint and inside it.
    //
    // Measured as INK, not as the rect handed to the target. Those are the same
    // thing only while a band is at least a line box tall; the short ones go
    // through toybox::inkCentred, whose rect is deliberately taller than the
    // band and would read here as an overlap that no reader can see.
    std::vector<fui::Rect> lines;
    for (const auto& drawn : out.target.texts) {
      if (drawn.rect.x < cell.x || drawn.rect.x >= cell.x + cell.width) continue;
      if (drawn.rect.y < cell.y || drawn.rect.y >= cell.y + cell.height) continue;
      lines.push_back(inkBandOf(drawn));
    }
    for (size_t i = 0; i < lines.size(); ++i) {
      CHECK(lines[i].y + lines[i].height <= cell.y + cell.height);
      for (size_t j = i + 1; j < lines.size(); ++j) {
        const bool disjoint = lines[i].y + lines[i].height <= lines[j].y ||
                              lines[j].y + lines[j].height <= lines[i].y || lines[i].x + lines[i].width <= lines[j].x ||
                              lines[j].x + lines[j].width <= lines[i].x;
        CHECK(disjoint);
      }
    }
  }
}

void testEverySeaSaltHintFitsTheBox() {
  constexpr int kMaxLine = 46;
  auto worstLine = [](const char* text) {
    int worst = 0, run = 0;
    for (const char* at = text; *at; ++at) {
      if (*at == '.' && at[1] == ' ') {
        run += 1;  // the period stays on the line
        if (run > worst) worst = run;
        run = 0;
        ++at;  // skip the space
        continue;
      }
      ++run;
    }
    if (run > worst) worst = run;
    return worst;
  };
  for (int k = 0; k < 14; ++k) CHECK(worstLine(seasaltui::kindHint(k)) <= kMaxLine);
  for (int k = 0; k < 5; ++k) CHECK(worstLine(seasaltui::pairHint(k)) <= kMaxLine);
}

void testTheSeaSaltTutorialPagesAndEnds() {
  for (int page = 0; page < seasaltui::tutorialPages(); ++page) {
    seasaltui::TutorialModel model;
    model.page = page;
    Rendered out;
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    seasaltui::buildTutorial(screen, model);
    CHECK(!out.interactions.overflowed());
    CHECK(out.tap(240, 400).action == seasaltui::ActionAdvance);
  }
}

void testTheMinesweeperMenuLeadsWithTheRecord() {
  // The front door in the band order: record line on top, the last field with
  // its verdict as the ornament, doors anchored to the floor with PLAY first.
  mineui::MenuModel model;
  model.hasHistory = true;
  model.wins = 12;
  model.losses = 5;
  minesweeper::start(model.lastBoard, 9u);
  model.lastBoard.cell[3][4] |= minesweeper::kMine | minesweeper::kRevealed;

  Rendered out;
  buildMs<mineui::MenuModel, mineui::buildMenu>(out, model);
  CHECK(out.target.drew("17 PLAYED   12 CLEARED"));
  CHECK(out.target.drew("LAST GAME: BOOM"));
  CHECK(out.target.drew("HOW TO PLAY"));
  CHECK(!out.interactions.overflowed());

  // The same floor arithmetic the builder uses, exercised from the other end.
  const int listHeight = 2 * toybox::kRowHeight + toybox::kGutter / 2 + toybox::kGutter;
  const int firstRowY = 800 - toybox::kMargin - listHeight + toybox::kRowHeight / 2;
  const fui::ActionEvent first = out.tap(240, firstRowY);
  CHECK(first.action == mineui::ActionMenuRow);
  CHECK(first.value == static_cast<int>(mineui::MenuRow::Play));

  // A mine that was never dug reads CLEARED instead.
  mineui::MenuModel won = model;
  minesweeper::start(won.lastBoard, 9u);
  won.lastBoard.cell[3][4] |= minesweeper::kMine;
  Rendered cleared;
  buildMs<mineui::MenuModel, mineui::buildMenu>(cleared, won);
  CHECK(cleared.target.drew("LAST GAME: CLEARED"));
}

// --- toy battle -------------------------------------------------------------

// The rack holds eight TROOPS, not eight kinds. Drawing two used to add one
// tile whenever one of the two was a duplicate, and the second lived as a 4px
// pip nobody could see -- which is how Mario found it: "I drew two and only got
// one."
void testTheRackShowsEveryTroopYouHold() {
  toybattle::Game game;
  game.newGame(4242u, static_cast<int>(toybattle::TerrainId::CastleField), 0);

  // Force the case that broke: two of the same kind, plus one other.
  for (int k = 0; k < toybattle::kTroopKinds; ++k) game.rack[0][k] = 0;
  game.rack[0][static_cast<int>(toybattle::Troop::Skully)] = 2;
  game.rack[0][static_cast<int>(toybattle::Troop::Roxy)] = 1;

  int filled = 0;
  int seen[toybattle::kTroopKinds] = {};
  for (int position = 0; position < toybattle::kTroopKinds; ++position) {
    const int kind = tbui::handKindAt(game, 0, position);
    if (kind < 0) continue;
    ++filled;
    ++seen[kind];
  }
  CHECK(filled == 3);
  CHECK(seen[static_cast<int>(toybattle::Troop::Skully)] == 2);
  CHECK(seen[static_cast<int>(toybattle::Troop::Roxy)] == 1);

  // And the count of occupied slots tracks the rack exactly, at every size a
  // hand can be, so a draw always shows up.
  for (int k = 0; k < toybattle::kTroopKinds; ++k) game.rack[0][k] = 0;
  for (int total = 0; total <= toybattle::kRackLimit; ++total) {
    for (int k = 0; k < toybattle::kTroopKinds; ++k) game.rack[0][k] = 0;
    int left = total;
    for (int k = 0; k < toybattle::kTroopKinds && left > 0; ++k) {
      const int take = left > toybattle::kCopiesEach ? toybattle::kCopiesEach : left;
      game.rack[0][k] = static_cast<uint8_t>(take);
      left -= take;
    }
    int occupied = 0;
    for (int position = 0; position < toybattle::kTroopKinds; ++position) {
      if (tbui::handKindAt(game, 0, position) >= 0) ++occupied;
    }
    CHECK(occupied == game.rackSize(0));
  }
}

// A tap has to land on the troop that was drawn there, duplicates included.
void testTheRackTileYouTapIsTheTroopYouGet() {
  toybattle::Game game;
  game.newGame(7u, static_cast<int>(toybattle::TerrainId::CastleField), 0);
  for (int k = 0; k < toybattle::kTroopKinds; ++k) game.rack[0][k] = 0;
  game.rack[0][static_cast<int>(toybattle::Troop::Capn)] = 3;
  game.rack[0][static_cast<int>(toybattle::Troop::Star)] = 1;

  for (int position = 0; position < toybattle::kTroopKinds; ++position) {
    const fui::Rect tile = tbui::rackTile(device(), position);
    const int expected = tbui::handKindAt(game, 0, position);
    const int probes[4][2] = {
        {tile.x + 4, tile.y + 4},
        {tile.x + tile.width - 5, tile.y + 4},
        {tile.x + 4, tile.y + tile.height - 5},
        {tile.x + tile.width / 2, tile.y + tile.height / 2},
    };
    for (const auto& probe : probes) {
      CHECK(tbui::rackAt(device(), game, kFreshDraft, 0, probe[0], probe[1]) == expected);
    }
  }
  {
    // EVERY OPEN QUESTION MUST BE ANSWERABLE FROM THE SCREEN.
    //
    // Cursed Cemetery asks "RAISE ONE FROM THE DISCARD?" and, until Mario hit
    // it in a real game on 2026-08-11, offered nothing that could answer it:
    // candidateTroops returns 0 unless the ask is Troop, candidateSlots does
    // not handle ExhumeKind, and the rack row drew the HAND. SKIP and BACK
    // were the only tappable things on the screen.
    //
    // The flow fuzzer missed it because it calls answerTarget on the model
    // directly. The model was always fine. It answered a question the UI never
    // let a human answer, which is the shape of bug a test driving the model
    // cannot see, so this one goes through the SCREEN's own hit test.
    toybattle::Game g;
    g.newGame(7u, static_cast<int>(toybattle::TerrainId::CursedCemetery), 0, true);
    const toybattle::Terrain& b = g.board();

    // A reachable grave. No grave on this board touches an H.Q., so walk to
    // the nearest one and hold every base on the way: the placement is then
    // legal for the ordinary connection reason and nothing is special-cased.
    int hq = -1;
    for (int slot = b.baseCount; slot < b.slotCount(); ++slot) {
      if (b.hqSeat[slot - b.baseCount] == 0) hq = slot;
    }
    CHECK(hq >= 0);

    int parent[toybattle::kMaxSlots];
    for (int i = 0; i < toybattle::kMaxSlots; ++i) parent[i] = -2;
    int queue[toybattle::kMaxSlots];
    int head = 0, tail = 0;
    queue[tail++] = hq;
    parent[hq] = -1;
    int grave = -1;
    while (head < tail && grave < 0) {
      const int at = queue[head++];
      for (int next = 0; next < b.baseCount; ++next) {
        if (parent[next] != -2) continue;
        if (!(b.adj[at] & (uint64_t{1} << next))) continue;
        parent[next] = at;
        queue[tail++] = next;
        if (b.specialAt(next) == toybattle::Special::Exhume) {
          grave = next;
          break;
        }
      }
    }
    CHECK(grave >= 0);

    // Hold everything between the H.Q. and the grave, but not the grave.
    for (int at = parent[grave]; at >= 0 && at < b.baseCount; at = parent[at]) {
      g.placeSlot[g.placementCount] = static_cast<uint8_t>(at);
      g.placeTile[g.placementCount] = static_cast<uint8_t>((0 << 3) | static_cast<int>(toybattle::Troop::Roxy));
      ++g.placementCount;
    }

    g.discarded[0][static_cast<int>(toybattle::Troop::Jumbo)] = 1;
    g.discarded[0][static_cast<int>(toybattle::Troop::Star)] = 1;
    for (int k = 0; k < toybattle::kTroopKinds; ++k) g.rack[0][k] = 0;
    // Roxy, because it is the one troop with no effect of its own: the base is
    // then the only thing left to ask about.
    g.rack[0][static_cast<int>(toybattle::Troop::Skully)] = 1;
    g.rack[0][static_cast<int>(toybattle::Troop::Roxy)] = 1;

    toybattle::Draft d{};
    CHECK(toybattle::answerTroop(g, d, toybattle::Troop::Roxy));
    CHECK(toybattle::answerSlot(g, d, grave));
    CHECK(toybattle::pending(g, d) == toybattle::Ask::ExhumeKind);

    // The row must now be the DISCARD, and every troop in it must be reachable
    // by a tap. Two in the discard means two tiles, and they must be the two
    // kinds that are actually there.
    bool sawJumbo = false, sawStar = false;
    int offered = 0;
    for (int position = 0; position < toybattle::kTroopKinds; ++position) {
      const fui::Rect tile = tbui::rackTile(device(), position);
      const int kind = tbui::rackAt(device(), g, d, 0, tile.x + tile.width / 2, tile.y + tile.height / 2);
      if (kind < 0) continue;
      ++offered;
      if (kind == static_cast<int>(toybattle::Troop::Jumbo)) sawJumbo = true;
      if (kind == static_cast<int>(toybattle::Troop::Star)) sawStar = true;
      // And the model must accept exactly what the screen offered.
      toybattle::Draft probe = d;
      CHECK(toybattle::answerTarget(g, probe, kind));
    }
    CHECK(offered == 2);
    CHECK(sawJumbo);
    CHECK(sawStar);

    // The hand is NOT what is on the row while the question is open: Skully is
    // held but is not in the discard, so it must not be tappable here.
    bool sawSkully = false;
    for (int position = 0; position < toybattle::kTroopKinds; ++position) {
      const fui::Rect tile = tbui::rackTile(device(), position);
      const int kind = tbui::rackAt(device(), g, d, 0, tile.x + tile.width / 2, tile.y + tile.height / 2);
      if (kind == static_cast<int>(toybattle::Troop::Skully)) sawSkully = true;
    }
    CHECK(!sawSkully);
  }

  {
    // EVERY QUESTION THE GAME ASKS MUST BE ANSWERABLE FROM THE SCREEN.
    //
    // The Exhume bug was one instance of a class: the model opens a question
    // and the screen offers nothing that can answer it. Checking that class by
    // reading the code is exactly what missed it, so this walks real games and
    // checks every question it actually meets.
    //
    // The check is "something the model ACCEPTS", not "something is tappable".
    // The weaker version passed with the original bug in place, because the row
    // still drew the hand and a tile that only earns a refusal looks tappable.
    int seen[16] = {};
    int deadEnds = 0;
    const int boards[] = {
        static_cast<int>(toybattle::TerrainId::CursedCemetery), static_cast<int>(toybattle::TerrainId::CastleField),
        static_cast<int>(toybattle::TerrainId::VolcanicJungle), static_cast<int>(toybattle::TerrainId::Battlefield),
        static_cast<int>(toybattle::TerrainId::CityOfClouds),
    };
    uint32_t rng = 0x2026u;
    auto next = [&rng]() {
      rng = rng * 1664525u + 1013904223u;
      return rng >> 16;
    };

    for (int bi = 0; bi < 5; ++bi) {
      for (int gameNo = 0; gameNo < 120; ++gameNo) {
        toybattle::Game g;
        g.newGame(next() | 1u, boards[bi], static_cast<int>(next() & 1u), true);
        toybattle::Draft d{};
        for (int step = 0; step < 400 && g.currentPhase() == toybattle::Phase::Playing; ++step) {
          const toybattle::Ask ask = toybattle::pending(g, d);
          const int idx = static_cast<int>(ask);
          if (idx < 16) ++seen[idx];
          const bool yesNo = ask == toybattle::Ask::DrawOffer || ask == toybattle::Ask::StealOffer ||
                             ask == toybattle::Ask::ChainOffer || ask == toybattle::Ask::BaseOffer;

          if (ask != toybattle::Ask::Ready && !yesNo) {
            bool answerable = toybattle::candidateSlots(g, d) != 0;
            // An empty rack is not a dead end: DRAW 2 is the answer, and the
            // capsule always draws it.
            if (ask == toybattle::Ask::Troop && g.canDraw(g.turn)) answerable = true;
            for (int pos = 0; pos < toybattle::kTroopKinds && !answerable; ++pos) {
              const fui::Rect tl = tbui::rackTile(device(), pos);
              const int kind = tbui::rackAt(device(), g, d, g.turn, tl.x + tl.width / 2, tl.y + tl.height / 2);
              if (kind < 0) continue;
              toybattle::Draft probe = d;
              if (ask == toybattle::Ask::Troop ? toybattle::answerTroop(g, probe, static_cast<toybattle::Troop>(kind))
                                               : toybattle::answerTarget(g, probe, kind)) {
                answerable = true;
              }
            }
            if (!answerable) ++deadEnds;
          }

          bool moved = false;
          if (ask == toybattle::Ask::Ready) {
            moved = g.apply(d.move);
            d = kFreshDraft;
          } else if (yesNo) {
            moved = toybattle::answerOffer(g, d, (next() & 1u) != 0);
          } else if (ask == toybattle::Ask::Troop) {
            const int spin = static_cast<int>(next() % toybattle::kTroopKinds);
            for (int i = 0; i < toybattle::kTroopKinds && !moved; ++i)
              moved = toybattle::answerTroop(g, d, static_cast<toybattle::Troop>((spin + i) % toybattle::kTroopKinds));
          } else if (ask == toybattle::Ask::ExhumeKind) {
            for (int k = 0; k < toybattle::kTroopKinds && !moved; ++k) moved = toybattle::answerTarget(g, d, k);
          } else {
            // From a random offset: always taking the lowest index meant the
            // walk never landed on Castle Field's wells, so RecallFrom was
            // never met and the count said nothing about it.
            const uint64_t mask = toybattle::candidateSlots(g, d);
            const int spin = static_cast<int>(next() % 64u);
            for (int i = 0; i < 64 && !moved; ++i) {
              const int slot = (spin + i) % 64;
              if (!(mask & (uint64_t{1} << slot))) continue;
              moved =
                  ask == toybattle::Ask::Slot ? toybattle::answerSlot(g, d, slot) : toybattle::answerTarget(g, d, slot);
            }
          }
          if (!moved) {
            if (!g.apply(toybattle::Move::draw())) break;
            d = kFreshDraft;
          }
        }
      }
    }

    CHECK(deadEnds == 0);
    // The walk has to have MET these, or the zero above is a zero about nothing.
    CHECK(seen[static_cast<int>(toybattle::Ask::Troop)] > 0);
    CHECK(seen[static_cast<int>(toybattle::Ask::Slot)] > 0);
    CHECK(seen[static_cast<int>(toybattle::Ask::ExhumeKind)] > 0);
    CHECK(seen[static_cast<int>(toybattle::Ask::RecallFrom)] > 0);
    CHECK(seen[static_cast<int>(toybattle::Ask::ShoveFrom)] > 0);
    // The Cap'n chain used to be excluded here by name, because inside one the
    // Exhume question offered troops an earlier grave had already taken and
    // then refused the answer. Fixed by counting what the chain has claimed
    // (discardLeft), so the exclusion is gone and the general check covers it.
    CHECK(seen[static_cast<int>(toybattle::Ask::ChainOffer)] > 0);
  }

  // Neighbouring tiles never share a pixel.
  for (int position = 0; position + 1 < toybattle::kTroopKinds; ++position) {
    const fui::Rect a = tbui::rackTile(device(), position);
    const fui::Rect b = tbui::rackTile(device(), position + 1);
    CHECK(a.x + a.width == b.x);
  }
}

// --- toy battle: the shell -------------------------------------------------

void buildTbMenu(Rendered& out, const tbui::MenuModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  tbui::buildMenu(screen, model);
}

void buildTbSetup(Rendered& out, const tbui::SetupModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  tbui::buildSetup(screen, model);
}

void buildTbMaps(Rendered& out, const tbui::MapPickModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  tbui::buildMapPick(screen, model);
}

void buildTbBrief(Rendered& out, const tbui::BriefModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  tbui::buildBrief(screen, model);
}

void buildTbHowTo(Rendered& out, const tbui::HowToModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  tbui::buildHowTo(screen, model);
}

void buildTbBoard(Rendered& out, const tbui::BoardModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  tbui::buildBoard(screen, model);
}

// Counts rack tiles filled with a given dither level, by matching the fill rect
// against the geometry the rack itself computes.
int rackTilesPainted(const Rendered& out, const fui::Color shade) {
  int found = 0;
  for (size_t i = 0; i < out.target.fills.size(); ++i) {
    const fui::Paint& paint = out.target.fillPaints[i];
    if (paint.kind != fui::PaintKind::Dither || paint.color != shade) continue;
    for (int position = 0; position < 8; ++position) {
      const fui::Rect tile = tbui::rackTile(device(), position);
      const fui::Rect& r = out.target.fills[i];
      if (r.x > tile.x - 6 && r.x < tile.x + 6 && r.y > tile.y - 6 && r.y < tile.y + 6) ++found;
    }
  }
  return found;
}

void testAFrozenCardLooksDifferent() {
  // Battlefield points at a troop on your rack without looking, and it sits out
  // your turn. That is a state done TO you, so it cannot look the same as "there
  // is nowhere legal to put this" -- and it is not something a screenshot of an
  // ordinary game will contain, so it is asserted here instead.
  toybattle::Game game;
  game.newGame(31u, static_cast<int>(toybattle::TerrainId::Battlefield), 0, true);

  int held = -1;
  for (int position = 0; position < 8 && held < 0; ++position) held = tbui::handKindAt(game, 0, position);
  CHECK(held >= 0);

  tbui::BoardModel model;
  model.game = game;
  model.seat = 0;
  model.yourTurn = true;
  model.prompt = "";
  model.canDraw = true;

  Rendered plain;
  buildTbBoard(plain, model);
  const int darkBefore = rackTilesPainted(plain, fui::Color::DarkGray);

  model.game.frozenKind[0] = static_cast<uint8_t>(held);
  Rendered frozen;
  buildTbBoard(frozen, model);
  const int darkAfter = rackTilesPainted(frozen, fui::Color::DarkGray);

  // Nothing on the rack wears the dark dither until a troop is frozen, and then
  // exactly one does.
  CHECK(darkBefore == 0);
  CHECK(darkAfter == 1);
  // And it is still a troop you are holding: freezing must not remove it.
  CHECK(toybattle::whyNotTroop(model.game, tbui::BoardModel{}.draft, static_cast<toybattle::Troop>(held)) ==
        toybattle::Refusal::Pinned);
}

void testToyBattleShell() {
  // The row set shifts rather than leaving a hole, so no index ever names a row
  // that is not on the screen.
  tbui::MenuModel bare;
  CHECK(tbui::shellRowCount(bare) == static_cast<int>(tbui::ShellRow::Count) - 1);
  CHECK(tbui::shellRowAt(bare, 0) == tbui::ShellRow::Play);
  tbui::MenuModel saved;
  saved.hasSave = true;
  CHECK(tbui::shellRowAt(saved, 0) == tbui::ShellRow::Continue);
  for (int i = -2; i < 8; ++i) {
    CHECK(tbui::shellRowAt(bare, i) != tbui::ShellRow::Count);
    CHECK(tbui::shellRowAt(saved, i) != tbui::ShellRow::Count);
  }

  toybattle::Game preview;
  preview.newGame(7u, 0, 0, true);

  for (int save = 0; save < 2; ++save) {
    tbui::MenuModel model;
    model.hasSave = save != 0;
    model.saveDetail = "2-1";
    model.preview = &preview;
    model.played = 3;
    model.won = 2;
    Rendered out;
    buildTbMenu(out, model);
    // The 24-rect ceiling. Past it a control draws and registers nothing, which
    // looks exactly like a control that works.
    CHECK(out.interactions.count() <= toybox::kMaxInteractions);
    CHECK(out.interactions.count() > 0);
    CHECK(out.target.drew("PLAY NEARBY"));
    CHECK(out.target.drew("HOW TO PLAY"));
    // A save that is offered has to say what it is offering.
    CHECK(out.target.drew("CONTINUE") == (save != 0));
  }

  for (int link = 0; link < 2; ++link) {
    tbui::SetupModel model;
    model.forLink = link != 0;
    model.selected = 0;
    Rendered out;
    buildTbSetup(out, model);
    CHECK(out.interactions.count() <= toybox::kMaxInteractions);
    CHECK(out.target.drew("START"));
    // Against a person there is no difficulty to choose, and the row that would
    // set one must not be on the screen at all.
    //
    // Asks for whatever rung the model actually holds, not for "SERGEANT".
    // This said SERGEANT until 2026-08-11 and broke the moment the default
    // moved to GENERAL -- it was testing the default's NAME while meaning
    // "the difficulty row is present", so it failed for a change it had no
    // opinion about.
    CHECK(out.target.drew(tbui::skillName(model.options.skill)) == (link == 0));
  }

  {
    // Every map has to be REACHABLE, which is not the same as every map being
    // drawn: the list held five at a fixed card height and silently dropped the
    // sixth. Walk the pages and require the whole table to turn up across them.
    CHECK(tbui::mapsPerPage() >= 1);
    CHECK(tbui::mapPages() * tbui::mapsPerPage() >= toybattle::kPlayableTerrainCount);
    for (int n = 0; n < toybattle::kPlayableTerrainCount; ++n) {
      const int i = toybattle::playableTerrainAt(n);
      bool found = false;
      for (int page = 0; page < tbui::mapPages() && !found; ++page) {
        tbui::MapPickModel model;
        model.page = page;
        Rendered out;
        buildTbMaps(out, model);
        CHECK(out.interactions.count() <= toybox::kMaxInteractions);
        found = out.target.drew(toybattle::terrainAt(i).name);
      }
      CHECK(found);
    }
    // The other direction, and the one that rots silently: PROVING GROUND is
    // ours and must never appear beside nine real boards. Without this, any
    // future off-by-one in the offset puts it back and every check above still
    // passes, because they only ever ask whether the real maps are present.
    for (int page = 0; page < tbui::mapPages(); ++page) {
      tbui::MapPickModel model;
      model.page = page;
      Rendered out;
      buildTbMaps(out, model);
      // BY NAME. This said terrainAt(0) and passed while the picker was
      // hiding the wrong board, because terrainAt(0) is Castle Field.
      CHECK(!out.target.drew("PROVING GROUND"));
    }
    // And nothing on the page is inverted, because a picker has no cursor.
    tbui::MapPickModel first;
    Rendered out;
    buildTbMaps(out, first);
    CHECK(rackTilesPainted(out, fui::Color::DarkGray) == 0);
  }

  for (int page = 0; page < tbui::howToPages(); ++page) {
    tbui::HowToModel model;
    model.page = page;
    Rendered out;
    buildTbHowTo(out, model);
    CHECK(out.interactions.count() <= toybox::kMaxInteractions);
    // Every page has a way forward. The first version of this in another game
    // put NEXT after three early-returning branches, so two of three pages had
    // none.
    CHECK(out.interactions.count() > 0);
    CHECK(out.target.drew(page + 1 == tbui::howToPages() ? "PLAY" : "NEXT"));
  }
}

// A single-line run wider than the rect it was given is a silent truncation:
// the SDK ellipsizes, draws, and logs nothing, so it looks exactly like text
// that fits. That is what the terrain card did to "IN THE BAR: TILE = TROOPS IN
// HAND, TRIANGLE = LEFT TO DRAW, CROSS = OUT OF THE GAME. TOP ROW IS THEIRS."
// for as long as the card existed -- 110 characters into a 448px row -- and
// what the header did to CURSED CEMETERY, which came out as CURSED CEMETER.
//
// Checked over every playable terrain, because the card is per-map and only the
// two maps with the longest names and the most special kinds ever showed it.
void testTheTerrainCardNeverTruncatesWhatItDraws() {
  for (int nth = 0; nth < toybattle::kPlayableTerrainCount; ++nth) {
    const int index = toybattle::playableTerrainAt(nth);
    const toybattle::Terrain& terrain = toybattle::terrainAt(index);
    for (int special = 0; special < 2; ++special) {
      tbui::BriefModel model;
      model.board = &terrain;
      model.specialBases = special != 0;
      Rendered out;
      buildTbBrief(out, model);
      for (const auto& run : out.target.texts) {
        if (run.style.maxLines != 1) continue;
        const fui::Size size = out.target.measureText(run.style.font, run.text.c_str(), run.style);
        CHECK(size.width <= run.rect.width);
      }
      // And the card itself fits the panel: the special-base list grows with
      // the map, and the troop list under it was already within one row of the
      // bottom edge on the four-kind maps.
      for (const auto& run : out.target.texts) {
        CHECK(run.rect.y >= 0);
        CHECK(run.rect.bottom() <= 800);
      }
    }
  }
}

// Nothing any rules page draws may land on the buttons, at any line height.
//
// The line height is the point. The old deck reserved a flat 132px for its
// caption, which holds four lines of the 20px cell this fake target used to
// have and three of the 45px cell the device actually renders -- so the suite
// was green while SPECIAL BASES drew its fourth line straight through PREV and
// PLAY. A layout that survives 20, 45 and 60 is one that measured rather than
// guessed.
void testNoRulesPageDrawsOverItsOwnButtons() {
  constexpr int16_t kActionTop = 800 - toybox::kMargin - toybox::kPillHeight;
  const int16_t heights[] = {20, 45, 60};
  for (const int16_t lineH : heights) {
    for (int page = 0; page < tbui::howToPages(); ++page) {
      tbui::HowToModel model;
      model.page = page;
      Rendered out;
      out.target.lineH = lineH;
      buildTbHowTo(out, model);
      CHECK(out.interactions.count() <= toybox::kMaxInteractions);
      // Every page still has a way forward, whatever the metric.
      CHECK(out.target.drew(page + 1 == tbui::howToPages() ? "PLAY" : "NEXT"));
      for (const auto& run : out.target.texts) {
        // The pill labels live in the action band by definition.
        if (run.text == "BACK" || run.text == "PREV" || run.text == "NEXT" || run.text == "PLAY") continue;
        CHECK(run.rect.bottom() <= kActionTop);
        CHECK(run.rect.y >= 0);
        CHECK(run.rect.x >= 0);
        CHECK(run.rect.right() <= 480);
      }
      // Pictures too: a spotlight bracket or a verdict mark hangs outside the
      // node it belongs to, and the inset has to cover the widest of them.
      for (const auto& rect : out.target.fills) {
        CHECK(rect.x >= 0);
        CHECK(rect.y >= 0);
        CHECK(rect.right() <= 480);
        CHECK(rect.bottom() <= 800);
      }
    }
  }

  // The ? card is one screen carrying two lists, and the one that varies is the
  // map's. Every playable board, at every metric: nothing off the panel.
  for (const int16_t lineH : heights) {
    for (int nth = 0; nth < toybattle::kPlayableTerrainCount; ++nth) {
      tbui::BriefModel model;
      model.board = &toybattle::terrainAt(toybattle::playableTerrainAt(nth));
      Rendered out;
      out.target.lineH = lineH;
      buildTbBrief(out, model);
      CHECK(out.interactions.count() <= toybox::kMaxInteractions);
      for (const auto& run : out.target.texts) {
        CHECK(run.rect.bottom() <= 800);
        CHECK(run.rect.right() <= 480);
        CHECK(run.rect.y >= 0);
      }
      // And every troop is still on it: a budget that silently drops the tail
      // of the list looks exactly like a list that fits.
      for (int k = 0; k < toybattle::kTroopKinds; ++k) {
        CHECK(out.target.drew(tbui::troopBlurb(static_cast<toybattle::Troop>(k))));
      }
    }
  }
}

// Every troop the rules deck draws has to be one that could be standing there.
//
// The pages are hand-authored, so nothing in the drawing enforces it, and they
// did not hold: COVERING put two enemy troops on bases with no walk back to
// their own H.Q. -- the exact rule the page after it teaches. A deck that
// breaks the game's rules in its own illustrations teaches them wrong.
//
// The walk is Game::reachable's: start from your H.Q., grow only through bases
// you hold. An H.Q. is the starting point and never a stepping stone. The one
// page about a base that has LOST its walk home marks that base with a cross,
// and those are the only exceptions allowed.
void testEveryRulesPositionCouldExist() {
  const int nodes = tbui::howToNodeCount();
  for (int page = 0; page < tbui::howToPages(); ++page) {
    for (int seat = 0; seat < 2; ++seat) {
      bool reached[16] = {};
      // Seed from this seat's H.Q.
      for (int n = 0; n < nodes; ++n) {
        if (!tbui::howToIsHq(n) || tbui::howToHqSeat(n) != seat) continue;
        for (int e = 0; e < tbui::howToLinkCount(); ++e) {
          int a = 0, b = 0;
          tbui::howToLinkAt(e, a, b);
          if (a == n) reached[b] = true;
          if (b == n) reached[a] = true;
        }
      }
      for (bool grew = true; grew;) {
        grew = false;
        for (int n = 0; n < nodes; ++n) {
          if (!reached[n] || tbui::howToIsHq(n)) continue;
          if (tbui::howToOwnerAt(page, n) != seat) continue;
          for (int e = 0; e < tbui::howToLinkCount(); ++e) {
            int a = 0, b = 0;
            tbui::howToLinkAt(e, a, b);
            const int other = a == n ? b : (b == n ? a : -1);
            if (other >= 0 && !reached[other]) {
              reached[other] = true;
              grew = true;
            }
          }
        }
      }
      for (int n = 0; n < nodes; ++n) {
        if (tbui::howToIsHq(n)) continue;
        if (tbui::howToOwnerAt(page, n) != seat) continue;
        if (tbui::howToCutOff(page, n)) continue;  // the page about losing the walk
        CHECK(reached[n]);
      }
    }
  }
}

// The board stays up when the game ends, so it has to carry the ending itself:
// the verdict is in the hint line, the way on is in the action bar, and the
// reason is marked where it happened. Mario, 2026-08-12 -- winning used to
// sweep the position away and replace it with a sentence.
void testTheFinishedBoardCarriesItsOwnEnding() {
  for (int mine = 0; mine < 2; ++mine) {
    toybattle::Game game;
    game.newGame(11u, static_cast<int>(toybattle::TerrainId::CastleField), 0, true);
    // Walk a troop onto an H.Q. the short way: hand the winner the slot.
    const toybattle::Terrain& b = game.board();
    int hq = -1;
    for (int s = b.baseCount; s < b.slotCount(); ++s) {
      if (b.hqOwner(s) == (mine == 1 ? 1 : 0)) hq = s;
    }
    CHECK(hq >= 0);
    game.placeSlot[game.placementCount] = static_cast<uint8_t>(hq);
    game.placeTile[game.placementCount] =
        static_cast<uint8_t>(((mine == 1 ? 0 : 1) << 3) | static_cast<int>(toybattle::Troop::Roxy));
    ++game.placementCount;
    game.winner = static_cast<uint8_t>(mine == 1 ? 0 : 1);
    game.ending = static_cast<uint8_t>(toybattle::Ending::HqCaptured);
    game.phase = static_cast<uint8_t>(toybattle::Phase::GameOver);

    tbui::BoardModel model;
    model.game = game;
    model.seat = 0;
    model.yourTurn = false;
    model.prompt = mine == 1 ? "YOU WIN: THEIR H.Q. IS TAKEN" : "YOU LOSE: YOUR H.Q. IS TAKEN";
    Rendered out;
    buildTbBoard(out, model);

    // The verdict is on screen, and so is the way on -- the objection that sent
    // this to its own screen the first time was that there was none.
    CHECK(out.target.drew(model.prompt));
    CHECK(out.target.drew("HOW IT ENDED"));
    // And none of the mid-turn controls, which would offer moves in a game that
    // is over.
    CHECK(!out.target.drew("DRAW 2"));
    CHECK(!out.target.drew("SKIP"));
    CHECK(!out.target.drew("WAIT"));
  }
}

// Playing the other side has to be the same game seen from the other chair, not
// the same picture with the labels swapped. Two boards are not symmetric -- La
// Croisette's H.Q. are not mirror images, Caribbean Sea gives seat 0 two H.Q.
// against seat 1's one -- so on those, seat 1 was a game nobody could reach.
void testEitherSideSeesItsOwnHqAtTheBottom() {
  const int boards[] = {static_cast<int>(toybattle::TerrainId::LaCroisette),
                        static_cast<int>(toybattle::TerrainId::CaribbeanSea),
                        static_cast<int>(toybattle::TerrainId::CastleField)};
  for (const int which : boards) {
    for (int seat = 0; seat < 2; ++seat) {
      toybattle::Game game;
      game.newGame(5u, which, 0, true);
      const toybattle::Terrain& b = game.board();

      // Your own H.Q. is drawn below the middle of the board, whichever seat
      // you took, because the board turns round with you.
      int mine = -1;
      for (int s = b.baseCount; s < b.slotCount(); ++s) {
        if (b.hqOwner(s) == seat) mine = s;
      }
      CHECK(mine >= 0);
      const fui::Point at = tbui::slotCenter(device(), b, mine, seat);
      CHECK(at.y > 400);

      // And the letter under it says H. It read `hqOwner == 0`, which labelled
      // seat 1's own H.Q. as the enemy's.
      tbui::BoardModel model;
      model.game = game;
      model.seat = static_cast<uint8_t>(seat);
      model.yourTurn = game.turn == seat;
      model.prompt = "";
      Rendered out;
      buildTbBoard(out, model);
      const FakeTarget::TextRun* h = out.target.find("H");
      CHECK(h != nullptr);
      if (h != nullptr) CHECK(h->rect.y > 400);

      // The tap that lands on a slot is the slot the player is looking at.
      for (int s = 0; s < b.slotCount(); ++s) {
        const fui::Point p = tbui::slotCenter(device(), b, s, seat);
        CHECK(tbui::slotAt(device(), b, p.x, p.y, seat) == s);
      }

      // And YOUR troops are the ones knocked out of black, theirs the ones on
      // the ground -- the inversion is what says whose a troop is, so getting
      // it backwards swaps the two armies while the board still looks like a
      // board. Give each seat one troop and read the ink back.
      toybattle::Game two = game;
      two.placeSlot[two.placementCount] = 0;
      two.placeTile[two.placementCount] = static_cast<uint8_t>((seat << 3) | static_cast<int>(toybattle::Troop::Roxy));
      ++two.placementCount;
      two.placeSlot[two.placementCount] = 1;
      two.placeTile[two.placementCount] =
          static_cast<uint8_t>(((seat ^ 1) << 3) | static_cast<int>(toybattle::Troop::Jumbo));
      ++two.placementCount;

      tbui::BoardModel pair;
      pair.game = two;
      pair.seat = static_cast<uint8_t>(seat);
      pair.yourTurn = two.turn == seat;
      pair.prompt = "";
      Rendered ink;
      buildTbBoard(ink, pair);
      const FakeTarget::TextRun* yours = ink.target.find("7");   // Roxy, placed for `seat`
      const FakeTarget::TextRun* theirs = ink.target.find("3");  // Jumbo, placed for the other
      CHECK(yours != nullptr);
      CHECK(theirs != nullptr);
      if (yours != nullptr) CHECK(yours->color == fui::Color::White);
      if (theirs != nullptr) CHECK(theirs->color == fui::Color::Black);
    }
  }
}

// --- sudoku ------------------------------------------------------------------

sudoku::Workspace& sudokuWorkspace() {
  // 1.1KB. Static rather than a local so the stack frames here stay small.
  static sudoku::Workspace work;
  return work;
}

sudoku::Game aSudokuGame(const sudoku::Level level) {
  uint32_t rng = 0x51DA0000u + static_cast<uint32_t>(level) * 7919u;
  sudoku::Puzzle puzzle;
  const bool made = sudoku::generate(puzzle, level, sudokuWorkspace(), rng, 400);
  CHECK(made);
  sudoku::Game game;
  sudoku::startGame(game, puzzle);
  return game;
}

void buildSudokuBoard(Rendered& out, const sudokuui::BoardModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  sudokuui::buildBoard(screen, model);
}

// Every action a tap can reach anywhere on the panel. The grid and the pad are
// deliberately NOT in the interaction table -- ninety regions against a
// twenty-four slot buffer -- so this is the direct assertion that they are not,
// and it needs no access to the private geometry to make it.
std::vector<int> sudokuReachableActions(Rendered& out) {
  // A 7px lattice. The smallest control on any of these screens is a 50px grid
  // cell, so nothing tappable can hide between the samples, and a prime step
  // cannot line up with the 50px and 69px pitches and miss a whole column.
  std::vector<int> found;
  for (int y = 2; y < 800; y += 7) {
    for (int x = 2; x < 480; x += 7) {
      const fui::ActionEvent event = out.tap(x, y);
      if (event.action == fui::NO_ACTION) continue;
      bool seen = false;
      for (const int action : found) {
        if (action == static_cast<int>(event.action)) seen = true;
      }
      if (!seen) found.push_back(static_cast<int>(event.action));
    }
  }
  return found;
}

bool contains(const std::vector<int>& actions, const int action) {
  for (const int found : actions) {
    if (found == action) return true;
  }
  return false;
}

// The pair has to be an exact inverse, not merely agree at the centres. Both
// halves matter: a rect the hit test does not cover is a dead region, and a
// point the hit test claims for a cell outside that cell's rect is a tap that
// lands somewhere the player did not touch.
void testTheSudokuGridAndItsHitTestAreExactInverses() {
  const fui::DeviceContext ctx = device();
  bool everyPixelMapsHome = true;
  bool everyClaimIsInsideItsRect = true;
  for (int cell = 0; cell < sudoku::kCells; ++cell) {
    const fui::Rect box = sudokuui::cellRect(ctx, cell);
    for (int y = box.y; y < box.bottom(); ++y) {
      for (int x = box.x; x < box.right(); ++x) {
        int got = -1;
        if (!sudokuui::cellAt(ctx, x, y, got) || got != cell) everyPixelMapsHome = false;
      }
    }
  }
  for (int y = 0; y < 800; ++y) {
    for (int x = 0; x < 480; ++x) {
      int got = -1;
      if (!sudokuui::cellAt(ctx, x, y, got)) continue;
      const fui::Rect box = sudokuui::cellRect(ctx, got);
      if (x < box.x || x >= box.right() || y < box.y || y >= box.bottom()) everyClaimIsInsideItsRect = false;
    }
  }
  CHECK(everyPixelMapsHome);
  CHECK(everyClaimIsInsideItsRect);

  // And the grid claims nothing in the header band or below the board.
  int stray = -1;
  CHECK(!sudokuui::cellAt(ctx, 240, 40, stray));
  CHECK(!sudokuui::cellAt(ctx, 240, 700, stray));
  CHECK(!sudokuui::cellAt(ctx, 2, 300, stray));
}

void testTheSudokuPadAndItsHitTestAreExactInverses() {
  const fui::DeviceContext ctx = device();
  bool everyPixelMapsHome = true;
  bool everyClaimIsInsideItsRect = true;
  for (int digit = 1; digit <= sudoku::kSize; ++digit) {
    const fui::Rect key = sudokuui::padKeyRect(ctx, digit);
    for (int y = key.y; y < key.bottom(); ++y) {
      for (int x = key.x; x < key.right(); ++x) {
        int got = -1;
        if (!sudokuui::padKeyAt(ctx, x, y, got) || got != digit) everyPixelMapsHome = false;
      }
    }
  }
  for (int y = 0; y < 800; ++y) {
    for (int x = 0; x < 480; ++x) {
      int got = -1;
      if (!sudokuui::padKeyAt(ctx, x, y, got)) continue;
      const fui::Rect key = sudokuui::padKeyRect(ctx, got);
      if (x < key.x || x >= key.right() || y < key.y || y >= key.bottom()) everyClaimIsInsideItsRect = false;
    }
  }
  CHECK(everyPixelMapsHome);
  CHECK(everyClaimIsInsideItsRect);

  // The pad sits under the grid and never over it: no point belongs to both.
  bool disjoint = true;
  for (int y = 0; y < 800; ++y) {
    for (int x = 0; x < 480; ++x) {
      int cell = -1;
      int digit = -1;
      if (sudokuui::cellAt(ctx, x, y, cell) && sudokuui::padKeyAt(ctx, x, y, digit)) disjoint = false;
    }
  }
  CHECK(disjoint);
}

// The board screen spends three interactions, and the ninety regions the player
// spends most of their time tapping spend none. This is the assertion that the
// twenty-four slot buffer is respected structurally rather than by luck.
void testTheSudokuBoardSpendsThreeInteractions() {
  sudokuui::BoardModel model;
  model.game = aSudokuGame(sudoku::Level::Easy);
  Rendered out;
  buildSudokuBoard(out, model);
  CHECK(!out.interactions.overflowed());

  const std::vector<int> actions = sudokuReachableActions(out);
  CHECK(actions.size() == 2);
  CHECK(contains(actions, sudokuui::ActionUndo));
  CHECK(contains(actions, sudokuui::ActionHint));

  // Every cell and every key answers nothing here, because both are hit-tested
  // by the activity against the geometry that drew them.
  const fui::DeviceContext ctx = device();
  bool gridIsSilent = true;
  for (int cell = 0; cell < sudoku::kCells; ++cell) {
    const fui::Rect box = sudokuui::cellRect(ctx, cell);
    if (out.tap(box.x + box.width / 2, box.y + box.height / 2).action != fui::NO_ACTION) gridIsSilent = false;
  }
  bool padIsSilent = true;
  for (int digit = 1; digit <= sudoku::kSize; ++digit) {
    const fui::Rect key = sudokuui::padKeyRect(ctx, digit);
    if (out.tap(key.x + key.width / 2, key.y + key.height / 2).action != fui::NO_ACTION) padIsSilent = false;
  }
  CHECK(gridIsSilent);
  CHECK(padIsSilent);
}

// The status capsule is a readout while you solve and a door once you are
// finished. A readout that answers a tap is a control the player has to learn
// is not one, which is the bug chess's own capsule test pins.
void testTheSudokuCapsuleIsInertUntilTheGridIsFinished() {
  sudokuui::BoardModel playing;
  playing.game = aSudokuGame(sudoku::Level::Easy);
  Rendered mid;
  buildSudokuBoard(mid, playing);
  CHECK(!contains(sudokuReachableActions(mid), sudokuui::ActionSeeResult));

  sudokuui::BoardModel solved;
  solved.game = playing.game;
  for (int cell = 0; cell < sudoku::kCells; ++cell) {
    if (sudoku::isGiven(solved.game, cell)) continue;
    solved.game.entry[cell] = solved.game.puzzle.solution[cell];
  }
  solved.game.solvedFlag = 1;
  Rendered done;
  buildSudokuBoard(done, solved);
  CHECK(!done.interactions.overflowed());
  CHECK(contains(sudokuReachableActions(done), sudokuui::ActionSeeResult));
}

// A control that cannot act dims. It does not disappear, because a button that
// vanishes takes its space with it and the layout jumps.
void testTheSudokuUndoDimsRatherThanVanishing() {
  sudokuui::BoardModel model;
  model.game = aSudokuGame(sudoku::Level::Easy);
  CHECK(!sudoku::canUndo(model.game));
  Rendered fresh;
  buildSudokuBoard(fresh, model);
  CHECK(fresh.target.find("UNDO") != nullptr);
  CHECK(fresh.target.find("HINT") != nullptr);

  sudoku::tapCell(model.game, 0);
  CHECK(sudoku::canUndo(model.game));
  Rendered used;
  buildSudokuBoard(used, model);
  CHECK(used.target.find("UNDO") != nullptr);
}

void testEverySudokuScreenStaysOnThePanel() {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  const sudoku::Game game = aSudokuGame(sudoku::Level::Medium);

  {
    sudokuui::MenuModel model;
    model.hasGame = true;
    model.game = game;
    model.level = sudoku::Level::Medium;
    model.record.solved[1] = 3;
    model.record.bestMs[1] = 512000;
    Rendered out;
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    sudokuui::buildMenu(screen, model);
    CHECK(!out.interactions.overflowed());
    const std::vector<int> actions = sudokuReachableActions(out);
    CHECK(contains(actions, sudokuui::ActionPlay));
    CHECK(contains(actions, sudokuui::ActionMenuRow));
    for (const auto& run : out.target.texts) {
      CHECK(run.rect.y >= 0);
      CHECK(run.rect.bottom() <= 800);
      CHECK(run.rect.x >= 0);
      CHECK(run.rect.right() <= 480);
    }
  }
  {
    sudokuui::ResultModel model;
    model.level = sudoku::Level::Expert;
    model.hardest = sudoku::Technique::XYWing;
    model.elapsedMs = 3721000;  // past the hour, which is the long clock
    model.bestMs = 900000;
    model.hintsUsed = 2;
    model.clues = 26;
    model.solvedAtThisLevel = 7;
    Rendered out;
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    sudokuui::buildResult(screen, model);
    CHECK(!out.interactions.overflowed());
    const std::vector<int> actions = sudokuReachableActions(out);
    CHECK(contains(actions, sudokuui::ActionAgain));
    CHECK(contains(actions, sudokuui::ActionDone));
    CHECK(out.target.find("1:02:01") != nullptr);
    for (const auto& run : out.target.texts) {
      CHECK(run.rect.y >= 0);
      CHECK(run.rect.bottom() <= 800);
    }
  }
}

void testEverySudokuLessonPagesAndClearsItsButton() {
  for (int page = 0; page < sudokuui::howToPages(); ++page) {
    sudokuui::HowToModel model;
    model.page = page;
    Rendered out;
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    sudokuui::buildHowTo(screen, model);
    CHECK(!out.interactions.overflowed());
    CHECK(contains(sudokuReachableActions(out), sudokuui::ActionHowToNext));

    // The button is taken from the bottom before the page draws, so nothing the
    // page draws may reach it. Its own label is the exception.
    const FakeTarget::TextRun* button = out.target.find(page + 1 < sudokuui::howToPages() ? "NEXT" : "GOT IT");
    CHECK(button != nullptr);
    for (const auto& run : out.target.texts) {
      if (run.rect.y == button->rect.y) continue;
      CHECK(run.rect.bottom() <= button->rect.y);
    }
  }
}

// The design language's test for anything decorative: would a screenshot of it
// be identical on everyone's device? The ornament is the player's own grid, so
// two different puzzles have to produce two different pictures, and progress
// through one puzzle has to change it.
void testTheSudokuOrnamentCarriesTheGame() {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  auto drawnRects = [&ctx, &noInput](const sudokuui::MenuModel& model) {
    Rendered out;
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    sudokuui::buildMenu(screen, model);
    return out.target.fills.size();
  };

  sudokuui::MenuModel easy;
  easy.hasGame = true;
  easy.game = aSudokuGame(sudoku::Level::Easy);
  sudokuui::MenuModel expert;
  expert.hasGame = true;
  expert.level = sudoku::Level::Expert;
  expert.game = aSudokuGame(sudoku::Level::Expert);

  sudokuui::MenuModel empty;
  empty.hasGame = false;
  CHECK(drawnRects(easy) != drawnRects(empty));

  // And filling cells in adds to it, which is what makes it worth coming back to.
  sudokuui::MenuModel partway = easy;
  const size_t before = drawnRects(partway);
  int filled = 0;
  for (int cell = 0; cell < sudoku::kCells && filled < 12; ++cell) {
    if (sudoku::isGiven(partway.game, cell)) continue;
    partway.game.entry[cell] = partway.game.puzzle.solution[cell];
    ++filled;
  }
  CHECK(drawnRects(partway) > before);
}

// --- vertical centring -------------------------------------------------------

// Every cut the fork ships, so a regenerated face is checked here as well as by
// verifyCutMetrics() on the device.
const toybox::CutMetrics kEveryCut[] = {
    toybox::kTileCut,       toybox::kButtonCut,           toybox::kUiCut,         toybox::kDisplayCut,
    toybox::kSerifSmallCut, toybox::kSerifTileCut,        toybox::kSerifTitleCut, toybox::kReadingSmallCut,
    toybox::kReadingCut,    toybox::kReadingBoldSmallCut, toybox::kReadingBoldCut};

void testInkCentredPutsTheInkInTheMiddleOfAnyBox() {
  for (const toybox::CutMetrics& cut : kEveryCut) {
    // From well under the line box to well over it. Under is where the target's
    // clamp bites; over is where it already worked, and must keep working.
    for (int16_t height = cut.inkHeight; height <= 100; ++height) {
      const fui::Rect box = fui::makeRect(40, 120, 200, height);
      const fui::Rect given = toybox::inkCentred(box, cut);
      const int above = inkTopIn(given, cut) - box.y;
      const int below = box.y + box.height - (inkTopIn(given, cut) + cut.inkHeight);
      // Centred means the two gaps match, to the pixel a whole-pixel offset can
      // manage. Stated as a symmetry rather than as a formula, so the check
      // cannot pass by restating the code it is checking.
      CHECK(above >= 0 && below >= 0);
      CHECK(above - below <= 1 && below - above <= 1);
      // The x axis is the caller's business and must survive untouched.
      CHECK(given.x == box.x);
      CHECK(given.width == box.width);
    }
  }
}

// The other half of the same claim: handing the target the box itself is wrong
// once the box is shorter than the line box, and wrong by more the smaller it
// gets. Without this the check above could pass against a target that never
// needed correcting.
void testAShortBoxIsWhatMakesTheCorrectionNecessary() {
  const toybox::CutMetrics& cut = toybox::kDisplayCut;
  // A 50px box under a 63px line box: the clamp pins the offset at zero, so the
  // ink lands `ascender - inkHeight` down and its foot leaves the box. That is
  // exactly what a Knucklebones total used to do.
  const fui::Rect tight = fui::makeRect(0, 0, 100, 50);
  CHECK(inkTopIn(tight, cut) == cut.ascender - cut.inkHeight);
  CHECK(inkTopIn(tight, cut) + cut.inkHeight > tight.height);
  CHECK(inkTopIn(toybox::inkCentred(tight, cut), cut) + cut.inkHeight <= tight.height);
  // And the error grows as the box shrinks, which is why it reads as an
  // intermittent font problem rather than as a rule.
  const fui::Rect tighter = fui::makeRect(0, 0, 100, 44);
  const int offBy = [&](const fui::Rect& box) { return inkTopIn(box, cut) - (box.height - cut.inkHeight) / 2; }(tight);
  const int offByMore = [&](const fui::Rect& box) {
    return inkTopIn(box, cut) - (box.height - cut.inkHeight) / 2;
  }(tighter);
  CHECK(offByMore > offBy);
}

// And the same claim at a call site, so removing a wrapper fails a test rather
// than only looking slightly wrong in a render.
void testAMinesweeperDigitIsCentredInItsCell() {
  mineui::BoardModel model;
  minesweeper::start(model.game, 5u);
  // The first dig is what lays the mines, so the board has to be played into
  // rather than assembled by hand.
  minesweeper::reveal(model.game, 0, 0);
  for (int c = 0; c < minesweeper::kColumns; ++c) {
    for (int r = 0; r < minesweeper::kRows; ++r) {
      if ((model.game.cell[c][r] & minesweeper::kMine) == 0) model.game.cell[c][r] |= minesweeper::kRevealed;
    }
  }
  model.game.status = minesweeper::Status::Playing;
  Rendered out;
  buildMs<mineui::BoardModel, mineui::buildBoard>(out, model);

  int checked = 0;
  for (int c = 0; c < minesweeper::kColumns && checked == 0; ++c) {
    for (int r = 0; r < minesweeper::kRows && checked == 0; ++r) {
      if ((model.game.cell[c][r] & minesweeper::kMine) != 0) continue;
      if (minesweeper::neighbouringMines(model.game, c, r) <= 0) continue;
      const fui::Rect cell = mineui::cellRect(device(), c, r);
      for (const auto& run : out.target.texts) {
        if (run.style.font != toybox::kDisplayFont) continue;
        if (run.rect.x != cell.x || run.rect.width != cell.width) continue;
        // Derived, not literal: the rect handed to the target is one line box
        // tall wherever it sits, and the ink it produces is centred in the cell.
        CHECK(run.rect.height == toybox::kDisplayCut.lineHeight);
        CHECK(inkTopIn(run.rect, toybox::kDisplayCut) == cell.y + (cell.height - toybox::kDisplayCut.inkHeight) / 2);
        ++checked;
        break;
      }
    }
  }
  CHECK(checked == 1);
}

// Knucklebones' column total is the one Mario saw first: the ui cut in a 28px
// band, where the clamp drops it six pixels out of the bottom.
void testAKnucklebonesColumnTotalClearsItsBand() {
  knuckleui::BoardModel model;
  // One die in one column, so the total is the die and the label is known.
  model.yours.cell[0][0] = 5;
  model.yourTurn = true;
  model.die = 3;
  Rendered out;
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, device(), noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  knuckleui::buildBoard(screen, model);

  const FakeTarget::TextRun* total = out.target.find("5");
  CHECK(total != nullptr);
  if (total != nullptr) CHECK(total->rect.height == toybox::kUiCut.lineHeight);
}

// No two lines of the front door's prose may share ink.
//
// Measured as INK, not as the rect handed to the component. Those are not the
// same thing in either direction: `inkCentred` returns a rect one line box tall
// that deliberately overhangs its band, and a plain rect holds a line box far
// taller than the letters in it. Comparing rects reports collisions that are
// not there and misses the one that is.
//
// The one that was there: the state and the record shared a band, set left and
// right, which is invisible while both strings are short -- "35 LEFT" beside
// "10 SOLVED BEST 8:32" -- and runs them through each other the moment neither
// is. Checked in BOTH states, because the empty card is the one nobody renders:
// every screenshot of this screen had been taken against a seeded save.
fui::Rect sudokuInkBand(const FakeTarget::TextRun& run) {
  toybox::CutMetrics cut = toybox::kUiCut;
  if (run.style.font == toybox::kSmallFont) cut = toybox::kTileCut;
  if (run.style.font == toybox::kDisplayFont) cut = toybox::kDisplayCut;
  // What GfxRendererTarget::text does: centre the line box in the rect, clamped
  // at zero, then drawText places the ascender box at that y.
  const int16_t slack = static_cast<int16_t>(run.rect.height - cut.lineHeight);
  const int16_t y = static_cast<int16_t>(run.rect.y + (slack > 0 ? slack / 2 : 0) + cut.ascender - cut.inkHeight);
  return fui::makeRect(run.rect.x, y, run.rect.width, cut.inkHeight);
}

void testTheSudokuFrontDoorNeverSharesInkBetweenTwoLines() {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  for (int seeded = 0; seeded < 2; ++seeded) {
    sudokuui::MenuModel model;
    model.level = sudoku::Level::Easy;
    model.hasGame = seeded != 0;
    if (seeded) {
      model.game = aSudokuGame(sudoku::Level::Easy);
      model.record.solved[0] = 10;
      model.record.bestMs[0] = 512000;
    }
    Rendered out;
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    sudokuui::buildMenu(screen, model);

    // The header band lays its own title and label out; this is about the prose
    // the screen builder places itself.
    for (size_t a = 0; a < out.target.texts.size(); ++a) {
      if (out.target.texts[a].rect.y < toybox::kHeaderHeight) continue;
      for (size_t b = a + 1; b < out.target.texts.size(); ++b) {
        if (out.target.texts[b].rect.y < toybox::kHeaderHeight) continue;
        const fui::Rect one = sudokuInkBand(out.target.texts[a]);
        const fui::Rect two = sudokuInkBand(out.target.texts[b]);
        const bool sameColumn = one.x < two.right() && two.x < one.right();
        const bool sameRows = one.y < two.bottom() && two.y < one.bottom();
        CHECK(!(sameColumn && sameRows));
      }
    }
  }
}

// --- FOREHEAD ---------------------------------------------------------------

namespace {

// The round is played in landscape, so its tests are too. Everything the key
// bands claim is claimed about THIS frame.
fui::DeviceContext landscapeDevice() {
  fui::DeviceContext ctx;
  ctx.width = 800;
  ctx.height = 480;
  ctx.hasTouch = true;
  ctx.hasButtons = true;
  return ctx;
}

void buildForeheadPlay(Rendered& out, const foreheadui::PlayModel& model) {
  const fui::DeviceContext ctx = landscapeDevice();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  foreheadui::buildPlay(screen, model);
}

void buildForeheadMenu(Rendered& out, const foreheadui::MenuModel& model) {
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, device(), noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  foreheadui::buildMenu(screen, model);
}

void buildForeheadPicker(Rendered& out, const foreheadui::PickerModel& model) {
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, device(), noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  foreheadui::buildPicker(screen, model);
}

void buildForeheadResult(Rendered& out, const foreheadui::ResultModel& model) {
  const fui::DeviceContext ctx = landscapeDevice();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  foreheadui::buildResult(screen, model);
}

}  // namespace

// THE test for this app.
//
// The guesser cannot see the screen. The room reads the two edge labels aloud
// and the guesser presses what the room tells them, so a label on the wrong
// edge is not cosmetic -- it makes every player in the room give the wrong
// instruction, and it would look exactly like the player being bad at the game.
//
// Asserting the labels alone would prove nothing: this fork already shipped a
// FACE TO FACE setting that was verified by screenshotting its own label. So
// this checks the label's POSITION and the ACTION a tap in that half returns,
// together, from one paint.
void testTheForeheadKeyLabelsSitOnTheEdgesTheyAct() {
  Rendered out;
  foreheadui::PlayModel model;
  model.word = "PENGUIN";
  model.secondsLeft = 42;
  model.lengthSeconds = 60;
  buildForeheadPlay(out, model);

  const FakeTarget::TextRun* pass = out.target.find("PASS");
  const FakeTarget::TextRun* got = out.target.find("GOT IT");
  CHECK(pass != nullptr);
  CHECK(got != nullptr);
  if (pass == nullptr || got == nullptr) return;

  // PASS is on the top edge, GOT IT on the bottom, and they are on opposite
  // halves of the panel rather than merely in that order.
  CHECK(pass->rect.y < 240);
  CHECK(got->rect.y >= 240);

  // And the halves do what their labels say. Tapping the middle of the top
  // half gives up on the card; the bottom half scores it.
  CHECK(out.tap(400, 160).action == foreheadui::ActionMissed);
  Rendered again;
  buildForeheadPlay(again, model);
  CHECK(again.tap(400, 320).action == foreheadui::ActionGot);
}

void testTheForeheadRoundIgnoresTapsWhereFingersGrip() {
  Rendered out;
  foreheadui::PlayModel model;
  model.word = "PENGUIN";
  model.secondsLeft = 42;
  buildForeheadPlay(out, model);
  // The guesser's hands are on the short edges and their fingers curl over the
  // long ones to reach the keys. A half-screen target would sit under both, so
  // the corners and the extreme edges answer nothing.
  CHECK(out.tap(20, 240).action == fui::NO_ACTION);
  Rendered b;
  buildForeheadPlay(b, model);
  CHECK(b.tap(780, 30).action == fui::NO_ACTION);
  Rendered c;
  buildForeheadPlay(c, model);
  CHECK(c.tap(400, 10).action == fui::NO_ACTION);
  Rendered d;
  buildForeheadPlay(d, model);
  CHECK(d.tap(400, 470).action == fui::NO_ACTION);
}

void testTheForeheadCardNeverDrawsPastItsBox() {
  // The fake target measures ten pixels a character at every size, so this pins
  // the ALGORITHM -- greedy wrap, never inside a word, at most three lines --
  // and not the real fit. Real fit is held by the generator's 22-character cap
  // and by looking at a render.
  //
  // NARROW boxes as well as the real one, and that is the whole point. At the
  // panel's 768px every 22-character entry is 220 fake pixels and fits on one
  // line, so a mutant that let the ladder accept three times the box width
  // survived: the wrap it was supposed to break had never once run. A fixture
  // more convenient than the real caller stops testing the real caller.
  const fui::DeviceContext ctx = landscapeDevice();
  // 200 and 150 are chosen so the wrap is FORCED at the fake metric: 20 and 15
  // characters a line against a 22-character longest entry and a 15-character
  // longest word. 240 was the first try and it fits every entry on one line,
  // which is how a width can look narrow and test nothing.
  const int16_t widths[] = {768, 200, 150};
  for (const int16_t width : widths) {
    const fui::Rect box = fui::makeRect(16, 100, width, 300);
    bool anyWrapped = false;
    for (int entry = 0; entry < forehead::kEntryCount; ++entry) {
      Rendered out;
      const fui::InputSnapshot noInput{};
      toybox::Frame frame(out.target, ctx, noInput, out.interactions);
      toybox::Screen screen(frame, toybox::themeTokens());
      const char* text = forehead::kEntries[entry];
      const foreheadui::CardLayout layout = foreheadui::layOutCard(screen, box, text);
      CHECK(layout.lines >= 1 && layout.lines <= foreheadui::kCardMaxLines);
      if (layout.lines > 1) anyWrapped = true;
      int covered = 0;
      for (int line = 0; line < layout.lines; ++line) {
        CHECK(layout.width[line] <= box.width);
        CHECK(layout.end[line] > layout.start[line]);
        // A break lands on a space or on the end of the string: a word split in
        // half is unreadable in a way a smaller word never is.
        const int at = layout.end[line];
        CHECK(text[at] == '\0' || text[at] == ' ');
        covered += layout.end[line] - layout.start[line];
      }
      // Every character is drawn once, so nothing is silently dropped.
      CHECK(covered >= static_cast<int>(std::strlen(text)) - (layout.lines - 1));
    }
    // At the two narrow widths the wrap MUST have run, or the loop above was
    // checking a property nothing exercises.
    if (width < 768) CHECK(anyWrapped);
  }
}

void buildForeheadSettings(Rendered& out, const foreheadui::SettingsModel& model) {
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, device(), noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  foreheadui::buildSettings(screen, model);
}

void testTheForeheadPagingWraps() {
  // Forward off the end returns to the first page, back off the front reaches
  // the last. The complaint that produced this was a key that stopped
  // answering on the last page, which on a 0.3s panel is indistinguishable
  // from a key that was not registered at all.
  CHECK(foreheadui::pageAfter(0, 1, 3) == 1);
  CHECK(foreheadui::pageAfter(2, 1, 3) == 0);
  CHECK(foreheadui::pageAfter(0, -1, 3) == 2);
  CHECK(foreheadui::pageAfter(1, -1, 3) == 0);
  // One page: every key is a no-op, and specifically not an index of 1 into a
  // one-page screen.
  CHECK(foreheadui::pageAfter(0, 1, 1) == 0);
  CHECK(foreheadui::pageAfter(0, -1, 1) == 0);
  // Never out of range, in either direction, for any real page count.
  for (int pages = 1; pages <= 12; ++pages) {
    for (int page = 0; page < pages; ++page) {
      for (const int step : {1, -1}) {
        const int next = foreheadui::pageAfter(page, step, pages);
        CHECK(next >= 0);
        CHECK(next < pages);
      }
    }
    // And it is a CYCLE: stepping forward `pages` times comes home, which a
    // clamp would also satisfy at the end but not on the way there.
    int walk = 0;
    for (int i = 0; i < pages; ++i) walk = foreheadui::pageAfter(walk, 1, pages);
    CHECK(walk == 0);
  }
  // A screen with nothing on it does not page to a negative index.
  CHECK(foreheadui::pageAfter(0, 1, 0) == 0);
}

void testTheForeheadResetSaysWhatItDestroysAndAsksFirst() {
  Rendered armed;
  foreheadui::SettingsModel model;
  model.anythingToClear = true;
  model.roundSeconds = 90;
  buildForeheadSettings(armed, model);
  // The row enumerates what "everything" means before it is tapped. All three
  // things, because the reset also drops the chosen category and the round
  // length: a row promising only scores that also moves you back to the first
  // list is a surprise found later, on a different screen.
  CHECK(armed.target.drew("SCORES WORDS AND SETTINGS"));
  CHECK(armed.target.drew("90 SECONDS"));
  CHECK(!armed.target.drew("TAP AGAIN TO CONFIRM"));

  // Offered means TAPPABLE. Without this the row could be made permanently
  // inert and the suite would not notice -- proved by mutation: forcing
  // enabled=false left 0 failures before this line existed.
  const fui::ActionEvent hit = armed.tap(240, 220);
  CHECK(hit.action == foreheadui::ActionSettingsRow);
  CHECK(hit.value == static_cast<int>(foreheadui::SettingRow::Reset));

  Rendered asking;
  model.confirmingReset = true;
  buildForeheadSettings(asking, model);
  // The LABEL changes, not only the subtitle: an armed destructive action
  // that looks almost identical to an unarmed one is one you can arm by
  // accident and never notice.
  CHECK(asking.target.drew("TAP AGAIN TO WIPE"));
  CHECK(asking.target.drew("THIS CANNOT BE UNDONE"));
  CHECK(!asking.target.drew("RESET EVERYTHING"));
  // Still tappable while armed, or the confirmation could never be given.
  CHECK(asking.tap(240, 220).action == foreheadui::ActionSettingsRow);

  // With nothing to clear the row is not offered at all, so the one
  // irreversible control on the device cannot be armed by a player who has
  // never played -- and cannot be armed twice by one who just used it.
  Rendered fresh;
  foreheadui::SettingsModel blank;
  blank.anythingToClear = false;
  buildForeheadSettings(fresh, blank);
  CHECK(fresh.target.drew("NOTHING TO CLEAR YET"));
  CHECK(fresh.tap(240, 220).action != foreheadui::ActionSettingsRow);
}

void testTheForeheadStartControlLooksLikeAButton() {
  Rendered out;
  forehead::Record record;
  record.push(0, 11);
  foreheadui::MenuModel model;
  model.category = 0;
  model.record = &record;
  buildForeheadMenu(out, model);

  // The thing that starts the game is a BOX with a play mark in it. It used to
  // be the category name with "TAP TO PLAY" under it and no border at all,
  // which read as a heading on a screen whose three real controls are bordered
  // rows -- so the one element that was tappable was the only one that did not
  // look it.
  //
  // Asserted as geometry rather than as a caption, because a caption is what it
  // had: the old screen SAID "TAP TO PLAY" in words and still nobody tapped it.
  CHECK(!out.target.strokes.empty());
  fui::Rect box{};
  for (const auto& s : out.target.strokes) {
    if (s.width == 0) continue;
    if (s.rect.height >= 100 && s.rect.width >= 300) box = s.rect;
  }
  CHECK(box.width > 0);
  // Drawn AND visible. A zero-width border and a white-on-white triangle both
  // used to pass this: the target threw the stroke width away and never read
  // the triangle's colour back, so the two things the test is named for were
  // the two things it could not see.
  CHECK(out.target.outlined(box));
  CHECK(out.target.triangleInside(box, fui::Color::Black));
  CHECK(!out.target.triangleInside(box, fui::Color::White));

  // The border is the tap target, not a decoration drawn near one. Corners
  // included: a box you can only press in the middle is worse than no box,
  // because it teaches the wrong edge.
  const int midX = box.x + box.width / 2;
  const int midY = box.y + box.height / 2;
  CHECK(out.tap(midX, midY).action == foreheadui::ActionReady);
  CHECK(out.tap(box.x + 4, box.y + 4).action == foreheadui::ActionReady);
  CHECK(out.tap(box.x + box.width - 4, box.y + box.height - 4).action == foreheadui::ActionReady);
  // And it does not swallow the screen: below the box is the record line, which
  // is not a control at all.
  CHECK(out.tap(midX, box.y + box.height + 30).action != foreheadui::ActionReady);

  // The state band says ONE thing. It used to append the category best, which
  // is the same number the record line below already prints under its own
  // label, so the screen said it twice.
  CHECK(out.target.find("TAP TO PLAY   BEST HERE 11") == nullptr);

  Rendered doors;
  buildForeheadMenu(doors, model);
  const fui::ActionEvent row = doors.tap(240, 620);
  CHECK(row.action == foreheadui::ActionMenuRow);
  CHECK(row.value == static_cast<int>(foreheadui::MenuRow::Category));
}

void testTheForeheadPickerReportsAbsoluteCategories() {
  Rendered out;
  foreheadui::PickerModel model;
  model.page = 1;
  model.current = 0;
  buildForeheadPicker(out, model);
  // The screen is handed a slice, but a tap has to report WHICH LIST it is,
  // not which row of which page -- the shelf learned this the hard way.
  const fui::ActionEvent hit = out.tap(240, 150);
  CHECK(hit.action == foreheadui::ActionCategoryRow);
  CHECK(hit.value == foreheadui::pickerRowsPerPage());
  CHECK(hit.value < forehead::kCategoryCount);
}

void testTheForeheadResultsMarkTheUnansweredCardApart() {
  forehead::Deck deck;
  deck.reset();
  forehead::Rng rng(5u);
  forehead::Round round;
  round.begin(0, 60, deck, rng);
  round.got(deck, rng);
  round.missed(deck, rng);
  round.expire();

  Rendered out;
  foreheadui::ResultModel model;
  model.category = 0;
  model.score = round.score();
  model.round = &round;
  buildForeheadResult(out, model);

  // Three cards, three different marks. The card in hand when the clock ran
  // out is neither got nor given up on, and the table will argue about it, so
  // it must not be drawn as either.
  CHECK(round.cards() == 3);
  CHECK(out.target.find(round.textAt(0)) != nullptr);
  CHECK(out.target.find(round.textAt(1)) != nullptr);
  CHECK(out.target.find(round.textAt(2)) != nullptr);
  CHECK(out.target.find("OUT OF 3") != nullptr);
  // One point, and the screen says WORD rather than WORDS for it.
  CHECK(out.target.find("WORD") != nullptr);
}

// --- Instapaper ------------------------------------------------------------

void buildInstaQueue(Rendered& out, const instapaperui::QueueModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  instapaperui::buildQueue(screen, model);
}

void buildInstaReader(Rendered& out, const instapaperui::ReaderModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  instapaperui::buildReader(screen, model);
}

instapaperui::ReaderModel instaArticleModel() {
  instapaperui::ReaderModel model;
  model.title = "What the panel does with a long article";
  model.text = "Some words that go on for a while and wrap onto more than one line of the panel.";
  model.pageLabel = "1 / 3";
  model.canPagePrev = false;
  model.canPageNext = true;
  return model;
}

// An empty queue still has to offer the door. It is the one moment a reader
// certainly wants to pull, and a control that appears only once there is
// something to do teaches nobody where it lives.
void testTheEmptyQueueStillOffersSync() {
  Rendered out;
  const instapaperui::QueueModel model;
  buildInstaQueue(out, model);

  CHECK(drewText(out, "NOTHING TO READ"));
  const fui::DeviceContext ctx = device();
  const fui::ActionEvent event = out.tap(ctx.width / 2, ctx.height - toybox::kMargin - toybox::kPillHeight / 2);
  CHECK(event.action == instapaperui::ActionSync);
}

void testTappingAQueueRowOpensThatArticle() {
  Rendered out;
  fui::ListItem rows[3];
  const char* titles[3] = {"First article", "Second article", "Third article"};
  for (int i = 0; i < 3; ++i) {
    rows[i] = fui::ListItem{};
    rows[i].label = titles[i];
    rows[i].subtitle = "6 min . example.com";
    rows[i].value = "";
    rows[i].actionValue = static_cast<int16_t>(i);
  }
  instapaperui::QueueModel model;
  model.items = rows;
  model.count = 3;
  model.lastSync = "SYNCED 14:32";
  buildInstaQueue(out, model);

  CHECK(drewText(out, "Second article"));
  CHECK(!out.interactions.overflowed());

  const fui::Rect band = instapaperui::queueBand(device());
  const int16_t rowH = instapaperui::queueRowHeight(out.target, toybox::themeTokens());
  // The middle of the second row, computed from the same numbers the builder
  // handed the component.
  const fui::ActionEvent event = out.tap(band.x + band.width / 2, band.y + rowH + rowH / 2);
  CHECK(event.action == instapaperui::ActionOpenArticle);
  CHECK(event.value == 1);
}

// The row's title gets one line, because a subtitle collapses the title band
// to one line and a wrapping label would draw straight through the subtitle.
// So the width the Activity fits against has to be the width the component
// draws into, and it has to leave room for the value.
void testTheQueueTitleWidthLeavesRoomForThePosition() {
  Rendered out;
  const fui::Rect band = instapaperui::queueBand(device());
  const int16_t titleWidth = instapaperui::queueTitleWidth(out.target, device(), toybox::themeTokens());
  CHECK(titleWidth > 0);
  CHECK(titleWidth < band.width);
}

void testTheReaderPagesAndArchives() {
  Rendered out;
  buildInstaReader(out, instaArticleModel());

  const fui::DeviceContext ctx = device();
  const int16_t footerY = static_cast<int16_t>(ctx.height - toybox::kMargin - toybox::kPillHeight / 2);
  bool sawNext = false;
  bool sawArchive = false;
  for (int x = toybox::kMargin; x < ctx.width - toybox::kMargin; x += 8) {
    const fui::ActionEvent event = out.tap(x, footerY);
    if (event.action == instapaperui::ActionPageNext) sawNext = true;
    if (event.action == instapaperui::ActionArchive) sawArchive = true;
    // The first page cannot go back, and a dimmed control must be DEAD rather
    // than merely grey: this is the assertion that a disabled button is not
    // still routing.
    CHECK(event.action != instapaperui::ActionPagePrev);
  }
  CHECK(sawNext);
  CHECK(sawArchive);
}

// ARCHIVE is the only control here that changes anything outside this screen,
// and it is live on every page including the last. A reader who finishes an
// article should not have to page backwards to put it away.
void testArchiveIsLiveOnTheLastPage() {
  Rendered out;
  instapaperui::ReaderModel model = instaArticleModel();
  model.canPagePrev = true;
  model.canPageNext = false;
  buildInstaReader(out, model);

  const fui::DeviceContext ctx = device();
  const int16_t footerY = static_cast<int16_t>(ctx.height - toybox::kMargin - toybox::kPillHeight / 2);
  bool sawArchive = false;
  for (int x = toybox::kMargin; x < ctx.width - toybox::kMargin; x += 8) {
    const fui::ActionEvent event = out.tap(x, footerY);
    if (event.action == instapaperui::ActionArchive) sawArchive = true;
    CHECK(event.action != instapaperui::ActionPageNext);
  }
  CHECK(sawArchive);
}

// ARCHIVE is the one control in this app that changes anything outside the
// screen it is on, and it used to be the WIDE MIDDLE of the reader's footer --
// the easiest target on the panel, directly between the two controls a reader
// taps on every page. A miss while paging took the article away, silently.
//
// This asserts the geometry that fixes it, over every pixel of the bar rather
// than over three sampled points: no archive pixel may sit between two page
// pixels, and the two families may not touch.
void testArchiveIsNotBetweenThePageControls() {
  Rendered out;
  instapaperui::ReaderModel model = instaArticleModel();
  model.canPagePrev = true;
  model.canPageNext = true;
  buildInstaReader(out, model);

  const fui::DeviceContext ctx = device();
  const int16_t footerY = static_cast<int16_t>(ctx.height - toybox::kMargin - toybox::kPillHeight / 2);
  int archiveLeft = ctx.width;
  int archiveRight = -1;
  int pageLeft = ctx.width;
  int archivePixels = 0;
  int prevPixels = 0;
  int nextPixels = 0;
  for (int x = 0; x < ctx.width; ++x) {
    const fui::ActionEvent event = out.tap(x, footerY);
    if (event.action == instapaperui::ActionArchive) {
      ++archivePixels;
      if (x < archiveLeft) archiveLeft = x;
      archiveRight = x;
    }
    if (event.action == instapaperui::ActionPagePrev) ++prevPixels;
    if (event.action == instapaperui::ActionPageNext) ++nextPixels;
    if ((event.action == instapaperui::ActionPagePrev || event.action == instapaperui::ActionPageNext) &&
        x < pageLeft) {
      pageLeft = x;
    }
  }
  CHECK(archivePixels > 0);
  CHECK(prevPixels > 0);
  CHECK(nextPixels > 0);
  // Every archive pixel is left of every page pixel.
  CHECK(archiveRight < pageLeft);
  // And the two do not touch: a thumb that misses a page control has a gap to
  // cross before it reaches the destructive one.
  CHECK(pageLeft - archiveRight > toybox::kGutter);
  // And the word survives whole. Its box is sized from the label rather than
  // as a fraction of the bar, because a fraction is a number nobody re-checks
  // when the reading face changes under it.
  CHECK(drewLabelWhole(out, "ARCHIVE"));
  CHECK(archiveLeft >= 0);
  // And the page controls keep a box a thumb can hit, which is the constraint
  // the archive box is capped BY rather than a second number about it.
  CHECK(prevPixels >= fui::ButtonProps{}.minTouchSize);
  CHECK(nextPixels >= fui::ButtonProps{}.minTouchSize);
}

// The undo lives on the queue and only while there is something to undo. It is
// what makes a mis-tapped archive recoverable without charging every
// deliberate archive a confirmation tap.
void testTheQueueOffersUndoOnlyAfterAnArchive() {
  fui::ListItem row{};
  row.label = "Something to read";
  row.subtitle = "6 min . example.com";
  row.value = "";
  row.actionValue = 0;

  const fui::DeviceContext ctx = device();
  const int16_t footerY = static_cast<int16_t>(ctx.height - toybox::kMargin - toybox::kPillHeight / 2);

  Rendered quiet;
  instapaperui::QueueModel model;
  model.items = &row;
  model.count = 1;
  buildInstaQueue(quiet, model);
  CHECK(!drewText(quiet, "PUT BACK"));
  for (int x = toybox::kMargin; x < ctx.width - toybox::kMargin; x += 8) {
    CHECK(quiet.tap(x, footerY).action != instapaperui::ActionUndoArchive);
  }

  Rendered offered;
  model.canUndoArchive = true;
  buildInstaQueue(offered, model);
  // Either label is fine -- the builder drops to the short one at a cut where
  // the long one will not fit -- but whichever it drew has to fit its box.
  CHECK(drewLabelWhole(offered, "PUT BACK") || drewLabelWhole(offered, "BACK"));
  bool sawUndo = false;
  bool sawSync = false;
  for (int x = toybox::kMargin; x < ctx.width - toybox::kMargin; x += 4) {
    const fui::ActionId action = offered.tap(x, footerY).action;
    if (action == instapaperui::ActionUndoArchive) sawUndo = true;
    if (action == instapaperui::ActionSync) sawSync = true;
  }
  // Both, because an undo that took the whole bar would cost the reader the
  // control they came to this screen for.
  CHECK(sawUndo);
  CHECK(sawSync);
  CHECK(!offered.interactions.overflowed());
}

// A title wider than the band must be cut on a word and marked, never clipped
// mid-word: a word broken in half reads as a rendering fault.
void testALongTitleIsEllipsisedRatherThanClipped() {
  Rendered out;
  instapaperui::ReaderModel model = instaArticleModel();
  model.title =
      "An extremely long article title that could not possibly fit across the header band of this panel at any cut";
  buildInstaReader(out, model);
  CHECK(drewText(out, "..."));
}

void testTheReaderTextGoesInTheReaderBody() {
  Rendered out;
  buildInstaReader(out, instaArticleModel());
  const fui::Rect body = instapaperui::readerBody(device());
  bool drewInside = false;
  for (const auto& run : out.target.texts) {
    if (run.text.find("Some words") == std::string::npos) continue;
    drewInside = run.rect.y >= body.y && run.rect.bottom() <= body.bottom();
  }
  CHECK(drewInside);
}

// One unbreakable token wider than its box. The word-boundary rule has nothing
// to work with, and before this it returned the ellipsis and nothing else --
// which is how the Instapaper pairing screen came to ask "IS THIS YOU?" over a
// row reading "...". Half an address beats none of one.
// A tap places the marker, so every slot on the strip must be reachable by one.
// A rounding error at either end silently makes slot 1 or slot 20 untappable,
// and those are the two the deck's clearest clues point at.
// Every reachable WAVELENGTH screen must offer a way onward that is not the
// hardware Back key. A practice reveal once lost its NEXT ROUND button to an
// early return and looked entirely finished without it: a cold table tried
// fourteen different gestures and sixteen seconds of waiting on the first round
// of the very first session.
// Nothing is drawn through anything else. Three separate times this app moved
// or added one element and did not check what it landed on: a large session
// average composited into a small reference number on the end screen, and a
// hairline rule struck straight through the label under the guess. Both looked
// completely finished in code and were only visible in a render.
//
// Two checks, because the two failures have different shapes: no two pieces of
// text may overlap, and a RULE -- a fill thin enough to be a hairline -- may not
// cross any text. Thick fills are buttons and legitimately sit under their own
// labels, so they are excluded rather than special-cased away.
void testWavelengthNothingIsDrawnThroughAnything() {
  const auto inkOf = [](const FakeTarget::TextRun& run) {
    const int16_t measured = static_cast<int16_t>(run.text.size() * 10);
    const int16_t w = measured < run.rect.width ? measured : run.rect.width;
    int16_t x = run.rect.x;
    if (run.style.align == fui::TextAlign::Right)
      x = static_cast<int16_t>(run.rect.x + run.rect.width - w);
    else if (run.style.align == fui::TextAlign::Center)
      x = static_cast<int16_t>(run.rect.x + (run.rect.width - w) / 2);
    return fui::Rect{x, run.rect.y, w, run.rect.height};
  };
  const auto overlaps = [](const fui::Rect& a, const fui::Rect& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
  };
  struct Case {
    const char* name;
    void (*build)(Rendered&);
  };
  static const Case kCases[] = {
      {"dial",
       [](Rendered& out) {
         const fui::DeviceContext ctx = device();
         const fui::InputSnapshot noInput{};
         toybox::Frame frame(out.target, ctx, noInput, out.interactions);
         toybox::Screen screen(frame, toybox::themeTokens());
         wavelengthui::DialModel m;
         m.spectrum = wavelengthui::Spectrum{"UNDERRATED LETTER OF THE ALPHABET", "MOVIE THAT GODZILLA WOULD IMPROVE"};
         m.guess = 13;
         m.roundNumber = 12;
         wavelengthui::renderDial(screen, m);
       }},
      {"summary",
       [](Rendered& out) {
         const fui::DeviceContext ctx = device();
         const fui::InputSnapshot noInput{};
         toybox::Frame frame(out.target, ctx, noInput, out.interactions);
         toybox::Screen screen(frame, toybox::themeTokens());
         wavelengthui::SummaryModel m;
         m.rounds = 7;
         m.total = 19;
         m.averageTenths = 27;
         wavelengthui::renderSummary(screen, m);
       }},
      // Every screen the 2026-09-01 wording pass re-laid out. Two screens were
      // covered here and eight were not, which is why a rule through a label
      // had to be found by looking at a render.
      {"how to play",
       [](Rendered& out) {
         const fui::DeviceContext ctx = device();
         const fui::InputSnapshot noInput{};
         toybox::Frame frame(out.target, ctx, noInput, out.interactions);
         toybox::Screen screen(frame, toybox::themeTokens());
         wavelengthui::renderHowTo(screen);
       }},
      {"menu, no session",
       [](Rendered& out) {
         const fui::DeviceContext ctx = device();
         const fui::InputSnapshot noInput{};
         toybox::Frame frame(out.target, ctx, noInput, out.interactions);
         toybox::Screen screen(frame, toybox::themeTokens());
         wavelengthui::MenuModel m;
         wavelengthui::renderMenu(screen, m);
       }},
      {"menu, session running",
       [](Rendered& out) {
         const fui::DeviceContext ctx = device();
         const fui::InputSnapshot noInput{};
         toybox::Frame frame(out.target, ctx, noInput, out.interactions);
         toybox::Screen screen(frame, toybox::themeTokens());
         wavelengthui::MenuModel m;
         m.sessionInProgress = true;
         m.sessionRound = 7;
         m.sessionTotal = 8;
         m.sessionScored = 5;
         wavelengthui::renderMenu(screen, m);
       }},
      {"pause",
       [](Rendered& out) {
         const fui::DeviceContext ctx = device();
         const fui::InputSnapshot noInput{};
         toybox::Frame frame(out.target, ctx, noInput, out.interactions);
         toybox::Screen screen(frame, toybox::themeTokens());
         wavelengthui::PauseModel m;
         m.roundNumber = 4;
         m.total = 11;
         m.abandoned = 2;
         wavelengthui::renderPause(screen, m);
       }},
      {"pass, abandoned",
       [](Rendered& out) {
         const fui::DeviceContext ctx = device();
         const fui::InputSnapshot noInput{};
         toybox::Frame frame(out.target, ctx, noInput, out.interactions);
         toybox::Screen screen(frame, toybox::themeTokens());
         wavelengthui::PassModel m;
         m.roundNumber = 4;
         m.total = 11;
         m.abandoned = true;
         m.abandonedCount = 2;
         wavelengthui::renderPassLeft(screen, m);
       }},
      {"pass, practice",
       [](Rendered& out) {
         const fui::DeviceContext ctx = device();
         const fui::InputSnapshot noInput{};
         toybox::Frame frame(out.target, ctx, noInput, out.interactions);
         toybox::Screen screen(frame, toybox::themeTokens());
         wavelengthui::PassModel m;
         m.practice = true;
         wavelengthui::renderPassLeft(screen, m);
       }},
      {"clue",
       [](Rendered& out) {
         const fui::DeviceContext ctx = device();
         const fui::InputSnapshot noInput{};
         toybox::Frame frame(out.target, ctx, noInput, out.interactions);
         toybox::Screen screen(frame, toybox::themeTokens());
         wavelengthui::ClueModel m;
         m.spectrum = wavelengthui::Spectrum{"HOT", "COLD"};
         wavelengthui::renderClue(screen, m);
       }},
      {"peek, revealed",
       [](Rendered& out) {
         const fui::DeviceContext ctx = device();
         const fui::InputSnapshot noInput{};
         toybox::Frame frame(out.target, ctx, noInput, out.interactions);
         toybox::Screen screen(frame, toybox::themeTokens());
         wavelengthui::PeekModel m;
         m.spectrum = wavelengthui::Spectrum{"HOT", "COLD"};
         m.target = 14;
         m.revealed = true;
         m.everRevealed = true;
         wavelengthui::renderPeek(screen, m);
       }},
      {"reveal, scored",
       [](Rendered& out) {
         const fui::DeviceContext ctx = device();
         const fui::InputSnapshot noInput{};
         toybox::Frame frame(out.target, ctx, noInput, out.interactions);
         toybox::Screen screen(frame, toybox::themeTokens());
         wavelengthui::RevealModel m;
         m.spectrum = wavelengthui::Spectrum{"HOT", "COLD"};
         m.guess = 12;
         m.target = 13;
         m.points = 4;
         m.callWasRight = true;
         m.roundNumber = 4;
         m.total = 11;
         wavelengthui::renderReveal(screen, m);
       }},
      // EXACT and TWO OFF depend on a random target and did not come up in
      // twenty-five driven rounds, so the only place their layout is exercised
      // is here: EXACT also takes the side call's NOT NEEDED branch.
      {"reveal, exact",
       [](Rendered& out) {
         const fui::DeviceContext ctx = device();
         const fui::InputSnapshot noInput{};
         toybox::Frame frame(out.target, ctx, noInput, out.interactions);
         toybox::Screen screen(frame, toybox::themeTokens());
         wavelengthui::RevealModel m;
         m.spectrum = wavelengthui::Spectrum{"HOT", "COLD"};
         m.guess = 9;
         m.target = 9;
         m.points = wavelength::kPointsExact;
         m.roundNumber = 6;
         m.total = 17;
         wavelengthui::renderReveal(screen, m);
       }},
      {"reveal, two off",
       [](Rendered& out) {
         const fui::DeviceContext ctx = device();
         const fui::InputSnapshot noInput{};
         toybox::Frame frame(out.target, ctx, noInput, out.interactions);
         toybox::Screen screen(frame, toybox::themeTokens());
         wavelengthui::RevealModel m;
         m.spectrum = wavelengthui::Spectrum{"HOT", "COLD"};
         m.guess = 9;
         m.target = 11;
         m.points = wavelength::kPointsOffByTwo;
         m.roundNumber = 7;
         m.total = 18;
         wavelengthui::renderReveal(screen, m);
       }},
      {"reveal, practice",
       [](Rendered& out) {
         const fui::DeviceContext ctx = device();
         const fui::InputSnapshot noInput{};
         toybox::Frame frame(out.target, ctx, noInput, out.interactions);
         toybox::Screen screen(frame, toybox::themeTokens());
         wavelengthui::RevealModel m;
         m.spectrum = wavelengthui::Spectrum{"HOT", "COLD"};
         m.guess = 6;
         m.target = 9;
         m.practice = true;
         wavelengthui::renderReveal(screen, m);
       }},
  };

  for (const Case& c : kCases) {
    Rendered out;
    c.build(out);
    const auto& texts = out.target.texts;
    for (size_t i = 0; i < texts.size(); ++i) {
      for (size_t j = i + 1; j < texts.size(); ++j) {
        if (!overlaps(inkOf(texts[i]), inkOf(texts[j]))) continue;
        std::printf("  %s: %s overlaps %s\n", c.name, texts[i].text.c_str(), texts[j].text.c_str());
        CHECK(false);
        return;
      }
    }
    for (const fui::Rect& f : out.target.fills) {
      if (f.height > toybox::kRule) continue;  // a rule, not a button
      for (const FakeTarget::TextRun& t : texts) {
        if (!overlaps(f, inkOf(t))) continue;
        std::printf("  %s: a rule is drawn through %s\n", c.name, t.text.c_str());
        CHECK(false);
        return;
      }
    }
  }
}

void testWavelengthEveryRevealOffersAWayOn() {
  for (const bool practice : {false, true}) {
    Rendered out;
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    wavelengthui::RevealModel model;
    model.spectrum = wavelengthui::Spectrum{"HOT", "COLD"};
    model.practice = practice;
    model.guess = 7;
    model.target = 9;
    wavelengthui::renderReveal(screen, model);
    bool found = false;
    for (const FakeTarget::TextRun& run : out.target.texts)
      if (run.text == "NEXT ROUND") found = true;
    if (!found) std::printf("  reveal with practice=%d has no way forward\n", static_cast<int>(practice));
    CHECK(found);
  }
}

// The two ends of one spectrum are a single object and must be set at a single
// size. Sized independently, the longer pole dropped a whole cut: PHYSICAL
// ACTIVITY printed at half the height of MENTAL ACTIVITY in the same card, and
// a cold table read the pair as a heading with a subheading rather than as two
// ends of a scale.
void testWavelengthSpectrumEndsShareOneSize() {
  const struct {
    const char* top;
    const char* bottom;
  } kPairs[] = {
      {"MENTAL ACTIVITY", "PHYSICAL ACTIVITY"},
      {"HOT", "UNDERRATED LETTER OF THE ALPHABET"},
      {"MOVIE THAT GODZILLA WOULD IMPROVE", "COLD"},
      {"LOUD", "QUIET"},
  };
  for (const auto& pair : kPairs) {
    Rendered out;
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    wavelengthui::PickModel model;
    model.first = wavelengthui::Spectrum{pair.top, pair.bottom};
    model.second = wavelengthui::Spectrum{"NEAR", "FAR"};
    wavelengthui::renderPick(screen, model);

    fui::FontId topFont = 0;
    fui::FontId bottomFont = 0;
    for (const FakeTarget::TextRun& run : out.target.texts) {
      if (run.text == pair.top) topFont = run.style.font;
      if (run.text == pair.bottom) bottomFont = run.style.font;
    }
    if (topFont != bottomFont)
      std::printf("  %s / %s drawn in different slots (%d vs %d)\n", pair.top, pair.bottom, static_cast<int>(topFont),
                  static_cast<int>(bottomFont));
    CHECK(topFont == bottomFont);
  }
}

// Four fixes that a reconciliation silently dropped once and shipped. Each has
// a test now rather than a claim in a release note, because a note is written
// by whoever did the merge and these were lost by exactly that person checking
// one place and assuming the rest.
void testWavelengthTheFourThatWereDropped() {
  const int16_t w = 480;
  const int16_t h = 800;

  // 1. THE RESULT MUST NOT DRAW A BUTTON WHERE THE FINGER ALREADY IS. The lock
  // fires while the thumb is down, so the result appears under it; if its
  // NEXT ROUND shares the lock bar's rect, releasing presses it and the round's
  // whole payoff is gone before the table sees it.
  const fui::Rect lockBar = wavelengthui::lockBarRect(w, h);
  Rendered rev;
  {
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(rev.target, ctx, noInput, rev.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    wavelengthui::RevealModel m;
    m.spectrum = wavelengthui::Spectrum{"HOT", "COLD"};
    m.guess = 13;
    m.target = 10;
    wavelengthui::renderReveal(screen, m);
  }
  bool clash = false;
  for (const fui::Rect& f : rev.target.fills) {
    if (f.height <= toybox::kRule) continue;
    const bool overlapsLock = f.x < lockBar.x + lockBar.width && lockBar.x < f.x + f.width &&
                              f.y < lockBar.y + lockBar.height && lockBar.y < f.y + f.height;
    if (overlapsLock) clash = true;
  }
  if (clash) std::printf("  the reveal draws a button over the lock bar's rect\n");
  CHECK(!clash);

  // 2. THE LOCK BAR MUST NOT REACH THE BOTTOM CORNERS, where a thumb rests when
  // a portrait slab is lifted off a table. It locked the guess at the untouched
  // default with nobody having decided anything.
  CHECK(lockBar.x > toybox::kMargin);
  CHECK(lockBar.x + lockBar.width < w - toybox::kMargin);

  // 3. THE STRIP'S LEFT GUTTER IS NOT THE STRIP. The numerals hang left of the
  // board; a tap at x=25 moved the table's guess.
  bool gutterLive = false;
  for (int16_t y = 0; y < h; ++y)
    if (wavelengthui::dialSlotAt(w, h, 25, y) != 0) gutterLive = true;
  if (gutterLive) std::printf("  a tap in the numeral gutter moves the guess\n");
  CHECK(!gutterLive);

  // 4. BEFORE THE NUMBER HAS BEEN SEEN THERE IS NO SECOND BUTTON. A disabled
  // one whose label is an imperative reads as the other way to do the thing;
  // testers in two separate rounds tapped it and concluded the device had
  // frozen. Exactly one filled control on that screen until it has been held.
  Rendered peek;
  {
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(peek.target, ctx, noInput, peek.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    wavelengthui::PeekModel m;
    m.spectrum = wavelengthui::Spectrum{"HOT", "COLD"};
    m.target = 8;
    m.everRevealed = false;
    wavelengthui::renderPeek(screen, m);
  }
  int wideBars = 0;
  for (const fui::Rect& f : peek.target.fills)
    if (f.height > 30 && f.width > 200) ++wideBars;
  if (wideBars != 1) std::printf("  peek shows %d full-width bars before the number is seen, want 1\n", wideBars);
  CHECK(wideBars == 1);
}

// THE LOCK IS AN ORDINARY BUTTON, and the stray tap it used to guard against is
// stopped by geometry instead of by a duration.
//
// It shipped as HOLD TO LOCK: the activity watched for 600ms of held finger on
// the bar's rect and fired while the finger was still down. Two things were
// wrong with that. Nothing on the panel said 600 -- a hold whose duration is
// invisible is a guessing game, not a safeguard -- and firing mid-contact meant
// the reveal drew under a finger that was already down, so the lift-off pressed
// whatever the new screen put there. Four cold testers advanced past their own
// score without ever seeing it.
//
// What the hold was really buying is that this control sits in the same footer
// band as the strip the table has just been tapping. That is what the geometry
// now buys instead, and these checks are the ones that go red if it drifts back.
void testWavelengthTheLockIsAnOrdinaryButton() {
  const int16_t w = 480;
  const int16_t h = 800;
  const fui::Rect lockBar = wavelengthui::lockBarRect(w, h);

  Rendered dial;
  {
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(dial.target, ctx, noInput, dial.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    wavelengthui::DialModel m;
    m.spectrum = wavelengthui::Spectrum{"HOT", "COLD"};
    m.guess = 13;
    wavelengthui::renderDial(screen, m);
  }

  // 1. ONE PRESS LOCKS. The rect has to carry the action, because that is what
  // makes the frame route it on the touch RELEASE like every other control in
  // the fork. With no action on it the bar was inert to the router and only the
  // activity's hold timer could commit.
  const int16_t midX = static_cast<int16_t>(lockBar.x + lockBar.width / 2);
  const int16_t midY = static_cast<int16_t>(lockBar.y + lockBar.height / 2);
  if (dial.tap(midX, midY).action != wavelengthui::ActionLock)
    std::printf("  a tap in the middle of the lock bar does not lock\n");
  CHECK(dial.tap(midX, midY).action == wavelengthui::ActionLock);
  // And across the whole face of it, not just the centre.
  bool everyPixelLocks = true;
  for (int16_t x = lockBar.x; x < lockBar.x + lockBar.width; x = static_cast<int16_t>(x + 4))
    for (int16_t y = lockBar.y; y < lockBar.y + lockBar.height; y = static_cast<int16_t>(y + 4))
      if (dial.tap(x, y).action != wavelengthui::ActionLock) everyPixelLocks = false;
  CHECK(everyPixelLocks);

  // 2. AND IT SAYS SO. A label asking for a hold is the thing Mario named: the
  // player cannot know whether it wants 200ms or four seconds.
  bool sawLabel = false;
  bool askedForAHold = false;
  for (const FakeTarget::TextRun& run : dial.target.texts) {
    if (run.text == "LOCK IT IN") sawLabel = true;
    if (run.text.find("HOLD") != std::string::npos) askedForAHold = true;
  }
  if (!sawLabel) std::printf("  the dial has no LOCK IT IN button\n");
  if (askedForAHold) std::printf("  the dial still asks for a hold\n");
  CHECK(sawLabel);
  CHECK(!askedForAHold);

  // 3. THE STRIP'S COLUMN AND THE BUTTON'S COLUMN ARE DISJOINT. This is the
  // replacement for the hold and the only one of these checks that stops the
  // stray tap the hold existed for: the table moves the marker by tapping the
  // strip, dozens of times a round, and the bar used to span x=80..399 while
  // dialSlotAt answers out to x=226. A finger sliding off the bottom of the
  // board was over the commit control. Now nothing below the strip is live at
  // all -- not a smaller target, no target.
  bool sharesAColumn = false;
  for (int16_t x = lockBar.x; x < lockBar.x + lockBar.width && !sharesAColumn; ++x)
    for (int16_t y = 0; y < h; ++y)
      if (wavelengthui::dialSlotAt(w, h, x, y) != 0) {
        std::printf("  the lock button shares column x=%d with the strip (slot at y=%d)\n", static_cast<int>(x),
                    static_cast<int>(y));
        sharesAColumn = true;
        break;
      }
  CHECK(!sharesAColumn);
  // The other direction: nothing that moves the marker can also lock.
  bool oneTapDoesBoth = false;
  for (int16_t x = 0; x < w && !oneTapDoesBoth; ++x)
    for (int16_t y = 0; y < h; y = static_cast<int16_t>(y + 3)) {
      if (wavelengthui::dialSlotAt(w, h, x, y) == 0) continue;
      if (dial.tap(x, y).action != wavelengthui::ActionLock) continue;
      std::printf("  a tap at (%d,%d) both moves the marker and locks it\n", static_cast<int>(x), static_cast<int>(y));
      oneTapDoesBoth = true;
      break;
    }
  CHECK(!oneTapDoesBoth);

  // 4. AND THERE IS DEAD PAPER BETWEEN THEM, not merely a column boundary: the
  // strip's live region has to stop well above the button, or an overshoot that
  // drifts right lands on it anyway.
  int16_t lowestLive = 0;
  for (int16_t y = 0; y < h; ++y)
    for (int16_t x = 0; x < w; ++x)
      if (wavelengthui::dialSlotAt(w, h, x, y) != 0 && y > lowestLive) lowestLive = y;
  if (lockBar.y - lowestLive < 32)
    std::printf("  only %dpx of paper between the strip and the lock button\n",
                static_cast<int>(lockBar.y - lowestLive));
  CHECK(lockBar.y - lowestLive >= 32);

  // 5. NEITHER BOTTOM CORNER. Stronger than testWavelengthTheFourThatWereDropped
  // asks for, which is the point: that test set the floor at the old 64px inset
  // and the button no longer needs to be anywhere near the left one.
  CHECK(lockBar.x > toybox::kMargin + 64);
  CHECK(lockBar.x + lockBar.width <= w - toybox::kMargin - 64);

  // 6. THE REVEAL PUTS NOTHING WHERE THE LOCK WAS. testWavelengthTheFourThatWereDropped
  // checks this against the FILLS, which catches a button drawn there; this
  // checks the routing table, which is the thing that actually fires. The rule
  // is about the rect's MEANING changing across the transition, so separating
  // the coordinates is the only defence -- the touch table is live before the
  // panel has painted, so "the action is harmless" is not one.
  Rendered reveal;
  {
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(reveal.target, ctx, noInput, reveal.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    wavelengthui::RevealModel m;
    m.spectrum = wavelengthui::Spectrum{"HOT", "COLD"};
    m.guess = 13;
    m.target = 10;
    wavelengthui::renderReveal(screen, m);
  }
  bool revealAnswersUnderTheLock = false;
  for (int16_t x = lockBar.x; x < lockBar.x + lockBar.width && !revealAnswersUnderTheLock;
       x = static_cast<int16_t>(x + 2))
    for (int16_t y = lockBar.y; y < lockBar.y + lockBar.height; y = static_cast<int16_t>(y + 2)) {
      const fui::ActionId landed = reveal.tap(x, y).action;
      if (landed == 0) continue;
      std::printf("  the reveal answers action %d at (%d,%d), inside the lock button's rect\n",
                  static_cast<int>(landed), static_cast<int>(x), static_cast<int>(y));
      revealAnswersUnderTheLock = true;
      break;
    }
  CHECK(!revealAnswersUnderTheLock);
  // And the reverse: the reveal's own control must not sit over anything that
  // locks, or a double tap on NEXT ROUND would commit the next round's guess.
  bool sharedPixel = false;
  for (int16_t x = 0; x < w && !sharedPixel; x = static_cast<int16_t>(x + 2))
    for (int16_t y = 0; y < h; y = static_cast<int16_t>(y + 2)) {
      if (reveal.tap(x, y).action != wavelengthui::ActionNextRound) continue;
      if (dial.tap(x, y).action != wavelengthui::ActionLock) continue;
      std::printf("  NEXT ROUND and LOCK IT IN share the pixel (%d,%d)\n", static_cast<int>(x), static_cast<int>(y));
      sharedPixel = true;
      break;
    }
  CHECK(!sharedPixel);

  CHECK(!dial.interactions.overflowed());
}

void testWavelengthEverySlotIsTappable() {
  const int16_t w = 480;
  const int16_t h = 800;
  bool seen[wavelength::kSlots + 1] = {};
  for (int16_t y = 0; y < h; ++y) {
    const int slot = wavelengthui::dialSlotAt(w, h, 140, y);
    if (slot >= 1 && slot <= wavelength::kSlots) seen[slot] = true;
  }
  for (int i = 1; i <= wavelength::kSlots; ++i) {
    if (!seen[i]) std::printf("  slot %d cannot be tapped\n", i);
    CHECK(seen[i]);
  }

  // One slot of overshoot at either end clamps to that end rather than being
  // ignored. Beyond that it is off the board and must stay inert.
  bool sawTop = false;
  bool sawBottom = false;
  bool clampedFarAway = false;
  for (int16_t y = 0; y < h; ++y) {
    const int slot = wavelengthui::dialSlotAt(w, h, 140, y);
    if (slot == wavelength::kSlots) sawTop = true;
    if (slot == 1) sawBottom = true;
  }
  CHECK(sawTop);
  CHECK(sawBottom);
  if (wavelengthui::dialSlotAt(w, h, 140, 0) != 0) clampedFarAway = true;
  if (wavelengthui::dialSlotAt(w, h, 140, static_cast<int16_t>(h - 1)) != 0) clampedFarAway = true;
  if (clampedFarAway) std::printf("  a tap far off the board still moves the mark\n");
  CHECK(!clampedFarAway);

  // And the other half: the instruction column is not part of the board.
  for (int16_t y = 0; y < h; ++y) {
    if (wavelengthui::dialSlotAt(w, h, 300, y) != 0) {
      std::printf("  tapping the instruction column at y=%d moves the mark\n", static_cast<int>(y));
      CHECK(false);
      return;
    }
  }
}

void testFitLinesCutsAnUnbreakableTokenRatherThanVanishing() {
  Rendered out;
  const fui::TextStyle style = toybox::themeTokens().bodyText;
  const std::string fitted = toybox::fitLines(out.target, "mario@averylongdomainnameindeed.example.com", 80, 1, style);
  CHECK(fitted.size() > 3);
  CHECK(fitted.rfind("...") == fitted.size() - 3);
  CHECK(fitted.compare(0, 5, "mario") == 0);
  // And it still fits, which is the whole point of cutting it.
  CHECK(out.target.measureText(style.font, fitted.c_str(), style).width <= 80);

  // A box too narrow for even one character plus the mark gives back nothing
  // rather than a bare ellipsis, so a caller drawing it shows an empty row
  // instead of a row that looks like it lost its content.
  CHECK(toybox::fitLines(out.target, "mario@example.com", 4, 1, style).empty());

  // The ordinary case is untouched: a sentence wide enough for several words
  // still breaks between them and never mid-word. Width chosen to hold more
  // than one word, or this would assert nothing.
  const std::string sentence = toybox::fitLines(out.target, "one two three four five six seven", 240, 1, style);
  CHECK(sentence.find(' ') != std::string::npos);
  CHECK(sentence.find("...") != std::string::npos);
  // The cut lands on a boundary: the character before the mark is not a
  // fragment of a word that continues.
  const std::string kept = sentence.substr(0, sentence.size() - 3);
  CHECK(std::string("one two three four five six seven").compare(0, kept.size(), kept) == 0);
}

// --- trivia ------------------------------------------------------------------

// Every option must register its OWN index. Frame::hit's value parameter
// defaults to 0, so all four boxes reported option 1: solo scoring was decided
// by whether the answer happened to land in the top slot, and a cold tester
// measured 3/12 across twelve questions, which is chance. It shipped in v1.12.0.
//
// Nothing caught it because nothing in this repo had ever tapped a solo option
// -- shoot-trivia.sh taps QUIZMASTER and REVEAL, and no host suite compiled
// these screens at all until now. Taps are routed against the table the paint
// produced, so this fails if the index is dropped again.
void testTriviaOptionsCarryTheirIndex() {
  triviaui::ChoiceModel model;
  model.clue = "Which one?";
  static const char* kLabels[trivia::kOptions] = {"ALPHA", "BRAVO", "CHARLIE", "DELTA"};
  for (int i = 0; i < trivia::kOptions; ++i) model.option[i] = kLabels[i];
  model.correct = 2;

  Rendered out;
  buildChoice(out, model);

  for (int i = 0; i < trivia::kOptions; ++i) {
    const FakeTarget::TextRun* run = out.target.find(kLabels[i]);
    CHECK(run != nullptr);
    if (run == nullptr) continue;
    const fui::ActionEvent event = out.tap(run->rect.x + run->rect.width / 2, run->rect.y + run->rect.height / 2);
    CHECK(event.action == triviaui::ActionOption);
    CHECK(event.value == i);
  }
}

// The way out, in both states and in the SAME place. Solo had no exit at all:
// no footer action before an answer, no header target, and the app is
// touch-only, so Back did nothing and only the HOME key escaped -- which also
// meant there was no way to finish deliberately and see a score.
// With no question at the chosen difficulty the clue carries the message and
// there are no options -- so no option boxes, and nothing tappable that would
// score a question that is not there. Found by looking at a render, not by a
// suite: four empty boxes draw exactly like four real ones.
void testTriviaDrawsNoOptionsWithoutAQuestion() {
  triviaui::ChoiceModel model;
  model.clue = "No multiple-choice question available at this difficulty.";
  // option[] left null, which is what the activity passes in this state.

  Rendered out;
  buildChoice(out, model);

  CHECK(out.target.drew("No multiple-choice question available at this difficulty."));
  // No question means no difficulty meter. Five pips beside that message
  // described a question that was not there, filled from a default rather than
  // from anything the player had set.
  CHECK(!out.target.drew("DIFFICULTY"));
  // The way out is still offered; it is the only control that should exist here.
  CHECK(out.target.drew("END"));

  // Nothing in the option band answers a tap. The boxes sat above the footer,
  // so probe the band rather than one point.
  for (int y = 430; y <= 700; y += 30) {
    const fui::ActionEvent event = out.tap(240, y);
    CHECK(event.action != triviaui::ActionOption);
  }
}

void testTriviaAlwaysOffersAWayOut() {
  triviaui::ChoiceModel model;
  model.clue = "Which one?";
  static const char* kLabels[trivia::kOptions] = {"ALPHA", "BRAVO", "CHARLIE", "DELTA"};
  for (int i = 0; i < trivia::kOptions; ++i) model.option[i] = kLabels[i];
  model.correct = 2;

  fui::Rect unanswered{};
  {
    Rendered out;
    buildChoice(out, model);
    CHECK(out.target.drew("END"));
    CHECK(!out.target.drew("NEXT"));  // nothing to advance to yet
    const FakeTarget::TextRun* end = out.target.find("END");
    CHECK(end != nullptr);
    if (end != nullptr) {
      unanswered = end->rect;
      const fui::ActionEvent event = out.tap(end->rect.x + end->rect.width / 2, end->rect.y + end->rect.height / 2);
      CHECK(event.action == triviaui::ActionQuit);
    }
  }

  model.chosen = 0;
  {
    Rendered out;
    buildChoice(out, model);
    CHECK(out.target.drew("END"));
    CHECK(out.target.drew("NEXT"));
    const FakeTarget::TextRun* end = out.target.find("END");
    CHECK(end != nullptr);
    if (end != nullptr) {
      // Same place with NEXT beside it. A way out that moves under the finger
      // when the question is answered would be its own bug.
      CHECK(end->rect.x == unanswered.x);
      CHECK(end->rect.y == unanswered.y);
      const fui::ActionEvent event = out.tap(end->rect.x + end->rect.width / 2, end->rect.y + end->rect.height / 2);
      CHECK(event.action == triviaui::ActionQuit);
    }
  }
}

int main() {
  testTriviaOptionsCarryTheirIndex();
  testTriviaAlwaysOffersAWayOut();
  testTriviaDrawsNoOptionsWithoutAQuestion();
  testTheSeaSaltCardYouTapIsTheCardTheRulesGet();
  testTheSeaSaltChromeIsTappableAndTheCallPillIsEarned();
  testTheSeaSaltCallChoiceSaysWhatEachWordCosts();
  testTheSeaSaltRoundOverNamesTheBet();
  testTheSeaSaltCardBandsNeverCollide();
  testEverySeaSaltHintFitsTheBox();
  testTheSeaSaltTutorialPagesAndEnds();
  testEitherSideSeesItsOwnHqAtTheBottom();
  testTheFinishedBoardCarriesItsOwnEnding();
  testEveryRulesPositionCouldExist();
  testTheTerrainCardNeverTruncatesWhatItDraws();
  testNoRulesPageDrawsOverItsOwnButtons();
  testTheForeheadKeyLabelsSitOnTheEdgesTheyAct();
  testTheForeheadRoundIgnoresTapsWhereFingersGrip();
  testTheForeheadCardNeverDrawsPastItsBox();
  testTheForeheadPagingWraps();
  testTheForeheadResetSaysWhatItDestroysAndAsksFirst();
  testTheForeheadStartControlLooksLikeAButton();
  testTheForeheadPickerReportsAbsoluteCategories();
  testTheForeheadResultsMarkTheUnansweredCardApart();
  testToyBattleShell();
  testAFrozenCardLooksDifferent();
  testSearchingAsksNothing();
  testTheSudokuGridAndItsHitTestAreExactInverses();
  testTheSudokuPadAndItsHitTestAreExactInverses();
  testTheSudokuBoardSpendsThreeInteractions();
  testTheSudokuCapsuleIsInertUntilTheGridIsFinished();
  testTheSudokuUndoDimsRatherThanVanishing();
  testEverySudokuScreenStaysOnThePanel();
  testEverySudokuLessonPagesAndClearsItsButton();
  testTheSudokuOrnamentCarriesTheGame();
  testTheSudokuFrontDoorNeverSharesInkBetweenTwoLines();
  testMurdleGridResolvesEveryCellItDrew();
  testMurdleGridEdgesAreLive();
  testMurdleGridDrawsMarksItIsGiven();
  testMurdleClueFaceIsPagedAndNeverOverflows();
  testMurdleSettingsPicksAnAbsoluteTier();
  testMurdleAccusationIsInertUntilComplete();
  testMurdleMenuHeadlineIsTheDoorAcrossItsWidth();
  testSeatsSayWhatEachPlayerHasDecided();
  testTheRematchShowsBothAnswers();
  testTheRematchBandIsNotTheWayOut();
  testTheLoneWayOutKeepsTheBottomBand();
  testACapsuleThatChangedMeaningWaitsForThePanel();
  testARepaintThatChangedNothingStillAnswers();
  testAnUnshownRebuildDoesNotCountAsShown();
  testACapsuleThatWasDeadMidGameAlsoWaits();
  testAControlComingBackToLifeAlsoWaits();
  testTheRevealGateWaitsForOnePaintAndThenLatches();
  testTheSurfaceGateHoldsAChangedMeaningAndPassesAnUnchangedOne();
  testMeaningsMixPositionally();
  testAPublishingBufferDigestsWhatThePanelIsShowing();
  testBeginBuildDigestsThePublishedGenerationNotTheBuildingOne();
  testAnOptionPopupHighlightRepaintStillAnswers();
  testAnOpponentWhoHasGoneTakesTheButtonWithThem();
  testRowModel();
  testSettingsOpenedFromTheMenuOffersOnlyPreferences();
  testSettingsScreen();
  testSettingsRouting();
  testBoardChrome();
  testConnectionsLostBoard();
  testConnectionsWonBoard();
  testConnectionsTilesShareOneSize();
  testConnectionsCalendarEveryDayIsReachable();
  testConnectionsMenuOrnamentOpensArchive();
  testConnectionsHowToFitsOnePage();
  testBattleshipStartMenu();
  testBattleshipCapsuleIsOnlyATriggerWhenItSaysSo();
  testBattleshipPlacementControls();
  testHnReaderFooter();
  testHnReaderDisabledControls();
  testHnReaderSwapLabelFollowsMode();
  testHnReaderTextStaysInItsRect();
  testHnNotice();
  testHnEveryNoticeCarriesAWayOff();
  testHnList();
  testHnEmptyFrontPageOffersAWayOnward();
  testHnEmptyStateStacksWithoutOverlap();
  testHnFitLines();
  testHnReaderShowsWhereYouAre();
  testHnSaveMarkIsLoudestWhenSaved();
  testHnAThreadCanBeKept();
  testTheColumnYouTapIsTheColumnTheRulesGet();
  testRowZeroIsDrawnAtTheBottom();
  testTheConnectFourGridKeepsOffTheChrome();
  testTheBoardSaysWhoseDrop();
  testTheConnectFourResultNamesTheOutcomeFromYourSeat();
  testTheRackShowsEveryTroopYouHold();
  testTheRackTileYouTapIsTheTroopYouGet();
  testAFullBoardDoesNotOverflowTheInteractionBuffer();
  testTheSquareYouTapIsTheSquareTheRulesGet();
  testTheBoardKeepsOffTheChrome();
  testTheBoardSaysWhoseMoveAndWho();
  testTheResultNamesTheOutcomeFromYourSeat();
  testTheCheckersHowToPagesAndEnds();
  testShelfFolderDrawsItsOwnNameAndRows();
  testShelfFolderMarksNoRow();
  testShelfIconsFollowTheRowsWhenTheListScrolls();
  testTheShelfPagesWhenAFolderOverflows();
  testAPageStepMovesExactlyOnePage();
  testTheShelfStepStopsAtBothEnds();
  testAFolderComesBackToThePageItWasLeftOn();
  testThePageMarksReadAsAControl();
  testARowOnARestoredPageOpensItsOwnGame();
  testAFolderWithoutADeviceNameHasNoFooter();
  testTheShelfFooterIsADoorWithAFaceOnIt();
  testPlayerOffersThreeSeparateWords();
  testPlayerWordsTileTheRowWithoutGapsOrOverlap();
  testPlayerDrawsTheFaceItsNameDescribes();
  testPlayerBackLeaves();
  testEveryWordHasTheArtworkItNames();
  testAnUnreadableNameDrawsThePlainHead();
  testABoardShowsWhoYouArePlaying();
  testBothSeatsWearTheirOwnFace();
  testStudyDeckLeadsWithTheCount();
  testStudyHeadlineIsTheHitTarget();
  testStudyDeckRowSwitchesOnlyWhenThereIsSomewhereToGo();
  testStudyOffersNothingWhenNothingIsDue();
  testStudyForecastBarsStayInsideTheirPanel();
  testStudyRecordShowsTheStreak();
  testStudyPanelSaysSoWhenItHasNothing();
  testStudyWarnsWhenAReviewDidNotSave();
  testInsiderCitizenIsNeverToldTheWord();
  testInsiderFaceDownCardShowsNothingAtAll();
  testInsiderFaceDownCardTakesATapAnywhere();
  testInsiderMasterCannotBeAccused();
  testInsiderVoteWaitsForAChoice();
  testInsiderSteppersDieAtTheEnds();
  testInsiderRevealAlwaysSaysTheWord();
  testInsiderTutorialLosesNoWords();

  testKnucklebonesMenuOffersItsThreeRows();
  testTappingAColumnReportsThatColumn();
  testTheBoardOnlyAcceptsAColumnOnYourOwnTurn();
  testTheMinesweeperBoardFitsThePanel();
  testTheCounterSaysWhatItCounts();
  testTheBoardStaysWithinItsOwnArea();
  testTheMinesweeperResultNamesTheOutcome();
  testTheSettledBoardStaysAndWearsItsVerdict();
  testTheHowToPagesAndEndsOnGotIt();
  testTheMinesweeperMenuLeadsWithTheRecord();
  testTheTwoGridsDoNotOverlap();

  testMurdleGridResolvesEveryCellItDrew();
  testTheCellYouTapIsTheCellTheRulesGet();

  testTheEmptyQueueStillOffersSync();
  testTappingAQueueRowOpensThatArticle();
  testTheQueueTitleWidthLeavesRoomForThePosition();
  testTheReaderPagesAndArchives();
  testArchiveIsLiveOnTheLastPage();
  testArchiveIsNotBetweenThePageControls();
  testTheQueueOffersUndoOnlyAfterAnArchive();
  testALongTitleIsEllipsisedRatherThanClipped();
  testTheReaderTextGoesInTheReaderBody();
  testWavelengthSpectrumEndsShareOneSize();
  testWavelengthNothingIsDrawnThroughAnything();
  testWavelengthEveryRevealOffersAWayOn();
  testWavelengthTheFourThatWereDropped();
  testWavelengthTheLockIsAnOrdinaryButton();
  testWavelengthEverySlotIsTappable();
  testFitLinesCutsAnUnbreakableTokenRatherThanVanishing();

  testInkCentredPutsTheInkInTheMiddleOfAnyBox();
  testAShortBoxIsWhatMakesTheCorrectionNecessary();
  testAMinesweeperDigitIsCentredInItsCell();
  testAKnucklebonesColumnTotalClearsItsBand();

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
