#include "SolitaireActivity.h"

#include <HalStorage.h>
#include <Memory.h>

#include <cstring>

#include "../../activities/ActivityManager.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"

namespace {

namespace fui = freeink::ui;
namespace ui = solitaireui;

constexpr char kSavePath[] = "/.crosspoint/solitaire.sav";
constexpr char kResultsPath[] = "/.crosspoint/solitaire.res";
// A byte a game. A thousand games is a kilobyte, and nobody plays a thousand
// games of solitaire on an e-reader, but the cap means the file cannot grow
// without bound if somebody does.
constexpr int kMaxResults = 2048;

// How many moves may go unwritten.
//
// The save used to go out after every single one. That is about 340 bytes to
// the SD card per tap, and a draw-one game runs well past a hundred taps, so a
// single sitting was a hundred-odd writes -- on a card that would rather not
// have them, through a filesystem that has to rewrite a whole block for each.
// Chess and Connections never noticed this because they have an order of
// magnitude fewer moves per session.
//
// The board is now written when you leave it, when the app exits, and every
// kSaveEvery moves as a floor against a flat battery. The cost of that floor is
// bounded and stated: yank the power mid-game and you lose at most the last ten
// moves, which for a game you can undo ninety-six steps of is a fair trade for
// a tenth of the writes.
constexpr int kSaveEvery = 10;

// Bumped when the save layout changes, per the cache-format rule. An old save
// is then discarded rather than misread, which for a card game means a board
// with two of the same card on it.
constexpr uint8_t kSaveVersion = 1;

}  // namespace

std::unique_ptr<Activity> SolitaireActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<SolitaireActivity>(renderer, mappedInput);
}

void SolitaireActivity::onEnter() {
  // Landscape. This is the whole reason the app looks different from the
  // others: seven columns of cards want a wide screen far more than a page of
  // text wants a tall one.
  renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
  toybox::ensureFonts(renderer);

  hasGame = loadGame();
  if (hasGame) drawThree = game.drawThree();
  view = View::Menu;
  selectedPile = -1;
  recorded = false;
  requestUpdate();
}

void SolitaireActivity::onExit() {
  flushSave();
  // Put the screen back the way it was found. The orientation is global, so
  // leaving it turned would rotate the Games menu on the way out.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
}

void SolitaireActivity::newDeal() {
  // millis() is the only entropy on this device that differs between two boots
  // to the same menu. It is a seed, not a key.
  game.deal(static_cast<uint32_t>(millis()), drawThree);
  hasGame = true;
  recorded = false;
  selectedPile = -1;
  view = View::Board;
  // A fresh deal is written immediately rather than debounced: it is the one
  // state where losing the last few moves means losing the whole game.
  unsaved = 0;
  saveGame();
  requestUpdate();
}

// Called after anything that could have completed the game. Everything a win
// changes happens here, exactly once: the result is recorded, the save is
// dropped so the menu does not offer to resume a finished game, and the view
// moves to the payoff screen with a full refresh behind it.
//
// This deliberately does not live in render(). A screen builder that mutates
// state is a builder that cannot be host-tested, and render() runs more than
// once per move.
void SolitaireActivity::settleWin() {
  if (!game.won() || recorded) return;
  recorded = true;
  unsaved = 0;
  recordResult(true);
  clearSave();
  hasGame = false;
  selectedPile = -1;
  view = View::Won;
  // The refresh flash as punctuation, per docs/design-language.md. A win is
  // the one moment in this game that has earned the full blink.
  //
  // The fork's earlier base carried a renderer.requestNextFullRefresh() that set a one-shot
  // override; CrossPoint's GfxRenderer has no such thing, only a mode argument
  // on displayBuffer(). A flag the next paint consumes is the same behaviour
  // without asking upstream for an API.
  flashOnNextPaint = true;
}

