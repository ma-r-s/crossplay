#pragma once

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "ConnectionsCore.h"
#include "ConnectionsPack.h"
#include "ConnectionsResults.h"
#include "ConnectionsScreens.h"

// Connections. Sixteen words, four hidden groups of four.
//
// Puzzles come from a pack on the SD card, imported over WiFi by the GET
// PUZZLES item and then never touched again: playing is pure offline reads, so
// this works on a train.
class ConnectionsActivity final : public Activity {
 public:
  ConnectionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Connections", renderer, mappedInput) {}
  ~ConnectionsActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { Menu, Archive, Board, Importing, HowTo };
  // Split so the "connecting" and "downloading" screens paint BEFORE the work
  // that follows them starts; both block for seconds with no repaint.
  enum class ImportStep : uint8_t { Connecting, Ready, Downloading, Done, Failed };

  bool openPack();
  void closePack();
  // Loads puzzle `index` from the pack and starts (or resumes) a game on it.
  bool startPuzzle(int index);
  void buildCalendar();
  // The date a grid cell stands for, or 0 for a blank or out-of-archive cell.
  uint32_t dateForCell(int cell) const;
  void showMonthOf(uint32_t date);
  bool canStepMonth(int delta) const;
  bool canStepYear(int delta) const;
  void stepMonth(int delta);
  void stepYear(int delta);
  void saveProgress() const;
  bool loadProgress();
  void routeAction(const freeink::ui::ActionEvent& event);
  connections::Record readResult(uint32_t date) const;

  // Fills the menu's played/perfect/streak counters from one pass over the
  // results file. Reading the file whole (~1.2KB) rather than seeking per day
  // is what makes this affordable: 1143 seeks to draw a menu would not be.
  void fillStats(connectionsui::MenuModel& model) const;
  void writeResult(uint32_t date, const connections::Record& record) const;
  uint32_t newestPackDate() const;
  void beginImport();
  void runImport();
  void handleSubmit();

  View view = View::Menu;
  int menuIndex = 0;
  int puzzleIndex = -1;

  ImportStep importStep = ImportStep::Connecting;
  int importedCount = 0;
  const char* importDetail = "";
  // True once the radio has been switched on, so onExit knows to tear it down
  // and take the heap-defrag restart that every wifi user in this firmware takes.
  bool wifiActivated = false;

  connections::PackReader pack;
  bool packOpen = false;
  connections::Game game;
  // Cleared on the next input, so a wrong-guess message does not linger into
  // the following move.
  const char* toast = nullptr;

  connectionsui::CalendarDay calCells[42] = {};
  // Filled by the render that drew the month; a tap on the calendar block is
  // resolved against it rather than against a second copy of the arithmetic.
  connectionsui::CalendarLayout calLayout;
  int calYear = 2026;
  int calMonth = 8;
  int calPlayed = 0;
  // The archive's bounds, read from the pack so the steppers stop rather than
  // wandering into years with no puzzles in them.
  uint32_t firstDate = 0;
  uint32_t lastDate = 0;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};
