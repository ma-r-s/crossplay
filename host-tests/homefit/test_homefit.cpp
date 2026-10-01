// Home's menu fits under the cover tile in every theme: see run.sh and
// src/activities/home/HomeMenuFit.h.
#include <cstdio>

#include "HomeMenuFit.h"
#include "theme_metrics.h"

static int checksRun = 0;
static int checksFailed = 0;

static void check(const bool ok, const char* what, const char* theme, const int rows, const int extra) {
  ++checksRun;
  if (!ok) {
    ++checksFailed;
    std::printf("FAIL homefit  %s: %s, %d rows (%d)\n", theme, what, rows, extra);
  }
}

// The X4 Pro's logical portrait panel; touch boards draw no button hints.
constexpr int kPanelHeight = 800;

static void checkTheme(const ThemeMetrics& t, const int drawnRowHeight, const int rows) {
  const bool classic = t.name[0] == 'c';
  homefit::Input in{};
  in.rows = rows;
  in.rowHeight = drawnRowHeight;
  in.rowGap = t.rowGap;
  in.menuTop = t.topPadding + t.tile + t.verticalSpacing + t.menuOffset;
  in.leadIn = classic ? t.verticalSpacing : 0;
  in.pageHeight = kPanelHeight;
  in.hintsHeight = 0;
  in.sidePadding = t.side;
  const homefit::Fit f = homefit::fit(in);

  check(f.fits, "the menu does not fit", t.name, rows, homefit::need(rows, f.rowHeight, f.rowGap));
  // The last row's bottom edge, measured as drawn, stays inside the margin.
  const int lastRowBottom = in.menuTop + in.leadIn + rows * (f.rowHeight + f.rowGap) - f.rowGap;
  check(lastRowBottom <= kPanelHeight - homefit::kMinBottomMargin, "the last row reaches the panel's bottom edge",
        t.name, rows, lastRowBottom);
  check(f.bottomMargin >= homefit::kMinBottomMargin, "the bottom margin is under the floor", t.name, rows,
        f.bottomMargin);
  check(f.rowHeight >= homefit::kMinRowHeight || f.rowHeight == drawnRowHeight, "rows shrank under the floor", t.name,
        rows, f.rowHeight);
  // RoundedRaff pages by rows * (height + gap) over the rect; the rect must
  // hold every row or the last one moves to a page nobody can see.
  const int rect = f.menuBottom - in.menuTop;
  check(rect / (f.rowHeight + f.rowGap) >= rows, "RoundedRaff would page the menu", t.name, rows, rect);

  // Nothing shrinks that did not have to: the theme's own numbers survive
  // whenever they already fit inside a side-padding margin.
  homefit::Input roomy = in;
  roomy.rows = 1;
  const homefit::Fit untouched = homefit::fit(roomy);
  check(untouched.rowHeight == drawnRowHeight && untouched.rowGap == t.rowGap && untouched.bottomMargin == t.side,
        "a menu that fits was changed anyway", t.name, 1, untouched.rowHeight);
}

int main() {
  for (const ThemeMetrics& t : kThemes) {
    const bool roundedRaff = t.name[0] == 'r';
    // Browse Files, Library, File Transfer, Settings; the library slot (an
    // OPDS catalog or a plugin); RoundedRaff's Continue Reading once a book was
    // opened; the shelf's Games and Apps.
    for (int librarySlot = 0; librarySlot <= 1; ++librarySlot) {
      for (int continueRow = 0; continueRow <= (roundedRaff ? 1 : 0); ++continueRow) {
        const int rows = 4 + librarySlot + continueRow + 2;
        checkTheme(t, t.rowHeight, rows);
        // RoundedRaff draws font-derived rows (its metric says it is not
        // authoritative), so check a taller one than the table too.
        if (roundedRaff) checkTheme(t, t.rowHeight + 8, rows);
      }
    }
  }
  std::printf("homefit: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
