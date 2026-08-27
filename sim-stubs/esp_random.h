#pragma once
#include <stdint.h>

#include <random>

// Host stand-in for the ESP32's hardware RNG, used by Developer Mode to mint
// pairing codes and tokens (src/DevMode.cpp).
//
// Unlike the other stubs here this one is REAL rather than a stub that fails:
// nvs_open returning -1 is fine because the simulator has no NVS, but a
// pairing code of 000000 every time would make the simulator quietly wrong
// instead of visibly unsupported, and it is the kind of wrong that reads as
// working. std::random_device is not guaranteed non-deterministic by the
// standard, but on macOS and Linux it is, and nothing on the host side of this
// firmware is a security boundary anyway -- the real device uses the hardware
// RNG.
inline uint32_t esp_random() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<uint32_t> dist(0, UINT32_MAX);
  return dist(gen);
}
