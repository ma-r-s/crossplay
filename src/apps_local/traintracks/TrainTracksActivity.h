#pragma once

// Train Tracks on the device. The thin layer: renderer, storage, input, shelf.

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "TrainTracksFlow.h"
#include "TrainTracksGame.h"
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

  // The Home gesture leaves without passing through Back, and the swap is
  // deferred, so loop() runs once more after it; the preview stops there.
  bool handleHomeGesture() override;

  // Generating is work, not idleness: without this the loop's idle timer drops
  // the CPU to its low-power clock part-way through a Hard deal, which on the
  // Picco stretched a five-attempt deal past four seconds.
  // Only while a deal is in hand on the screen that shows it: generation is
  // paused anywhere else, and a stale flag would spin the loop at full power.
  bool skipLoopDelay() override { return screen == traintracks::Screen::NewGame && (generating || dealLanded); }

 private:
  void goTo(traintracks::Screen next);
  bool unfinished() const { return hasGame && !game.solved; }
  void goToFrontDoor();
  void enterNewGame(traintracks::Level level);
  void startPreview(traintracks::Level level);
  void cancelGeneration();
  void stepGeneration();
  bool publishDeal();
  void playPreview();
  void afterMove();
  void check();
  void hint();
  void tickClock();
  void loadState();
  void saveState();
  // Records that the game moved on; maybeSave() writes it once play pauses.
  void touchSave();
  void maybeSave();
  uint32_t surfaceMeaning() const override;

  // ~9.6KB of generator and solver scratch, allocated once in onEnter so
  // generation, the win check and hints never touch the heap or the stack.
  std::unique_ptr<traintracks::GenWork> work;

  traintracks::Screen screen = traintracks::Screen::Menu;
  traintracks::Game game{};
  bool hasGame = false;
  // The difficulty last picked on the new-puzzle screen, which opens on it.
  traintracks::Level menuLevel = traintracks::Level::Easy;
  int howToPage = 0;

  // The new-puzzle screen's deal. It is laid one loop pass after the frame
  // that announces it, a few attempts per pass, so LAYING TRACK is on the
  // panel and the loop never disappears for long. It replaces the saved game
  // only when the player presses PLAY PUZZLE.
  traintracks::Game preview{};
  bool previewReady = false;
  // A deal that has finished but not yet been published: publishing waits for
  // the render lock, and the loop never blocks on it (see publishDeal).
  traintracks::Generated landed{};
  bool dealLanded = false;
  bool generating = false;
  bool generateDeferred = false;
  uint32_t seed = 0;
  uint16_t nextAttempt = 0;
  unsigned long generateStartMs = 0;

  // The finger on the board: which square it rests on, since when, and
  // whether that hold has already crossed it. Not a mode; there is no mode.
  int heldCell = -1;
  unsigned long holdSinceMs = 0;
  bool holdFired = false;
  bool holdShown = false;

  // Where HINT tries a hint before giving it: a copy of the board, so the one
  // on screen is never changed and changed back under the render task.
  traintracks::Board hintProbe{};
  // HINT's mark lasts until the next move. CHECK's flags are the game's own
  // (Game::marked), since they are saved with it.
  int hintCell = -1;
  const char* notice = nullptr;
  char noticeText[24] = {};

  unsigned long lastTickMs = 0;
  // An unsaved change: when the first one happened, and the latest. A save
  // waits kSaveQuietMs after the last change, or kSaveMaxMs after the first,
  // so a run of taps is one SD write and losing power costs seconds of play.
  static constexpr unsigned long kSaveQuietMs = 4000;
  static constexpr unsigned long kSaveMaxMs = 30000;
  bool unsaved = false;
  unsigned long unsavedSinceMs = 0;
  unsigned long lastChangeMs = 0;
  // A new puzzle and a solve are the two beats that earn a full refresh.
  bool flashOnNextPaint = false;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};
