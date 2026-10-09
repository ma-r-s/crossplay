#include "WorkoutsActivity.h"

#include <ESPmDNS.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>
#include <ctime>

#include "../../DevMode.h"
#include "../../activities/ActivityResult.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../util/DeviceHostname.h"
#include "../../util/QrUtils.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "WorkoutsStore.h"

namespace fui = freeink::ui;

namespace {

// Below this the clock was never set; the same floor Study and Live use.
constexpr time_t kClockFloor = 1700000000;
// Years of training at a line a day, and still small enough to read whole.
constexpr size_t kLogMax = 64 * 1024;
// Lines kept when the log is rewritten; at the longest line this format writes
// (a 40-byte title) that is well under kLogMax.
constexpr size_t kLogKeep = 1000;
constexpr size_t kTodayMax = 4 * 1024;

}  // namespace

std::unique_ptr<Activity> WorkoutsActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<WorkoutsActivity>(renderer, mappedInput);
}

void WorkoutsActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  if (!workouts::store::begin()) {
    showNotice("The card would not open, so nothing was changed.");
    return;
  }
  loadAll();
  openHome();
}

void WorkoutsActivity::onExit() {
  stopPhone();
  Activity::onExit();
}

// --- Data ----------------------------------------------------------------

int WorkoutsActivity::currentDay() const {
  const time_t now = time(nullptr);
  if (now < kClockFloor) return -1;
  tm local{};
  localtime_r(&now, &local);
  return workouts::daysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

void WorkoutsActivity::loadAll() {
  plan_ = workouts::parsePlan(workouts::store::read(workouts::store::kPlanPath, workouts::kMaxPlanBytes));
  today_ = workouts::parseToday(workouts::store::read(workouts::store::kTodayPath, kTodayMax));
  log_ = workouts::parseLog(workouts::store::read(workouts::store::kLogPath, kLogMax));
  rollDay();
}

void WorkoutsActivity::rollDay() {
  const int day = currentDay();
  // An unset clock keeps whatever day the file carries: losing the time is not
  // a reason to lose the sets already ticked.
  if (day < 0 || today_.day == day) return;
  today_ = workouts::Today{};
  today_.day = day;
  undo_.clear();
  saveToday();
}

bool WorkoutsActivity::saveToday() {
  if (workouts::store::write(workouts::store::kTodayPath, workouts::formatToday(today_))) return true;
  LOG_ERR("WORKOUTS", "today.txt was not written");
  return false;
}

void WorkoutsActivity::rebuildHome() {
  cards_.clear();
  cards_.reserve(plan_.schedules.size());
  for (const workouts::Schedule& schedule : plan_.schedules) {
    workoutsui::ScheduleCard card;
    card.title = schedule.title.c_str();
    card.icon = schedule.icon;
    card.exercises = static_cast<int>(schedule.exercises.size());
    card.sets = schedule.totalSets();
    for (const workouts::Progress& progress : today_.entries) {
      if (progress.title == schedule.title) card.done = progress.total();
    }
    cards_.push_back(card);
  }
}

void WorkoutsActivity::rebuildSchedule() {
  rows_.clear();
  tally_.clear();
  if (open_ < 0 || open_ >= static_cast<int>(plan_.schedules.size())) return;
  const workouts::Schedule& schedule = plan_.schedules[static_cast<size_t>(open_)];
  const workouts::Progress& progress = workouts::progressFor(today_, schedule);
  rows_.reserve(schedule.exercises.size());
  for (size_t i = 0; i < schedule.exercises.size(); i++) {
    workoutsui::ExerciseRow row;
    row.name = schedule.exercises[i].name.c_str();
    row.sets = schedule.exercises[i].sets;
    row.done = i < progress.done.size() ? progress.done[i] : 0;
    row.weight = schedule.exercises[i].weight;
    rows_.push_back(row);
  }
  char tally[24];
  std::snprintf(tally, sizeof(tally), "%d/%d", progress.total(), schedule.totalSets());
  tally_ = tally;
}

// "1 / 2" for whichever list is on the panel, and the top row snapped onto a
// page that exists after the data under it changed.
void WorkoutsActivity::relabel() {
  pageLabel_.clear();
  const fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const bool home = view_ == View::Home;
  const int page =
      home ? workoutsui::homeCapacity(target.deviceContext()) : workoutsui::scheduleCapacity(target.deviceContext());
  const int count = static_cast<int>(home ? cards_.size() : rows_.size());
  int& top = home ? homeTop_ : scheduleTop_;
  if (page <= 0 || count <= page) {
    top = 0;
    return;
  }
  if (top >= count) top = ((count - 1) / page) * page;
  top = (top / page) * page;
  char label[24];
  // The band's form on the opening screen, the shelf folders' form; the
  // spaced one under a schedule's rows, Notes' form.
  std::snprintf(label, sizeof(label), home ? "%d/%d" : "%d / %d", top / page + 1, (count + page - 1) / page);
  pageLabel_ = label;
}

// --- Navigation ----------------------------------------------------------

void WorkoutsActivity::openHome() {
  view_ = View::Home;
  open_ = -1;
  undo_.clear();
  rollDay();
  rebuildHome();
  relabel();
  interactionsReady_ = false;
  requestUpdate();
}

void WorkoutsActivity::openSchedule(const int index) {
  if (index < 0 || index >= static_cast<int>(plan_.schedules.size())) return;
  rollDay();
  open_ = index;
  scheduleTop_ = 0;
  undo_.clear();
  loggedThisVisit_ = false;
  view_ = View::Schedule;
  rebuildSchedule();
  relabel();
  interactionsReady_ = false;
  requestUpdate();
}

void WorkoutsActivity::showNotice(const char* text) {
  notice_ = text;
  view_ = View::Notice;
  interactionsReady_ = false;
  requestUpdate();
}

// Only the newest entries are kept: the strip reads a week, and a log past
// kLogMax would not read back at all.
void WorkoutsActivity::saveLog() {
  if (log_.size() > kLogKeep) log_.erase(log_.begin(), log_.end() - static_cast<long>(kLogKeep));
  if (!workouts::store::write(workouts::store::kLogPath, workouts::formatLog(log_))) {
    LOG_ERR("WORKOUTS", "log.txt was not written");
  }
}

// --- Ticks ---------------------------------------------------------------

void WorkoutsActivity::addSet(const int exercise) {
  if (open_ < 0 || open_ >= static_cast<int>(plan_.schedules.size())) return;
  const workouts::Schedule& schedule = plan_.schedules[static_cast<size_t>(open_)];
  workouts::Progress& progress = workouts::progressFor(today_, schedule);
  // A full exercise ignores the tap: the boxes already say it is done, and a
  // tap that wrapped back to zero would erase a whole exercise on one slip.
  if (!workouts::addSet(progress, schedule, exercise)) return;
  undo_.push_back(exercise);
  // Written NOW, not on the way out: a set somebody saw ticked and the card did
  // not is the one failure this app can have, and the power key is under a
  // thumb that is also holding a dumbbell.
  if (!saveToday()) {
    workouts::removeSet(progress, exercise);
    undo_.pop_back();
    showNotice("The card would not take the change, so that set was not saved.");
    return;
  }
  // The first set of a schedule today is what makes it a workout on the week
  // strip. A clock that was never set has no day to put it on.
  if (today_.day >= 0 && !workouts::logged(log_, today_.day, schedule.title)) {
    workouts::LogEntry entry;
    entry.day = today_.day;
    entry.icon = schedule.icon;
    entry.title = schedule.title;
    log_.push_back(entry);
    saveLog();
    loggedThisVisit_ = true;
  }
  rebuildSchedule();
  // UNDO may have just appeared on the bar, so the table changes with the paint.
  interactionsReady_ = false;
  requestUpdate();
}

void WorkoutsActivity::undo() {
  if (undo_.empty() || open_ < 0 || open_ >= static_cast<int>(plan_.schedules.size())) return;
  const workouts::Schedule& schedule = plan_.schedules[static_cast<size_t>(open_)];
  workouts::Progress& progress = workouts::progressFor(today_, schedule);
  const int exercise = undo_.back();
  undo_.pop_back();
  if (!workouts::removeSet(progress, exercise)) return;
  saveToday();
  // Taken all the way back to nothing, it was never a workout.
  if (progress.total() == 0 && loggedThisVisit_ && today_.day >= 0 &&
      workouts::unlog(log_, today_.day, schedule.title)) {
    loggedThisVisit_ = false;
    saveLog();
  }
  rebuildSchedule();
  interactionsReady_ = false;
  requestUpdate();
}

// --- Weight and reset ---------------------------------------------------

void WorkoutsActivity::adjustWeight(const int exercise, const int delta) {
  if (open_ < 0 || open_ >= static_cast<int>(plan_.schedules.size())) return;
  workouts::Schedule& schedule = plan_.schedules[static_cast<size_t>(open_)];
  if (exercise < 0 || exercise >= static_cast<int>(schedule.exercises.size())) return;
  workouts::Exercise& item = schedule.exercises[static_cast<size_t>(exercise)];
  const int before = item.weight;
  if (!workouts::adjustWeight(item, delta)) return;
  // Into the plan itself, so the next session starts from it and the phone
  // page shows it too.
  if (!workouts::store::write(workouts::store::kPlanPath, workouts::formatPlan(plan_))) {
    item.weight = before;
    showNotice("The card would not take the change, so the weight was not saved.");
    return;
  }
  rebuildSchedule();
  interactionsReady_ = false;
  requestUpdate();
}

void WorkoutsActivity::askReset() {
  if (open_ < 0 || open_ >= static_cast<int>(plan_.schedules.size())) return;
  const workouts::Schedule& schedule = plan_.schedules[static_cast<size_t>(open_)];
  const int sets = workouts::progressFor(today_, schedule).total();
  char prose[96];
  std::snprintf(prose, sizeof(prose), "Clear all %d sets and take today off the calendar?", sets);
  confirm_ = prose;
  view_ = View::ConfirmReset;
  interactionsReady_ = false;
  requestUpdate();
}

void WorkoutsActivity::reset() {
  if (open_ < 0 || open_ >= static_cast<int>(plan_.schedules.size())) return;
  const workouts::Schedule& schedule = plan_.schedules[static_cast<size_t>(open_)];
  workouts::Progress& progress = workouts::progressFor(today_, schedule);
  const std::vector<int> before = progress.done;
  if (workouts::resetProgress(progress) && !saveToday()) {
    progress.done = before;
    showNotice("The card would not take the change, so nothing was reset.");
    return;
  }
  // A reset workout was not done, so today loses its mark until a set is
  // ticked again.
  if (today_.day >= 0 && workouts::unlog(log_, today_.day, schedule.title)) saveLog();
  undo_.clear();
  loggedThisVisit_ = false;
  view_ = View::Schedule;
  rebuildSchedule();
  relabel();
  interactionsReady_ = false;
  requestUpdate();
}

// --- The phone -----------------------------------------------------------

void WorkoutsActivity::startPhone() {
#ifndef SIMULATOR
  // Never launch the picker unconditionally: it disconnects a working
  // association on every path. The same guard Notes and four others use.
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    startActivityForResult(makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                               showNotice("Setting up on your phone needs Wi-Fi. Nothing changed.");
                               return;
                             }
                             startPhone();
                           });
    return;
  }
