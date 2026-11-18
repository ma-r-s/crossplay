// No title can overflow its box: measured in the real system face.
//
// host-tests/ui measures ten pixels a character, which can say a title was
// split into lines and cannot say whether those lines fit. This builds the
// title pass against fittedtitle's RealTarget bound to the three ubuntu cuts
// the activity binds (UI 12, UI 10 and the 8 the firmware calls SMALL), which
// measures with EpdFont over the same data drawText uses and records what the
// renderer would really have drawn -- including the ellipsis it would add to a
// line wider than its rect.
//
// The property, for every title: every line lands inside the box and is drawn
// whole. For every title a person would actually have, additionally: the
// whole title is on the screen. Only a title no real book has may end in
// "...", and even that one stays inside its box.

#include <cstdio>
#include <string>
#include <vector>

#include "../../lib/EpdFont/builtinFonts/ubuntu_10_regular.h"
#include "../../lib/EpdFont/builtinFonts/ubuntu_12_regular.h"
#include "../../lib/EpdFont/builtinFonts/ubuntu_8_regular.h"
#include "../../src/apps_local/ui/fonts/toybox_10.h"
#include "../../src/apps_local/ui/fonts/toybox_20.h"
#include "../../src/apps_local/ui/fonts/toybox_30.h"
#include "../fittedtitle/real_target.h"
#include "ReadingStatsCore.h"
#include "ReadingStatsScreens.h"
#include "ReadingStatsTitles.h"

static int checks = 0;
static int failures = 0;

static void check(const bool ok, const std::string& what) {
  ++checks;
  if (ok) return;
  ++failures;
  std::printf("FAIL titlefit  %s\n", what.c_str());
}

