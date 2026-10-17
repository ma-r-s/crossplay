#pragma once

#include <memory>
#include <string>

#include "../../activities/Activity.h"
#include "../connections/ConnectionsScreens.h"
#include "../ui/ToyboxScreen.h"
#include "WordleCore.h"
#include "WordleScreens.h"

// Wordle, in Connections' shape: one download puts every past answer and the
// guess list on the card, and after that playing never touches the network.
class WordleActivity final : public Activity {
 public:
  WordleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Wordle", renderer, mappedInput) {}
  ~WordleActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { Menu, Game, Archive, HowTo, Importing };
  enum class ImportStep : uint8_t { Connecting, Ready, Downloading, Done, Failed };

  bool loadWords();
  int today() const;
  int newestDay() const;
  void openDay(int day);
  // The newest day with an answer on the card that is not yet finished, or -1.
  int nextUnfinished() const;
  void saveGame();
  void submit();
  void routeAction(const freeink::ui::ActionEvent& event, int tapX, int tapY);

  void showMonthOf(int day);
  void buildCalendar();
  bool canStep(int months) const;
  void step(int months);

  void beginImport();
  void runImport();

  View view_ = View::Menu;
  ImportStep importStep_ = ImportStep::Connecting;
  const char* importDetail_ = "";
  int imported_ = 0;
  bool wifiActivated_ = false;

  std::string answers_;  // five bytes per day, '.' for unknown
  std::string results_;  // one byte per day, WordleCore's encoding
  std::unique_ptr<char[]> words_;
  size_t wordCount_ = 0;

  wordle::Game game_;
  const char* message_ = nullptr;
  wordleui::KeyboardLayout keys_;

  connectionsui::CalendarDay calCells_[42] = {};
  connectionsui::CalendarLayout calLayout_;
  int calYear_ = 2026;
  int calMonth_ = 9;
  int calPlayed_ = 0;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