#endif

  // Developer Mode holds port 80 while its toggle is on; it yields while this
  // screen is up, and every way out of here goes through stopPhone().
  devmode::pause();
  devPaused_ = true;

  server_ = makeUniqueNoThrow<WorkoutsServer>();
  if (!server_) {
    stopPhone();
    showNotice("There was not enough memory to start.");
    return;
  }
  const bool started = server_->begin();
#ifndef SIMULATOR
  if (!started) {
    stopPhone();
    showNotice("The reader could not open its web server. Try again in a moment.");
    return;
  }
#else
  (void)started;
#endif

#ifdef SIMULATOR
  const bool mdnsUp = false;
  const std::string dotted = "127.0.0.1";
#else
  MDNS.end();
  const bool mdnsUp = MDNS.begin(devicehost::mdnsName());
  const std::string dotted = std::string(WiFi.localIP().toString().c_str());
#endif
  // The code carries the address, which depends on no service; the name, which
  // does, is only what a person reads.
  phoneUrl_ = "http://" + dotted + "/gym";
#ifdef SIMULATOR
  phoneReadable_ = phoneUrl_;
  (void)mdnsUp;
#else
  phoneReadable_ = mdnsUp ? std::string("http://") + devicehost::mdnsName() + ".local/gym" : phoneUrl_;