namespace {

fui::DeviceContext device() {
  fui::DeviceContext ctx;
  ctx.width = 480;
  ctx.height = 800;
  ctx.hasTouch = true;
  return ctx;
}

struct Outcome {
  bool inside = true;
  bool drawnWhole = true;
  bool titleWhole = true;
  int lines = 0;
};

Outcome fit(const std::string& title) {
  fitted::RealTarget target(fitted::Faces{&ubuntu_8_regular, &ubuntu_10_regular, &ubuntu_12_regular});
  const fui::Rect box = fui::makeRect(100, 300, rstatsui::titleWidth(device()), rstatsui::titleHeight());
  rstatsui::Titles titles;
  titles.slots[0].box = box;
  titles.slots[0].title = title.c_str();
  titles.count = 1;
  rstatsui::drawTitles(target, titles);

  Outcome out;
  out.lines = static_cast<int>(target.texts.size());
  std::string joined;
  for (const fitted::TextRun& run : target.texts) {
    if (run.rect.x < box.x || run.rect.right() > box.right() || run.rect.y < box.y ||
        run.rect.bottom() > box.bottom()) {
      out.inside = false;
    }
    if (run.cut()) out.drawnWhole = false;
    if (!joined.empty() && !run.drawn.empty()) {
      // A CJK line break swallows no space; a Latin one swallows exactly one.
      const unsigned char last = static_cast<unsigned char>(joined.back());
      joined += last >= 0x80 ? "" : " ";
    }
    joined += run.drawn;
  }
  // Compare with spaces removed: what matters is that no character went missing.
  auto squeeze = [](const std::string& s) {
    std::string r;
    for (const char c : s) {
      if (c != ' ') r.push_back(c);
    }
    return r;
  };
  out.titleWhole = squeeze(joined) == squeeze(title);
  return out;
}

// Every string the main and books screens draw, in every state, measured in
// the faces the activity binds for each pass: Jersey 10/20/30 for the first,
// the ubuntu cuts for the titles. A line the renderer would cut is a fail.
// The ui suite cannot see this -- its ten pixels a character is narrower than
// Jersey's UI cut -- and that is how "JOIN WI-FI TO SET THE CLOCK" reached the
// simulator ending in "CL...".
struct Screens {
  int cut = 0;
  int runs = 0;
};

void measureRuns(const fitted::RealTarget& target, Screens& out, const char* where) {
  for (const fitted::TextRun& run : target.texts) {
    ++out.runs;
    if (run.cut()) {
      ++out.cut;
      check(false, std::string(where) + " cuts \"" + run.asked + "\" to \"" + run.drawn + "\"");
    }
  }
}

Screens everyScreen() {
  Screens out;
  const fitted::Faces jersey{&toybox_10, &toybox_20, &toybox_30};
  const fitted::Faces ubuntu{&ubuntu_8_regular, &ubuntu_10_regular, &ubuntu_12_regular};
  const rstatsui::BookRow books[] = {
      {"The Left Hand of Darkness", "16 NOV"},
      {"The Hitchhiker's Guide to the Galaxy", "2 NOV"},
      {"Le Comte de Monte-Cristo", "1 OCT"},
      {"The Strange Case of Dr Jekyll and Mr Hyde", "8 SEP"},
      {"Sapiens", "30 SEP"},
      {"Dune", "28 SEP"},
      {"It", "27 SEP"},
      {"Emma", "26 SEP"},
      {"Persuasion", "25 SEP"},
      {"Ulysses", "24 SEP"},
      {"Middlemarch", "23 SEP"},
      {"Beloved", "22 SEP"},
  };
  const int bookCount = static_cast<int>(sizeof(books) / sizeof(books[0]));

  // The widest each figure can be: 168 hours in a week, a streak at the cap,
  // 23 hours in a day, a year of four digits, three-digit book counts.
  rstatsui::MainModel widest;
  widest.anySession = true;
  widest.year = 2026;
  widest.weekSeconds = (168 * 60 - 1) * 60;
  widest.streak = readingstats::kStreakWindow - 1;
  widest.streakAtCap = true;
  for (int i = 0; i < rstatsui::kChartCells; ++i) {
    widest.chart[i].seconds = (i % 2 == 0 ? 999 : 23 * 60) * 60;
    widest.chart[i].letter = 'W';
    widest.chart[i].thisWeek = i >= 7;
    widest.chart[i].today = i == rstatsui::kChartCells - 1;
  }
  widest.books = books;
  widest.bookCount = bookCount;

  rstatsui::MainModel firstRun;
  firstRun.year = 2026;
  rstatsui::MainModel noClock = widest;
  noClock.clockSet = false;
  rstatsui::MainModel oneDay = widest;
  oneDay.streak = 1;
  oneDay.streakAtCap = false;
  oneDay.weekSeconds = 59 * 60;

  const rstatsui::MainModel* models[] = {&widest, &firstRun, &noClock, &oneDay};
  const char* names[] = {"the main screen at its widest", "the first run", "the missing clock", "a one-day streak"};
  for (int m = 0; m < 4; ++m) {
    fitted::RealTarget first(jersey);
    rstatsui::Titles titles;
    toybox::Interactions interactions;
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(first, device(), noInput, interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    rstatsui::buildMain(screen, *models[m], titles);
    measureRuns(first, out, names[m]);
    fitted::RealTarget second(ubuntu);
    rstatsui::drawTitles(second, titles);
    measureRuns(second, out, names[m]);
  }

  rstatsui::BooksModel page;
  page.year = 2026;
  page.books = books;
  page.count = 999;  // the count line at its widest; only the first page is drawn
  page.pageLabel = "99/99";
  {
    fitted::RealTarget first(jersey);
    rstatsui::Titles titles;
    toybox::Interactions interactions;
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(first, device(), noInput, interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    page.count = bookCount;
    rstatsui::buildBooks(screen, page, titles);
    measureRuns(first, out, "the books page");
    fitted::RealTarget second(ubuntu);
    rstatsui::drawTitles(second, titles);
    measureRuns(second, out, "the books page");
  }
  return out;
}

}  // namespace

int main() {
  {
    const Screens screens = everyScreen();
    check(screens.runs > 60, "the screen sweep measured almost nothing");
    std::printf("  screen sweep: %d text runs measured in the real faces, %d cut\n", screens.runs, screens.cut);
  }

  // Titles people have: long, accented, Cyrillic, Japanese, and the subtitle
  // pile-ups calibre and Gutenberg produce. All of them are cleaned the way
  // the recorder cleans them before they reach the screen.
  const char* real[] = {
      "The Left Hand of Darkness",
      "The Hitchhiker\xe2\x80\x99s Guide to the Galaxy",
      "Frankenstein; or, The Modern Prometheus",
      "Cien a\xc3\xb1os de soledad",
      "G\xc3\xb6"
      "del, Escher, Bach: An Eternal Golden Braid",
      "\xd0\x9c\xd0\xb0\xd1\x81\xd1\x82\xd0\xb5\xd1\x80 \xd0\xb8 "
      "\xd0\x9c\xd0\xb0\xd1\x80\xd0\xb3\xd0\xb0\xd1\x80\xd0\xb8"
      "\xd1\x82\xd0\xb0",
      "\xe3\x83\x8e\xe3\x83\xab\xe3\x82\xa6\xe3\x82\xa7\xe3\x82\xa4\xe3\x81\xae\xe6\xa3\xae",
      "Sapiens: A Brief History of Humankind",
      "The Strange Case of Dr Jekyll and Mr Hyde and Other Tales of Terror",
      "Zen and the Art of Motorcycle Maintenance: An Inquiry into Values, 25th Anniversary Edition with a New "
      "Introduction by the Author",
      "The Life and Opinions of Tristram Shandy, Gentleman",
      "Les Mis\xc3\xa9rables",
      "One Hundred Years of Solitude (Harper Perennial Modern Classics)",
      "A Song of Ice and Fire, Book One: A Game of Thrones",
      "The Complete Works of William Shakespeare: Comedies, Histories, Tragedies, Poems and Sonnets",
      "Pride and Prejudice",
      "Dune",
      "It",
  };
  for (const char* raw : real) {
    const std::string title = readingstats::cleanTitle(raw, "/books/x.epub");
    const Outcome o = fit(title);
    check(o.inside, "drawn outside its box: " + title);
    check(o.drawnWhole, "a line was cut by the renderer: " + title);
    check(o.titleWhole, "not all of the title reached the screen: " + title);
  }

  // The longest title a record can hold, in ordinary words of three languages,
  // the German one for its long compounds: whole. This is what sizes
  // kTitleCapacity -- the record holds no more than the row can show.
  const char* prose[] = {
      "Remembrance of Things Past and the Captive ",
      "Cr\xc3\xb3nica de una muerte anunciada y otros relatos del caribe ",
      "Die Donaudampfschifffahrtsgesellschaftskapit\xc3\xa4nsm\xc3\xbctze und andere Erz\xc3\xa4hlungen ",
  };
  for (const char* phrase : prose) {
    std::string longest;
    while (longest.size() < 400) longest += phrase;
    const std::string capped = readingstats::cleanTitle(longest.c_str(), nullptr);
    check(capped.size() <= readingstats::kTitleCapacity, "the cleaner kept more than the record holds");
    const Outcome o = fit(capped);
    check(o.inside && o.drawnWhole, "the longest storable title overflowed its box: " + capped);
    check(o.titleWhole, "the longest storable title of ordinary words is not shown whole: " + capped);
  }

  // The longest CJK title a record holds (41 characters and "..."): whole, broken
  // between characters since there are no spaces to break at.
  std::string cjk;
  while (cjk.size() < 400) cjk += "\xe6\xba\x90\xe6\xb0\x8f\xe7\x89\xa9\xe8\xaa\x9e";
  const std::string cappedCjk = readingstats::cleanTitle(cjk.c_str(), nullptr);
  {
    const Outcome o = fit(cappedCjk);
    check(o.inside && o.drawnWhole, "the longest storable CJK title overflowed its box");
    check(o.titleWhole, "the longest storable CJK title is not shown whole");
  }
  // A CJK title two lines long at the largest cut is set there: a CJK line
  // ends between characters as a matter of course, so it must not fall to the
  // smallest cut's last resort of breaking inside a word. Twenty-four
  // characters, which the face draws as 24px replacement boxes without an SD
  // font: 576px, two lines of the box.
  {
    std::string cjk24;
    for (int i = 0; i < 6; ++i) cjk24 += "\xe6\xba\x90\xe6\xb0\x8f\xe7\x89\xa9\xe8\xaa\x9e";
    fitted::RealTarget target(fitted::Faces{&ubuntu_8_regular, &ubuntu_10_regular, &ubuntu_12_regular});
    const rstatsui::TitleLayout layout =
        rstatsui::layOutTitle(target, cjk24.c_str(), rstatsui::titleWidth(device()), rstatsui::titleHeight());
    check(layout.font == fui::FONT_SLOT_TITLE && layout.lines == 2 && layout.whole,
          "a two-line CJK title did not get the largest cut");
  }

  // Nothing any book is called: the widest letter, as many as a record holds,
  // with no space to break at. It may end in "...", and it stays in its box.
  {
    const std::string widest(readingstats::kTitleCapacity, 'W');
    const Outcome o = fit(widest);
    check(o.inside && o.drawnWhole, "a pathological title escaped its box");
    check(o.lines <= rstatsui::kMaxTitleLines, "a pathological title drew more lines than the box has");
  }

  std::printf("%s  readingstats-titlefit: %d checks, %d failed\n", failures ? "FAIL" : "ok  ", checks, failures);
  return failures == 0 ? 0 : 1;
}
