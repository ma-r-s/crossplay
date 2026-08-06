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
#include "../ui/ToyboxTheme.h"
#include "fontIds.h"

namespace {

// Where anki_to_deck.py and make_fonts.py put their output.
constexpr const char* kDeckDir = "/study/mandarin";

// The footer. One height for both faces, because it is drawn on the question
// side too: docs/design-language.md asks a layout to reserve the space a
// control will arrive into, and a card that reflows when you reveal it is the
// same defect as a board that reflows mid-game.
constexpr int kFooterHeight = 128;

// Shown in the header when a card carries a photograph, and the word is the
// button: tapping anywhere in the header's right-hand end opens it.
constexpr const char* kPhotoLabel = "PHOTO";
constexpr const char* kBackLabel = "BACK";

constexpr int kPillHeight = 34;
constexpr int kPillPadding = 18;
// The battery indicator owns the right-hand end of the header and the card
// count owns the left, so the label sits between them. Both of those are drawn
// by the theme, not by this app, which is why the collision only showed up on
// screen.
constexpr int kPhotoTapHalfWidth = 90;

// Latin type. The built-in serif covers U+0100-U+017F and U+01C4-U+021F, so
// every tone-marked pinyin vowel in the deck draws -- checked against the
// deck's own 131-codepoint Latin set before relying on it.
constexpr int kReadingFontId = NOTOSERIF_18_FONT_ID;
constexpr int kMeaningFontId = NOTOSERIF_16_FONT_ID;
constexpr int kSmallFontId = NOTOSERIF_12_FONT_ID;

// Longest line the wrapper assembles. The widest field in the deck is 178
// bytes; 256 leaves room, and two of these are live at once.
constexpr int kLineBytes = 256;

int utf8Length(const char* p) {
  const unsigned char c = static_cast<unsigned char>(*p);
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;  // a stray continuation byte: step one, never zero, or we spin
}

uint32_t nextCodepoint(const char*& p) {
  const unsigned char c = static_cast<unsigned char>(*p);
  const int length = utf8Length(p);
  uint32_t value = c;
  if (length == 2) {
    value = c & 0x1F;
  } else if (length == 3) {
    value = c & 0x0F;
  } else if (length == 4) {
    value = c & 0x07;
  }
  for (int i = 1; i < length; ++i) {
    value = (value << 6) | (static_cast<unsigned char>(p[i]) & 0x3F);
  }
  p += length;
  return value;
}

// May a line break happen either side of this character? True for spaces and
// for CJK, which is written without spaces and breaks almost anywhere.
bool isBreakable(const uint32_t codepoint) {
  if (codepoint == ' ') return true;
  return (codepoint >= 0x2E80 && codepoint <= 0x9FFF) || (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||
         (codepoint >= 0xFF00 && codepoint <= 0xFFEF);
}

// A ByteSource over a HalFile. Every read seeks first: the deck index and the
// record it points at are in different places, so sequential reads are the
// exception rather than the rule.
class FileSource final : public study::WritableByteSource {
 public:
  explicit FileSource(HalFile& file) : file_(file), size_(static_cast<uint32_t>(file.size())) {}

  bool read(const uint32_t offset, void* dst, const uint32_t length) override {
    if (length == 0) return true;
    if (offset > size_ || offset + length > size_) return false;
    if (!file_.seekSet(offset)) return false;
    return file_.read(dst, length) == static_cast<int>(length);
  }
  bool write(const uint32_t offset, const void* src, const uint32_t length) override {
    if (length == 0) return true;
    if (offset + length > size_) return false;  // cards.dat never grows
    if (!file_.seekSet(offset)) return false;
    return file_.write(static_cast<const uint8_t*>(src), length) == length;
  }
  bool flush() override {
    file_.flush();
    return true;
  }
  uint32_t size() const override { return size_; }

 private:
  HalFile& file_;
  uint32_t size_;
};

// xorshift32. Deterministic enough to reproduce a complaint about a specific
// card by seeding it the same way.
uint32_t nextRandom(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

}  // namespace

std::unique_ptr<Activity> StudyActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<StudyActivity>(renderer, mappedInput);
}

void StudyActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);

  if (openDeck()) {
    today_ = study::dayNumber(deck_.meta(), static_cast<int64_t>(time(nullptr)));
    startMinute_ = nowMinute();
    fsrs_ = study::Fsrs(deck_.meta().hasParams() ? deck_.meta().params : nullptr, deck_.meta().desiredRetention);
    fsrs_.setMaximumInterval(deck_.meta().maximumInterval);

    study::Steps steps;
    steps.learnCount = deck_.meta().learnStepCount;
    steps.relearnCount = deck_.meta().relearnStepCount;
    for (int i = 0; i < study::kMaxLearningSteps; ++i) {
      steps.learn[i] = deck_.meta().learnSteps[i];
      steps.relearn[i] = deck_.meta().relearnSteps[i];
    }
    if (steps.learnCount == 0 && steps.relearnCount == 0) steps = study::Steps::defaults();
    scheduler_ = study::Scheduler(fsrs_, steps);

    shuffle_ = static_cast<uint32_t>(today_) * 2654435761u + 1u;
    buildQueue();
    refreshStats();
    view_ = View::Deck;
  }
  requestUpdate();
}

