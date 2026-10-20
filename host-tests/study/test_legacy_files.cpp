// Glyph lists left on the card by older converters stop showing up as books.
//
// The converter used to write glyphs-{headword,sentence,latin,ruby}.txt into
// every deck folder. The Library lists every .txt on the card, so each deck put
// four "books" on the shelf. The converter now writes .lst, and opening a deck
// removes the old .txt files it no longer reads -- and nothing else.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "HalStorage.h"
#include "StudyLegacyFiles.h"

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      ++failures;                                                 \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

namespace {

std::string freshDeck() {
  char pattern[] = "/tmp/study-legacy-XXXXXX";
  const char* made = mkdtemp(pattern);
  if (made == nullptr) {
    std::perror("mkdtemp");
    std::exit(2);
  }
  Storage.root = made;
  ::mkdir((Storage.root + "/study").c_str(), 0777);
  ::mkdir((Storage.root + "/study/mandarin").c_str(), 0777);
  return "/study/mandarin";
}

void touch(const std::string& path) { std::ofstream(Storage.real(path.c_str())) << "x"; }

bool present(const std::string& path) { return Storage.exists(path.c_str()); }

void testOldGlyphListsAreRemoved() {
  const std::string deck = freshDeck();
  for (const char* name : {"headword", "sentence", "latin", "ruby"}) touch(deck + "/glyphs-" + name + ".txt");
  CHECK(study::removeLegacyGlyphLists(deck.c_str()) == 4);
  for (const char* name : {"headword", "sentence", "latin", "ruby"}) {
    CHECK(!present(deck + "/glyphs-" + name + ".txt"));
  }
}

void testEverythingElseIsKept() {
  const std::string deck = freshDeck();
  for (const char* name : {"cards.dat", "revlog.dat", "deck.dat", "meta.dat", "glyphs-latin.lst", "notes.txt"}) {
    touch(deck + "/" + name);
  }
  touch(deck + "/glyphs-latin.txt");
  CHECK(study::removeLegacyGlyphLists(deck.c_str()) == 1);
  for (const char* name : {"cards.dat", "revlog.dat", "deck.dat", "meta.dat", "glyphs-latin.lst", "notes.txt"}) {
    CHECK(present(deck + "/" + name));
  }
}

void testADeckWithoutThemIsLeftAlone() {
  const std::string deck = freshDeck();
  touch(deck + "/cards.dat");
  CHECK(study::removeLegacyGlyphLists(deck.c_str()) == 0);
  CHECK(present(deck + "/cards.dat"));
}

}  // namespace

int main() {
  testOldGlyphListsAreRemoved();
  testEverythingElseIsKept();
  testADeckWithoutThemIsLeftAlone();
  std::printf("%s %d checks, %d failed\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
  return failures == 0 ? 0 : 1;
}
