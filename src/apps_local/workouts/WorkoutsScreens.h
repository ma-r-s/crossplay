#pragma once

// The Workouts screens.
//
// Freestanding builders in the NotesScreens mould: a model in, a drawn frame
// out, no renderer and no Activity, so host-tests/ui can assert what they drew
// and what they made tappable.
//
// Two screens carry the app. The opening one is the schedules as cards, with
// last week and this one pinned to the foot as a calendar, and the pencil on
// the band that opens the phone page. The schedule itself is one row per
// exercise with a box per set: the whole row is the target, because the person
// tapping it is between sets and holding something heavy. Only the weight's -
// and + beside the boxes are kept out of it.

#include <cstdint>

#include "../ui/ToyboxScreen.h"
#include "WorkoutsCore.h"

namespace workoutsui {

namespace fui = freeink::ui;

// Notes has the 340s; Workouts takes the 620s.
enum : fui::ActionId {
  ActionOpenSchedule = 620,
  ActionAddSet = 621,
  ActionUsePhone = 622,
  ActionUndo = 623,
  ActionDone = 624,
  ActionDismiss = 625,
  ActionWeightDown = 626,
  ActionWeightUp = 627,
  ActionReset = 628,
  ActionResetConfirm = 629,
  ActionResetKeep = 630,
};

// The mark for kIcons[index] at 24 or 32px. An index out of range draws the
// first mark, the same rule the files follow.
const freeink::Icon& scheduleIcon(int index, bool small);

// --- The schedules -------------------------------------------------------

struct ScheduleCard {
  const char* title = "";
  int icon = 0;
  int exercises = 0;
  int sets = 0;
  int done = 0;  // sets ticked today
};

struct HomeModel {
  const ScheduleCard* cards = nullptr;
  int count = 0;
  int firstVisible = 0;
  // "1 / 2" when the schedules do not fit one page, drawn on the band.
  const char* pageLabel = nullptr;
  // The calendar needs a date. A device whose clock was never set draws the
  // calendar's frame with a line saying so, rather than two weeks of 1970.
  bool clockSet = false;
  workouts::WeekCell days[workouts::kCalendarDays];
};

void buildHome(toybox::Screen& screen, const HomeModel& model);
// How many cards a page holds, asked of the layout the drawing uses.
int homeCapacity(const fui::DeviceContext& device);

// --- One schedule --------------------------------------------------------

struct ExerciseRow {
  const char* name = "";
  int sets = 0;
  int done = 0;
  int weight = 0;  // kg, drawn between the row's - and + beside its boxes
};

struct ScheduleModel {
  const char* title = "";
  const ExerciseRow* rows = nullptr;
  int count = 0;
  int firstVisible = 0;
  const char* pageLabel = nullptr;
  // "9/18" on the band: the whole session at a glance.
  const char* tally = nullptr;
  // UNDO is drawn only when there is a tap this visit can take back. It sits on
  // the right of the bar, the side that takes things away, and its half of the
  // bar is simply empty when there is nothing to take.
  bool canUndo = false;
  // Every set ticked: RESET takes UNDO's place, since there is nothing left to
  // tick and the likeliest next wish is to go again.
  bool canReset = false;
};

void buildSchedule(toybox::Screen& screen, const ScheduleModel& model);
int scheduleCapacity(const fui::DeviceContext& device);

// --- The reset confirm --------------------------------------------------

// KEEP IT occupies exactly the pixels RESET had on the schedule's bar, so a
// second jab at RESET during the repaint keeps the sets. RESET IT sits where
// DONE was, which never leads here.
void buildResetConfirm(toybox::Screen& screen, const char* title, const char* prose);

// --- The phone -----------------------------------------------------------

struct PhoneModel {
  // What the QR carries: the device's own address, from the live IP.
  const char* url = "";
  // What a person reads: the mDNS name when it started, else the address.
  const char* readable = "";
  bool saved = false;
};

// Returns the square the caller draws the code into: QrUtils needs a renderer,
// which this layer does not have.
fui::Rect buildPhone(toybox::Screen& screen, const PhoneModel& model);

// --- A refusal -----------------------------------------------------------

void buildNotice(toybox::Screen& screen, const char* prose);

}  // namespace workoutsui
