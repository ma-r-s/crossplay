// Parses a real converted deck with the same StudyDeck the device runs.
//
// The deck is produced by tools_local/study/anki_to_deck.py from Mario's own
// collection, so this is an end-to-end check of the format: what the converter
// writes is what the firmware reads. A format bug that a synthetic fixture
// would hide -- an off-by-two in the emphasis span, a miscounted index base --
// shows up here as a wrong headword.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../../src/apps_local/study/StudyDeck.h"

namespace {

int failures = 0;
int checks = 0;

void check(const bool ok, const char* what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("  FAIL: %s\n", what);
  }
}

// A ByteSource over a plain file -- the host's stand-in for HalStorage.
class FileSource final : public study::WritableByteSource {
 public:
  explicit FileSource(const std::string& path) {
    file_ = std::fopen(path.c_str(), "rb+");
    if (file_) {
      std::fseek(file_, 0, SEEK_END);
      size_ = static_cast<uint32_t>(std::ftell(file_));
      std::fseek(file_, 0, SEEK_SET);
    }
  }
  ~FileSource() override {
    if (file_) std::fclose(file_);
  }
  bool ok() const { return file_ != nullptr; }

  bool read(const uint32_t offset, void* dst, const uint32_t length) override {
    if (!file_ || offset + length > size_) return false;
    if (std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) return false;
    return std::fread(dst, 1, length, file_) == length;
  }
  bool write(const uint32_t offset, const void* src, const uint32_t length) override {
    if (!file_ || offset + length > size_) return false;
    if (std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) return false;
    return std::fwrite(src, 1, length, file_) == length;
  }
  bool flush() override { return file_ && std::fflush(file_) == 0; }
  uint32_t size() const override { return size_; }

 private:
  std::FILE* file_ = nullptr;
  uint32_t size_ = 0;
};

