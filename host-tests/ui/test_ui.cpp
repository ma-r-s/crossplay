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
#include "../../src/apps_local/chess/ChessScreens.h"
#include "../../src/apps_local/connections/ConnectionsScreens.h"
#include "../../src/apps_local/link/LinkScreens.h"

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
  };

  std::vector<TextRun> texts;
  std::vector<fui::Rect> fills;

  fui::Size measureText(const fui::FontId, const char* text, const fui::TextStyle) const override {
    // A fixed 10x20 cell. Layout maths only needs a monotonic width; nothing
    // here depends on real glyph metrics.
    return fui::Size{static_cast<int16_t>(text ? std::strlen(text) * 10 : 0), 20};
  }
  int16_t lineHeight(const fui::FontId) const override { return 20; }

  void fill(const fui::Rect rect, const fui::Paint paint, const uint8_t = 0, const uint8_t = 0xFF) override {
    if (paint.kind != fui::PaintKind::None) fills.push_back(rect);
  }
  void stroke(const fui::Rect, const fui::Paint, const uint8_t, const uint8_t = 0, const uint8_t = 0xFF) override {}
  void line(const fui::Point, const fui::Point, const uint8_t, const fui::Paint) override {}
  void triangle(const fui::Point, const fui::Point, const fui::Point, const fui::Paint) override {}
  void text(const fui::Rect rect, const char* text, const fui::TextStyle style) override {
    if (text != nullptr) texts.push_back(TextRun{rect, text, style.color});
  }
  void bitmap(const fui::Rect, const fui::BitmapRef, const fui::BitmapMode, const fui::Paint = {},
              const fui::Rotation = fui::Rotation::None) override {}

  bool drew(const char* needle) const {
    for (const auto& run : texts) {
      // cppcheck-suppress useStlAlgorithm
      if (run.text == needle) return true;
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

// The X4 Pro's logical frame.
fui::DeviceContext device() {
  fui::DeviceContext ctx;
  ctx.width = 480;
  ctx.height = 800;
  ctx.hasTouch = true;
  ctx.hasButtons = true;
  return ctx;
}

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
  saved.continueDetail = "14 SHOTS, 2 SUNK";
  saved.played = 12;
  saved.won = 7;
  saved.streak = 3;
  CHECK(bshipui::startRows(saved) == 3);
  CHECK(bshipui::startRowAt(saved, 0) == bshipui::StartRow::Continue);

  Rendered out;
  buildBattleshipStart(out, saved);
  CHECK(out.target.drew("BATTLESHIP"));
  CHECK(out.target.drew("CONTINUE"));
  CHECK(out.target.drew("14 SHOTS, 2 SUNK"));
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
  model.playerName = "STORMY LYNX";

  Rendered menu;
  buildShelf(menu, model);
  CHECK(menu.target.drew("GAMES"));
  CHECK(menu.target.drew("CHESS"));
  CHECK(menu.target.drew("SOLITAIRE"));
  CHECK(menu.target.drew("STORMY LYNX"));
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
  CHECK(!menu.target.drew("STORMY LYNX"));

  // Not drawing the name is not enough: the control must not be there at all.
  // A footer built from a null label draws nothing visible, so an assertion on
  // the text alone passes while an invisible reroll button sits at the bottom
  // of the screen waiting to be pressed. Tap where it would be.
  const int footerY = 800 - toybox::kMargin - toybox::kPillHeight / 2;
  CHECK(menu.tap(240, footerY).action != shelfui::ActionRerollName);

  // The footer is not just hidden, its space is returned to the list. A folder
  // that reserved room for a control it never draws is dead space, and the list
  // would think it had one row less than it does.
  const fui::Rect withName = shelfui::listBand(device(), true);
  const fui::Rect without = shelfui::listBand(device(), false);
  CHECK(without.height > withName.height);
  CHECK(without.height - withName.height == toybox::kPillHeight + toybox::kGutter);
}

}  // namespace

int main() {
  testSearchingAsksNothing();
  testSeatsSayWhatEachPlayerHasDecided();
  testTheRematchShowsBothAnswers();
  testAnOpponentWhoHasGoneTakesTheButtonWithThem();
  testRowModel();
  testSettingsOpenedFromTheMenuOffersOnlyPreferences();
  testSettingsScreen();
  testSettingsRouting();
  testBoardChrome();
  testConnectionsLostBoard();
  testConnectionsWonBoard();
  testBattleshipStartMenu();
  testBattleshipCapsuleIsOnlyATriggerWhenItSaysSo();
  testBattleshipPlacementControls();
  testShelfFolderDrawsItsOwnNameAndRows();
  testAFolderWithoutADeviceNameHasNoFooter();

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
