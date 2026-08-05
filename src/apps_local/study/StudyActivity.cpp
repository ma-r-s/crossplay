#include "StudyActivity.h"

#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "../../components/UITheme.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxMetrics.h"
#include "fontIds.h"

namespace {

// Where the converter and the font pipeline put their output.
constexpr const char* kDeckDir = "/study/mandarin";

// The four rating buttons. App-specific rather than a Toybox token: no other
// screen in this fork has a four-way grading bar, and a shared constant that
// one caller uses is a shared constant nobody can change safely.
constexpr int kRatingBarHeight = 132;

// Latin type. The built-in serif covers U+0100-U+017F and U+01C4-U+021F, so
// every tone-marked pinyin vowel in the deck draws -- checked against the
// deck's own 131-codepoint Latin set before relying on it.
constexpr int kReadingFontId = NOTOSERIF_18_FONT_ID;
constexpr int kMeaningFontId = NOTOSERIF_16_FONT_ID;
constexpr int kSmallFontId = NOTOSERIF_12_FONT_ID;

// A ByteSource over a HalFile. Every read seeks first: the deck index and the
// record it points at are in different places, so sequential reads are the
// exception rather than the rule.
class FileSource final : public study::ByteSource {
 public:
  explicit FileSource(HalFile& file) : file_(file), size_(static_cast<uint32_t>(file.size())) {}

  bool read(const uint32_t offset, void* dst, const uint32_t length) override {
    if (length == 0) return true;
    if (offset > size_ || offset + length > size_) return false;
    if (!file_.seekSet(offset)) return false;
    return file_.read(dst, length) == static_cast<int>(length);
  }
  uint32_t size() const override { return size_; }

 private:
  HalFile& file_;
  uint32_t size_;
};

// xorshift32. The face has to differ from the last one often enough to be worth
// having, and this is deterministic enough to reproduce a complaint about a
// specific card by seeding it the same way.
uint32_t nextRandom(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

void formatInterval(const int days, char* out, const size_t size) {
  if (days < 1) {
    std::snprintf(out, size, "<1d");
  } else if (days < 30) {
    std::snprintf(out, size, "%dd", days);
  } else if (days < 365) {
    std::snprintf(out, size, "%.1fmo", static_cast<double>(days) / 30.0);
  } else {
    std::snprintf(out, size, "%.1fy", static_cast<double>(days) / 365.0);
  }
}

}  // namespace

std::unique_ptr<Activity> StudyActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<StudyActivity>(renderer, mappedInput);
}

void StudyActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);

  ready_ = openDeck();
  if (ready_) {
    // Anki's day numbering, so a due date here and one on the phone agree.
    today_ = study::dayNumber(deck_.meta(), static_cast<int64_t>(time(nullptr)));
    fsrs_ = study::Fsrs(deck_.meta().hasParams() ? deck_.meta().params : nullptr, deck_.meta().desiredRetention);
    fsrs_.setMaximumInterval(deck_.meta().maximumInterval);
    shuffle_ = static_cast<uint32_t>(today_) * 2654435761u + 1u;
    buildQueue();
    ready_ = loadCurrent();
  }
  requestUpdate();
}

void StudyActivity::onExit() {
  fonts_.unload(renderer);
  deckSource_.reset();
  cardSource_.reset();
  metaSource_.reset();
  Activity::onExit();
}

bool StudyActivity::openDeck() {
  char path[96];
  std::snprintf(path, sizeof(path), "%s/meta.dat", kDeckDir);
  if (!Storage.openFileForRead("STUDY", path, metaFile_)) {
    LOG_ERR("STUDY", "No deck at %s -- run tools_local/study/anki_to_deck.py", kDeckDir);
    return false;
  }
  std::snprintf(path, sizeof(path), "%s/deck.dat", kDeckDir);
  if (!Storage.openFileForRead("STUDY", path, deckFile_)) return false;
  std::snprintf(path, sizeof(path), "%s/cards.dat", kDeckDir);
  if (!Storage.openFileForRead("STUDY", path, cardFile_)) return false;

  metaSource_ = makeUniqueNoThrow<FileSource>(metaFile_);
  deckSource_ = makeUniqueNoThrow<FileSource>(deckFile_);
  cardSource_ = makeUniqueNoThrow<FileSource>(cardFile_);
  if (!metaSource_ || !deckSource_ || !cardSource_) {
    LOG_ERR("STUDY", "OOM opening deck sources");
    return false;
  }

  if (!deck_.openMeta(*metaSource_)) {
    LOG_ERR("STUDY", "meta.dat is not a study deck");
    return false;
  }
  if (!deck_.openDeck(*deckSource_)) {
    LOG_ERR("STUDY", "deck.dat is not a study deck");
    return false;
  }
  LOG_INF("STUDY", "Deck '%s': %d cards", deck_.meta().name, deck_.noteCount());
  return true;
}

