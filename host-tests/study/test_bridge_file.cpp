// The pairing reaches the card on a reader that has never held a deck.
//
// Report #631 (2026-10-01): "every time i type in the code, my x4pro says that
// theres issue connecting". The bridge log showed the code claimed and the
// device token handed over, then the reader abandoning it seconds later, twice.
// The reader saved the token to /study/.bridge, and nothing had created /study:
// a first-time user with no decks fails at the confirm step, every time. The
// simulator creates missing folders on write, so it never failed there.
//
// stubs/HalStorage.h refuses a write into a missing folder, as SdFat does.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "HalStorage.h"
#include "StudyBridgeFile.h"

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

std::string freshCard() {
  char pattern[] = "/tmp/study-bridge-XXXXXX";
  const char* made = mkdtemp(pattern);
  if (made == nullptr) {
    std::perror("mkdtemp");
    std::exit(2);
  }
  return made;
}

std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool isDir(const std::string& path) {
  struct stat st;
  return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void testAReaderWithNoDecksCanStillBePaired() {
  Storage.root = freshCard();
  CHECK(!isDir(Storage.root + "/study"));

  CHECK(study::writeBridgeStateFile("{\"token\":\"t1\"}"));
  CHECK(isDir(Storage.root + "/study"));
  CHECK(slurp(Storage.root + "/study/.bridge") == "{\"token\":\"t1\"}");
  // The temp file is renamed into place, not left beside it.
  CHECK(access((Storage.root + "/study/.bridge.part").c_str(), F_OK) != 0);
}

void testAnExistingFolderAndStateAreReplacedNotRefused() {
  // Storage.mkdir is O_CREAT|O_EXCL and refuses a folder that is already
  // there; the folder a deck made must not turn every later save into a
  // failure.
  Storage.root = freshCard();
  CHECK(::mkdir((Storage.root + "/study").c_str(), 0777) == 0);
  CHECK(study::writeBridgeStateFile("{\"token\":\"old\"}"));
  CHECK(study::writeBridgeStateFile("{\"token\":\"new\"}"));
  CHECK(slurp(Storage.root + "/study/.bridge") == "{\"token\":\"new\"}");
}

}  // namespace

int main() {
  testAReaderWithNoDecksCanStillBePaired();
  testAnExistingFolderAndStateAreReplacedNotRefused();
  std::printf("%s %d checks, %d failed\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
  return failures == 0 ? 0 : 1;
}
