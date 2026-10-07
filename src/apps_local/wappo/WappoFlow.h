#pragma once

#include <cstdint>

namespace wappo {

enum class Screen : uint8_t {
  Menu,
  HowTo,
  Levels,
  Board,
  Ending,
};

constexpr Screen back(const Screen screen) {
  switch (screen) {
    case Screen::Menu:
      return Screen::Menu;
    case Screen::HowTo:
      return Screen::Menu;
    case Screen::Levels:
      return Screen::Menu;
    case Screen::Board:
      return Screen::Menu;
    case Screen::Ending:
      return Screen::Menu;
  }
  return Screen::Menu;
}

constexpr bool leavesApp(const Screen screen) { return screen == Screen::Menu; }

}  // namespace wappo
