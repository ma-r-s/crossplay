#pragma once

// Train Tracks on the device. The thin layer: renderer, storage, input, shelf.
//
// Milestone 3 scope: the play screen only. It opens straight onto a fresh
// Medium puzzle; the front door, difficulty picker, CHECK, HINT, save and wake
// resume arrive in Milestone 4 (see docs/apps/traintracks.md).

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "TrainTracksCore.h"
#include "TrainTracksGenerator.h"
#include "TrainTracksScreens.h"

class TrainTracksActivity final : public Activity {
 public:
  TrainTracksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TrainTracks", renderer, mappedInput) {}
  ~TrainTracksActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void beginGame(traintracks::Level level);
  void stepGeneration();
  uint32_t surfaceMeaning() const override;

  // ~8.5KB of generator and solver scratch, allocated once in onEnter so
  // generation and the win check never touch the heap or the stack.
  std::unique_ptr<traintracks::GenWork> work;

  traintracks::Level level = traintracks::Level::Medium;
  traintracks::Puzzle puzzle{};
  traintracks::Board board{};
  bool solved = false;

  // Generation runs one loop pass after the frame that announces it, then a
  // few attempts per pass, so LAYING TRACK is on the panel and the loop never
  // disappears for long. Past the wall-clock budget it takes a bundled puzzle.
  bool generating = false;
  bool generateDeferred = false;
  uint32_t seed = 0;
  uint16_t nextAttempt = 0;
  unsigned long generateStartMs = 0;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};
