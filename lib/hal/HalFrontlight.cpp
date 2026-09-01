#include "HalFrontlight.h"

#include <Logging.h>

HalFrontlight HalFrontlight::instance;

void HalFrontlight::begin(const uint8_t brightness, const uint8_t warmth, const bool on) {
  if (!manager.present()) return;

  // Checked, because this line used to be `manager.begin();` on its own and
  // the SDK returned void. A board whose channels did not configure was
  // indistinguishable from one whose owner had not touched the light: the
  // "Frontlight up" line below printed either way, the panel opened, the sun
  // icon filled, and settings recorded frontlightOn=1. That is how the X4 Pro
  // shipped two releases with a light that could not be turned on, and why
  // reading /api/dev/log on the affected device taught the reader nothing.
  ready = manager.begin();
  lastBrightness = brightness > 100 ? 100 : brightness;
  manager.setColorTemperature(warmth > 100 ? 100 : warmth);
  lit = on;
  manager.setBrightness(lit ? lastBrightness : 0);
  if (!ready) {
    // Deliberately not a silent degrade to present()==false: the panel stays
    // reachable so the failure is visible to whoever is holding the device,
    // rather than a frontlight quietly disappearing from a board that has one.
    LOG_ERR("LIGHT", "Frontlight did NOT come up; every brightness change from here is accepted and does nothing");
    return;
  }
  LOG_INF("LIGHT", "Frontlight up: %u%% warm=%u%% %s", lastBrightness, manager.colorTemperature(), lit ? "on" : "off");
}

void HalFrontlight::setBrightness(const uint8_t percent) {
  lastBrightness = percent > 100 ? 100 : percent;
  if (lit) manager.setBrightness(lastBrightness);
}

void HalFrontlight::setWarmth(const uint8_t warmPercent) {
  manager.setColorTemperature(warmPercent > 100 ? 100 : warmPercent);
}

void HalFrontlight::setOn(const bool on) {
  if (on == lit) return;
  lit = on;
  manager.setBrightness(lit ? lastBrightness : 0);
}