void StudyActivity::onExit() {
  if (cardSource_) cardSource_->flush();
  revlogFile_.flush();
  fonts_.unload(renderer);
  deckSource_.reset();
  cardSource_.reset();
  metaSource_.reset();
  Activity::onExit();
}

int StudyActivity::nowMinute() const {
  const int64_t now = static_cast<int64_t>(time(nullptr));
  const int64_t dayStart = deck_.meta().collectionCreated + static_cast<int64_t>(today_) * 86400;
  const int64_t seconds = now - dayStart;
  if (seconds < 0) return 0;
  const int minutes = static_cast<int>(seconds / 60);
  return minutes > 1439 ? 1439 : minutes;
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

  // O_RDWR without O_TRUNC: openFileForWrite truncates, which on cards.dat
  // would erase every card's scheduling state the moment the app opened.
  std::snprintf(path, sizeof(path), "%s/cards.dat", kDeckDir);
  cardFile_ = Storage.open(path, O_RDWR);
  if (!cardFile_.isOpen()) {
    LOG_ERR("STUDY", "Cannot open cards.dat for update");
    return false;
  }
  std::snprintf(path, sizeof(path), "%s/revlog.dat", kDeckDir);
  revlogFile_ = Storage.open(path, O_RDWR | O_CREAT);
  if (!revlogFile_.isOpen()) LOG_ERR("STUDY", "Cannot open revlog.dat -- reviews will not be logged");
  // A second handle over the same file, for reading history back. Separate from
  // the append handle so a stats read cannot disturb the write position.
  if (Storage.openFileForRead("STUDY", path, revlogReadFile_)) {
    revlogSource_ = makeUniqueNoThrow<FileSource>(revlogReadFile_);
  }

  std::snprintf(path, sizeof(path), "%s/images.dat", kDeckDir);
  if (Storage.openFileForRead("STUDY", path, imageFile_)) {
    imageSource_ = makeUniqueNoThrow<FileSource>(imageFile_);
    if (imageSource_ && images_.open(*imageSource_)) {
      LOG_INF("STUDY", "Photographs: %d entries", images_.count());
    }
  }

  metaSource_ = makeUniqueNoThrow<FileSource>(metaFile_);
  deckSource_ = makeUniqueNoThrow<FileSource>(deckFile_);
  cardSource_ = makeUniqueNoThrow<FileSource>(cardFile_);
  if (!metaSource_ || !deckSource_ || !cardSource_) {
    LOG_ERR("STUDY", "OOM opening deck sources");
    return false;
  }

  if (!deck_.openMeta(*metaSource_) || !deck_.openDeck(*deckSource_)) {
    LOG_ERR("STUDY", "%s is not a study deck (or is a different format version)", kDeckDir);
    return false;
  }
  LOG_INF("STUDY", "Deck '%s': %d cards", deck_.meta().name, deck_.noteCount());
  return true;
}

