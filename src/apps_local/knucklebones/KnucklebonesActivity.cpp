#include "KnucklebonesActivity.h"

#include <Memory.h>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "KnucklebonesBrain.h"
#include "KnucklebonesScreens.h"

namespace kb = knucklebones;

std::unique_ptr<Activity> KnucklebonesActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<KnucklebonesActivity>(renderer, mappedInput);
}

void KnucklebonesActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  screen = kb::Screen::Menu;
  menuSelected = -1;
  requestUpdate();
}

void KnucklebonesActivity::goTo(const kb::Screen next) {
  screen = next;
  requestUpdate();
}

void KnucklebonesActivity::beginMatch() {
  // millis() is the only entropy on this device that differs between two boots.
  // It enters here and nowhere deeper: the core takes a seed and never reaches
  // for a clock, which is what keeps a match replayable and a screenshot
  // reproducible.
  kb::start(game, static_cast<uint32_t>(millis()) * 2654435761u + 1u);
  seat = 0;
  goTo(kb::Screen::Board);
}

void KnucklebonesActivity::takeOpponentTurn() {
  // One placement per render, not a loop to the end of the game. The panel
  // takes half a second to repaint, so playing several of their moves before
  // showing anything would look like the device had frozen and then skipped.
  if (kb::over(game) || game.turn == seat) return;
  const int column = kb::chooseColumn(game);
  if (column < 0) return;
  kb::place(game, column);
  requestUpdate();
}

void KnucklebonesActivity::loop() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // The flow owns this, not the activity. Every screen has a defined Back and
    // exactly one of them leaves the app; asking here rather than deciding here
    // is what keeps that true.
    if (kb::leavesApp(screen)) {
      shelf::leave(renderer, mappedInput);
      return;
    }
    goTo(kb::back(screen));
    return;
  }

  // The opponent moves between frames rather than inside a tap handler, so
  // watching them play is the same code path as playing yourself.
  if (screen == kb::Screen::Board && !kb::over(game) && game.turn != seat) {
    takeOpponentTurn();
    return;
  }
  if (screen == kb::Screen::Board && kb::over(game)) {
    goTo(kb::Screen::Result);
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
  if (!input.touchReleased || !interactionsReady) return;

  const fui::ActionEvent event = interactions.route(input);
  switch (event.action) {
    case knuckleui::ActionMenuRow:
      switch (static_cast<knuckleui::MenuRow>(event.value)) {
        case knuckleui::MenuRow::Play:
          beginMatch();
          return;
        case knuckleui::MenuRow::PlayNearby:
          // Not wired yet. Deliberately does nothing rather than pretending:
          // the two-device path goes through src/apps_local/link/ and lands
          // separately.
          return;
        case knuckleui::MenuRow::HowTo:
          howToPage = 0;
          goTo(kb::Screen::HowTo);
          return;
        case knuckleui::MenuRow::Count:
          return;
      }
      return;

    case knuckleui::ActionHowToNext:
      if (howToPage + 1 < knuckleui::howToPages()) {
        ++howToPage;
        requestUpdate();
        return;
      }
      goTo(kb::Screen::Menu);
      return;

    case knuckleui::ActionColumn:
      // The rules decide whether this is legal, not the screen and not here.
      // place() refuses and changes nothing if it is not, so a stale frame
      // cannot half-apply a move.
      if (kb::place(game, event.value)) requestUpdate();
      return;

    case knuckleui::ActionAgain:
      beginMatch();
      return;

    case knuckleui::ActionDone:
      goTo(kb::Screen::Menu);
      return;

    default:
      return;
  }
}

void KnucklebonesActivity::render(RenderLock&&) {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen surface(frame);

  switch (screen) {
    case kb::Screen::Menu: {
      knuckleui::MenuModel model;
      model.selected = menuSelected;
      knuckleui::buildMenu(surface, model);
      break;
    }
    case kb::Screen::HowTo: {
      knuckleui::HowToModel model;
      model.page = howToPage;
      model.pageCount = knuckleui::howToPages();
      knuckleui::buildHowTo(surface, model);
      break;
    }
    case kb::Screen::Board: {
      knuckleui::BoardModel model;
      model.yours = game.grid[seat];
      model.theirs = game.grid[1 - seat];
      model.die = game.die;
      model.yourTurn = game.turn == seat;
      knuckleui::buildBoard(surface, model);
      break;
    }
    case kb::Screen::Result: {
      knuckleui::ResultModel model;
      model.yourScore = kb::score(game.grid[seat]);
      model.theirScore = kb::score(game.grid[1 - seat]);
      knuckleui::buildResult(surface, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Knucklebones");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
