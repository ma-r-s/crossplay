#pragma once

// Keeps a DEV build reachable over Wi-Fi so scripts_local/wifi-flash.sh can
// reach POST /api/dev/flash without anyone walking to the device.
//
// Only compiled under -DCROSSPOINT_DEV_WIFI_FLASH ([env:x4pro], [env:sticky]).
// A release build has none of this: no boot-time join, no background server.
//
// Stock behaviour is that nothing connects to Wi-Fi at boot -- WiFi.begin()
// lives in WifiSelectionActivity and nowhere else -- and the web server exists
// only while its settings screen is open. That is correct for a reader in
// someone's bag and useless for a desk device you want to reflash, because
// every flash ends in a reboot that takes the server down with it.
//
// See docs/wireless-flashing.md.

#if CROSSPOINT_DEV_WIFI_FLASH

namespace devwifi {

// Kick off the join. Non-blocking: boot must not wait on an access point, so
// this only starts the attempt and returns. Call late in setup(), after
// storage is mounted (the credentials are a file on the SD card).
void begin();

// Poll the join and pump the server. Call once per loop().
void update();

// Hand the ports over. CrossPointWebServerActivity binds the same 80/81/8134,
// and two servers on one port is a bind failure that reads as "the web screen
// is broken". The activity owns them while it is open; this yields.
void pause();
void resume();

// True while this module is holding the server up, for status lines.
bool serving();

}  // namespace devwifi

#endif  // CROSSPOINT_DEV_WIFI_FLASH
