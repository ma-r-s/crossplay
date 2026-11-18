#pragma once

// READING STATS, in the Apps folder: this week's reading time, the streak, two
// weeks of minutes a day, and the books finished this year.
//
// The reader writes the records (ReadingStatsLog.h, called from one fork-local
// line in ReaderActivity::flushReaderSession()); this activity only reads them,
// once, when it opens. Nothing here runs while a book is open.

#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "ReadingStatsCore.h"
#include "ReadingStatsScreens.h"

class ReadingStatsActivity final : public Activity {
 public:
  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingStats", renderer, mappedInput) {}
  ~ReadingStatsActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View { Main, Books };

  void load();
  rstatsui::MainModel mainModel() const;
  int pageCount() const;
  void showPage(int page);

  View view_ = View::Main;
  bool clockSet_ = false;
  // 4KB of day totals and the year's books; held only while the app is open.
  std::unique_ptr<readingstats::Tally> tally_;
  std::vector<std::string> dates_;  // "12 MAR", one per book, same order
  std::vector<rstatsui::BookRow> rows_;
  int page_ = 0;
  std::string pageLabel_;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
