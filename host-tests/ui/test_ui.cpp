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
#include "../../src/apps_local/hackernews/HackerNewsScreens.h"
#include "../../src/apps_local/insider/InsiderScreens.h"
#include "../../src/apps_local/knucklebones/KnucklebonesScreens.h"
#include "../../src/apps_local/link/LinkScreens.h"
#include "../../src/apps_local/murdle/MurdleScreens.h"
#include "../../src/apps_local/murdle/MurdleText.h"
#include "../../src/apps_local/player/PlayerAvatar.h"
#include "../../src/apps_local/player/PlayerScreen.h"
#include "../../src/apps_local/study/StudyScreens.h"
#include "../../src/apps_local/ui/ToyboxIcons.h"

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
  std::vector<Blit> blits;

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
  const fui::Rect withName = shelfui::listBand(device(), true);
  const fui::Rect without = shelfui::listBand(device(), false);
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
  model.title = "ARTICLE";
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
  model.actionLabel = "READ THE COMMENTS";
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

  const int firstRowY = toybox::kHeaderHeight + toybox::kGutter * 3 + toybox::kRowHeight / 2;
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
    // The last page says so, or a player pages forever looking for the end.
    CHECK(out.target.drew(page + 1 < knuckleui::howToPages() ? "NEXT" : "GOT IT"));
  }
}

int main() {
  testSearchingAsksNothing();
  testMurdleGridResolvesEveryCellItDrew();
  testMurdleGridEdgesAreLive();
  testMurdleGridDrawsMarksItIsGiven();
  testMurdleClueFaceIsPagedAndNeverOverflows();
  testMurdleSettingsPicksAnAbsoluteTier();
  testMurdleAccusationIsInertUntilComplete();
  testMurdleMenuHeadlineIsTheDoorAcrossItsWidth();
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
  testHnReaderFooter();
  testHnReaderDisabledControls();
  testHnReaderSwapLabelFollowsMode();
  testHnReaderTextStaysInItsRect();
  testHnNotice();
  testHnList();
  testHnFitLines();
  testHnReaderShowsWhereYouAre();
  testKnucklebonesMenuOffersItsThreeRows();
  testTappingAColumnReportsThatColumn();
  testTheBoardOnlyAcceptsAColumnOnYourOwnTurn();
  testTheBoardFitsThePanel();
  testTheTwoGridsDoNotOverlap();
  testTheResultNamesTheOutcome();
  testTheHowToEndsOnGotIt();
  testShelfFolderDrawsItsOwnNameAndRows();
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

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