void StudyActivity::buildQueue() {
  queueCount_ = 0;
  queuePos_ = 0;
  dueCount_ = 0;
  newCount_ = 0;

  // Read cards.dat in chunks rather than record by record. 5001 records is
  // 5001 seeks otherwise, against a file that is only 156KB.
  constexpr int kChunkRecords = 64;
  uint8_t chunk[kChunkRecords * study::kCardRecordSize];
  const int total = deck_.noteCount();

  // Two passes: everything due today first, then new cards to top up. Reviews
  // before new is Anki's own order, and it is the one that matters -- new
  // cards first means the backlog never shrinks.
  for (int pass = 0; pass < 2; ++pass) {
    const int limit = (pass == 0) ? deck_.meta().reviewsPerDay : deck_.meta().newPerDay;
    int taken = 0;
    for (int base = 0; base < total && queueCount_ < kMaxQueue && taken < limit; base += kChunkRecords) {
      const int count = (total - base) < kChunkRecords ? (total - base) : kChunkRecords;
      const uint32_t bytes = static_cast<uint32_t>(count) * study::kCardRecordSize;
      if (!cardSource_->read(static_cast<uint32_t>(base) * study::kCardRecordSize, chunk, bytes)) break;

      for (int i = 0; i < count && queueCount_ < kMaxQueue && taken < limit; ++i) {
        const uint8_t* record = chunk + i * study::kCardRecordSize;
        const uint8_t state = record[28];
        int32_t dueDay;
        std::memcpy(&dueDay, record + 16, sizeof(dueDay));

        const bool isNew = state == 0;
        if (pass == 0 && (isNew || dueDay > today_)) continue;
        if (pass == 1 && !isNew) continue;

        queue_[queueCount_++] = base + i;
        ++taken;
        if (isNew) {
          ++newCount_;
        } else {
          ++dueCount_;
        }
      }
    }
  }
  LOG_INF("STUDY", "Queue: %d due, %d new (day %d)", dueCount_, newCount_, today_);
}

bool StudyActivity::loadCurrent() {
  if (queuePos_ >= queueCount_) return false;
  const int index = queue_[queuePos_];
  if (!deck_.loadNote(*deckSource_, index, note_)) return false;
  if (!deck_.loadCard(*cardSource_, index, card_)) return false;

  face_ = Face::Question;

  // The face changes per card. This is the feature: Mario learned to read in
  // one typeface and found the others hard afterwards, so the deck stops
  // letting him settle into any of them.
  const int family = static_cast<int>(nextRandom(shuffle_) % study::StudyFonts::kFamilyCount);
  fontsReady_ = fonts_.load(renderer, family);
  if (fontsReady_) {
    fonts_.prewarm(renderer, note_.field(study::Field::Headword), note_.field(study::Field::Sentence));
  }

  const study::Memory memory = card_.memory();
  const int elapsed = card_.lastReviewDay < 0 ? 0 : today_ - card_.lastReviewDay;
  fsrs_.previewIntervals(memory, elapsed, intervals_);
  return true;
}

void StudyActivity::advance() {
  ++queuePos_;
  if (!loadCurrent()) {
    ready_ = queuePos_ < queueCount_;
  }
  requestUpdate();
}

void StudyActivity::grade(const study::Rating rating) {
  const study::Memory before = card_.memory();
  const int elapsed = card_.lastReviewDay < 0 ? 0 : today_ - card_.lastReviewDay;
  const study::Memory after = fsrs_.review(before, rating, elapsed);

  card_.setMemory(after);
  card_.lastReviewDay = today_;
  card_.dueDay = today_ + fsrs_.intervalDays(after);
  card_.state = 2;
  if (card_.reps < 0xFFFF) ++card_.reps;
  if (rating == study::Rating::Again && card_.lapses < 0xFFFF) ++card_.lapses;

  // Persisting the graded card and appending to revlog.dat is the next slice;
  // cards.dat is opened read-only here on purpose rather than half-written.
  LOG_INF("STUDY", "Graded %d: S %.2f -> %.2f, due day %d", static_cast<int>(rating),
          static_cast<double>(before.stability), static_cast<double>(after.stability), card_.dueDay);
  advance();
}

void StudyActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
    return;
  }
  if (!ready_) return;

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;

  if (face_ == Face::Question) {
    // Anywhere reveals. A reveal target smaller than the card is something to
    // aim at, for the only action the screen offers.
    face_ = Face::Answer;
    requestUpdate();
    return;
  }

  if (tapY >= renderer.getScreenHeight() - kRatingBarHeight) {
    // Slot from the same division that drew the buttons, so the hit region
    // cannot drift from the pixels. See docs/building-apps.md.
    const int width = renderer.getScreenWidth();
    const int slot = width > 0 ? tapX * 4 / width : 0;
    const int clamped = slot < 0 ? 0 : (slot > 3 ? 3 : slot);
    grade(static_cast<study::Rating>(clamped + 1));
  }
}

int StudyActivity::drawCentered(const int fontId, const int y, const int width, const char* text) const {
  if (text == nullptr || *text == '\0') return y;
  const int textWidth = renderer.getTextWidth(fontId, text);
  renderer.drawText(fontId, (width - textWidth) / 2, y, text, true);
  return y + renderer.getTextHeight(fontId);
}

void StudyActivity::drawCard(const Rect& body) {
  const int width = renderer.getScreenWidth();
  int y = body.y + 24;

  // The headword, in whichever of the five faces this card drew.
  if (fontsReady_) {
    y = drawCentered(fonts_.headwordFontId(), y, width, note_.field(study::Field::Headword));
  } else {
    y = drawCentered(kReadingFontId, y, width, note_.field(study::Field::Headword));
  }

  if (face_ == Face::Answer) {
    y += 12;
    y = drawCentered(kReadingFontId, y, width, note_.field(study::Field::Reading));
    y += 6;
    y = drawCentered(kMeaningFontId, y, width, note_.field(study::Field::Meaning));
    y = drawCentered(kSmallFontId, y, width, note_.field(study::Field::PartOfSpeech));
  }

  // The rule, exactly as the Anki template has it: the word above, the
  // sentence it lives in below.
  y += 20;
  toybox::rule(renderer, y, toybox::kHairline);
  y += 20;

  if (!note_.empty(study::Field::Sentence)) {
    if (fontsReady_) {
      y = drawCentered(fonts_.sentenceFontId(), y, width, note_.field(study::Field::Sentence));
    } else {
      y = drawCentered(kMeaningFontId, y, width, note_.field(study::Field::Sentence));
    }
    if (face_ == Face::Answer) {
      y += 8;
      y = drawCentered(kMeaningFontId, y, width, note_.field(study::Field::SentenceReading));
      y += 4;
      drawCentered(kMeaningFontId, y, width, note_.field(study::Field::SentenceMeaning));
    }
  }
}

void StudyActivity::drawRatingBar(const int y, const int height) {
  const int width = renderer.getScreenWidth();
  const int slot = width / 4;
  static constexpr const char* kLabels[4] = {"AGAIN", "HARD", "GOOD", "EASY"};

  renderer.fillRect(0, y, width, toybox::kRule, true);
  for (int i = 0; i < 4; ++i) {
    const int x = i * slot;
    if (i > 0) renderer.fillRect(x, y, toybox::kHairline, height, true);

    char interval[16];
    formatInterval(intervals_[i], interval, sizeof(interval));
    const int intervalWidth = renderer.getTextWidth(kSmallFontId, interval);
    renderer.drawText(kSmallFontId, x + (slot - intervalWidth) / 2, y + 18, interval, true);

    const int labelWidth = renderer.getTextWidth(toybox::kUiFontId, kLabels[i]);
    toybox::drawCapsCentered(renderer, toybox::kUiFontId, x + (slot - labelWidth) / 2, y + 56, height - 64, kLabels[i],
                             true);
  }
}

void StudyActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();

  char title[64];
  if (ready_) {
    std::snprintf(title, sizeof(title), "%d / %d", queuePos_ + 1, queueCount_);
  } else {
    std::snprintf(title, sizeof(title), "STUDY");
  }
  GUI.drawHeader(renderer, Rect{0, 0, width, toybox::kHeaderHeight}, title);

  if (!ready_) {
    const int bodyY = toybox::kHeaderHeight + 40;
    const char* message = queueCount_ > 0 ? "Done for today." : "No deck on the card.";
    UITheme::drawCenteredWrappedText(renderer, Rect{0, bodyY, width, height - bodyY - 80}, kMeaningFontId, message, 4);
    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int barTop = height - kRatingBarHeight;
  drawCard(Rect{0, toybox::kHeaderHeight, width, barTop - toybox::kHeaderHeight});
  if (face_ == Face::Answer) {
    drawRatingBar(barTop, kRatingBarHeight);
  }

  renderer.displayBuffer();
}
