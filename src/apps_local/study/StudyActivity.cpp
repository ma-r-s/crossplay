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

        const bool isNew = probe.state == static_cast<uint8_t>(study::State::New);
        if (pass == 0 && (isNew || !study::Scheduler::isDue(probe, today_, minute))) continue;
        if (pass == 1 && !isNew) continue;

        queue_[queueCount_++] = base + i;
        ++taken;
      }
    }
  }
  LOG_INF("STUDY", "Queue: %d of %d due, %d new (day %d, minute %d)", queueCount_, dueTotal_, newTotal_, today_, minute);
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
  if (best < 0) return false;

  currentIndex_ = learning_[best].index;
  learning_[best] = learning_[--learningCount_];
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

  scheduler_.preview(card_, today_, nowMinute(), preview_);
  return true;
}

bool StudyActivity::persist(const int index, const study::CardState& card, const study::Rating rating,
                            const study::Outcome& outcome) {
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
    if (!revlogFile_.seekSet(revlogFile_.size()) || revlogFile_.write(record, sizeof(record)) != sizeof(record)) {
      LOG_ERR("STUDY", "Failed to append to revlog.dat");
      ok = false;
    }
  }
  return ok;
}

void StudyActivity::grade(const study::Rating rating) {
  const study::Outcome outcome = scheduler_.answer(card_, rating, today_, nowMinute());

  if (!persist(currentIndex_, card_, rating, outcome)) writeFailed_ = true;
  ++reviewedThisSession_;
  if (rating == study::Rating::Again) ++againThisSession_;

  LOG_INF("STUDY", "Graded %d: S %.2f -> %.2f, %s", static_cast<int>(rating), static_cast<double>(card_.stability),
          static_cast<double>(outcome.card.stability), outcome.delayMinutes > 0 ? "back this session" : "scheduled");

  // Still inside a step list: it comes back before the session ends.
  if (outcome.delayMinutes > 0 && learningCount_ < kMaxLearning) {
    learning_[learningCount_++] = {currentIndex_, outcome.card.dueDay, outcome.card.dueMinute};
  }

  // Flush every review rather than at exit. The device can lose power or sleep
  // mid-session, and a review the user gave is not ours to lose.
  cardSource_->flush();
  revlogFile_.flush();

  if (!takeNext()) view_ = View::Deck;
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
  if (view_ != View::Card) return;

  const int footerTop = renderer.getScreenHeight() - kFooterHeight;
  if (face_ == Face::Question) {
    // The footer is the button, and it says so. A tap on the card reveals too:
    // it is the only action the screen offers and it destroys nothing, so
    // making the user aim would be friction for its own sake.
    face_ = Face::Answer;
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
  int y = body.y + toybox::kMargin + 8;

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

void StudyActivity::buildDeckModel(studyui::DeckModel& out) const {
  out.name = deck_.meta().name;
  out.due = dueTotal_;
  out.fresh = newTotal_;
  out.total = deck_.noteCount();
  out.reviewed = reviewedThisSession_;
  out.recalled = reviewedThisSession_ - againThisSession_;
  out.forecast = forecast_;
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
  if (view_ == View::Card) {
    const int remaining = (queueCount_ - queuePos_) + learningCount_ + 1;
    std::snprintf(title, sizeof(title), "%d LEFT", remaining);
  } else {
    std::snprintf(title, sizeof(title), "STUDY");
  }
  GUI.drawHeader(renderer, Rect{0, 0, width, toybox::kHeaderHeight}, title);

  const int bodyTop = toybox::kHeaderHeight;
  const int footerTop = height - kFooterHeight;

  if (view_ == View::Card) {
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