void StudyActivity::buildQueue() {
  queueCount_ = 0;
  queuePos_ = 0;
  dueTotal_ = 0;
  newTotal_ = 0;
  for (int& day : forecast_) day = 0;

  // Read cards.dat in chunks rather than record by record: 5001 records is
  // 5001 seeks otherwise, against a file that is only 156KB.
  constexpr int kChunkRecords = 64;
  uint8_t chunk[kChunkRecords * study::kCardRecordSize];
  const int total = deck_.noteCount();
  const int minute = nowMinute();

  // Counting and queueing are separate passes over the same read, because they
  // stop at different points. The queue fills to the day's limit and to what
  // the session can hold; the counts and the forecast must see *every* record
  // or the screen lies -- "256 DUE" was the queue cap, not the backlog, and
  // the forecast panel was drawn from whatever the first 256 records happened
  // to contain.
  int dueSeen = 0;
  int newSeen = 0;
  for (int base = 0; base < total; base += kChunkRecords) {
    const int count = (total - base) < kChunkRecords ? (total - base) : kChunkRecords;
    const uint32_t bytes = static_cast<uint32_t>(count) * study::kCardRecordSize;
    if (!cardSource_->read(static_cast<uint32_t>(base) * study::kCardRecordSize, chunk, bytes)) break;

    for (int i = 0; i < count; ++i) {
      const uint8_t* record = chunk + i * study::kCardRecordSize;
      study::CardState probe;
      std::memcpy(&probe.dueDay, record + 16, sizeof(probe.dueDay));
      probe.state = record[28];
      probe.dueMinute = static_cast<uint16_t>(record[30] | (record[31] << 8));

      if (probe.state == static_cast<uint8_t>(study::State::Suspended)) continue;
      if (probe.state == static_cast<uint8_t>(study::State::New)) {
        ++newSeen;
        continue;
      }
      // The forecast panel's data, from the pass that is already reading every
      // record. Anything overdue piles onto today, which is what the user is
      // actually looking at when they open the app.
      const int offset = probe.dueDay - today_;
      if (offset < studyui::kForecastDays) forecast_[offset < 0 ? 0 : offset] += 1;
      if (study::Scheduler::isDue(probe, today_, minute)) ++dueSeen;
    }
  }
  dueTotal_ = dueSeen;
  newTotal_ = newSeen;

  // Now fill the queue itself. Reviews before new is Anki's order and the one
  // that matters: new cards first means the backlog never shrinks.
  for (int pass = 0; pass < 2; ++pass) {
    const int limit = (pass == 0) ? deck_.meta().reviewsPerDay : deck_.meta().newPerDay;
    int taken = 0;
    for (int base = 0; base < total && queueCount_ < kMaxQueue && taken < limit; base += kChunkRecords) {
      const int count = (total - base) < kChunkRecords ? (total - base) : kChunkRecords;
      const uint32_t bytes = static_cast<uint32_t>(count) * study::kCardRecordSize;
      if (!cardSource_->read(static_cast<uint32_t>(base) * study::kCardRecordSize, chunk, bytes)) break;

      for (int i = 0; i < count && queueCount_ < kMaxQueue && taken < limit; ++i) {
        const uint8_t* record = chunk + i * study::kCardRecordSize;
        study::CardState probe;
        std::memcpy(&probe.dueDay, record + 16, sizeof(probe.dueDay));
        probe.state = record[28];
        probe.dueMinute = static_cast<uint16_t>(record[30] | (record[31] << 8));

        if (probe.state == static_cast<uint8_t>(study::State::Suspended)) continue;
        const bool isNew = probe.state == static_cast<uint8_t>(study::State::New);
        if (pass == 0 && (isNew || !study::Scheduler::isDue(probe, today_, minute))) continue;
        if (pass == 1 && !isNew) continue;

        queue_[queueCount_++] = base + i;
        ++taken;
      }
    }
  }
  LOG_INF("STUDY", "Queue: %d of %d due, %d new (day %d, minute %d)", queueCount_, dueTotal_, newTotal_, today_,
          minute);
}

bool StudyActivity::takeNext() {
  const int minute = nowMinute();

  // A card whose step has elapsed comes first: that is the whole point of a
  // one-minute step, and letting the main queue run first would turn every
  // learning step into "at the end of the session".
  int best = -1;
  for (int i = 0; i < learningCount_; ++i) {
    study::CardState probe;
    probe.state = static_cast<uint8_t>(study::State::Learning);
    probe.dueDay = learning_[i].dueDay;
    probe.dueMinute = static_cast<uint16_t>(learning_[i].dueMinute);
    if (study::Scheduler::isDue(probe, today_, minute)) {
      best = i;
      break;
    }
  }

  // Nothing ripe: take from the main queue.
  if (best < 0 && queuePos_ < queueCount_) {
    currentIndex_ = queue_[queuePos_++];
    took_ = Took::Queue;
    return loadCurrent();
  }

  // Main queue empty and something is still in a step: show the one closest to
  // due rather than making the user wait out a ten-minute timer holding a
  // device that cannot notify them.
  if (best < 0 && learningCount_ > 0) {
    best = 0;
    for (int i = 1; i < learningCount_; ++i) {
      const bool earlier =
          learning_[i].dueDay < learning_[best].dueDay ||
          (learning_[i].dueDay == learning_[best].dueDay && learning_[i].dueMinute < learning_[best].dueMinute);
      if (earlier) best = i;
    }
  }
  if (best < 0) {
    took_ = Took::Nothing;
    return false;
  }

  currentIndex_ = learning_[best].index;
  learning_[best] = learning_[--learningCount_];
  took_ = Took::Learning;
  return loadCurrent();
}

bool StudyActivity::loadCurrent() {
  if (currentIndex_ < 0) return false;
  if (!deck_.loadNote(*deckSource_, currentIndex_, note_)) return false;
  if (!deck_.loadCard(*cardSource_, currentIndex_, card_)) return false;

  face_ = Face::Question;

  // The face changes per card. This is the feature: Mario learned to read in
  // one typeface and found the others hard afterwards, so the deck stops
  // letting him settle into any of them.
  const int family = static_cast<int>(nextRandom(shuffle_) % study::StudyFonts::kFamilyCount);
  fontsReady_ = fonts_.load(renderer, family);
  if (fontsReady_) {
    fonts_.prewarm(renderer, note_.field(study::Field::Headword), note_.field(study::Field::Sentence));
  }

  image_ = study::ImageRef{};
  if (imageSource_ && images_.ready()) {
    image_ = images_.at(*imageSource_, currentIndex_);
  }

  scheduler_.preview(card_, today_, nowMinute(), preview_);
  return true;
}

