#pragma once

#include <string>

#include "activities/Activity.h"

// The on-device half of Developer Mode, and the reason the rest of it works.
//
// Everything the feature promises -- pair with the six digits shown on the
// device -- depends on those digits being visible somewhere other than a serial
// cable. Without this screen the pairing code exists only in a log line, which
// is readable over the very USB connection the feature exists to remove, or
// through an endpoint that needs the token you are trying to obtain.
//
// It is also where turning Developer Mode ON happens. That is deliberate: the
// setting is excluded from the web settings API (see
// CrossPointWebServer::isLocalOnlySetting), so becoming a development device is
// a decision made in front of the device, by someone holding it.
class DeveloperModeActivity final : public Activity {
 public:
  explicit DeveloperModeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DeveloperMode", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  // The panel has to follow the state machine: off -> connecting -> a code and
  // an address. Nothing else here drives a repaint, so poll rather than sleep.
  bool skipLoopDelay() override { return true; }
  void render(RenderLock&&) override;

 private:
  // Last painted snapshot, so an e-ink refresh only happens when something a
  // reader can see has actually changed.
  bool lastEnabled = false;
  bool lastConnected = false;
  std::string lastIp;
  std::string lastCode;
  unsigned long nextPollAt = 0;

  void toggle();
};
