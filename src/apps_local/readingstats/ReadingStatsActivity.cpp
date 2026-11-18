#include "ReadingStatsActivity.h"

#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "ReadingStatsLog.h"
#include "fontIds.h"

namespace fui = freeink::ui;

std::unique_ptr<Activity> ReadingStatsActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<ReadingStatsActivity>(renderer, mappedInput);
}

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  view_ = View::Main;
  page_ = 0;
  load();
  requestUpdate();
}

void ReadingStatsActivity::onExit() {
  Activity::onExit();
  tally_.reset();
  rows_.clear();
  rows_.shrink_to_fit();
  dates_.clear();
  dates_.shrink_to_fit();
}

// Once per visit. The records only change while a book is open, and a book is
// never open while this is on screen.
void ReadingStatsActivity::load() {
  tally_.reset();
  rows_.clear();
  dates_.clear();
  int32_t today = 0;
  clockSet_ = readingstats::localToday(today);
  if (!clockSet_) {
    LOG_INF("RSTATS", "clock not trusted; nothing can be dated");
    return;
  }
  tally_ = makeUniqueNoThrow<readingstats::Tally>(today);
  if (!tally_) {
    LOG_ERR("RSTATS", "OOM: tally");
    return;
  }
  readingstats::loadTally(*tally_);

  // The dates first and the rows after: a row points into dates_, so dates_
  // must not grow once a row holds one of its strings.
  const auto& books = tally_->books();
  dates_.reserve(books.size());
  rows_.reserve(books.size());
  for (const readingstats::Book& book : books) {
    char date[16];
    readingstats::formatDay(book.day, date, sizeof(date));
    dates_.emplace_back(date);
  }
  for (size_t i = 0; i < books.size(); ++i) {
    rstatsui::BookRow row;
    row.title = books[i].title.c_str();
    row.date = dates_[i].c_str();
    rows_.push_back(row);
  }
}

rstatsui::MainModel ReadingStatsActivity::mainModel() const {
  rstatsui::MainModel model;
  model.clockSet = clockSet_ && tally_ != nullptr;
  if (!model.clockSet) return model;
  const readingstats::Tally& tally = *tally_;
  const int32_t today = tally.today();
  model.anySession = tally.anyInChart() || tally.anySession();
  model.year = tally.year();
  model.weekSeconds = tally.weekSeconds();
  model.streak = tally.streak();
  model.streakAtCap = tally.streakAtCap();
  const int32_t weekStart = today - readingstats::weekday(today);
  const int32_t first = today + rstatsui::kFirstChartDay;
  for (int i = 0; i < rstatsui::kChartCells; ++i) {
    const int32_t day = first + i;
    rstatsui::Day& cell = model.chart[i];
    cell.seconds = tally.secondsOn(day);
    cell.letter = readingstats::weekdayLetter(day);
    cell.today = day == today;
    cell.thisWeek = day >= weekStart && day <= today;
  }
  model.books = rows_.data();
  model.bookCount = static_cast<int>(rows_.size());
  return model;
}

int ReadingStatsActivity::pageCount() const {
  const fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const int perPage = rstatsui::booksPerPage(target.deviceContext());
  const int count = static_cast<int>(rows_.size());
  return count <= 0 ? 1 : (count + perPage - 1) / perPage;
}

void ReadingStatsActivity::showPage(const int page) {
  page_ = page;
  const int pages = pageCount();
  if (pages > 1) {
    char label[24];
    std::snprintf(label, sizeof(label), "%d/%d", page_ + 1, pages);
    pageLabel_ = label;
  } else {
    pageLabel_.clear();
  }
  interactionsReady_ = false;
  requestUpdate();
}

void ReadingStatsActivity::loop() {
  // Read on the per-frame path, above the tap guard: the back swipe is not a
  // tap. host-tests/backgesture holds every app to this.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (view_ == View::Books) {
      view_ = View::Main;
      interactionsReady_ = false;
      requestUpdate();
      return;
    }
    shelf::leave(renderer, mappedInput);
    return;
  }

  if (view_ == View::Books) {
    // The side keys step a page, and so does a vertical swipe, the way the
    // shelf pages: never sideways, which is Back's axis. Nothing wraps.
    const MappedInputManager::SwipeDir swipe = mappedInput.wasSwipe();
    const bool next =
        mappedInput.wasReleased(MappedInputManager::Button::Down) || swipe == MappedInputManager::SwipeDir::Up;
    const bool prev =
        mappedInput.wasReleased(MappedInputManager::Button::Up) || swipe == MappedInputManager::SwipeDir::Down;
    if (next || prev) {
      const int to = page_ + (next ? 1 : -1);
      if (to >= 0 && to < pageCount()) showPage(to);
      return;
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
  if (action.action == rstatsui::ActionAllBooks && view_ == View::Main) {
    view_ = View::Books;
    showPage(0);
  }
}

void ReadingStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);
  rstatsui::Titles titles;

  // First pass: everything that is the app's own, in Jersey.
  if (view_ == View::Main) {
    rstatsui::buildMain(screen, mainModel(), titles);
  } else {
    rstatsui::BooksModel model;
    model.year = tally_ != nullptr ? tally_->year() : 0;
    model.books = rows_.data();
    model.count = static_cast<int>(rows_.size());
    model.first = page_ * rstatsui::booksPerPage(target.deviceContext());
    model.pageLabel = pageLabel_.empty() ? nullptr : pageLabel_.c_str();
    rstatsui::buildBooks(screen, model, titles);
  }

  // Second pass: the titles, in the system face's three cuts. They are the
  // books' own words, in any script, and Jersey carries ASCII alone.
  target.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  target.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  target.setFont(fui::GfxRendererTarget::FONT_TITLE, UI_12_FONT_ID);
  rstatsui::drawTitles(target, titles);

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Reading Stats");
  renderer.displayBuffer();
}
