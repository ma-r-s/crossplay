// What a slow screen is made of, split into the panel's time and ours.
//
// This exists because GitHub issue #7 could not be answered from a shipped
// build: a page turn took 4.2s, the panel driver printed 772ms for its own
// refresh, and nothing in the firmware subtracted the two. PanelBusy is the
// subtraction, and these are the ways it can lie.

#include <PaintClock.h>

#include <cstdio>

static int checks = 0;
static int failed = 0;

static void eq(const uint32_t got, const uint32_t want, const char* what) {
  checks++;
  if (got != want) {
    failed++;
    std::printf("FAIL panelclock  %s\n      want %u\n      got  %u\n", what, want, got);
  }
}

static void isTrue(const bool got, const char* what) {
  checks++;
  if (!got) {
    failed++;
    std::printf("FAIL panelclock  %s\n      want true, got false\n", what);
  }
}

int main() {
  // -- the number the issue actually needed ---------------------------------
  //
  // One repaint, 4200ms wall, of which the panel held us for 772ms. The answer
  // the report wanted is the other 3428ms, and it is only available if the
  // panel's own time is billed separately.
  {
    paintclock::PanelBusy b;
    b.begin(1000);
    b.end(1772);
    eq(b.busyMs(), 772, "one blocking refresh bills its own wall time");
  }

  // -- several panel calls in one repaint -----------------------------------
  //
  // The reader's anti-aliased page turn drives the panel three times (B/W
  // base, the grayscale planes, the cleanup). Billing only the first would
  // understate the panel and blame our code for the rest.
  {
    paintclock::PanelBusy b;
    b.begin(0);
    b.end(300);
    b.begin(500);
    b.end(900);
    b.begin(1000);
    b.end(1010);
    eq(b.busyMs(), 710, "every panel call in a repaint adds up");
  }

  // -- time outside a span belongs to us ------------------------------------
  //
  // The gap between two panel calls is our rendering. If it were billed to the
  // panel the split would always read "it is the hardware", which is exactly
  // the conclusion this instrument exists to test rather than assume.
  {
    paintclock::PanelBusy b;
    b.begin(0);
    b.end(100);
    b.begin(2000);  // 1900ms of our own work in between
    b.end(2100);
    eq(b.busyMs(), 200, "the gap between panel calls is not the panel's");
  }

  // -- a nested call is billed once -----------------------------------------
  //
  // A paint helper that grows a second display call inside an existing span
  // would otherwise be counted twice, and could report more panel time than
  // the repaint containing it: a wrong number that still looks plausible.
  {
    paintclock::PanelBusy b;
    b.begin(0);
    b.begin(50);
    b.end(150);
    b.end(200);
    eq(b.busyMs(), 200, "a nested panel call is billed once, not twice");
  }

  // -- an unmatched end bills nothing ---------------------------------------
  //
  // It cannot know when its span began. Guessing (say, from reset) would
  // attribute the whole repaint to the panel.
  {
    paintclock::PanelBusy b;
    b.end(5000);
    eq(b.busyMs(), 0, "an end with no begin bills nothing");
  }

  // -- an open span is not billed until it closes ---------------------------
  //
  // The repaint log reads busyMs() after render() returns, so a span that is
  // still open is a bug elsewhere; reporting a partial figure for it would
  // hide that bug behind a number.
  {
    paintclock::PanelBusy b;
    b.begin(0);
    eq(b.busyMs(), 0, "an open span has not been billed yet");
    isTrue(b.inPanelCall(), "an open span is visible as one");
    b.end(400);
    eq(b.busyMs(), 400, "closing the span bills it");
  }

  // -- reset starts the next repaint clean ----------------------------------
  //
  // Every repaint is measured on its own. Without the reset the log would
  // report a running total and every screen would look worse than the last.
  {
    paintclock::PanelBusy b;
    b.begin(0);
    b.end(700);
    b.reset();
    eq(b.busyMs(), 0, "reset clears the previous repaint");
    b.begin(1000);
    b.end(1100);
    eq(b.busyMs(), 100, "and the next repaint is billed from zero");
  }

  // -- reset while a span is open ------------------------------------------
  //
  // The repaint log resets immediately before render(); if a previous paint
  // left a span open, the new repaint must not inherit it and bill from the
  // old start.
  {
    paintclock::PanelBusy b;
    b.begin(0);
    b.reset();
    b.end(9000);
    eq(b.busyMs(), 0, "a span orphaned by reset bills nothing");
  }

  std::printf("panelclock: %d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}