void SolitaireActivity::routePile(const int pile, const int tapY) {
  const int card = layout.cardAt(game, pile, tapY);

  // The stock is a button that happens to look like a pile.
  if (pile == solitaire::kStockPile) {
    selectedPile = -1;
    game.drawFromStock();
    touchSave();
    requestUpdate();
    return;
  }

  if (selectedPile < 0) {
    if (card < 0) return;
    // Tapping a card that can go straight to a foundation sends it, rather than
    // selecting it and making you find the target. This is the move you meant
    // nearly every time, and on a screen this slow a wasted tap costs a second.
    const int run = game.runLength(pile, card);
    if (run == 1) {
      const int foundation = game.foundationFor(pile);
      if (foundation >= 0) {
        game.move(pile, foundation, 1);
        touchSave();
        settleWin();
        requestUpdate();
        return;
      }
    }
    if (run == 0) return;
    selectedPile = pile;
    selectedCard = card;
    requestUpdate();
    return;
  }

  // A second tap on the pile already held puts it down again.
  if (pile == selectedPile) {
    selectedPile = -1;
    requestUpdate();
    return;
  }

  const int count = game.pile(selectedPile).count - selectedCard;
  if (game.move(selectedPile, pile, count)) {
    selectedPile = -1;
    touchSave();
    settleWin();
    requestUpdate();
    return;
  }

  // An illegal destination re-aims onto whatever you tapped, so a mistap on a
  // card you meant to pick up costs one tap to recover from instead of two.
  //
  // But an empty pile has nothing to re-aim onto, and dropping the selection
  // there was the worst possible answer: tapping an empty column with a card in
  // hand is unambiguously "put it here", and silently letting go made it look
  // like empty columns did not accept cards at all. They do -- they take a king
  // and nothing else, which is now printed in the slot. Keep hold of the card.
  const int run = game.runLength(pile, card);
  if (run > 0) {
    selectedPile = pile;
    selectedCard = card;
  } else if (!game.pile(pile).empty()) {
    selectedPile = -1;
  }
  requestUpdate();
}

void SolitaireActivity::routeButton(const int button) {
  switch (button) {
    case ui::ButtonUndo:
      selectedPile = -1;
      if (game.undo()) touchSave();
      requestUpdate();
      break;
    case ui::ButtonNew:
      // Abandoning a game in progress is a result too, and not recording it
      // would let you farm a streak by restarting every deal that went badly.
      if (hasGame && !game.won() && game.moveCount() > 0) recordResult(false);
      newDeal();
      break;
    case ui::ButtonFinish:
      selectedPile = -1;
      game.autoPlay();
      touchSave();
      settleWin();
      requestUpdate();
      break;
    default:
      break;
  }
}

void SolitaireActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (view == View::Menu) {
      // See src/apps_local/Shelf.h: no app names its own destination.
      shelf::leave(renderer, mappedInput);
    } else if (view == View::Won) {
      view = View::Menu;
      requestUpdate();
    } else {
      flushSave();
      selectedPile = -1;
      view = View::Menu;
      requestUpdate();
    }
    return;
  }

  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  // Touch only, like the rest of this fork's games. See the note in
  // ConnectionsActivity for why there is no cursor.
  if (!input.touchReleased || !interactionsReady) return;

  const fui::ActionEvent action = interactions.route(input);
  if (action.action == ui::ActionPile && view == View::Board) {
    routePile(action.value, tapY);
  } else if (action.action == ui::ActionButton) {
    if (view == View::Won) {
      if (action.value == ui::WinAgain) {
        newDeal();
      } else {
        view = View::Menu;
        requestUpdate();
      }
    } else if (view == View::Menu) {
      switch (action.value) {
        case ui::MenuResume:
          view = View::Board;
          requestUpdate();
          break;
        case ui::MenuNew:
          if (hasGame && !game.won() && game.moveCount() > 0) recordResult(false);
          newDeal();
          break;
        case ui::MenuDrawMode:
          // Changes in place and bites on the next deal, so it can sit next to
          // RESUME without threatening the game you have going.
          drawThree = !drawThree;
          requestUpdate();
          break;
        default:
          break;
      }
    } else {
      routeButton(action.value);
    }
  }
}

void SolitaireActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, toybox::toyboxFaces());
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);

  // A slimmer header than the portrait screens use: in landscape the usual 76
  // would cost a sixth of the height the tableau needs. It was 48, which was
  // too tight -- the title's cap-centring is sized for the band, and at 48 the
  // bowl of the S and the leg of the R were sliced off by the bar's own bottom
  // edge. 56 is the smallest that clears the descender box.
  // Built once, not per frame. Screen refers to this rather than copying it, so
  // it only has to outlive the screen -- and a function-local static outlives
  // everything, which a named local did at the cost of 2712 bytes of ThemeTokens
  // on the render task's stack every repaint. That task overflowed on this very
  // path in v1.12.45 (card #398).
  static const fui::ThemeTokens tokens = [] {
    fui::ThemeTokens t = toybox::themeTokens();
    t.headerHeight = solitaireui::kHeaderBand;
    return t;
  }();
  toybox::Screen screen(frame, tokens);

  if (view == View::Board) {
    ui::BoardModel model;
    model.game = &game;
    model.selectedPile = selectedPile;
    model.selectedCard = selectedCard;
    model.won = game.won();
    ui::buildBoard(screen, model, layout);
  } else if (view == View::Won) {
    ui::WinModel model;
    model.moves = game.moveCount();
    model.drawThree = game.drawThree();
    ui::MenuModel stats;
    fillStats(stats);
    model.wins = stats.wins;
    model.streak = stats.streak;
    ui::buildWin(screen, model);
  } else {
    ui::MenuModel model;
    model.hasSave = hasGame && !game.won();
    model.savedMoves = game.moveCount();
    model.savedDrawThree = game.drawThree();
    model.drawThree = drawThree;
    fillStats(model);
    ui::buildMenu(screen, model);
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Solitaire");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  // A win asked for the full blink; every other paint is the fast refresh.
  renderer.displayBuffer(flashOnNextPaint ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  flashOnNextPaint = false;
}

void SolitaireActivity::touchSave() {
  if (++unsaved < kSaveEvery) return;
  flushSave();
}

void SolitaireActivity::flushSave() {
  if (unsaved == 0) return;
  unsaved = 0;
  // A finished game has already had its save removed; writing here would put it
  // back and the menu would offer to resume a game that is over.
  if (!hasGame || game.won()) return;
  saveGame();
}

void SolitaireActivity::saveGame() const {
  solitaire::Game::Save state;
  game.save(state);
  HalFile file;
  if (!Storage.openFileForWrite("SOL", kSavePath, file)) return;
  const uint8_t version = kSaveVersion;
  file.write(&version, 1);
  file.write(reinterpret_cast<const uint8_t*>(&state), sizeof(state));
  file.flush();
}

bool SolitaireActivity::loadGame() {
  if (!Storage.exists(kSavePath)) return false;
  HalFile file;
  if (!Storage.openFileForRead("SOL", kSavePath, file)) return false;
  uint8_t version = 0;
  if (file.read(&version, 1) != 1 || version != kSaveVersion) return false;
  solitaire::Game::Save state;
  if (file.read(reinterpret_cast<uint8_t*>(&state), sizeof(state)) != sizeof(state)) return false;
  // restore() validates the deck, so a truncated or edited save is refused
  // rather than dealt. See SolitaireCore::restore.
  return game.restore(state);
}

void SolitaireActivity::clearSave() const { Storage.remove(kSavePath); }

void SolitaireActivity::recordResult(const bool won) const {
  auto buffer = makeUniqueNoThrow<uint8_t[]>(kMaxResults);
  if (!buffer) {
    LOG_ERR("SOL", "OOM recording result");
    return;
  }
  std::memset(buffer.get(), 0, kMaxResults);
  size_t existing = 0;
  HalFile in;
  if (Storage.openFileForRead("SOL", kResultsPath, in)) {
    existing = in.read(buffer.get(), kMaxResults);
    in = HalFile{};
  }
  if (existing >= static_cast<size_t>(kMaxResults)) {
    // Drop the oldest game rather than stop recording. A record that silently
    // stops updating is worse than one with a horizon.
    std::memmove(buffer.get(), buffer.get() + 1, static_cast<size_t>(kMaxResults) - 1);
    existing = static_cast<size_t>(kMaxResults) - 1;
  }
  buffer[existing] = won ? 2 : 1;
  existing++;

  HalFile out;
  if (!Storage.openFileForWrite("SOL", kResultsPath, out)) return;
  out.write(buffer.get(), existing);
  out.flush();
}

void SolitaireActivity::fillStats(ui::MenuModel& model) const {
  auto buffer = makeUniqueNoThrow<uint8_t[]>(kMaxResults);
  if (!buffer) return;
  std::memset(buffer.get(), 0, kMaxResults);
  size_t read = 0;
  HalFile file;
  if (Storage.openFileForRead("SOL", kResultsPath, file)) {
    read = file.read(buffer.get(), kMaxResults);
  }

  for (size_t i = 0; i < read; ++i) {
    if (buffer[i] == 0) continue;
    model.played++;
    if (buffer[i] == 2) model.wins++;
  }
  for (int i = static_cast<int>(read) - 1; i >= 0; --i) {
    if (buffer[i] != 2) break;
    model.streak++;
  }
  // The last sixteen, oldest first, so the newest game is the bottom-right
  // cell and the grid reads in the same direction as everything else.
  for (int i = 0; i < 16; ++i) {
    const int index = static_cast<int>(read) - 16 + i;
    if (index < 0 || index >= static_cast<int>(read)) continue;
    model.recent[i] = buffer[index];
  }
}
