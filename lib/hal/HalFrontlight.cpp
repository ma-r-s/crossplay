#include "HalFrontlight.h"

#include <Logging.h>

HalFrontlight HalFrontlight::instance;

void HalFrontlight::begin(const uint8_t brightness, const uint8_t warmth, const bool on) {
  // begin() runs the hardware probe (EEGO A4: an I2C ACK from the LM3630A at
  // 0x36) that decides present(), so it must come FIRST. Guarding on present()
  // before begin() would skip the probe forever and hide the light on units
  // that actually have one. begin() is inert on boards without a frontlight.
  //
  // Checked, because this line used to be `manager.begin();` on its own and
  // the SDK returned void. A board whose channels did not configure was
  // indistinguishable from one whose owner had not touched the light: the
  // "Frontlight up" line below printed either way, the panel opened, the sun
  // icon filled, and settings recorded frontlightOn=1. That is how the X4 Pro
  // shipped two releases with a light that could not be turned on, and why
  // reading /api/dev/log on the affected device taught the reader nothing.
  ready = manager.begin();
  if (!manager.present()) return;


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
  // begin() returns early on a board with no frontlight and never records a
  // state, so without this a setOn(true) would leave isOn() claiming a lit
  // panel on a device that has no light at all -- and armSilentReboot(), which
  // does not check present(), would carry that claim across a restart. Every
  // other caller already guards on present(); this makes the guard the HAL's.
  if (!manager.present()) return;
  if (on == lit) return;
  lit = on;
  manager.setBrightness(lit ? lastBrightness : 0);
}
