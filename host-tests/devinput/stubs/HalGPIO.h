#pragma once
#include <cstdint>
// Only the button indices are used, and only as opaque values handed to the
// injector. The test pins the NAME -> constant association, not the numbers,
// which belong to the hardware.
struct HalGPIO {
  static constexpr uint8_t BTN_BACK = 10;
  static constexpr uint8_t BTN_CONFIRM = 11;
  static constexpr uint8_t BTN_LEFT = 12;
  static constexpr uint8_t BTN_RIGHT = 13;
  static constexpr uint8_t BTN_UP = 14;
  static constexpr uint8_t BTN_DOWN = 15;
  static constexpr uint8_t BTN_POWER = 16;
};
