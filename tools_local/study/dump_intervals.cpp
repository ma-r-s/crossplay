// Prints the four button labels the device would show, for every card in a
// converted deck. Built and driven by compare_with_anki.py, which asks Anki the
// same question through Anki's own scheduler and diffs the two.
//
// This is the only way to answer "are the times on the buttons the same as
// Anki's" honestly: not by reading both implementations and reasoning, but by
// running both and comparing every card.
//
// Deliberately shares StudyScheduler / StudyFsrs / StudyDeck with the firmware
// rather than reimplementing anything -- a comparison against a second
// implementation of my own would prove nothing.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "../../src/apps_local/study/StudyDeck.h"
#include "../../src/apps_local/study/StudyScheduler.h"

namespace {

class FileSource final : public study::ByteSource {
 public:
  explicit FileSource(const std::string& path) {
    file_ = std::fopen(path.c_str(), "rb");
    if (file_ != nullptr) {
      std::fseek(file_, 0, SEEK_END);
      size_ = static_cast<uint32_t>(std::ftell(file_));
      std::fseek(file_, 0, SEEK_SET);
    }
  }
  ~FileSource() override {
    if (file_ != nullptr) std::fclose(file_);
  }
  bool ok() const { return file_ != nullptr; }
  bool read(const uint32_t offset, void* dst, const uint32_t length) override {
    if (file_ == nullptr || offset + length > size_) return false;
    if (std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) return false;
    return std::fread(dst, 1, length, file_) == length;
  }
  uint32_t size() const override { return size_; }

 private:
  std::FILE* file_ = nullptr;
  uint32_t size_ = 0;
};

}  // namespace

int main(const int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: dump_intervals <deckdir> <todayDay> <nowMinute>\n");
    return 2;
  }
  const std::string dir = argv[1];
  const int today = std::atoi(argv[2]);
  const int nowMinute = std::atoi(argv[3]);

  FileSource meta(dir + "/meta.dat");
  FileSource deckFile(dir + "/deck.dat");
  FileSource cards(dir + "/cards.dat");
  if (!meta.ok() || !deckFile.ok() || !cards.ok()) {
    std::fprintf(stderr, "cannot open deck at %s\n", dir.c_str());
    return 1;
  }

  study::StudyDeck deck;
  if (!deck.openMeta(meta) || !deck.openDeck(deckFile)) {
    std::fprintf(stderr, "not a study deck\n");
    return 1;
  }

  study::Fsrs fsrs(deck.meta().hasParams() ? deck.meta().params : nullptr, deck.meta().desiredRetention);
  fsrs.setMaximumInterval(deck.meta().maximumInterval);

  study::Steps steps;
  steps.learnCount = deck.meta().learnStepCount;
  steps.relearnCount = deck.meta().relearnStepCount;
  for (int i = 0; i < study::kMaxLearningSteps; ++i) {
    steps.learn[i] = deck.meta().learnSteps[i];
    steps.relearn[i] = deck.meta().relearnSteps[i];
  }
  if (steps.learnCount == 0 && steps.relearnCount == 0) steps = study::Steps::defaults();
  const study::Scheduler scheduler(fsrs, steps);

  // ankiCardId<TAB>again<TAB>hard<TAB>good<TAB>easy
  for (int i = 0; i < deck.noteCount(); ++i) {
    study::CardState card;
    if (!deck.loadCard(cards, i, card)) continue;
    study::Outcome out[4];
    scheduler.preview(card, today, nowMinute, out);

    std::printf("%lld", static_cast<long long>(card.ankiCardId));
    for (const auto& o : out) {
      char label[16];
      study::formatDelay(o.delayMinutes, o.intervalDays, label, sizeof(label));
      std::printf("\t%s", label);
    }
    std::printf("\n");
  }
  return 0;
}
