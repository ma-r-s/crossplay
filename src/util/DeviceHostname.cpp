#include "DeviceHostname.h"

#include <cstdint>
#include <cstdio>

#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
#include <esp_mac.h>
#endif

namespace devicehost {

namespace {

// FNV-1a over all six MAC bytes, rather than "take the low three".
//
// That shortcut is a coin flip on byte order, and losing it is catastrophic
// and silent: the first three bytes of a MAC are the OUI, identical on every
// unit of the same make. Take the wrong end and EVERY device gets the same
// name -- which is the bug this file exists to fix, reintroduced in the fix.
// ESP.getEfuseMac() returns its bytes reversed relative to esp_read_mac(), so
// there is a real ambiguity here and no reason to bet on it. Hashing all six
// makes the question moot: every byte contributes, whichever end it came from.
uint32_t hashMac(const uint8_t (&mac)[6]) {
  uint32_t h = 2166136261u;
  for (const uint8_t b : mac) {
    h ^= b;
    h *= 16777619u;
  }
  return h;
}

}  // namespace

const char* mdnsName() {
  static std::string name;
  if (!name.empty()) return name.c_str();

#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
  uint8_t mac[6] = {};
  char buf[32];
  // esp_read_mac fills mac[0..5] in the printed order, and reads eFuse rather
  // than the radio, so it works before WiFi starts.
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
    std::snprintf(buf, sizeof(buf), "crossplay-%06x", hashMac(mac) & 0xFFFFFFu);
  } else {
    // Losing the MAC is not a reason to have no name: fall back to the old
    // fixed one, which is exactly as collision-prone as it always was and no
    // worse than refusing to advertise at all.
    std::snprintf(buf, sizeof(buf), "crossplay");
  }
  name = buf;
#else
  // No eFuse on the host or the simulator. A fixed, representative value keeps
  // rendered screenshots deterministic AND shows the real SHAPE of the name --
  // a bare "crossplay" here would have hidden the extra seven characters from
  // every render and every host test that measures one.
  name = "crossplay-a1b2c3";
#endif
  return name.c_str();
}

}  // namespace devicehost
