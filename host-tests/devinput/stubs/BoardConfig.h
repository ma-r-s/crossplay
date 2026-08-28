#pragma once
// Enough BoardConfig for the command parser: it reads the panel size and
// nothing else. 800x480 is both device envs.
namespace BoardConfig {
struct Board {
  int displayWidth;
  int displayHeight;
};
inline constexpr Board ACTIVE{800, 480};
}  // namespace BoardConfig
