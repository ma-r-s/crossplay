#include "PlayerActivity.h"

#include <Memory.h>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "PlayerName.h"
#include "PlayerScreen.h"

std::unique_ptr<Activity> PlayerActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<PlayerActivity>(renderer, mappedInput);
}

void PlayerActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  requestUpdate();
}

void PlayerActivity::loop() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Straight back to whichever folder opened us. This activity never names a
    // destination; see Shelf.h rule 3.
    shelf::leave(renderer, mappedInput);
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
  if (event.action == playerui::ActionLeavePlayer) {
    shelf::leave(renderer, mappedInput);
    return;
  }
  if (event.action == playerui::ActionStepSlot) {
    // Instant and reversible: keep tapping and the word comes back round, so
    // there is nothing to confirm and nothing to undo. stepSlot writes the file,
    // which is fine here -- this is a deliberate settings change, not a
    // per-interaction save in a game loop.
    player::stepSlot(event.value);
    requestUpdate();
  }
}

void PlayerActivity::render(RenderLock&&) {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::ThemeTokens& tokens = toybox::themeTokens();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen screen(frame);

  playerui::PlayerModel model;
  model.name = player::name();
  const player::Name parts = player::parts();
  for (int slot = 0; slot < player::kSlotCount; ++slot) {
    // Never null: name() guarantees three words this build knows, and a saved
    // name that does not parse was discarded and rerolled on load.
    model.words[slot] = player::word(slot, parts.word[slot]);
  }
  playerui::buildPlayer(screen, model);

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Player");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