bool StudyActivity::persist(const int index, const study::CardState& card, const study::Rating rating,
                            const study::Outcome& outcome, uint32_t& revlogOffset, bool& written) {
  revlogOffset = 0;
  written = false;
  bool ok = deck_.storeCard(*cardSource_, index, outcome.card);
  if (!ok) LOG_ERR("STUDY", "Failed to store card %d", index);

  // revlog.dat is append-only and never rewritten: it is what deck_to_anki.py
  // replays back into the collection, and what FSRS optimisation would retrain
  // from. See docs/study-deck-format.md.
  if (revlogFile_.isOpen()) {
    uint8_t record[32] = {};
    const int64_t nowMs = static_cast<int64_t>(time(nullptr)) * 1000;
    const int16_t elapsed = static_cast<int16_t>(card.lastReviewDay < 0 ? 0 : today_ - card.lastReviewDay);
    const int32_t interval = outcome.intervalDays > 0 ? outcome.intervalDays : -outcome.delayMinutes * 60;
    std::memcpy(record, &card.ankiCardId, 8);
    std::memcpy(record + 8, &nowMs, 8);
    record[16] = static_cast<uint8_t>(rating);
    record[17] = card.state;  // the state the card was in *before* the review
    std::memcpy(record + 18, &elapsed, 2);
    std::memcpy(record + 20, &interval, 4);
    // tookMs is left zero: nothing here times the user, and inventing a
    // plausible number would poison the data Anki reimports.
    const uint32_t at = static_cast<uint32_t>(revlogFile_.size());
    if (!revlogFile_.seekSet(at) || revlogFile_.write(record, sizeof(record)) != sizeof(record)) {
      LOG_ERR("STUDY", "Failed to append to revlog.dat");
      ok = false;
    } else {
      revlogOffset = at;
      written = true;
    }
  }
  return ok;
}

void StudyActivity::flushWrites() {
  // Every review rather than at exit. The device can lose power or sleep
  // mid-session, and a review the user gave is not ours to lose -- nor is one
  // they took back, which is why undo flushes on the same path.
  cardSource_->flush();
  revlogFile_.flush();
}

void StudyActivity::grade(const study::Rating rating) {
  const study::Outcome outcome = scheduler_.answer(card_, rating, today_, nowMinute());

  // Everything undo needs, taken before anything moves. card_ is the state as
  // it was *before* the answer, which is exactly what putting it back means.
  undo_ = Undo{};
  undo_.index = currentIndex_;
  undo_.before = card_;
  undo_.reviewed = reviewedThisSession_;
  undo_.again = againThisSession_;

  if (!persist(currentIndex_, card_, rating, outcome, undo_.revlogOffset, undo_.revlogWritten)) writeFailed_ = true;
  ++reviewedThisSession_;
  if (rating == study::Rating::Again) ++againThisSession_;

  LOG_INF("STUDY", "Graded %d: S %.2f -> %.2f, %s", static_cast<int>(rating), static_cast<double>(card_.stability),
          static_cast<double>(outcome.card.stability), outcome.delayMinutes > 0 ? "back this session" : "scheduled");

  // Still inside a step list: it comes back before the session ends.
  if (outcome.delayMinutes > 0 && learningCount_ < kMaxLearning) {
    learning_[learningCount_++] = {currentIndex_, outcome.card.dueDay, outcome.card.dueMinute};
    undo_.enteredLearning = true;
  }

  flushWrites();

  const bool more = takeNext();
  undo_.took = took_;
  // Only offer to take it back while the card view is up. Once the session is
  // over the deck screen is showing, and giving that screen a control it does
  // not otherwise need is a bigger change than the one card it would rescue.
  undo_.valid = more;
  if (!more) {
    refreshStats();
    view_ = View::Deck;
  }
  requestUpdate();
}