void run(const std::string& dir) {
  FileSource deckFile(dir + "/deck.dat");
  FileSource metaFile(dir + "/meta.dat");
  FileSource cardFile(dir + "/cards.dat");
  // A missing deck is a FAILURE, not a skip. This used to print
  //   SKIP: no converted deck at /tmp/studytest/mandarin
  // indented by two spaces, so check.sh's "^SKIP" surfacing could not see it
  // either, and the binary went on to print PASS (0 checks, 0 failures).
  // Nothing in check.sh or CI has ever produced a deck here, so every run of
  // this file for its whole life took this branch. run.sh now builds one with
  // make_fixture.py; if that failed, this must say so rather than pass.
  if (!deckFile.ok() || !metaFile.ok() || !cardFile.ok()) {
    check(false, "the converted deck exists");
    std::printf("        no deck.dat/meta.dat/cards.dat under %s -- host-tests/study/run.sh\n", dir.c_str());
    std::printf("        builds one with make_fixture.py; run the suite through it\n");
    return;
  }

  study::StudyDeck deck;
  check(deck.openMeta(metaFile), "meta.dat parses");
  check(deck.openDeck(deckFile), "deck.dat parses");
  std::printf("  deck '%s': %d notes\n", deck.meta().name, deck.noteCount());

  check(deck.noteCount() > 0, "deck is not empty");
  check(deck.meta().hasParams(), "deck carries FSRS parameters");
  check(deck.meta().desiredRetention > 0.5f, "retention target is sane");
  check(deck.meta().collectionCreated > 1000000000LL, "collection epoch is sane");

  // Every note must parse. This is the assertion that matters: it walks the
  // whole index, so a single bad length anywhere in 5001 records fails here
  // rather than on the device three weeks from now.
  study::Note note;
  int parsed = 0;
  int withSentence = 0;
  int withEmphasis = 0;
  int emphasisOutOfRange = 0;
  for (int i = 0; i < deck.noteCount(); ++i) {
    if (!deck.loadNote(deckFile, i, note)) continue;
    ++parsed;
    if (note.length(study::Field::Headword) == 0) {
      if (failures < 5) std::printf("  FAIL: note %d has an empty headword\n", i);
      ++failures;
    }
    if (!note.empty(study::Field::Sentence)) {
      ++withSentence;
      if (note.emphasisLength() > 0) {
        ++withEmphasis;
        // The span is in codepoints, so count them rather than bytes.
        int codepoints = 0;
        for (const char* p = note.field(study::Field::Sentence); *p; ++p) {
          if ((static_cast<unsigned char>(*p) & 0xC0) != 0x80) ++codepoints;
        }
        if (note.emphasisOffset() + note.emphasisLength() > codepoints) ++emphasisOutOfRange;
      }
    }
  }
  checks += 1;
  std::printf("  parsed %d/%d notes, %d with sentences, %d emphasised\n", parsed, deck.noteCount(), withSentence,
              withEmphasis);
  check(parsed == deck.noteCount(), "every note parses");
  check(emphasisOutOfRange == 0, "every emphasis span lies inside its sentence");

  // Field NUL-termination: the fields are handed straight to C APIs, so a
  // missing terminator is a buffer overrun rather than a display glitch.
  check(deck.loadNote(deckFile, 0, note), "first note loads");
  for (int f = 0; f < study::kFieldCount; ++f) {
    const study::Field field = static_cast<study::Field>(f);
    check(std::strlen(note.field(field)) == note.length(field), "field length matches its terminator");
  }

  // Card state must line up one-to-one with notes, and round-trip.
  study::CardState card;
  check(deck.loadCard(cardFile, 0, card), "first card loads");
  check(card.ankiCardId > 0, "card carries its Anki id");
  check(!deck.loadCard(cardFile, deck.noteCount(), card), "reading past the last card fails");

  study::CardState probe;
  check(deck.loadCard(cardFile, 3, probe), "card 3 loads");
  const study::CardState original = probe;
  probe.stability = 12.5f;
  probe.difficulty = 6.25f;
  probe.reps = 7;
  probe.state = 2;
  check(deck.storeCard(cardFile, 3, probe), "card 3 stores");
  study::CardState reread;
  check(deck.loadCard(cardFile, 3, reread), "card 3 re-reads");
  check(reread.stability == 12.5f && reread.difficulty == 6.25f && reread.reps == 7 && reread.state == 2,
        "card state round-trips through the file");
  check(reread.ankiCardId == original.ankiCardId, "round-trip preserves the Anki id");
  check(deck.storeCard(cardFile, 3, original), "card 3 restored");

  // Bad input must be refused, not misparsed.
  check(!deck.loadNote(deckFile, -1, note), "negative index is refused");
  check(!deck.loadNote(deckFile, deck.noteCount(), note), "past-the-end index is refused");
  study::StudyDeck empty;
  check(!empty.openDeck(metaFile), "meta.dat is not accepted as deck.dat");
  check(!empty.openMeta(deckFile), "deck.dat is not accepted as meta.dat");

  // Day numbering must agree with Anki's, which is what keeps due dates
  // consistent between the device and the phone.
  const int today = study::dayNumber(deck.meta(), deck.meta().collectionCreated + 86400 * 10 + 3600);
  check(today == 10, "day 10 plus an hour is still day 10");
  check(study::dayNumber(deck.meta(), deck.meta().collectionCreated) == 0, "the creation instant is day 0");
  check(study::dayNumber(deck.meta(), deck.meta().collectionCreated - 5) == 0, "before day zero clamps to 0");
}

}  // namespace

int main(const int argc, char** argv) {
  std::printf("StudyDeck\n");
  run(argc > 1 ? argv[1] : "/tmp/studytest/mandarin");
  // "failed", not "failures": check.sh counts sub-suites by grepping for
  // "checks, 0 failed", so the other spelling made every study binary
  // invisible to it and the suite reported "ok (0 sub-suite(s))".
  std::printf("%s %d checks, %d failed\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
  return failures == 0 ? 0 : 1;
}
