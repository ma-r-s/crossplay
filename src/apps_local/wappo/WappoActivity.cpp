#include "WappoActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstdlib>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"

namespace {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
constexpr char kSavePath[] = "/.crosspoint/wappo.sav";
#endif
}  // namespace

std::unique_ptr<Activity> WappoActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<WappoActivity>(renderer, mappedInput);
}

void WappoActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  screen = wappo::Screen::Menu;
  menuSelected = -1;
  loadState();
  wappo::initGame(game, game.levelIndex);
  requestUpdate();
}

void WappoActivity::loadState() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (!Storage.exists(kSavePath)) return;
  auto buf = makeUniqueNoThrow<char[]>(wappo::kSaveBytes);
  if (!buf) {
    LOG_ERR("WAPPO", "OOM: %d bytes for save", static_cast<int>(wappo::kSaveBytes));
    return;
  }
  if (Storage.readFileToBuffer(kSavePath, buf.get(), wappo::kSaveBytes) == 0) return;
  if (wappo::parseProgress(buf.get(), progress)) game.levelIndex = progress.current;
#endif
}

void WappoActivity::saveState() {
  progress.current = resumeLevel();
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  auto buf = makeUniqueNoThrow<char[]>(wappo::kSaveBytes);
  if (!buf) {
    LOG_ERR("WAPPO", "OOM: %d bytes for save", static_cast<int>(wappo::kSaveBytes));
    return;
  }
  wappo::formatProgress(progress, buf.get(), wappo::kSaveBytes);
  Storage.writeFile(kSavePath, String(buf.get()));
#endif
}

void WappoActivity::onLevelWon() {
  wappo::recordWin(progress, game.levelIndex, game.moves);
  saveState();
}

int WappoActivity::resumeLevel() const {
  if (game.outcome != wappo::Outcome::Won) return game.levelIndex;
  // After the last level, PLAY starts the run again from level 1.
  return game.levelIndex + 1 < wappo::kLevelCount ? game.levelIndex + 1 : 0;
}

void WappoActivity::goTo(const wappo::Screen next) {
  screen = next;
  requestUpdate();
}

void WappoActivity::beginGame(const int level) {
  wappo::initGame(game, level);
  goTo(wappo::Screen::Board);
}

uint32_t WappoActivity::surfaceMeaning() const {
  const uint32_t withScreen = paintclock::mixMeaning(paintclock::kMeaningSeed, static_cast<uint32_t>(screen));
  const uint32_t withLvl = paintclock::mixMeaning(withScreen, static_cast<uint32_t>(game.levelIndex));
  const uint32_t withTurn = paintclock::mixMeaning(withLvl, static_cast<uint32_t>(game.moves));
  return paintclock::mixMeaning(withTurn, static_cast<uint32_t>(game.outcome));
}

void WappoActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (wappo::leavesApp(screen)) {
      saveState();
      shelf::leave(renderer, mappedInput);
      return;
    }
    const bool overlay = screen == wappo::Screen::Levels || screen == wappo::Screen::HowTo;
    goTo(overlay ? returnTo : wappo::back(screen));
    return;
  }

  // Physical buttons for playing on the board
  if (screen == wappo::Screen::Board && game.outcome == wappo::Outcome::Playing) {
    if (game.phase == wappo::TurnPhase::MonstersTurn) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
          mappedInput.wasReleased(MappedInputManager::Button::Down) ||
          mappedInput.wasReleased(MappedInputManager::Button::Left) ||
          mappedInput.wasReleased(MappedInputManager::Button::Right)) {
        wappo::advanceMonsters(game);
        if (game.outcome == wappo::Outcome::Won) onLevelWon();
        requestUpdate();
        return;
      }
    } else {
      bool moved = false;
      if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
        moved = wappo::makePlayerMove(game, wappo::MoveDir::Up);
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        moved = wappo::makePlayerMove(game, wappo::MoveDir::Down);
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
        moved = wappo::makePlayerMove(game, wappo::MoveDir::Left);
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
        moved = wappo::makePlayerMove(game, wappo::MoveDir::Right);
      }

      if (moved) {
        if (game.outcome == wappo::Outcome::Won) onLevelWon();
        requestUpdate();
        return;
      }
    }
  }

  // Touch handling
  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;

  // Board tap: move towards tapped adjacent cell (or advance monsters if in MonstersTurn phase)
  if (screen == wappo::Screen::Board) {
    int col = 0;
    int row = 0;
    if (boardLayout.cellAt(tapX, tapY, col, row)) {
      if (!surfaceRevealed()) return;

      if (game.outcome == wappo::Outcome::Won) {
        if (game.levelIndex + 1 < wappo::kLevelCount) {
          beginGame(game.levelIndex + 1);
        } else {
          goTo(wappo::Screen::Ending);
        }
        return;
      }

      if (game.outcome == wappo::Outcome::Playing) {
        if (game.phase == wappo::TurnPhase::MonstersTurn) {
          wappo::advanceMonsters(game);
          if (game.outcome == wappo::Outcome::Won) onLevelWon();
          requestUpdate();
          return;
        }

        const int pCol = game.playerPos % wappo::kBoardWidth;
        const int pRow = game.playerPos / wappo::kBoardWidth;

        wappo::MoveDir dir = wappo::MoveDir::None;
        if (col == pCol && row == pRow - 1) {
          dir = wappo::MoveDir::Up;
        } else if (col == pCol && row == pRow + 1) {
          dir = wappo::MoveDir::Down;
        } else if (col == pCol - 1 && row == pRow) {
          dir = wappo::MoveDir::Left;
        } else if (col == pCol + 1 && row == pRow) {
          dir = wappo::MoveDir::Right;
        }

        if (dir != wappo::MoveDir::None && wappo::makePlayerMove(game, dir)) {
          if (game.outcome == wappo::Outcome::Won) onLevelWon();
          requestUpdate();
          return;
        }
      }
    }
  }

  // Level selection grid tap
  if (screen == wappo::Screen::Levels) {
    const int picked = levelsLayout.levelAt(tapX, tapY);
    if (picked >= 0 && picked <= progress.maxUnlocked) {
      beginGame(picked);
      return;
    }
  }

  if (!interactionsReady) return;

  freeink::ui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);

  const auto event = interactions.route(input);
  switch (event.action) {
    case wappoui::ActionMenuRow:
      switch (static_cast<wappoui::MenuRow>(event.value)) {
        case wappoui::MenuRow::Play:
          beginGame(resumeLevel());
          return;
        case wappoui::MenuRow::Levels:
          returnTo = wappo::Screen::Menu;
          levelsPage = resumeLevel() / 20;
          goTo(wappo::Screen::Levels);
          return;
        case wappoui::MenuRow::HowTo:
          returnTo = wappo::Screen::Menu;
          howToPage = 0;
          goTo(wappo::Screen::HowTo);
          return;
        case wappoui::MenuRow::Count:
          return;
      }
      return;

    case wappoui::ActionFinish:
      goTo(wappo::Screen::Ending);
      return;

    case wappoui::ActionMenu:
      goTo(wappo::Screen::Menu);
      return;

    case wappoui::ActionNextLevel:
      if (game.levelIndex + 1 < wappo::kLevelCount) beginGame(game.levelIndex + 1);
      return;

    case wappoui::ActionUndo:
      if (wappo::undoMove(game)) requestUpdate();
      return;

    case wappoui::ActionRestart:
      wappo::restartLevel(game);
      requestUpdate();
      return;

    case wappoui::ActionLevels:
      returnTo = screen == wappo::Screen::Board ? wappo::Screen::Board : wappo::Screen::Menu;
      levelsPage = resumeLevel() / 20;
      goTo(wappo::Screen::Levels);
      return;

    case wappoui::ActionHowTo:
      returnTo = wappo::Screen::Board;
      howToPage = 0;
      goTo(wappo::Screen::HowTo);
      return;

    case wappoui::ActionHowToNext:
      if (howToPage + 1 < wappoui::howToPages()) {
        ++howToPage;
        requestUpdate();
      } else {
        goTo(returnTo);
      }
      return;

    case wappoui::ActionHowToPrev:
      if (howToPage > 0) {
        --howToPage;
        requestUpdate();
      }
      return;

    case wappoui::ActionLevelsNext: {
      constexpr int kPerGrid = 20;
      const int totalPages = (wappo::kLevelCount + kPerGrid - 1) / kPerGrid;
      if (levelsPage + 1 < totalPages) {
        ++levelsPage;
        requestUpdate();
      }
      return;
    }

    case wappoui::ActionLevelsPrev:
      if (levelsPage > 0) {
        --levelsPage;
        requestUpdate();
      }
      return;

    case wappoui::ActionBackMenu:
      saveState();
      goTo(returnTo);
      return;

    default:
      return;
  }
}

void WappoActivity::render(RenderLock&&) {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen surface(frame);

  switch (screen) {
    case wappo::Screen::Menu: {
      wappoui::MenuModel model;
      model.selected = menuSelected;
      model.currentLevel = resumeLevel();
      model.progress = &progress;
      wappoui::buildMenu(surface, model);
      break;
    }
    case wappo::Screen::HowTo: {
      wappoui::HowToModel model;
      model.page = howToPage;
      wappoui::buildHowTo(surface, model);
      break;
    }
    case wappo::Screen::Levels: {
      wappoui::LevelsModel model;
      model.page = levelsPage;
      model.currentLevel = resumeLevel();
      model.progress = &progress;
      wappoui::buildLevels(surface, model, levelsLayout);
      break;
    }
    case wappo::Screen::Board: {
      wappoui::BoardModel model;
      model.game = &game;
      model.par = wappo::kLevels[game.levelIndex].par;
      wappoui::buildBoard(surface, model, boardLayout);
      break;
    }
    case wappo::Screen::Ending: {
      wappoui::EndingModel model;
      model.points = wappo::totalPoints(progress);
      model.atPar = wappo::atParCount(progress);
      wappoui::buildEnding(surface, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Wappo");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