void StudyActivity::undo() {
  if (!undo_.valid) return;

  // Put the card that is on screen back where takeNext() found it, before the
  // undone one displaces it. Nothing has been written for it, so its stored
  // state is still right and the step list can be rebuilt from it.
  switch (undo_.took) {
    case Took::Queue:
      if (queuePos_ > 0) --queuePos_;
      break;
    case Took::Learning:
      if (learningCount_ < kMaxLearning) {
        learning_[learningCount_++] = {currentIndex_, card_.dueDay, static_cast<int>(card_.dueMinute)};
      }
      break;
    case Took::Nothing:
      break;
  }

  // If grading pushed the undone card into a step, take it out again. Search by
  // index rather than trusting a position: takeNext() fills the hole it makes
  // with the last entry, so the one added here may have moved since.
  if (undo_.enteredLearning) {
    for (int i = 0; i < learningCount_; ++i) {
      if (learning_[i].index != undo_.index) continue;
      learning_[i] = learning_[--learningCount_];
      break;
    }
  }

  if (!deck_.storeCard(*cardSource_, undo_.index, undo_.before)) {
    LOG_ERR("STUDY", "Failed to restore card %d", undo_.index);
    writeFailed_ = true;
  }

  // The review itself is struck out rather than removed: revlog.dat is
  // append-only by design, and shrinking it would mean reaching past HalFile
  // into SdFat. Every reader skips a voided record. See StudyStats.h.
  if (undo_.revlogWritten && revlogFile_.isOpen()) {
    uint8_t flags = study::kRevlogVoided;
    const uint32_t at = undo_.revlogOffset + study::kRevlogFlagsOffset;
    if (!revlogFile_.seekSet(at) || revlogFile_.write(&flags, 1) != 1) {
      LOG_ERR("STUDY", "Failed to void the review at %u", static_cast<unsigned>(at));
      writeFailed_ = true;
    }
  }

  reviewedThisSession_ = undo_.reviewed;
  againThisSession_ = undo_.again;

  // If this ever goes wrong it goes wrong quietly -- a card silently duplicated
  // in the step list, or a queue cursor off by one -- so say what moved.
  LOG_INF("STUDY", "Undo card %d: took %d, %d in steps, queue at %d/%d, %d reviewed", undo_.index,
          static_cast<int>(undo_.took), learningCount_, queuePos_, queueCount_, reviewedThisSession_);

  currentIndex_ = undo_.index;
  const bool loaded = loadCurrent();
  if (loaded) {
    // Back on the answer, not the question: the user is here to change a grade,
    // and making them reveal the card again would be a step they did not ask
    // for.
    face_ = Face::Answer;
  } else {
    // The card came back off the deck a moment ago, so this means the card has
    // gone away underneath us. Fall back to the deck screen rather than leave a
    // stale card on screen that grading would apply to the wrong index.
    LOG_ERR("STUDY", "Undo could not reload card %d", undo_.index);
    refreshStats();
    view_ = View::Deck;
  }

  // One level. A second undo would need the state before the previous review,
  // which was not kept, and Mario asked for the fat-finger case rather than a
  // history.
  undo_ = Undo{};
  flushWrites();
  requestUpdate();
}

void StudyActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
    return;
  }
  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;

  if (view_ == View::Deck) {
    // Hit-testing comes from the buffer the screen filled while drawing, so a
    // region can never drift from the pixels that drew it.
    if (!interactionsReady_) return;
    fui::InputSnapshot input;
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
    routeAction(interactions_.route(input));
    return;
  }
  if (view_ == View::Image) {
    // Anywhere goes back. There is one thing to do here and no way to be wrong.
    view_ = View::Card;
    requestUpdate();
    return;
  }
  if (view_ != View::Card) return;

  const int footerTop = renderer.getScreenHeight() - kFooterHeight;
  if (face_ == Face::Question) {
    // Undo owns the first cell of the footer and nothing else. It is the one
    // action here that changes something already written, so unlike revealing
    // it is worth having to aim at -- a stray tap must not take back a review.
    const int width = renderer.getScreenWidth();
    if (undo_.valid && tapY >= footerTop && width > 0 && tapX < width / kUndoSlots) {
      undo();
      return;
    }
    // Everything else reveals, the card included: it is the only other action
    // the screen offers and it destroys nothing, so making the user aim would
    // be friction for its own sake.
    face_ = Face::Answer;
    requestUpdate();
    return;
  }

  // The card itself opens the photograph. The header only *says* PHOTO: taps in
  // that band never reach here because the theme owns it, which no amount of
  // reading the code revealed and one render made obvious.
  //
  // Making the body the target is the better design regardless. It is the
  // largest thing on the screen, it does nothing else on the answer side, and
  // docs/design-language.md's rule is that the commonest action should not be
  // something you have to aim at. Grading still belongs to the footer alone.
  if (cardHasImage() && tapY < footerTop) {
    view_ = View::Image;
    requestUpdate();
    return;
  }

  if (tapY >= footerTop) {
    // Slot from the same division that drew the cells, so the hit region cannot
    // drift from the pixels. See docs/building-apps.md.
    const int width = renderer.getScreenWidth();
    const int slot = width > 0 ? tapX * 4 / width : 0;
    const int clamped = slot < 0 ? 0 : (slot > 3 ? 3 : slot);
    grade(static_cast<study::Rating>(clamped + 1));
  }
}

