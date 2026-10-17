// The PART WAY verdict's body, through the formatter the sync and the preview
// share. A deck name a user cannot place, or a reason cut off mid-word, is the
// failure this screen exists to prevent; both have shipped once.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/apps_local/study/StudyVerdict.h"

static int checks = 0;
static int failures = 0;

static void expect(const char* got, const char* want, const char* what) {
  ++checks;
  if (std::strcmp(got, want) != 0) {
    ++failures;
    std::printf("FAIL %s\n  got:  \"%s\"\n  want: \"%s\"\n", what, got, want);
  }
}

int main() {
  char out[192];

  // The field case: a two-level deck and the bridge's reason.
  studyui::partWayDetail(out, sizeof(out), {"United States Immigration Exam (2025)::Famous Americans"},
                         "its fronts are pictures", true);
  expect(out, "Famous Americans could not be built: its fronts are pictures.",
         "the deck's own name, and the reason in place of the reassurance");

  // No reason from the bridge (an older bridge, or a code it has no sentence
  // for): exactly what the reader said before.
  studyui::partWayDetail(out, sizeof(out), {"Mandarin: Vocabulary"}, "", true);
  expect(out, "Mandarin: Vocabulary could not be built. Everything else is up to date.",
         "a colon inside a name is not a path separator");
  studyui::partWayDetail(out, sizeof(out), {"Mandarin: Vocabulary"}, "", false);
  expect(out, "Mandarin: Vocabulary could not be built.", "nothing else updated, nothing claimed");

  // Several: the count, the first deck's own name, and no reason, because the
  // reason belongs to the first deck only and would read as everyone's.
  studyui::partWayDetail(out, sizeof(out), {"A::B::Kanji", "Other"}, "its fronts are pictures", true);
  expect(out, "2 decks could not be built, starting with Kanji. Everything else is up to date.",
         "several failures name the count, not one deck's reason");

  // A trailing separator leaves an empty leaf rather than reading past the end.
  studyui::partWayDetail(out, sizeof(out), {"Odd::"}, "it has no cards", false);
  expect(out, " could not be built: it has no cards.", "a trailing :: is survivable");

  // Nothing failed: nothing to say.
  studyui::partWayDetail(out, sizeof(out), {}, "x", true);
  expect(out, "", "an empty list writes an empty body");

  std::printf("test_verdict: %d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
