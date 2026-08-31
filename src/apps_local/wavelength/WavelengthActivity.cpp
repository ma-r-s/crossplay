#include "WavelengthActivity.h"

#include <Memory.h>

#include "../../components/UITheme.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"

namespace fui = freeink::ui;

std::unique_ptr<Activity> WavelengthActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<WavelengthActivity>(renderer, mappedInput);
}

void WavelengthActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  guess = wavelength::kSlots / 2;
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  requestUpdate();
}

void WavelengthActivity::onExit() { Activity::onExit(); }

void WavelengthActivity::step(const int delta) {
  const int next = guess + delta;
  if (next < 1 || next > wavelength::kSlots) return;
  guess = next;
  requestUpdate();
}

void WavelengthActivity::routeAction(const int action) {
  switch (action) {
    case wavelengthui::ActionStepTowardTop:
      step(1);
      break;
    case wavelengthui::ActionStepTowardBottom:
      step(-1);
      break;
    default:
      break;
  }
}

void WavelengthActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // See src/apps_local/Shelf.h: no app names its own destination.
    shelf::leave(renderer, mappedInput);
    return;
  }

  // The two side keys step the marker. Up is the key toward the top of the
  // strip, which in portrait is the identity mapping and needs no rotation
  // arithmetic; that is the whole reason the strip is vertical.
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    step(1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    step(-1);
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

  const fui::ActionEvent action = interactions.route(input);
  routeAction(static_cast<int>(action.action));
}

void WavelengthActivity::render(RenderLock&&) {
  renderer.clearScreen();

  // Three cuts at once: the huge one for the number the table reads across a
  // table, the display cut for the end words, and the button cut for the hints.
  // The end words are deliberately NOT in the large or huge cut; measured
  // against the shipped deck, a quarter of the end words are wider than the
  // panel there.
  const toybox::Faces faces{toybox::kButtonFontId, toybox::kDisplayFontId, toybox::kHugeFontId};
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, faces);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);

  wavelengthui::DialModel model;
  model.guess = guess;
  wavelengthui::renderDial(screen, model);

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Wavelength");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // An activity publishes its own frame. Without this the screen is drawn into
  // the buffer and never shown, the previous activity's frame stays up, and it
  // looks exactly as though this one never started. The tell is the absence of
  // the "[GFX] Time = N ms from clearScreen to displayBuffer" log line.
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