int StudyActivity::drawWrapped(const int fontId, int y, const int maxWidth, const char* text,
                               const bool measureOnly) const {
  if (text == nullptr || *text == '\0') return y;

  const int lineHeight = renderer.getTextHeight(fontId);
  const int screenWidth = renderer.getScreenWidth();
  char line[kLineBytes];
  int lineLength = 0;

  const auto flush = [&]() {
    if (lineLength == 0) return;
    line[lineLength] = '\0';
    if (!measureOnly) {
      const int textWidth = renderer.getTextWidth(fontId, line);
      renderer.drawText(fontId, (screenWidth - textWidth) / 2, y, line, true);
    }
    y += lineHeight;
    lineLength = 0;
  };

  const char* p = text;
  while (*p != '\0') {
    // One break unit: a whole word for Latin, a single character for CJK.
    // Chinese is written without spaces, so a space-only rule finds no break
    // and every sentence runs off both edges.
    const char* unitEnd = p;
    if (isBreakable(nextCodepoint(unitEnd))) {
      unitEnd = p + utf8Length(p);
    } else {
      while (*unitEnd != '\0') {
        const char* peek = unitEnd;
        if (isBreakable(nextCodepoint(peek))) break;
        unitEnd = peek;
      }
    }
    const int unitBytes = static_cast<int>(unitEnd - p);
    if (unitBytes <= 0 || unitBytes >= kLineBytes) break;

    // Measure the candidate rather than counting characters: a CJK glyph is
    // three bytes and full width, a Latin one is one byte and narrow.
    char candidate[kLineBytes];
    std::memcpy(candidate, line, static_cast<size_t>(lineLength));
    std::memcpy(candidate + lineLength, p, static_cast<size_t>(unitBytes));
    candidate[lineLength + unitBytes] = '\0';

    if (lineLength > 0 && renderer.getTextWidth(fontId, candidate) > maxWidth) {
      flush();
      while (*p == ' ') ++p;  // a leading space after a break is the break
      continue;
    }
    std::memcpy(line + lineLength, p, static_cast<size_t>(unitBytes));
    lineLength += unitBytes;
    p = unitEnd;
  }
  flush();
  return y;
}

void StudyActivity::drawCard(const Rect& body) {
  const int maxWidth = renderer.getScreenWidth() - 2 * toybox::kMargin;
  const int headwordFont = fontsReady_ ? fonts_.headwordFontId() : kReadingFontId;
  const int sentenceFont = fontsReady_ ? fonts_.sentenceFontId() : kMeaningFontId;
  const bool answer = face_ == Face::Answer;

  // Anchored, not centred. docs/design-language.md: a block floating with equal
  // slack above and below reads as unresolved, so the word hangs from the
  // header and the footer is pinned to the bottom, leaving the slack as one
  // deliberate zone -- which is where the sentence image will go when it lands.
  // The theme's header is 13px taller than the one this screen used to assume,
  // and the tallest card had nine pixels spare -- so the inset gives that back.
  // Measured, not guessed: tools_local/study/measure_layout.py.
  int y = body.y + 8;

  y = drawWrapped(headwordFont, y, maxWidth, note_.field(study::Field::Headword));
  if (answer) {
    y += 6;
    y = drawWrapped(kReadingFontId, y, maxWidth, note_.field(study::Field::Reading));
    y += 2;
    y = drawWrapped(kMeaningFontId, y, maxWidth, note_.field(study::Field::Meaning));
    y = drawWrapped(kSmallFontId, y, maxWidth, note_.field(study::Field::PartOfSpeech));
  }

  if (!note_.empty(study::Field::Sentence)) {
    // The rule, as the Anki template has it: the word above, the sentence it
    // lives in below. Hairline against the footer's kRule, so the two dividers
    // read as different weights rather than competing.
    y += 20;
    toybox::rule(renderer, y, toybox::kHairline);
    y += 20;
    y = drawWrapped(sentenceFont, y, maxWidth, note_.field(study::Field::Sentence));
    if (answer) {
      y += 6;
      y = drawWrapped(kMeaningFontId, y, maxWidth, note_.field(study::Field::SentenceReading));
      y += 2;
      drawWrapped(kMeaningFontId, y, maxWidth, note_.field(study::Field::SentenceMeaning));
    }
  }

  if (answer) {
    // The card's own record, anchored to the bottom of the body rather than
    // left to float after the sentence. With the content anchored under the
    // header and the footer pinned below, this is the third anchor that turns
    // the leftover space into a composition instead of a gap -- which
    // docs/design-language.md calls a real defect on a screen that holds its
    // image for hours.
    //
    // It is information, not decoration: how often you have seen this card and
    // how often you have lost it is exactly what you want when deciding
    // between Hard and Good, and it is the one thing on screen that is yours.
    char record[64];
    const study::Memory memory = card_.memory();
    if (memory.learned) {
      char interval[16];
      study::formatDelay(0, fsrs_.intervalDays(memory), interval, sizeof(interval));
      std::snprintf(record, sizeof(record), "SEEN %u   LAPSED %u   NOW %s", card_.reps, card_.lapses, interval);
    } else {
      std::snprintf(record, sizeof(record), "NEW CARD");
    }
    const int recordWidth = renderer.getTextWidth(kSmallFontId, record);
    const int recordY = body.y + body.height - renderer.getTextHeight(kSmallFontId) - toybox::kMargin;
    // Only if it clears the sentence: a short card must not have its record
    // land on top of its own translation.
    if (recordY > y + 12) {
      renderer.drawText(kSmallFontId, (renderer.getScreenWidth() - recordWidth) / 2, recordY, record, true);
    }
  }
}