#endif
  phoneSaved_ = false;
  view_ = View::Phone;
  interactionsReady_ = false;
  requestUpdate();
}

void WorkoutsActivity::stopPhone() {
  if (server_) {
    server_->stop();
    server_.reset();
#ifndef SIMULATOR
    MDNS.end();
#endif
  }
  if (devPaused_) {
    devPaused_ = false;
    devmode::resume();
  }
}

// --- Input ---------------------------------------------------------------

void WorkoutsActivity::loop() {
  // Back is read above the tap guard: the global back-swipe arrives as
  // Button::Back, and a swipe is not a tap.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (view_) {
      case View::Home:
        shelf::leave(renderer, mappedInput);
        return;
      case View::Phone:
        stopPhone();
        openHome();
        return;
      case View::ConfirmReset:
        view_ = View::Schedule;
        interactionsReady_ = false;
        requestUpdate();
        return;
      case View::Schedule:
      case View::Notice:
        openHome();
        return;
    }
  }

  const bool down = mappedInput.wasReleased(MappedInputManager::Button::Down);
  const bool up = mappedInput.wasReleased(MappedInputManager::Button::Up);
  if ((down || up) && (view_ == View::Home || view_ == View::Schedule)) {
    const fui::GfxRendererTarget target = toybox::makeTarget(renderer);
    const bool home = view_ == View::Home;
    const int page =
        home ? workoutsui::homeCapacity(target.deviceContext()) : workoutsui::scheduleCapacity(target.deviceContext());
    const int count = static_cast<int>(home ? cards_.size() : rows_.size());
    int& top = home ? homeTop_ : scheduleTop_;
    const int next = down ? top + page : top - page;
    if (page > 0 && count > page && next >= 0 && next < count) {
      top = next;
      relabel();
      interactionsReady_ = false;
      requestUpdate();
    }
    return;
  }

  if (server_ && server_->isRunning()) {
    // Pumped from loop(): there are no background threads in this firmware.
    for (int i = 0; i < 8 && server_->isRunning(); ++i) server_->handleClient();
    if (server_->takeChanged()) {
      plan_ = workouts::parsePlan(workouts::store::read(workouts::store::kPlanPath, workouts::kMaxPlanBytes));
      // The phone may have set the clock, which is the first moment there is a
      // today to start.
      rollDay();
      phoneSaved_ = true;
      interactionsReady_ = false;
      requestUpdate();
    }
  }

  int x = 0;
  int y = 0;
  if (!mappedInput.wasScreenTapped(x, y) || !interactionsReady_) return;
  fui::InputSnapshot input{};
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(x);
  input.touchY = static_cast<int16_t>(y);
  const fui::ActionEvent action = interactions_.route(input);

  switch (action.action) {
    case workoutsui::ActionOpenSchedule:
      openSchedule(action.value);
      return;
    case workoutsui::ActionAddSet:
      addSet(action.value);
      return;
    case workoutsui::ActionUndo:
      undo();
      return;
    case workoutsui::ActionWeightDown:
      adjustWeight(action.value, -1);
      return;
    case workoutsui::ActionWeightUp:
      adjustWeight(action.value, 1);
      return;
    case workoutsui::ActionReset:
      askReset();
      return;
    case workoutsui::ActionResetConfirm:
      reset();
      return;
    case workoutsui::ActionResetKeep:
      view_ = View::Schedule;
      interactionsReady_ = false;
      requestUpdate();
      return;
    case workoutsui::ActionDone:
      openHome();
      return;
    case workoutsui::ActionUsePhone:
      startPhone();
      return;
    case workoutsui::ActionDismiss:
      stopPhone();
      openHome();
      return;
    default:
      return;
  }
}

