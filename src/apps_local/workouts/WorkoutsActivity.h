#pragma once

// Workouts: schedules written on a phone, sets ticked on the reader.
//
// The three-way split every app in this fork uses. WorkoutsCore holds the rules
// (the files, what a tap does, which days were trained) and has a host suite.
// WorkoutsScreens lays out and can be driven by host-tests/ui. This file keeps
// what needs hardware: the card, the clock, the phone page, and which screen is
// on the panel.
//
// There is no editor on the device. A schedule is a list of names and numbers,
// which is twenty taps on a phone keyboard and two hundred on this one, so the
// device's edits are the tick and the weight's - and +, and the phone page does
// the rest -- Notes' TYPE ON YOUR PHONE, promoted from a menu row to the app's
// one action.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "WorkoutsCore.h"
#include "WorkoutsScreens.h"
#include "WorkoutsServer.h"

class WorkoutsActivity final : public Activity {
 public:
  WorkoutsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Workouts", renderer, mappedInput) {}
  ~WorkoutsActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Not asleep with the phone page up: it would stop answering mid-save and the
  // phone would blame the network.
  bool preventAutoSleep() override { return server_ && server_->isRunning(); }
  bool skipLoopDelay() override { return server_ && server_->isRunning(); }

 private:
  enum class View : uint8_t { Home, Schedule, ConfirmReset, Phone, Notice };

  void loadAll();
  // Today, by the device's clock and timezone; -1 while the clock is unset.
  int currentDay() const;
  // A new day empties today.txt, so yesterday's ticks never greet a new session.
  void rollDay();
  void openHome();
  void openSchedule(int index);
  void addSet(int exercise);
  void undo();
  void adjustWeight(int exercise, int delta);
  void askReset();
  void reset();
  bool saveToday();
  void saveLog();
  void showNotice(const char* text);
  void startPhone();
  void stopPhone();
  void rebuildHome();
  void rebuildSchedule();
  void relabel();

  workouts::Plan plan_;
  workouts::Today today_;
  std::vector<workouts::LogEntry> log_;

  View view_ = View::Home;
  int open_ = -1;  // index into plan_.schedules while a schedule is open
  int homeTop_ = 0;
  int scheduleTop_ = 0;
  // Exercise indices ticked this visit, newest last: what UNDO takes back. Not
  // persisted, because an undo that reaches into a previous visit undoes a set
  // the person no longer remembers ticking.
  std::vector<int> undo_;
  // This visit wrote today's line in the log. Only then does undoing back to
  // nothing take the day off the calendar, so UNDO never erases a workout
  // logged on an earlier visit. RESET removes the line itself.
  bool loggedThisVisit_ = false;

  // Row storage the screens point into, rebuilt when the data changes.
  std::vector<workoutsui::ScheduleCard> cards_;
  std::vector<workoutsui::ExerciseRow> rows_;
  std::string pageLabel_;
  std::string tally_;
  std::string notice_;
  std::string confirm_;

  std::unique_ptr<WorkoutsServer> server_;
  bool devPaused_ = false;
  bool phoneSaved_ = false;
  std::string phoneUrl_;
  std::string phoneReadable_;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