void StudyActivity::drawImage(const Rect& body) {
  const int width = renderer.getScreenWidth();
  if (!image_.valid() || !imageSource_) {
    UITheme::drawCenteredWrappedText(renderer, body, kMeaningFontId, "No photograph on this card.", 4);
    return;
  }

  // Centred in the body. The picture was scaled and dithered at conversion
  // time, so there is nothing to compute here beyond where to put it.
  const int left = body.x + (width - image_.width) / 2;
  const int top = body.y + (body.height - image_.height) / 2;

  // A band at a time through a stack buffer: a whole 448x620 image is 34KB,
  // and taking a heap block that size once per card is the churn that
  // fragments a device with no room to spare. See StudyImages.h.
  uint8_t band[study::kImageBandBytes];
  int row = 0;
  while (row < image_.height) {
    const int rows = images_.readBand(*imageSource_, image_, row, band);
    if (rows <= 0) break;
    for (int y = 0; y < rows; ++y) {
      const uint8_t* line = band + static_cast<size_t>(y) * image_.stride;
      for (int x = 0; x < image_.width; ++x) {
        if ((line[x >> 3] >> (7 - (x & 7))) & 1) {
          renderer.drawPixel(left + x, top + row + y, true);
        }
      }
    }
    row += rows;
  }
}

void StudyActivity::drawFooter(const Rect& footer) {
  const int width = footer.width;
  renderer.fillRect(0, footer.y, width, toybox::kRule, true);
  const int inner = footer.y + toybox::kRule;
  const int innerHeight = footer.height - toybox::kRule;

  if (face_ == Face::Question) {
    // One control, named. docs/design-language.md: a button is a region, and a
    // control that cannot act dims rather than disappears -- so the four cells
    // are not drawn empty here, they are replaced by the one thing that can
    // happen.
    //
    // Undo is the exception to that rule, and deliberately: it appears only
    // when there is a review to take back. A permanently visible UNDO would be
    // dead for the first card of every session and greyed for most of the rest,
    // which reads as a broken control rather than a resting one.
    if (undo_.valid) {
      // The same division that hit-tests it, so the line lands on the boundary
      // rather than near it.
      const int split = width / kUndoSlots;
      renderer.fillRect(split, inner, toybox::kHairline, innerHeight, true);
      const int undoWidth = renderer.getTextWidth(toybox::kUiFontId, "UNDO");
      toybox::drawCapsCentered(renderer, toybox::kUiFontId, (split - undoWidth) / 2, inner, innerHeight, "UNDO", true);
      const int showWidth = renderer.getTextWidth(toybox::kUiFontId, "SHOW ANSWER");
      toybox::drawCapsCentered(renderer, toybox::kUiFontId, split + (width - split - showWidth) / 2, inner, innerHeight,
                               "SHOW ANSWER", true);
      return;
    }
    toybox::drawCapsCentered(renderer, toybox::kUiFontId,
                             (width - renderer.getTextWidth(toybox::kUiFontId, "SHOW ANSWER")) / 2, inner, innerHeight,
                             "SHOW ANSWER", true);
    return;
  }

  static constexpr const char* kLabels[4] = {"AGAIN", "HARD", "GOOD", "EASY"};
  const int slot = width / 4;
  for (int i = 0; i < 4; ++i) {
    const int x = i * slot;
    if (i > 0) renderer.fillRect(x, inner, toybox::kHairline, innerHeight, true);

    char delay[16];
    study::formatDelay(preview_[i].delayMinutes, preview_[i].intervalDays, delay, sizeof(delay));
    const int delayWidth = renderer.getTextWidth(kSmallFontId, delay);
    renderer.drawText(kSmallFontId, x + (slot - delayWidth) / 2, inner + 14, delay, true);

    const int labelWidth = renderer.getTextWidth(toybox::kUiFontId, kLabels[i]);
    toybox::drawCapsCentered(renderer, toybox::kUiFontId, x + (slot - labelWidth) / 2, inner + 48, innerHeight - 56,
                             kLabels[i], true);
  }
}

void StudyActivity::refreshStats() {
  if (!revlogSource_) return;
  // Re-open to pick up what this session appended: the read handle's cached
  // size is from when it was opened, and a session that just wrote forty
  // reviews would otherwise show none of them.
  char path[96];
  std::snprintf(path, sizeof(path), "%s/revlog.dat", kDeckDir);
  revlogSource_.reset();
  if (Storage.openFileForRead("STUDY", path, revlogReadFile_)) {
    revlogSource_ = makeUniqueNoThrow<FileSource>(revlogReadFile_);
  }
  if (revlogSource_) {
    study::readStats(*revlogSource_, today_, deck_.meta().collectionCreated, stats_);
  }
}

void StudyActivity::buildDeckModel(studyui::DeckModel& out) const {
  out.name = deck_.meta().name;
  out.due = dueTotal_;
  out.fresh = newTotal_;
  out.total = deck_.noteCount();
  out.reviewed = reviewedThisSession_;
  out.recalled = reviewedThisSession_ - againThisSession_;
  out.forecast = forecast_;
  out.history = stats_.reviewsPerDay;
  out.streak = stats_.streak;
  out.retention = stats_.retention();
  out.lifetimeReviews = stats_.lifetimeReviews;
  out.sessionOver = (queueCount_ - queuePos_) + learningCount_ == 0;
  out.writeFailed = writeFailed_;
}