// --- Render --------------------------------------------------------------

void WorkoutsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);

  switch (view_) {
    case View::Home: {
      workoutsui::HomeModel model;
      model.cards = cards_.data();
      model.count = static_cast<int>(cards_.size());
      model.firstVisible = homeTop_;
      model.pageLabel = pageLabel_.empty() ? nullptr : pageLabel_.c_str();
      const int day = currentDay();
      model.clockSet = day >= 0;
      if (model.clockSet) workouts::calendarCells(log_, day, model.days);
      workoutsui::buildHome(screen, model);
      break;
    }
    case View::Schedule: {
      workoutsui::ScheduleModel model;
      model.title = open_ >= 0 && open_ < static_cast<int>(plan_.schedules.size())
                        ? plan_.schedules[static_cast<size_t>(open_)].title.c_str()
                        : "WORKOUTS";
      model.rows = rows_.data();
      model.count = static_cast<int>(rows_.size());
      model.firstVisible = scheduleTop_;
      model.pageLabel = pageLabel_.empty() ? nullptr : pageLabel_.c_str();
      model.tally = tally_.empty() ? nullptr : tally_.c_str();
      model.canUndo = !undo_.empty();
      if (open_ >= 0 && open_ < static_cast<int>(plan_.schedules.size())) {
        const workouts::Schedule& schedule = plan_.schedules[static_cast<size_t>(open_)];
        const int total = schedule.totalSets();
        model.canReset = total > 0 && workouts::progressFor(today_, schedule).total() >= total;
      }
      workoutsui::buildSchedule(screen, model);
      break;
    }
    case View::ConfirmReset:
      workoutsui::buildResetConfirm(screen,
                                    open_ >= 0 && open_ < static_cast<int>(plan_.schedules.size())
                                        ? plan_.schedules[static_cast<size_t>(open_)].title.c_str()
                                        : "WORKOUTS",
                                    confirm_.c_str());
      break;
    case View::Phone: {
      workoutsui::PhoneModel model;
      model.url = phoneUrl_.c_str();
      model.readable = phoneReadable_.c_str();
      model.saved = phoneSaved_;
      const fui::Rect qr = workoutsui::buildPhone(screen, model);
      QrUtils::drawQrCode(renderer, Rect{qr.x, qr.y, qr.width, qr.height}, phoneUrl_);
      break;
    }
    case View::Notice:
      workoutsui::buildNotice(screen, notice_.c_str());
      break;
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Workouts");
  renderer.displayBuffer();
}
