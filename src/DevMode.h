#pragma once

#include <cstdint>
#include <string>

// Developer Mode: the device joins its last Wi-Fi network, keeps a control
// server up, and can be reflashed and driven from a computer with no cable.
//
// A RUNTIME setting (Settings > System > Developer Mode), not a build flag, and
// present in every build including releases. That is the whole point: a device
// running a shipped release can be turned into a development device and back
// without ever being plugged in. An earlier version gated this at compile time,
// which meant the only way to get a device into dev mode was the cable it was
// supposed to remove.
//
// Off is the default and off means off: no Wi-Fi join of its own, no server, no
// endpoints. Toggling takes effect immediately, without a reboot.
//
// PAIRING. While on, the device shows a six-digit code. A computer presents it
// once to POST /api/dev/pair and gets a token; every other dev endpoint
// requires that token. Without this, anything on the network could replace the
// firmware of any device whose owner left the toggle on -- which, unlike a
// build flag, is now a thing an ordinary user can do. The code changes every
// time dev mode is switched on, and turning it off revokes every token.
//
// See docs/developer-mode.md.

namespace devmode {

// Call late in setup(), after storage is mounted (the setting and the Wi-Fi
// credentials are both files on the card). Non-blocking: never waits on an AP.
void begin();

// Call once per loop(). Applies the setting, drives the join, and pumps the
// server. Cheap when dev mode is off.
void update();

// The web server activity binds the same ports; it owns them while it is open.
void pause();
void resume();

// True while the control server is up and reachable.
bool serving();

// What the on-device panel shows. Empty ip means "not connected yet".
struct Status {
  bool enabled = false;
  bool connected = false;
  std::string ip;
  std::string code;  // six digits, shown to the user for pairing
  std::string ssid;
};
Status status();

// True when this request carries a token handed out by a successful pair.
bool tokenValid(const std::string& token);

// Exchange the displayed code for a token. Empty return means wrong code.
std::string pair(const std::string& code);

}  // namespace devmode