void StudyActivity::routeAction(const fui::ActionEvent& event) {
  if (event.action != studyui::ActionStudy) return;
  // Re-scan rather than resuming a stale queue: a session can end, the user can
  // sit on this screen past the rollover hour, and what was due then is not
  // what is due now.
  buildQueue();
  reviewedThisSession_ = 0;
  againThisSession_ = 0;
  if (takeNext()) view_ = View::Card;
  requestUpdate();
}

void StudyActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();

  if (view_ == View::Deck) {
    // Chrome is a FreeInkUI component wearing Toybox, never hand-drawn: Screen
    // substitutes every theme token, and both bugs in this fork's first port
    // were tokens it would have supplied. See docs/design-language.md.
    fui::GfxRendererTarget target = toybox::makeTarget(renderer);
    const fui::InputSnapshot noInput{};
    interactionsReady_ = false;
    toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
    toybox::Screen screen(frame, toybox::themeTokens());

    studyui::DeckModel model;
    buildDeckModel(model);
    studyui::buildDeck(screen, model);

    interactionsReady_ = true;
    toybox::reportOverflow(interactions_, "Study deck");

    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // The header never repaints its shape, only its text, so solid black is free
  // here and ghosts nothing. docs/design-language.md, the one rule.
  char title[64];
  if (view_ == View::Image) {
    // No title. The headword cannot be drawn here at all -- this header uses
    // the Latin-only UI face, so a hanzi title came out as a replacement
    // diamond in the top-left corner -- and the BACK pill already says
    // everything this screen needs to.
    title[0] = '\0';
  } else if (view_ == View::Card) {
    const int remaining = (queueCount_ - queuePos_) + learningCount_ + 1;
    std::snprintf(title, sizeof(title), "%d LEFT", remaining);
  } else {
    std::snprintf(title, sizeof(title), "STUDY");
  }
  // The band, the rule and the battery come from the theme; the two things
  // inside it are placed here.
  //
  // Not stubbornness. GUI.drawHeader does not centre its title in the band it
  // is given -- measured, the title's cap sits 13px below centre while a pill
  // centred in the same band sits on it. Matching the title would have made
  // the two agree with each other and both look low, which is the thing that
  // felt wrong in the first place. Passing an empty title and placing both
  // ourselves is the smallest change that puts them where they belong, and the
  // chrome is still the theme's.
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect headerBand{0, metrics.topPadding, width, metrics.headerHeight};
  GUI.drawHeader(renderer, headerBand, "");

  if (title[0] != '\0') {
    toybox::drawCapsCentered(renderer, toybox::kUiFontId, toybox::kMargin, headerBand.y, headerBand.height, title,
                             true);
  }

  // One control, one place. It says PHOTO on the answer and BACK on the
  // photograph, so the thing you tap never moves between the two screens.
  //
  // Drawn as a pill because a bare word does not read as something you can
  // press -- and knocked out to white before it is stroked, since an outline
  // alone would let whatever is behind it show through. See
  // docs/design-language.md.
  const bool offeringPhoto = view_ == View::Card && face_ == Face::Answer && cardHasImage();
  if (offeringPhoto || view_ == View::Image) {
    const char* label = (view_ == View::Image) ? kBackLabel : kPhotoLabel;
    const int labelWidth = renderer.getTextWidth(toybox::kUiFontId, label);
    const int pillWidth = labelWidth + 2 * kPillPadding;
    const int pillX = (width - pillWidth) / 2;
    const int pillY = headerBand.y + (headerBand.height - kPillHeight) / 2;

    renderer.fillRoundedRect(pillX, pillY, pillWidth, kPillHeight, kPillHeight / 2, White);
    renderer.drawRoundedRect(pillX, pillY, pillWidth, kPillHeight, toybox::kHairline, kPillHeight / 2, true);
    toybox::drawCapsCentered(renderer, toybox::kUiFontId, pillX + kPillPadding, pillY, kPillHeight, label, true);
  }

  const int bodyTop = metrics.topPadding + metrics.headerHeight;
  const int footerTop = height - kFooterHeight;

  if (view_ == View::Image) {
    drawImage(Rect{0, bodyTop, width, height - bodyTop});
  } else if (view_ == View::Card) {
    drawCard(Rect{0, bodyTop, width, footerTop - bodyTop});
    drawFooter(Rect{0, footerTop, width, kFooterHeight});
  } else {
    UITheme::drawCenteredWrappedText(renderer, Rect{0, bodyTop + 40, width, height - bodyTop - 120}, kMeaningFontId,
                                     "No deck on the card. Convert one with anki_to_deck.py into /study/mandarin.", 4);
  }

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
