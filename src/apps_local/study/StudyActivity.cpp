#include "StudyActivity.h"

#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "../../DevMode.h"
#include "../../SilentRestart.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../components/UITheme.h"
#include "../../util/QrUtils.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxFormat.h"
#include "../ui/ToyboxMetrics.h"
#include "../ui/ToyboxTheme.h"
#include "StudyStats.h"
#include "StudyText.h"
#include "fontIds.h"

namespace {

// Where anki_to_deck.py and make_fonts.py put their output.
// Where decks live. The directory name under /study is the tool's choice, not
// ours: study.py names it after the deck, and Mario's card predates that and
// says "mandarin". findDeckDir scans for whichever single deck is installed.
constexpr const char* kStudyRoot = "/study";
constexpr const char* kFontsDirName = "fonts";

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

// utf8Length, nextCodepoint, isBreakable and the wrap itself moved to
// StudyText.h when cloze needed to know which codepoints landed on which
// line. Brought into this file's anonymous namespace by name so the call
// sites below read as they did.
using study::isBreakable;
using study::nextCodepoint;

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

  if (findDeckDirs() && openDeckAt(deckIndex_)) {
    beginDeckSession();
    view_ = View::Deck;
  } else {
    LOG_ERR("STUDY", "No deck under %s -- run tools_local/study/study.py setup", kStudyRoot);
  }
#if !defined(FREEINK_NET_WOLFSSL)
  // Render harness for the sync-flow screens, which are otherwise reachable
  // only mid-flow against a live bridge. Simulator-only by the same gate the
  // curl transport uses; drives the view with canned data for sim-shot.
  if (const char* preview = std::getenv("STUDY_SYNCFLOW_PREVIEW")) {
    applySyncFlowPreview(preview);
  }
#endif
  requestUpdate();
}

#if !defined(FREEINK_NET_WOLFSSL)
void StudyActivity::applySyncFlowPreview(const char* state) {
  if (std::strcmp(state, "pairqr") == 0) {
    pairCode_ = "GAS7V3AY";  // real codes are 8 characters; a shorter stand-in hides overflow
    view_ = View::PairQr;
    return;
  }
  if (std::strcmp(state, "picker") == 0) {
    static const char* kNames[] = {"Mandarin: Vocabulary", "GRE Vocab List", "Kanji N5", "Default"};
    static const int kCards[] = {5001, 1992, 812, 0};
    pickerRows_.clear();
    for (int i = 0; i < 4; ++i) {
      studyui::DeckPickerModel::Row row;
      row.name = kNames[i];
      row.cards = kCards[i];
      row.chosen = i == 0;
      pickerRows_.push_back(row);
    }
    picker_ = studyui::DeckPickerModel{};
    picker_.rows = pickerRows_.data();
    picker_.count = static_cast<int>(pickerRows_.size());
    picker_.maxChosen = study::kMaxChosenDecks;
    picker_.chosenCount = 1;
    picker_.withheld = true;
    view_ = View::DeckPicker;
    return;
  }
  if (std::strcmp(state, "pairconfirm") == 0) {
    pairUsername_ = "mario@example.com";
    view_ = View::PairConfirm;
    return;
  }
  // The new faces, canned per the brief's render matrix.
  studyui::SyncFlowModel& m = previewFlow_;
  m = studyui::SyncFlowModel{};
  if (std::strcmp(state, "ladder") == 0) {
    m.stages[0] = studyui::SyncStageState::Done;
    m.stages[1] = studyui::SyncStageState::Done;
    std::snprintf(m.facts[1], sizeof(m.facts[1]), "142 SENT");
    m.stages[2] = studyui::SyncStageState::Active;
    std::snprintf(m.caption, sizeof(m.caption), "This first time can take a while. It keeps working if you leave.");
    m.safety = studyui::SyncSafety::ReviewsSafe;
    m.leaveSafe = true;  // the preview must wear what the flow actually sets
    std::snprintf(m.facts[static_cast<int>(studyui::SyncStage::Build)], sizeof(m.facts[0]), "1m15s");
    previewFlowSet_ = true;
  } else if (std::strcmp(state, "success") == 0) {
    m.verdict = studyui::SyncVerdictKind::Success;
    std::snprintf(m.title, sizeof(m.title), "SYNCED");
    std::snprintf(m.body, sizeof(m.body), "This reader and your Anki are up to date.");
    std::snprintf(m.factLines[0], sizeof(m.factLines[0]), "142 SENT, 3 HAD NO CARD IN ANKI");
    std::snprintf(m.factLines[1], sizeof(m.factLines[1]), "2 DECKS UPDATED");
    std::snprintf(m.factLines[2], sizeof(m.factLines[2]), "LAST SYNC 20:15");
    m.factCount = 3;
    m.safety = studyui::SyncSafety::ReviewsSafe;
    previewFlowSet_ = true;
  } else if (std::strcmp(state, "partway") == 0) {
    m.verdict = studyui::SyncVerdictKind::Neutral;
    for (int i = 0; i < 4; ++i) m.stages[i] = studyui::SyncStageState::Done;
    std::snprintf(m.title, sizeof(m.title), "PART WAY");
    std::snprintf(m.body, sizeof(m.body), "Mandarin: Vocabulary could not be built. Everything else is up to date.");
    std::snprintf(m.whatNow, sizeof(m.whatNow), "Choose your decks again to drop it, or fix it in Anki.");
    std::snprintf(m.factLines[0], sizeof(m.factLines[0]), "142 SENT, 3 HAD NO CARD IN ANKI");
    m.factCount = 1;
    m.safety = studyui::SyncSafety::ReviewsSafePartialDecks;
    previewFlowSet_ = true;
  } else if (std::strcmp(state, "erroracked") == 0) {
    m.verdict = studyui::SyncVerdictKind::Error;
    for (int i = 0; i < 2; ++i) m.stages[i] = studyui::SyncStageState::Done;
    m.stages[2] = studyui::SyncStageState::Active;
    std::snprintf(m.title, sizeof(m.title), "NOT SYNCED");
    std::snprintf(m.body, sizeof(m.body), "The sync service could not reach AnkiWeb. This is usually brief.");
    std::snprintf(m.whatNow, sizeof(m.whatNow), "The service may be busy. Try again in a few minutes.");
    m.safety = studyui::SyncSafety::ReviewsSafe;
    previewFlowSet_ = true;
  }
}
#endif

// Everything that starts over when a deck opens -- first open and every switch.
void StudyActivity::beginDeckSession() {
  fonts_.setRoot(deckDir_);
  fonts_.probe();
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
  writeFailed_ = false;
  reviewedThisSession_ = 0;
  againThisSession_ = 0;
  learningCount_ = 0;
  undo_ = Undo{};
  currentIndex_ = -1;
  buildQueue();
  refreshStats();
}

void StudyActivity::closeDeck() {
  if (cardSource_) cardSource_->flush();
  revlogFile_.flush();
  fonts_.unload(renderer);
  imageSource_.reset();
  revlogSource_.reset();
  metaSource_.reset();
  deckSource_.reset();
  cardSource_.reset();
  // Member handles get reused by the next openDeck, and a HalFile must be
  // closed before the same variable opens a different path.
  metaFile_.close();
  deckFile_.close();
  cardFile_.close();
  revlogFile_.close();
  revlogReadFile_.close();
  imageFile_.close();
  images_ = study::StudyImages{};
  // The "no deck open" marker endSyncSession's reopen guard keys on. This was
  // never cleared, so the post-sync reopen could not fire and the first
  // START REVIEWING after any sync dereferenced a null cardSource_ -- a
  // device panic that shipped in v1.5.0 and hid because neither person nor
  // harness ever reviewed straight after syncing until 2026-08-27.
  deckDir_[0] = '\0';
}

void StudyActivity::switchDeck() {
  if (deckCount_ < 2) return;
  closeDeck();
  const int next = (deckIndex_ + 1) % deckCount_;
  if (openDeckAt(next)) {
    beginDeckSession();
    view_ = View::Deck;
  } else {
    // A deck that was there at scan time and is not now is a pulled SD card;
    // there is nothing sane to show but the empty state.
    LOG_ERR("STUDY", "Deck %s vanished", deckNames_[next]);
    view_ = View::NoDeck;
  }
  requestUpdate();
}

void StudyActivity::onExit() {
  if (wifiActivated_ && !devmode::holdsRadio() && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();  // on touch boards: stops SNTP and the radio in place
  }
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

bool StudyActivity::findDeckDirs() {
  // Every directory under /study holding a meta.dat is a deck. Sorted by name
  // so the order is the same on every boot -- openNextFile hands entries back
  // in FAT order, which changes when files do, and a switcher that reshuffles
  // itself between sessions would make "the next deck" mean nothing.
  deckCount_ = 0;
  decksOverCap_ = 0;
  HalFile root = Storage.open(kStudyRoot);
  if (!root.isOpen() || !root.isDirectory()) return false;
  // Keep counting past the cap. A deck beyond it is invisible in the switcher
  // AND unreachable to buildPayloads, so its reviews are never sent while the
  // door reports nothing outstanding. Nothing deletes a deck folder, so a card
  // grows past eight simply by changing your mind, and the user has to be told
  // which decks to remove.
  for (;;) {
    HalFile entry = root.openNextFile();
    if (!entry.isOpen()) break;
    if (!entry.isDirectory()) continue;
    // 48: the service slugifies deck names up to 40 characters, so a 32-byte
    // buffer truncated real Anki names ("AnkiDroid Japanese Core 2000 Step
    // 01"), the meta probe then missed, and the deck was skipped silently --
    // downloaded to the card and invisible on it.
    char name[48];
    entry.getName(name, sizeof(name));
    if (std::strcmp(name, kFontsDirName) == 0) continue;
    char probe[96];
    std::snprintf(probe, sizeof(probe), "%s/%s/meta.dat", kStudyRoot, name);
    if (!Storage.exists(probe)) continue;
    if (deckCount_ >= kMaxDecks) {
      // Full. Keep the alphabetically-first set rather than whichever eight
      // the filesystem happened to hand back: truncating before the sort meant
      // FAT order decided, which is creation order, so the decks dropped were
      // always the ones just chosen -- the worst possible choice and an
      // invisible one. Deterministic is not fair, but it is explicable, and
      // the same decks survive on every boot.
      int worst = 0;
      for (int i = 1; i < deckCount_; ++i) {
        if (std::strcmp(deckNames_[i], deckNames_[worst]) > 0) worst = i;
      }
      ++decksOverCap_;
      if (std::strcmp(name, deckNames_[worst]) >= 0) continue;
      std::snprintf(deckNames_[worst], sizeof(deckNames_[0]), "%s", name);
      continue;
    }
    std::snprintf(deckNames_[deckCount_], sizeof(deckNames_[0]), "%s", name);
    ++deckCount_;
  }
  if (decksOverCap_ > 0) {
    LOG_ERR("STUDY", "%d deck folder(s) past the %d this reader holds; their reviews cannot be sent", decksOverCap_,
            kMaxDecks);
  }
  for (int i = 1; i < deckCount_; ++i) {
    for (int j = i; j > 0 && std::strcmp(deckNames_[j], deckNames_[j - 1]) < 0; --j) {
      char swap[sizeof(deckNames_[0])];
      std::memcpy(swap, deckNames_[j], sizeof(swap));
      std::memcpy(deckNames_[j], deckNames_[j - 1], sizeof(swap));
      std::memcpy(deckNames_[j - 1], swap, sizeof(swap));
    }
  }

  // Reopen whatever was open last time. A device that greets you with a deck
  // you were not studying feels like someone else's.
  deckIndex_ = 0;
  char last[sizeof(deckNames_[0])] = "";
  HalFile lastFile;
  char lastPath[64];
  std::snprintf(lastPath, sizeof(lastPath), "%s/.last", kStudyRoot);
  if (Storage.openFileForRead("STUDY", lastPath, lastFile)) {
    const int n = lastFile.read(last, sizeof(last) - 1);
    if (n > 0) last[n] = '\0';
    for (int i = 0; i < deckCount_; ++i) {
      if (std::strcmp(deckNames_[i], last) == 0) deckIndex_ = i;
    }
  }
  return deckCount_ > 0;
}

bool StudyActivity::openDeckAt(const int index) {
  if (index < 0 || index >= deckCount_) return false;
  deckIndex_ = index;
  std::snprintf(deckDir_, sizeof(deckDir_), "%s/%s", kStudyRoot, deckNames_[index]);

  char lastPath[64];
  std::snprintf(lastPath, sizeof(lastPath), "%s/.last", kStudyRoot);
  HalFile lastFile;
  if (Storage.openFileForWrite("STUDY", lastPath, lastFile)) {
    lastFile.write(deckNames_[index], std::strlen(deckNames_[index]));
  }
  return openDeck();
}

bool StudyActivity::openDeck() {
  char path[96];
  std::snprintf(path, sizeof(path), "%s/meta.dat", deckDir_);
  if (!Storage.openFileForRead("STUDY", path, metaFile_)) return false;
  std::snprintf(path, sizeof(path), "%s/deck.dat", deckDir_);
  if (!Storage.openFileForRead("STUDY", path, deckFile_)) return false;

  // O_RDWR without O_TRUNC: openFileForWrite truncates, which on cards.dat
  // would erase every card's scheduling state the moment the app opened.
  std::snprintf(path, sizeof(path), "%s/cards.dat", deckDir_);
  cardFile_ = Storage.open(path, O_RDWR);
  if (!cardFile_.isOpen()) {
    LOG_ERR("STUDY", "Cannot open cards.dat for update");
    return false;
  }
  std::snprintf(path, sizeof(path), "%s/revlog.dat", deckDir_);
  revlogFile_ = Storage.open(path, O_RDWR | O_CREAT);
  if (!revlogFile_.isOpen()) LOG_ERR("STUDY", "Cannot open revlog.dat -- reviews will not be logged");
  // A second handle over the same file, for reading history back. Separate from
  // the append handle so a stats read cannot disturb the write position.
  if (Storage.openFileForRead("STUDY", path, revlogReadFile_)) {
    revlogSource_ = makeUniqueNoThrow<FileSource>(revlogReadFile_);
  }

  std::snprintf(path, sizeof(path), "%s/images.dat", deckDir_);
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
    LOG_ERR("STUDY", "%s is not a study deck (or is a different format version)", deckDir_);
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
  // What the deck screen promises has to be what a session will actually
  // give you, so the same daily limits the queue enforces are applied here.
  // Uncapped, home advertised every available card while the queue handed out
  // only the day's allowance: a deck with 10 due and 23 new under a 20/day
  // limit said 33 TO GO and then finished after 30, and three cards the user
  // was promised vanished with no explanation. For a flashcard app that reads
  // as losing their work, which is the one thing it cannot look like.
  //
  // Anki's own deck list shows the capped counts for the same reason, and this
  // app follows Anki's scheduling semantics everywhere else.
  const int dueCap = deck_.meta().reviewsPerDay;
  const int newCap = deck_.meta().newPerDay;
  dueTotal_ = dueSeen < dueCap ? dueSeen : dueCap;
  newTotal_ = newSeen < newCap ? newSeen : newCap;
  // The queue itself is bounded too, and reviews are taken before new, so a
  // backlog past the queue's size eats into what new cards can be promised.
  if (dueTotal_ > kMaxQueue) dueTotal_ = kMaxQueue;
  if (dueTotal_ + newTotal_ > kMaxQueue) newTotal_ = kMaxQueue - dueTotal_;

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
  otherWaiting_ = 0;
  for (int i = 0; i < deckCount_; ++i) {
    if (i == deckIndex_) continue;
    otherWaiting_ += countWaitingIn(deckNames_[i]);
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
  // The roll is over the families the card actually has (probed at onEnter),
  // not all six names: rolling an absent one and walking forward would favour
  // whichever family follows the gaps. loadPreferred stays as the fallback for
  // a family that is present but will not load -- a corrupt file, mid-copy.
  const int present = fonts_.presentCount();
  if (present > 0) {
    const int pick = fonts_.presentFamily(static_cast<int>(nextRandom(shuffle_) % static_cast<uint32_t>(present)));
    fontsReady_ = fonts_.load(renderer, pick) || fonts_.loadPreferred(renderer, pick) >= 0;
  } else {
    fontsReady_ = false;  // no families at all: the built-in serif draws everything
  }
  if (fontsReady_) {
    // A cloze card has no headword: both of its faces are drawn at sentence
    // size, so nothing is warmed at headword size and the question face gets
    // a pass of its own. The two faces differ by more than the brackets when
    // the hole carries a hint, and a hint is the one part of a cloze card
    // written in the deck's own script.
    if (note_.isCloze()) {
      fonts_.prewarm(renderer, "", note_.field(study::Field::Sentence));
      fonts_.prewarm(renderer, "", note_.field(study::Field::ClozeQuestion));
    } else {
      fonts_.prewarm(renderer, note_.field(study::Field::Headword), note_.field(study::Field::Sentence));
    }
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
  // from. See docs/apps/study-deck-format.md.
  if (!revlogFile_.isOpen()) {
    // The one file in a deck that has to be CREATED rather than opened, so on
    // a full or failing card it is the one that fails while the others open.
    // Logging that and continuing gave a whole working study session whose
    // every answer went nowhere: cards.dat advanced, the next build overwrote
    // it with the server's copy, and the sync said SYNCED with NONE NEW.
    // Failing here lights the banner the deck screen already has.
    LOG_ERR("STUDY", "no review log open; refusing to answer a card into nothing");
    return false;
  }
  {
    uint8_t record[32] = {};
    const int64_t nowS = static_cast<int64_t>(time(nullptr));
    if (nowS < study::kClockFloor) {
      // No clock yet (a flat battery clears the RTC; the first sync sets it).
      // A review logged now is stamped near the epoch, and deck_to_anki
      // replays that straight into the real collection as the card's last
      // review. Refusing the record loses one answer; writing it corrupts
      // the user's own scheduling history.
      LOG_ERR("STUDY", "clock is not set; not logging this review");
      return false;
    }
    const int64_t nowMs = nowS * 1000;
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
    // Append on the record grid, never after a half-written tail. A power cut
    // or an unclean eject can leave revlog.dat a non-multiple of the record
    // size; appending at that length puts every later record off-grid, so the
    // reviews either side of the seam are unreadable by anything that walks
    // the file in strides -- the device's own stats, its unsent count, and the
    // bridge's parser alike. Overwriting the partial tail loses only the
    // record that was already incomplete.
    uint32_t at = static_cast<uint32_t>(revlogFile_.size());
    const uint32_t stray = at % study::kRevlogRecordBytes;
    if (stray != 0) {
      LOG_ERR("STUDY", "revlog.dat ends %u bytes into a record; overwriting the partial tail", stray);
      at -= stray;
    }
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
    // Recount before showing the deck screen: dueTotal_/newTotal_ are from
    // session start, and "45 TO GO" over a finished session reads as a broken
    // counter. With zero waiting and reviewedThisSession_ intact, the screen's
    // DONE state -- which was unreachable while this recount was missing --
    // finally renders.
    buildQueue();
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
    if (view_ == View::SyncFlow) {
      view_ = deckCount_ > 0 ? View::Deck : View::NoDeck;
      requestUpdate();
      return;
    }
    if (view_ == View::DeckPicker) {
      // The picker's own loop owns Back while a sync is running; reaching
      // here means it is on screen with no flow behind it.
      view_ = View::Deck;
      requestUpdate();
      return;
    }
    if (view_ == View::Image) {
      view_ = View::Card;
      requestUpdate();
      return;
    }
    if (view_ == View::Card) {
      // Back walks up, never out: a review session returns to the deck
      // screen. The session state stays; the next START REVIEWING re-scans
      // anyway. Flush so a wall-charger yank right after costs nothing.
      flushWrites();
      // Re-read what this session just wrote. Without these the deck screen
      // redraws its session-start counts over an empty history panel, which
      // reads as "the answers you just gave were thrown away".
      buildQueue();
      refreshStats();
      view_ = View::Deck;
      requestUpdate();
      return;
    }
    shelf::leave(renderer, mappedInput);
    return;
  }
  if (syncQueued_) {
    syncQueued_ = false;
    runSyncFlow();
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
  if (view_ == View::SyncFlow) {
    if (interactionsReady_) {
      fui::InputSnapshot input;
      input.touchReleased = true;
      input.touchX = static_cast<int16_t>(tapX);
      input.touchY = static_cast<int16_t>(tapY);
      const fui::ActionEvent event = interactions_.route(input);
      if (event.action == studyui::ActionSyncVerdict && event.value == 1) {
        beginSync();
        return;
      }
    }
    // Back to whatever a fresh open would show: a reader with no decks yet
    // must not land on a deck screen announcing ALL CLEAR over nothing, with
    // the installer QR it still needs nowhere in sight.
    view_ = deckCount_ > 0 ? View::Deck : View::NoDeck;
    requestUpdate();
    return;
  }
  if (view_ == View::NoDeck) {
    if (tapX >= noDeckSyncX_ && tapX < noDeckSyncX_ + noDeckSyncW_ && tapY >= noDeckSyncY_ &&
        tapY < noDeckSyncY_ + noDeckSyncH_) {
      // A paired reader with an empty card is offered CHOOSE DECKS, so the
      // tap has to actually reopen the question; without this it ran a plain
      // sync that answered DECKS UP TO DATE over an empty card, forever.
      // Only ask which decks when that is genuinely open: a reader whose decks
      // are already chosen and merely missing wants them fetched, and being
      // asked to re-pick them is the screen contradicting its own button.
      study::BridgeState onCard;
      pickerRequested_ = !(study::loadBridgeState(onCard) && onCard.choseDecks);
      beginSync();
      return;
    }
    // The way out when the chosen decks are the problem. A deck the service
    // cannot build -- one renamed away in Anki is the ordinary case, and
    // nothing warns about it beforehand -- leaves a card with nothing on it,
    // and the door
    // above only re-runs the same failing build. Without this the only escape
    // was unpairing from a browser.
    if (noDeckPickH_ > 0 && tapX >= noDeckSyncX_ && tapX < noDeckSyncX_ + noDeckSyncW_ && tapY >= noDeckPickY_ &&
        tapY < noDeckPickY_ + noDeckPickH_) {
      pickerRequested_ = true;
      beginSync();
    }
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

int StudyActivity::drawWrapped(const int fontId, const int y, const int maxWidth, const char* text,
                               const bool measureOnly) const {
  return drawWrappedMarked(fontId, y, maxWidth, text, 0, 0, measureOnly);
}

int StudyActivity::drawWrappedUnderlined(const int fontId, const int y, const int maxWidth, const char* text,
                                         const int spanStart, const int spanLength) const {
  return drawWrappedMarked(fontId, y, maxWidth, text, spanStart, spanLength, false);
}

int StudyActivity::drawWrappedMarked(const int fontId, const int y, const int maxWidth, const char* text,
                                     const int spanStart, const int spanLength, const bool measureOnly) const {
  // The two buffers are the caller's so nothing here allocates and the sizes
  // stay visible next to the stack budget they count against.
  char line[kLineBytes];
  char scratch[kLineBytes];
  return study::drawWrappedMarked(renderer, fontId, y, maxWidth, text, spanStart, spanLength, measureOnly, line,
                                  kLineBytes, scratch);
}

bool StudyActivity::fitsAsDrawn(const int fontId, const char* text, const int maxWidth) const {
  // Two ways a face can fail a card, both ending in a card you cannot read.
  // Nothing painted at all -- stale or mis-built fonts -- shows as total width
  // zero. And a single unbreakable run wider than the screen: the wrap breaks
  // on spaces and before CJK characters, so "capricious" at the 100px headword
  // size has nowhere to break and hangs off both edges. 927 of the GRE deck's
  // 1992 headwords did exactly that. Runs are measured the same way drawWrapped
  // breaks them, so the two cannot disagree.
  if (renderer.getTextWidth(fontId, text) == 0) return false;
  char run[kLineBytes];
  int runLength = 0;
  const auto runFits = [&]() {
    if (runLength == 0) return true;
    run[runLength] = '\0';
    runLength = 0;
    return renderer.getTextWidth(fontId, run) <= maxWidth;
  };
  // The same isBreakable the wrap uses, decoded the same way. A private
  // approximation here once treated every 3-byte character as a break, so an
  // em-dash inside a word split the *measured* runs while the renderer drew
  // the word whole -- the guard approved exactly the overflow it exists to
  // stop.
  for (const char* p = text; *p != '\0';) {
    const char* at = p;
    const uint32_t codepoint = nextCodepoint(p);
    if (codepoint == 0) break;
    if (isBreakable(codepoint)) {
      if (!runFits()) return false;
      continue;
    }
    const int bytes = static_cast<int>(p - at);
    if (runLength + bytes < static_cast<int>(sizeof(run))) {
      for (int i = 0; i < bytes; ++i) run[runLength++] = at[i];
    }
  }
  return runFits();
}

void StudyActivity::drawClozeCard(const Rect& body) {
  const int maxWidth = renderer.getScreenWidth() - 2 * toybox::kMargin;
  const bool answer = face_ == Face::Answer;
  // A cloze card is a sentence with a hole in it, so it is drawn in the
  // sentence face at sentence size -- not the headword face, which is built
  // as large as a deck's longest single word and would fit about four words
  // of a cloze paragraph on the screen.
  int textFont = fontsReady_ ? fonts_.sentenceFontId() : kMeaningFontId;
  const char* question = note_.field(study::Field::ClozeQuestion);
  const char* revealed = note_.field(study::Field::Sentence);
  const char* shown = answer ? revealed : question;

  // Trusted per card, like the vocabulary face: a deck's custom font is often
  // subset to the words it was built from, and a cloze card carries whole
  // sentences of surrounding text it was never measured against. Both faces
  // are checked, so revealing the answer cannot change the typeface.
  if (fontsReady_ && (!fitsAsDrawn(textFont, question, maxWidth) ||
                      (*revealed != '\0' && !fitsAsDrawn(textFont, revealed, maxWidth)))) {
    textFont = kMeaningFontId;
  }

  int y = body.y + 8;
  if (answer) {
    // The span the converter recorded over the revealed text. Anki paints it;
    // here it is underlined, which is the only mark this panel has that does
    // not cost a second font.
    y = drawWrappedUnderlined(textFont, y, maxWidth, revealed, note_.emphasisOffset(), note_.emphasisLength());
    // Back Extra, under a hairline, in the same relationship the vocabulary
    // face gives the example sentence: the same rule, so the two card kinds
    // read as one app.
    if (!note_.empty(study::Field::Meaning)) {
      y += 20;
      toybox::rule(renderer, y, toybox::kHairline);
      y += 20;
      drawWrapped(kMeaningFontId, y, maxWidth, note_.field(study::Field::Meaning));
    }
  } else {
    drawWrapped(textFont, y, maxWidth, shown);
  }
}

void StudyActivity::drawCard(const Rect& body) {
  // A cloze note has no headword and no example sentence; everything below
  // reads those two fields. Dispatched here rather than inside each block so
  // there is one place that says which kind of card this is.
  if (note_.isCloze()) {
    drawClozeCard(body);
    return;
  }

  const int maxWidth = renderer.getScreenWidth() - 2 * toybox::kMargin;
  int headwordFont = fontsReady_ ? fonts_.headwordFontId() : kReadingFontId;
  int sentenceFont = fontsReady_ ? fonts_.sentenceFontId() : kMeaningFontId;
  const bool answer = face_ == Face::Answer;

  // Trust the face per card, not per install. A custom face that cannot draw a
  // single character of this text would leave the card blank -- a missing glyph
  // simply is not painted -- and stale or mis-built fonts on a card are a
  // normal state of someone setting up. Measured width zero means nothing would
  // have appeared; the built-in serif always has something to say.
  if (fontsReady_) {
    const char* headwordText = note_.field(study::Field::Headword);
    if (*headwordText != '\0' && !fitsAsDrawn(headwordFont, headwordText, maxWidth)) {
      headwordFont = kReadingFontId;
    }
    const char* sentenceText = note_.field(study::Field::Sentence);
    if (*sentenceText != '\0' && !fitsAsDrawn(sentenceFont, sentenceText, maxWidth)) {
      sentenceFont = kMeaningFontId;
    }
  }

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

  // The sentence follows the deck's own habit: an HSK card wants it in front of
  // you while you read, a vocabulary card wants it kept back with the answer it
  // half gives away. Both used to show it on the question face.
  if (!note_.empty(study::Field::Sentence) && (answer || deck_.meta().sentenceOnQuestion)) {
    // The rule, as the Anki template has it: the word above, the sentence it
    // lives in below. Hairline against the footer's kRule, so the two dividers
    // read as different weights rather than competing.
    y += 20;
    toybox::rule(renderer, y, toybox::kHairline);
    y += 20;
    // Underlined only on the answer face: on the question face the emphasis
    // is over the very word being asked for, and drawing it there points at
    // the answer.
    if (answer) {
      y = drawWrappedUnderlined(sentenceFont, y, maxWidth, note_.field(study::Field::Sentence),
                                note_.emphasisOffset(), note_.emphasisLength());
    } else {
      y = drawWrapped(sentenceFont, y, maxWidth, note_.field(study::Field::Sentence));
    }
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
  std::snprintf(path, sizeof(path), "%s/revlog.dat", deckDir_);
  revlogSource_.reset();
  if (Storage.openFileForRead("STUDY", path, revlogReadFile_)) {
    revlogSource_ = makeUniqueNoThrow<FileSource>(revlogReadFile_);
  }
  if (revlogSource_) {
    study::readStats(*revlogSource_, today_, deck_.meta().collectionCreated, stats_);
  }
}

namespace {
// Used by both the deck model (how much is waiting to send) and the sync
// flow (is a font already the right size), so it lives above both.
// Reviews between two offsets that have not been undone. A voided record is
// struck out in place rather than removed (revlog.dat is append-only), so a
// byte count over-reports by exactly the undos it contains.
// Both callers hold a different handle: the payload builder has the deck's
// ByteSource, the ack writer has a HalFile. One shim keeps a single hash.
inline bool readChunk(study::ByteSource& source, uint32_t at, uint8_t* out, uint32_t len) {
  return source.read(at, out, len);
}
inline bool readChunk(HalFile& file, uint32_t at, uint8_t* out, uint32_t len) {
  if (!file.seekSet(at)) return false;
  return file.read(out, len) == static_cast<int>(len);
}

// FNV-1a over the first `length` bytes. Cheap, allocation-free, and only has
// to distinguish one review log from another -- not resist an adversary.
template <typename Source>
uint64_t hashPrefix(Source& source, const uint32_t length) {
  uint64_t hash = 1469598103934665603ULL;
  uint8_t chunk[256];
  uint32_t at = 0;
  while (at < length) {
    const uint32_t want = (length - at) < sizeof(chunk) ? (length - at) : sizeof(chunk);
    if (!readChunk(source, at, chunk, want)) return 0;
    for (uint32_t i = 0; i < want; ++i) {
      hash ^= chunk[i];
      hash *= 1099511628211ULL;
    }
    at += want;
  }
  return hash == 0 ? 1 : hash;
}

int countLiveRecords(const char* path, const uint32_t from, const uint32_t to) {
  HalFile file;
  if (!Storage.openFileForRead("STUDY", path, file)) return 0;
  int live = 0;
  uint8_t record[study::kRevlogRecordBytes];
  for (uint32_t at = from; at + study::kRevlogRecordBytes <= to; at += study::kRevlogRecordBytes) {
    if (!file.seekSet(at)) break;
    if (file.read(record, sizeof(record)) != static_cast<int>(sizeof(record))) break;
    if ((record[study::kRevlogFlagsOffset] & study::kRevlogVoided) == 0) ++live;
  }
  return live;
}

int64_t localFileSize(const char* path) {
  HalFile file;
  if (!Storage.openFileForRead("STUDY", path, file)) return -1;
  return static_cast<int64_t>(file.size());
}
}  // namespace

// Cards waiting in a deck this reader holds but does not have open. Streams
// cards.dat with the same fixed offsets buildQueue() uses rather than opening
// the deck, so the whole pass is one sequential read and no allocation. All
// decks on a card come from one Anki collection through one bridge, so the
// open deck's day number applies to them too.
int StudyActivity::countWaitingIn(const char* dirName) const {
  char path[96];
  std::snprintf(path, sizeof(path), "%s/%s/cards.dat", kStudyRoot, dirName);
  HalFile file;
  if (!Storage.openFileForRead("STUDY", path, file)) return 0;
  const uint32_t size = static_cast<uint32_t>(file.size());
  const int records = static_cast<int>(size / study::kCardRecordSize);
  int waiting = 0;
  constexpr int kChunk = 64;
  uint8_t buffer[kChunk * study::kCardRecordSize];
  for (int base = 0; base < records; base += kChunk) {
    const int count = (records - base) < kChunk ? (records - base) : kChunk;
    if (!file.seekSet(static_cast<uint32_t>(base) * study::kCardRecordSize)) break;
    if (file.read(buffer, static_cast<size_t>(count) * study::kCardRecordSize) !=
        static_cast<int>(count * study::kCardRecordSize)) {
      break;
    }
    for (int i = 0; i < count; ++i) {
      const uint8_t* record = buffer + i * study::kCardRecordSize;
      const uint8_t state = record[28];
      if (state == static_cast<uint8_t>(study::State::Suspended)) continue;
      if (state == static_cast<uint8_t>(study::State::New)) {
        ++waiting;
        continue;
      }
      int32_t dueDay = 0;
      std::memcpy(&dueDay, record + 16, sizeof(dueDay));
      if (dueDay <= today_) ++waiting;
    }
  }
  return waiting;
}

void StudyActivity::buildDeckModel(studyui::DeckModel& out) const {
  {
    study::BridgeState bridge;
    out.paired = study::loadBridgeState(bridge);
    // How much this reader is holding that AnkiWeb has not seen: revlog.dat
    // past the acked offset, in whole records. Nothing syncs on its own, so
    // without this a finished session sits on the card indefinitely while the
    // screen says DONE and the door says LAST SYNC, and the user reasonably
    // assumes their answers are in Anki.
    int unsent = 0;
    if (out.paired) {
      for (int i = 0; i < deckCount_; ++i) {
        char path[96];
        std::snprintf(path, sizeof(path), "%s/%s/revlog.dat", kStudyRoot, deckNames_[i]);
        const int64_t size = localFileSize(path);
        if (size <= 0) continue;
        const uint32_t acked = bridge.ackFor(deckNames_[i]);
        if (static_cast<uint32_t>(size) > acked) {
          // Only reviews that still stand: an undone one is struck out in
          // place, and counting it made the door promise to send something
          // that no longer exists.
          unsent += countLiveRecords(path, acked, static_cast<uint32_t>(size));
        }
      }
    }
    out.otherWaiting = otherWaiting_;
    if (!out.paired) {
      std::snprintf(out.syncSubtitle, sizeof(out.syncSubtitle), "NOT PAIRED YET");
    } else if (unsent > 0) {
      std::snprintf(out.syncSubtitle, sizeof(out.syncSubtitle), "%d TO SEND", unsent);
    } else if (bridge.lastSyncAt > 0) {
      struct tm parts;
      const time_t at = static_cast<time_t>(bridge.lastSyncAt);
      localtime_r(&at, &parts);
      // A bare clock time reads as "this morning" three days later, so only
      // today gets a clock; anything older gets its date.
      const time_t nowT = time(nullptr);
      struct tm now;
      localtime_r(&nowT, &now);
      if (parts.tm_yday == now.tm_yday && parts.tm_year == now.tm_year) {
        std::snprintf(out.syncSubtitle, sizeof(out.syncSubtitle), "LAST SYNC %02d:%02d", parts.tm_hour, parts.tm_min);
      } else {
        static const char* kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
        std::snprintf(out.syncSubtitle, sizeof(out.syncSubtitle), "LAST SYNC %d %s", parts.tm_mday,
                      kMonths[parts.tm_mon % 12]);
      }
    } else {
      std::snprintf(out.syncSubtitle, sizeof(out.syncSubtitle), "PAIRED");
    }
  }
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
  out.deckIndex = deckIndex_;
  out.deckCount = deckCount_;
  out.sessionOver = (queueCount_ - queuePos_) + learningCount_ == 0;
  out.writeFailed = writeFailed_;
  out.clockUnset = time(nullptr) < study::kClockFloor;
  out.decksOverCap = decksOverCap_;
}

void StudyActivity::routeAction(const fui::ActionEvent& event) {
  if (event.action == studyui::ActionSync) {
    beginSync();
    return;
  }
  if (event.action != studyui::ActionStudy) return;
  if (event.value == 2) {
    beginSync();
    return;
  }
  if (event.value == 3) {
    switchDeck();
    return;
  }
  if (event.value == 4) {
    // Ask for the picker at RUNTIME rather than by clearing the persisted
    // flag. Clearing it wrote the request to the card, so cancelling the
    // picker left it cleared and every later plain SYNC was hijacked into
    // the question, with no way back. A cancel must leave nothing behind.
    pickerRequested_ = true;
    beginSync();
    return;
  }
  // Re-scan rather than resuming a stale queue: a session can end, the user can
  // sit on this screen past the rollover hour, and what was due then is not
  // what is due now.
  buildQueue();
  if (takeNext()) {
    // Reset the summary only when a session truly starts: a tap that finds
    // nothing to review must not wipe the DONE screen it lands back on.
    reviewedThisSession_ = 0;
    againThisSession_ = 0;
    view_ = View::Card;
  }
  requestUpdate();
}

void StudyActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();

#if !defined(FREEINK_NET_WOLFSSL)
  if (previewFlowSet_) {
    fui::GfxRendererTarget target = toybox::makeTarget(renderer);
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
    toybox::Screen screen(frame);
    studyui::buildSyncFlow(screen, previewFlow_);
    renderer.displayBuffer();
    return;
  }
#endif

  if (view_ == View::PairQr) {
    fui::GfxRendererTarget target = toybox::makeTarget(renderer);
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
    toybox::Screen screen(frame);
    const fui::Rect qr = studyui::buildPairQr(screen, pairCode_.c_str());
    QrUtils::drawQrCode(renderer, Rect{qr.x, qr.y, qr.width, qr.height}, study::StudySync::pairUrl(pairCode_));
    const auto labels = mappedInput.mapLabels("Cancel", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (view_ == View::PairConfirm) {
    fui::GfxRendererTarget target = toybox::makeTarget(renderer);
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
    toybox::Screen screen(frame);
    const studyui::PairConfirmLayout layout = studyui::buildPairConfirm(screen);
    // The account name in serif: the toybox cuts are ASCII-subset, and the
    // gate is only a gate if the name is legible whatever alphabet it uses.
    UITheme::drawCenteredWrappedText(
        renderer, Rect{layout.username.x, layout.username.y, layout.username.width, layout.username.height},
        kMeaningFontId, pairUsername_.c_str(), 2);
    confirmX_ = layout.pill.x;
    confirmY_ = layout.pill.y;
    confirmW_ = layout.pill.width;
    confirmH_ = layout.pill.height;
    const auto labels = mappedInput.mapLabels("Cancel", "", "", "Confirm");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (view_ == View::DeckPicker) {
    fui::GfxRendererTarget target = toybox::makeTarget(renderer);
    const fui::InputSnapshot noInput{};
    interactionsReady_ = false;
    toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
    toybox::Screen screen(frame);
    studyui::buildDeckPicker(screen, picker_);
    interactionsReady_ = true;
    toybox::reportOverflow(interactions_, "Study deck picker");
    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (view_ == View::SyncFlow) {
    fui::GfxRendererTarget target = toybox::makeTarget(renderer);
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
    toybox::Screen screen(frame);
    studyui::buildSyncFlow(screen, flow_);
    // Back honesty: before the ack the hint would advertise a leave that the
    // blocking stages cannot honour; from the ack on, and on any verdict,
    // Back is real.
    const bool backLive =
        flow_.verdict != studyui::SyncVerdictKind::None || flow_.safety >= studyui::SyncSafety::ReviewsSafe;
    const auto labels = mappedInput.mapLabels(backLive ? "Back" : "", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (view_ == View::Deck) {
    // Chrome is a FreeInkUI component wearing Toybox, never hand-drawn: Screen
    // substitutes every theme token, and both bugs in this fork's first port
    // were tokens it would have supplied. See docs/design-language.md.
    fui::GfxRendererTarget target = toybox::makeTarget(renderer);
    const fui::InputSnapshot noInput{};
    interactionsReady_ = false;
    toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
    toybox::Screen screen(frame);

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
    // First run. The deck installer is a web page, so this screen's whole job
    // is to put that page one phone-camera-scan away. QR-dominant layout,
    // chosen from three rendered variants (the numbered-steps arrangement
    // collapsed into overlaps on screen; the prose-first one buried the code).
    constexpr const char* kInstallerUrl = "https://crossplay.ma-r-s.com/study";
    // Two ways in, and the screen has to hold both without either crowding the
    // other: the installer QR for a computer, and the sync door for a reader
    // that only has wi-fi. The door is anchored to the bottom and the QR block
    // sized from what is left, so neither can overlap the other.
    noDeckSyncH_ = 56;
    noDeckSyncW_ = static_cast<int16_t>(width - 120);
    noDeckSyncX_ = 60;
    noDeckSyncY_ = static_cast<int16_t>(height - kFooterHeight - noDeckSyncH_ - 24);

    study::BridgeState pairedState;
    const bool paired = study::loadBridgeState(pairedState);
    UITheme::drawCenteredWrappedText(renderer, Rect{0, bodyTop + 16, width, 56}, kReadingFontId,
                                     !paired                  ? "Bring your Anki decks"
                                     : pairedState.choseDecks ? "Get your decks back"
                                                              : "Choose your decks",
                                     1);
    const int16_t qrSide = 208;
    const int16_t qrTop = static_cast<int16_t>(bodyTop + 88);
    // The QR is the from-a-computer route. Once paired, the heading above it
    // asks about choosing decks -- which that page cannot do -- so it would be
    // the largest thing on screen pointing at the wrong answer.
    if (!paired) {
      QrUtils::drawQrCode(renderer, Rect{static_cast<int16_t>((width - qrSide) / 2), qrTop, qrSide, qrSide},
                          kInstallerUrl);
      // Height 40, not 32: drawCenteredWrappedText draws nothing at all when
      // the box is shorter than the font's line box, and it fails silently.
      UITheme::drawCenteredWrappedText(renderer, Rect{0, static_cast<int16_t>(qrTop + qrSide + 8), width, 40},
                                       kSmallFontId, "crossplay.ma-r-s.com/study", 1);
    }
    // Paired, the words are all there is, so they get the QR's room: at 96px
    // this box fitted two lines of a three-line sentence and dropped the rest
    // in silence.
    const int16_t bodyBoxTop = static_cast<int16_t>(paired ? qrTop + 8 : qrTop + qrSide + 48);
    UITheme::drawCenteredWrappedText(
        renderer, Rect{24, bodyBoxTop, width - 48, static_cast<int16_t>(paired ? 200 : 96)}, kMeaningFontId,
        !paired                  ? "Scan it to add a deck from a computer."
        : pairedState.choseDecks ? "Your decks are chosen but not on this card yet."
                                 : "Your Anki account is connected. Pick the decks it keeps.",
        paired ? 4 : 3);

    noDeckPickH_ = paired && pairedState.choseDecks ? noDeckSyncH_ : 0;
    if (noDeckPickH_ > 0) {
      noDeckPickY_ = static_cast<int16_t>(noDeckSyncY_ - noDeckPickH_ - 12);
      renderer.drawRoundedRect(noDeckSyncX_, noDeckPickY_, noDeckSyncW_, noDeckPickH_, toybox::kHairline,
                               noDeckPickH_ / 2, true);
      const char* pick = "CHOOSE OTHER DECKS";
      const int pickWidth = renderer.getTextWidth(toybox::kUiFontId, pick);
      toybox::drawCapsCentered(renderer, toybox::kUiFontId, noDeckSyncX_ + (noDeckSyncW_ - pickWidth) / 2, noDeckPickY_,
                               noDeckPickH_, pick, true);
    }
    renderer.drawRoundedRect(noDeckSyncX_, noDeckSyncY_, noDeckSyncW_, noDeckSyncH_, toybox::kHairline,
                             noDeckSyncH_ / 2, true);
    {
      const char* label = !paired ? "SYNC WITH ANKI" : pairedState.choseDecks ? "GET MY DECKS" : "CHOOSE DECKS";
      const int labelWidth = renderer.getTextWidth(toybox::kUiFontId, label);
      toybox::drawCapsCentered(renderer, toybox::kUiFontId, noDeckSyncX_ + (noDeckSyncW_ - labelWidth) / 2,
                               noDeckSyncY_, noDeckSyncH_, label, true);
    }
  }

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

// ------------------------------------------------------------- the sync flow
//
// Everything below blocks the loop task on purpose (the KOSync pattern): the
// render task paints through requestUpdateAndWait, and the wait loops pump
// input themselves -- the sanctioned exception to the one-pump rule; nothing
// else pumps while they run.

void StudyActivity::drainInput() {
  mappedInput.update();
  (void)mappedInput.wasReleased(MappedInputManager::Button::Back);
  (void)mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  int tapX = 0;
  int tapY = 0;
  (void)mappedInput.wasScreenTapped(tapX, tapY);
}

void StudyActivity::showFlow() {
  view_ = View::SyncFlow;
  requestUpdateAndWait();
}

void StudyActivity::flowStage(const studyui::SyncStage stage, const char* caption) {
  const int at = static_cast<int>(stage);
  for (int i = 0; i < studyui::kSyncStageCount; ++i) {
    flow_.stages[i] = i < at    ? studyui::SyncStageState::Done
                      : i == at ? studyui::SyncStageState::Active
                                : studyui::SyncStageState::Pending;
  }
  std::snprintf(flow_.caption, sizeof(flow_.caption), "%s", caption != nullptr ? caption : "");
  showFlow();
}

void StudyActivity::beginSync() {
  // No SD writes here, deliberately. A wake-tap can land on SYNC before the
  // card's power-up re-init settles, and the first flush through a stale
  // SdFat handle was a LoadProhibited panic (caught on hardware, backtrace
  // folded into onExit by ICF). closeDeck() inside the flow flushes
  // everything through the ordinary path moments later.
  // Ownership means "this app raised the radio", not "this app wants it".
  // Developer Mode (and anything else already associated) leaves Wi-Fi up;
  // Study simply uses it and must not put it down afterwards. Set
  // unconditionally, this flag made every sync tear down a connection it did
  // not own -- which on a dev-mode device drops the flashing route mid-session
  // and looks like a crash. ClockSyncActivity is the same shape.
  const bool alreadyUp = WiFi.status() == WL_CONNECTED;
  if (!alreadyUp) WiFi.mode(WIFI_STA);
  wifiActivated_ = !alreadyUp;
  if (alreadyUp) {
    onSyncWifi(true);
    return;
  }
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onSyncWifi(!result.isCancelled); });
}

void StudyActivity::onSyncWifi(const bool connected) {
  if (!connected) {
    // The same guard the two other ways out of the flow carry. Without it,
    // backing out of the wi-fi list on a card with no decks landed on a deck
    // screen for a deck that does not exist -- ALL CLEAR, 0 CARDS -- and took
    // the pairing QR the user still needs off the screen with it.
    view_ = deckCount_ > 0 ? View::Deck : View::NoDeck;
    requestUpdate();
    return;
  }
  // Paint the busy screen now; the blocking flow starts one loop pass later,
  // so the panel is never blank while the radio settles.
  flow_ = studyui::SyncFlowModel{};
  flow_.stages[0] = studyui::SyncStageState::Active;
  std::snprintf(flow_.caption, sizeof(flow_.caption), "Connecting to the bridge.");
  view_ = View::SyncFlow;
  syncQueued_ = true;
  requestUpdate();
}

void StudyActivity::syncTimeIfNeeded() {
  // Certificate dates are validated on the bridge connection, so the clock
  // must be sane before the first handshake. Same dance as KOSync.
  if (time(nullptr) > study::kClockFloor) return;
  if (esp_sntp_enabled()) esp_sntp_stop();
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();
  for (int retry = 0; retry < 50 && sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED; ++retry) {
    delay(100);
  }
}

void StudyActivity::endSyncSession(const studyui::SyncVerdictKind kind, const studyui::SyncSafety safety,
                                   const char* title, const char* body, const char* whatNow, const bool describeQueue) {
  syncBusy_ = false;
  // Radio down while the user reads the result (on touch boards silentRestart
  // stops SNTP and the radio in place rather than rebooting) -- but only what
  // this app raised. Not ours to put down if Developer Mode brought it up.
  if (wifiActivated_ && !devmode::holdsRadio()) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
  wifiActivated_ = false;
  // The flow closed the deck for heap; put the app back together before the
  // result screen, so Back lands on a live deck.
  if (deckCount_ > 0 && deckDir_[0] == '\0') {
    openDeckAt(deckIndex_);
    beginDeckSession();
  }
  char waiting[64] = "";
  if (describeQueue && deckCount_ > 0) {
    std::snprintf(waiting, sizeof(waiting), "%s",
                  (dueTotal_ + newTotal_ + otherWaiting_) > 0 ? "Your decks are in Study, ready to review."
                                                              : "Your decks are in Study. Nothing is due right now.");
    whatNow = waiting;
  }
  flow_.verdict = kind;
  flow_.safety = safety;
  std::snprintf(flow_.title, sizeof(flow_.title), "%s", title);
  // A transport blip can hand an empty message up; the verdict never shows a
  // blank body (brief, state table row 8).
  std::snprintf(flow_.body, sizeof(flow_.body), "%s",
                body != nullptr && body[0] != '\0' ? body : "The sync service could not be reached.");
  std::snprintf(flow_.whatNow, sizeof(flow_.whatNow), "%s", whatNow != nullptr ? whatNow : "");
  showFlow();
}

bool StudyActivity::runDeckPicker() {
  std::string message;
  // No flowStage() here: the picker opens AFTER a completed pass, and rewinding
  // the ladder to CONNECT erased three finished stages -- which reads as a
  // failed sync retrying itself.
  std::snprintf(flow_.caption, sizeof(flow_.caption), "Reading your Anki decks.");
  showFlow();
  if (!sync_.listDecks(bridge_, deckChoices_, message)) {
    if (sync_.unpaired) {
      bridge_ = study::BridgeState{};
      Storage.remove("/study/.bridge");
      endSyncSession(studyui::SyncVerdictKind::Error, studyui::SyncSafety::NothingSent, "NOT PAIRED", message.c_str(),
                     "Pair it again to keep syncing.");
      return false;
    }
    endSyncSession(studyui::SyncVerdictKind::Error, studyui::SyncSafety::NothingSent, "NOT SYNCED", message.c_str(),
                   "The service may be busy. Try again in a few minutes.");
    return false;
  }
  if (deckChoices_.empty()) {
    endSyncSession(studyui::SyncVerdictKind::Neutral, studyui::SyncSafety::NothingSent, "NO DECKS",
                   "This Anki account has no decks yet. Add one in Anki, then sync.");
    return false;
  }

  pickerRows_.clear();
  pickerRows_.reserve(deckChoices_.size());
  for (const auto& choice : deckChoices_) {
    studyui::DeckPickerModel::Row row;
    row.name = choice.name.c_str();
    row.cards = choice.cards;
    row.chosen = choice.chosen;
    pickerRows_.push_back(row);
  }
  picker_ = studyui::DeckPickerModel{};
  picker_.rows = pickerRows_.data();
  picker_.count = static_cast<int>(pickerRows_.size());
  picker_.maxChosen = study::kMaxChosenDecks;
  for (const auto& row : pickerRows_) picker_.chosenCount += row.chosen ? 1 : 0;
  picker_.atCap = picker_.chosenCount >= picker_.maxChosen;
  picker_.withheld = sync_.decksWithheld;

  LOG_INF("STUDYSYNC", "picker: %d decks offered, %d already chosen", picker_.count, picker_.chosenCount);
  view_ = View::DeckPicker;
  requestUpdateAndWait();
  drainInput();

  for (;;) {
    delay(50);
    mappedInput.update();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) {
      // Nothing died here: the pass completed and the user stopped at the
      // question afterwards, so no stage should wear the "died here" mark.
      for (int i = 0; i < studyui::kSyncStageCount; ++i) {
        if (flow_.stages[i] == studyui::SyncStageState::Active) flow_.stages[i] = studyui::SyncStageState::Done;
      }
      // Not NothingSent: a full pass has already run and its acks are
      // durable, so claiming nothing was sent is false in the reassuring
      // direction -- the one direction this line must never be wrong in.
      endSyncSession(studyui::SyncVerdictKind::Neutral, flow_.safety, "STOPPED",
                     deckCount_ > 0 ? "Your decks are unchanged. Change them any time from DECKS FROM ANKI."
                                    : "Nothing was changed. Choose your decks whenever you are ready.");
      return false;
    }
    int tapX = 0;
    int tapY = 0;
    if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady_) continue;

    fui::InputSnapshot input;
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
    const fui::ActionEvent event = interactions_.route(input);
    if (event.action == studyui::ActionPickDeck) {
      if (event.value < 0) {
        // Page: the list shows what fits and the pager moves the window.
        picker_.topIndex += picker_.visibleRows;
        if (picker_.topIndex >= picker_.count) picker_.topIndex = 0;
      } else {
        studyui::DeckPickerModel::Row& row = pickerRows_[event.value];
        if (!row.chosen && picker_.chosenCount >= picker_.maxChosen) {
          picker_.atCap = true;  // say why nothing happened
        } else {
          row.chosen = !row.chosen;
          picker_.chosenCount += row.chosen ? 1 : -1;
          picker_.atCap = picker_.chosenCount >= picker_.maxChosen;
        }
      }
      requestUpdateAndWait();
      continue;
    }
    if (event.action == studyui::ActionPickDone && picker_.chosenCount > 0) break;
  }

  std::vector<std::string> chosen;
  for (size_t i = 0; i < pickerRows_.size(); ++i) {
    if (pickerRows_[i].chosen) chosen.push_back(deckChoices_[i].name);
  }
  LOG_INF("STUDYSYNC", "picker: chose %d deck(s)", static_cast<int>(chosen.size()));
  flowStage(studyui::SyncStage::Connect, "Saving your choice.");
  if (!sync_.chooseDecks(bridge_, chosen, message)) {
    endSyncSession(studyui::SyncVerdictKind::Error, studyui::SyncSafety::NothingSent, "NOT SYNCED", message.c_str(),
                   "The service may be busy. Try again in a few minutes.");
    return false;
  }
  return true;
}

bool StudyActivity::runPairing() {
  std::string message;
  study::StudySync::PairStart pair;
  LOG_INF("STUDYSYNC", "flow: pairing");
  flowStage(studyui::SyncStage::Connect, "Getting a pairing code.");
  if (!sync_.pairStart(pair, message)) {
    endSyncSession(studyui::SyncVerdictKind::Error, studyui::SyncSafety::NothingSent, "NOT PAIRED", message.c_str(),
                   "Try again when you are ready.");
    return false;
  }
  pairCode_ = pair.code;
  {
    view_ = View::PairQr;
  }
  requestUpdateAndWait();
  drainInput();

  std::string username;
  std::string token;
  uint32_t lastPoll = 0;
  for (;;) {
    delay(100);
    mappedInput.update();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) {
      sync_.pairAbandon(pair.pollToken, "");
      endSyncSession(studyui::SyncVerdictKind::Neutral, studyui::SyncSafety::NothingSent, "NOT PAIRED",
                     "Pairing stopped. Nothing was stored.");
      return false;
    }
    if (millis() - lastPoll >= 3000) {
      lastPoll = millis();
      const int result = sync_.pairPoll(pair.pollToken, username, token, message);
      if (result < 0) {
        endSyncSession(studyui::SyncVerdictKind::Error, studyui::SyncSafety::NothingSent, "NOT PAIRED", message.c_str(),
                       "Try again when you are ready.");
        return false;
      }
      if (result == 1) break;
    }
  }

  // The anti-hijack gate: the device names the account it is about to belong
  // to, and only a press on THIS hardware stores the token.
  pairUsername_ = username;
  {
    view_ = View::PairConfirm;
  }
  requestUpdateAndWait();
  // Without this drain, the Confirm release that picked the WiFi network two
  // screens ago sits latched until its first read -- which was here, and it
  // walked straight through the one gate that exists to need a human. Found
  // in the simulator: SYNCED appeared before any confirm tap was scheduled.
  drainInput();
  for (;;) {
    delay(50);
    mappedInput.update();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      // poll() already registered the token on the bridge; a declined
      // confirm revokes it, or a ghost device row outlives this screen.
      sync_.pairAbandon("", token);
      endSyncSession(studyui::SyncVerdictKind::Neutral, studyui::SyncSafety::NothingSent, "NOT PAIRED",
                     "Pairing cancelled. Nothing was stored.");
      return false;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      LOG_INF("STUDYSYNC", "confirm gate: Confirm button");
      break;
    }
    int tapX = 0;
    int tapY = 0;
    if (mappedInput.wasScreenTapped(tapX, tapY)) {
      LOG_INF("STUDYSYNC", "confirm gate: tap %d,%d (rect %d,%d %dx%d)", tapX, tapY, confirmX_, confirmY_, confirmW_,
              confirmH_);
      if (tapX >= confirmX_ && tapX < confirmX_ + confirmW_ && tapY >= confirmY_ && tapY < confirmY_ + confirmH_) {
        break;
      }
    }
  }
  bridge_.token = token;
  bridge_.paired = true;
  if (!study::saveBridgeState(bridge_)) {
    sync_.pairAbandon("", token);
    endSyncSession(studyui::SyncVerdictKind::Error, studyui::SyncSafety::NothingSent, "NOT PAIRED",
                   "Could not save the pairing to the card.");
    return false;
  }
  return true;
}

bool StudyActivity::buildPayloads(std::vector<study::DeckPayload>& out) {
  for (int i = 0; i < deckCount_; ++i) {
    char path[96];
    study::DeckPayload payload;
    payload.dirName = deckNames_[i];

    std::snprintf(path, sizeof(path), "%s/%s/revlog.dat", kStudyRoot, deckNames_[i]);
    HalFile revlog;
    if (Storage.openFileForRead("STUDYSYNC", path, revlog)) {
      const uint32_t size = revlog.size();
      uint32_t ack = bridge_.ackFor(deckNames_[i]);
      // An offset is only meaningful against the file it was measured on.
      // A bare `ack > size` check misses the case that matters: delete a deck
      // folder (which the app's own repair path invites), let it re-download
      // WITHOUT its review log, and a fresh revlog grows back to exactly or
      // past the old offset -- after which every review before that offset is
      // skipped for good and the reader still reports SYNCED. Tag the file by
      // its first record and resend everything when the tag changes.
      // Verify the bytes the ack claims were already sent, rather than
      // trusting the number alone. An offset cannot tell a log that grew from
      // one that was replaced, rolled back to an older copy, or left
      // half-updated by a failed sync; a checksum of [0, ack) can, and it
      // collapses the whole family of "the marker and the file disagree"
      // failures into one harmless resend that the service dedupes.
      const uint64_t knownHash = bridge_.ackHashFor(deckNames_[i]);
      if (ack > size) {
        LOG_INF("STUDYSYNC", "%s: the log is shorter than the ack; resending the whole log", deckNames_[i]);
        ack = 0;
      } else if (ack > 0) {
        const uint64_t actual = hashPrefix(revlog, ack);
        if (knownHash == 0 || actual != knownHash) {
          LOG_INF("STUDYSYNC", "%s: the sent-so-far bytes do not match; resending the whole log", deckNames_[i]);
          ack = 0;
        }
      }
      payload.revlogOffset = ack;
      if (size > ack) {
        payload.revlogTail.resize(size - ack);
        revlog.seek(ack);
        if (revlog.read(reinterpret_cast<uint8_t*>(payload.revlogTail.data()), payload.revlogTail.size()) !=
            static_cast<int>(payload.revlogTail.size())) {
          return false;
        }
      }
    } else {
      // No review log on the card at all: the deck folder was removed and
      // came back without one (a deck download never carries a review log).
      // The stored offset now points into a file that no longer exists, and
      // leaving it meant the reviews written into the NEXT log were skipped
      // whenever it grew back to the same length -- which it does, because
      // the first card offered after a restore is usually the same most-due
      // card, so even the first-record tag matched. Forget both here, where
      // the absence is known.
      payload.revlogOffset = 0;
      if (bridge_.ackFor(deckNames_[i]) != 0 || bridge_.ackHashFor(deckNames_[i]) != 0) {
        LOG_INF("STUDYSYNC", "%s: review log is gone; forgetting its ack", deckNames_[i]);
        bridge_.setAck(deckNames_[i], 0);
        bridge_.setAckHash(deckNames_[i], 0);
        study::saveBridgeState(bridge_);
      }
    }

    std::snprintf(path, sizeof(path), "%s/%s/cards.dat", kStudyRoot, deckNames_[i]);
    HalFile cards;
    if (Storage.openFileForRead("STUDYSYNC", path, cards)) {
      payload.cards.resize(cards.size());
      if (cards.read(reinterpret_cast<uint8_t*>(payload.cards.data()), payload.cards.size()) !=
          static_cast<int>(payload.cards.size())) {
        return false;
      }
    }
    out.push_back(std::move(payload));
  }
  return true;
}

bool StudyActivity::applyManifests(const std::vector<study::DeckManifest>& manifests, std::string& message,
                                   int& decksUpdated) {
  decksUpdated = 0;
  for (const auto& deck : manifests) {
    char dir[96];
    std::snprintf(dir, sizeof(dir), "%s/%s", kStudyRoot, deck.slug.c_str());
    // A repeated buildId means the card already holds this exact build --
    // but only if it still does. Trusting the cache blind meant a deck the
    // user deleted (or a download that never finished) was never fetched
    // again, while every sync went on reporting DECKS UP TO DATE forever.
    char cardsPath[128];
    std::snprintf(cardsPath, sizeof(cardsPath), "%s/cards.dat", dir);
    const bool onCard = Storage.exists(dir) && Storage.exists(cardsPath);
    if (onCard && deck.buildId == bridge_.buildFor(deck.slug.c_str())) continue;
    if (!Storage.exists(dir)) Storage.mkdir(dir);

    // Two passes: every needed file lands as .part first, then the batch
    // renames together. deck.dat and cards.dat are index-aligned; a torn pair
    // would show the wrong scheduling under the right cards.
    std::vector<std::pair<std::string, std::string>> renames;  // part -> final
    int fetched = 0;
    for (const auto& file : deck.files) {
      // The card's own review log is never downloaded: it is the device's
      // append-only truth and the ack offsets point into it. The glyph lists
      // are font-pipeline inputs the device never reads.
      if (file.path == "revlog.dat" || file.path.rfind("glyphs-", 0) == 0 || file.path[0] == '.') continue;
      const std::string finalPath = std::string(dir) + "/" + file.path;
      // Fonts are big and immutable per build; one with the manifest's exact
      // size is taken as current rather than re-downloaded.
      if (file.path.rfind("fonts/", 0) == 0 && localFileSize(finalPath.c_str()) == static_cast<int64_t>(file.size)) {
        continue;
      }
      const size_t slash = finalPath.find_last_of('/');
      const std::string parent = finalPath.substr(0, slash);
      if (!Storage.exists(parent.c_str())) Storage.mkdir(parent.c_str());

      std::snprintf(flow_.caption, sizeof(flow_.caption), "Fetching %s.", deck.slug.c_str());
      std::snprintf(flow_.facts[static_cast<int>(studyui::SyncStage::Download)], sizeof(flow_.facts[0]), "%d IN",
                    fetched);
      showFlow();
      const std::string part = finalPath + ".part";
      if (!sync_.downloadToPart(bridge_, deck, file, part, nullptr, message)) {
        for (const auto& r : renames) Storage.remove(r.first.c_str());
        return false;
      }
      renames.emplace_back(part, finalPath);
      ++fetched;
      mappedInput.update();
      if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) {
        for (const auto& r : renames) Storage.remove(r.first.c_str());
        message = "Stopped. The decks on the card are unchanged; sync again to finish.";
        return false;
      }
    }
    for (const auto& r : renames) {
      Storage.remove(r.second.c_str());
      Storage.rename(r.first.c_str(), r.second.c_str());
    }
    bridge_.setBuild(deck.slug.c_str(), deck.buildId.c_str());
    study::saveBridgeState(bridge_);
    ++decksUpdated;
  }
  // The honest DOWNLOAD fact: on a routine sync every buildId matched and
  // nothing ran; the stage completes as "up to date", not as theatre.
  if (decksUpdated == 0) {
    std::snprintf(flow_.facts[static_cast<int>(studyui::SyncStage::Download)], sizeof(flow_.facts[0]), "UP TO DATE");
  }
  return true;
}

void StudyActivity::runSyncFlow() {
  syncBusy_ = true;
  LOG_INF("STUDYSYNC", "flow: ntp");
  syncTimeIfNeeded();
  LOG_INF("STUDYSYNC", "flow: state");
  const bool freshPairing = !study::loadBridgeState(bridge_);
  // Forget any marker whose deck is not on the card. An ack is a byte offset
  // into a review log; if the folder is gone the log is gone, and the offset
  // now points into whatever the next sync downloads. Doing it here rather
  // than in buildPayloads is what makes it deterministic: the losing case is
  // a deck RESTORED by this very sync, which buildPayloads never sees because
  // it only walks decks already present.
  {
    bool forgot = false;
    for (int i = 0; i < bridge_.deckCount; ++i) {
      if (bridge_.ackOffsets[i] == 0 && bridge_.ackHashes[i] == 0) continue;
      // Keyed on what this reader can OPEN, not on what is present. Nothing
      // deletes a deck folder, so a card accumulates them, and the openable
      // set is the alphabetically-first kMaxDecks of a growing list: a deck
      // can fall out of that window while still sitting on the card, and its
      // slot was then held forever against a deck the reader actually uses.
      bool openable = false;
      for (int j = 0; j < deckCount_ && !openable; ++j) {
        openable = std::strcmp(deckNames_[j], bridge_.deckDirs[i]) == 0;
      }
      if (openable) continue;
      LOG_INF("STUDYSYNC", "%s: not open to this reader; releasing its slot", bridge_.deckDirs[i]);
      // Release the whole slot, not just the marker. There are kMaxSyncDecks
      // of them, one per openable deck, and
      // every setter silently no-ops when they are full, so decks left
      // behind by two changes of mind cost the current decks their build id
      // and their ack: a full re-download and a full revlog resend on every
      // sync, with nothing on screen to explain the wait.
      for (int j = i + 1; j < bridge_.deckCount; ++j) {
        std::snprintf(bridge_.deckDirs[j - 1], sizeof(bridge_.deckDirs[0]), "%s", bridge_.deckDirs[j]);
        std::snprintf(bridge_.lastBuilds[j - 1], sizeof(bridge_.lastBuilds[0]), "%s", bridge_.lastBuilds[j]);
        bridge_.ackOffsets[j - 1] = bridge_.ackOffsets[j];
        bridge_.ackHashes[j - 1] = bridge_.ackHashes[j];
      }
      --bridge_.deckCount;
      --i;  // the slot now holds the next deck
      forgot = true;
    }
    if (forgot) study::saveBridgeState(bridge_);
  }
  if (freshPairing) {
    if (!runPairing()) return;  // runPairing already ended the session
  }
  // An explicit "choose again" is answered first, before any pass that can
  // fail. It used to be read only after the build, so a chosen deck the
  // bridge could not build -- an empty one, one renamed
  // away in Anki -- ended the sync in an error before the request was ever
  // seen, and the door that sets the request was the only way out of that
  // state. The mirror this needs already exists: choseDecks means a cycle
  // has run.
  if (pickerRequested_ && bridge_.choseDecks) {
    if (!runDeckPicker()) {
      pickerRequested_ = false;  // a cancel leaves no request behind
      return;                    // runDeckPicker already ended the session
    }
    pickerRequested_ = false;
    secondPass_ = true;  // what follows fetches what was just chosen
  }
  // A reader that has never chosen decks would sync forever against an empty
  // manifest: the bridge builds only chosen decks and a new account has none.
  // Ask on the first sync after pairing, and any time the account still has
  // nothing chosen.
  // ...but it cannot be asked yet on a FIRST sync. The bridge only mirrors
  // the collection during a sync cycle, so before one has run its deck list
  // is empty and the question would offer nothing. The order that works is:
  // sync once (which mirrors), then ask, then sync again to build what was
  // chosen. runSyncPass() below is that pass, and it runs twice at most.

  flowStage(studyui::SyncStage::Send, secondPass_ ? "Fetching the decks you chose." : "Packing this card's reviews.");
  closeDeck();  // frees the fonts and file handles; TLS wants the heap
  std::vector<study::DeckPayload> payloads;
  if (!buildPayloads(payloads)) {
    endSyncSession(studyui::SyncVerdictKind::Error, studyui::SyncSafety::NothingSent, "NOT SYNCED",
                   "Could not read the card. Nothing was sent.");
    return;
  }
  // Count what will actually land: the tail carries undone reviews too, and
  // the bridge drops them, so a byte count made the verdict claim it had sent
  // reviews the server correctly refused.
  int reviewCount = 0;
  for (const auto& payload : payloads) {
    for (size_t at = 0; at + study::kRevlogRecordBytes <= payload.revlogTail.size(); at += study::kRevlogRecordBytes) {
      const uint8_t flags = static_cast<uint8_t>(payload.revlogTail[at + study::kRevlogFlagsOffset]);
      if ((flags & study::kRevlogVoided) == 0) ++reviewCount;
    }
  }

  std::string job;
  std::string message;
  std::vector<std::pair<std::string, uint32_t>> acks;
  flowStage(studyui::SyncStage::Send, "Sending your reviews.");
  if (!sync_.syncStart(bridge_, payloads, job, acks, message)) {
    if (sync_.unpaired) {
      // The token was revoked on the bridge. Clear it, or this refusal
      // repeats forever; the next SYNC walks through pairing again.
      bridge_ = study::BridgeState{};
      Storage.remove("/study/.bridge");
      endSyncSession(studyui::SyncVerdictKind::Error, studyui::SyncSafety::NothingSent, "NOT PAIRED",
                     "This reader was unpaired on the bridge.", "Pair it again to keep syncing.");
      return;
    }
    endSyncSession(studyui::SyncVerdictKind::Error, studyui::SyncSafety::NothingSent, "NOT SYNCED", message.c_str());
    return;
  }
  payloads.clear();
  payloads.shrink_to_fit();
  // The ack is valid the moment the POST answered: the reviews are durable in
  // the bridge's journal even if everything after this fails.
  for (const auto& ack : acks) {
    bridge_.setAck(ack.first.c_str(), ack.second);
    // Record what those bytes were, so the next sync can tell this log from a
    // different one that happens to be the same length.
    char logPath[96];
    std::snprintf(logPath, sizeof(logPath), "%s/%s/revlog.dat", kStudyRoot, ack.first.c_str());
    HalFile file;
    uint64_t hash = 0;
    if (ack.second > 0 && Storage.openFileForRead("STUDY", logPath, file)) {
      hash = hashPrefix(file, ack.second);
    }
    bridge_.setAckHash(ack.first.c_str(), hash);
  }
  study::saveBridgeState(bridge_);
  // The bridge owns the job from here, so leaving is safe even on a first
  // sync with no reviews to send; the reviews line is added on top only when
  // there were reviews. leaveSafe was declared and read but never assigned,
  // which removed the footer from every screen instead of adding it to more.
  flow_.leaveSafe = true;
  if (reviewCount > 0) flow_.safety = studyui::SyncSafety::ReviewsSafe;
  if (reviewCount > 0) {
    std::snprintf(flow_.facts[static_cast<int>(studyui::SyncStage::Send)], sizeof(flow_.facts[0]), "%d SENT",
                  reviewCount);
  } else {
    std::snprintf(flow_.facts[static_cast<int>(studyui::SyncStage::Send)], sizeof(flow_.facts[0]), "NONE NEW");
  }
  flowStage(studyui::SyncStage::Build, "Your decks are being built on the sync service.");

  const uint32_t started = millis();
  bool preparing = false;
  int transportBlips = 0;
  std::vector<study::DeckManifest> manifests;
  for (;;) {
    bool leave = false;
    for (int i = 0; i < 30; ++i) {
      delay(100);
      mappedInput.update();
      if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) {
        leave = true;
        break;
      }
    }
    if (leave) {
      endSyncSession(studyui::SyncVerdictKind::Neutral,
                     reviewCount > 0 ? studyui::SyncSafety::ReviewsSafe : studyui::SyncSafety::None, "STOPPED",
                     reviewCount > 0
                         ? "Your reviews are safely sent. The sync keeps running; sync again for the updated decks."
                         : "The sync keeps running. Sync again later to pick up the decks.");
      return;
    }
    manifests.clear();
    message.clear();
    const std::string status = sync_.syncStatus(bridge_, job, manifests, message);
    if (status == "done") break;
    if (status == "error" || status == "frozen") {
      endSyncSession(studyui::SyncVerdictKind::Error, studyui::SyncSafety::ReviewsSafe, "NOT SYNCED", message.c_str(),
                     "The service may be busy. Try again in a few minutes.");
      return;
    }
    if (status.empty() && ++transportBlips >= 5) {
      endSyncSession(studyui::SyncVerdictKind::Error, studyui::SyncSafety::ReviewsSafe, "NOT SYNCED", message.c_str(),
                     "The service may be busy. Try again in a few minutes.");
      return;
    }
    if (!status.empty()) transportBlips = 0;
    const uint32_t elapsed = (millis() - started) / 1000;
    // "%um%02us". Sized for what the format can PRINT, not for the range the
    // arguments happen to have: "%02u" is a minimum width of two, not a
    // maximum, and an argument range argued in a comment is not a bound. The
    // first attempt made this 15 on exactly that reasoning and host-tests/
    // fmtwidth rejected it -- correctly. The field it is copied into is what
    // moved instead.
    constexpr int kClockChars =
        toybox::kUIntChars + toybox::literalChars("m") + toybox::kUIntChars + toybox::literalChars("s") + 1;
    static_assert(kClockChars <= studyui::SyncFlowModel::kFactChars,
                  "the clock has to fit the stage fact it is copied into");
    char clock[kClockChars];
    if (elapsed < 60) {
      std::snprintf(clock, sizeof(clock), "%us", static_cast<unsigned>(elapsed / 5 * 5));
    } else {
      std::snprintf(clock, sizeof(clock), "%um%02us", static_cast<unsigned>(elapsed / 60),
                    static_cast<unsigned>((elapsed % 60) / 5 * 5));
    }
    char* buildFact = flow_.facts[static_cast<int>(studyui::SyncStage::Build)];
    if (std::strcmp(buildFact, clock) != 0) {
      std::snprintf(buildFact, sizeof(flow_.facts[0]), "%s", clock);
      showFlow();
    }
    if (!preparing && millis() - started > 30000) {
      // The first sync of a big collection is minutes, not seconds. Say so
      // once, and make leaving safe -- the job keeps running on the bridge.
      preparing = true;
      // Three lines at the caption width; the longer sentence overflowed on
      // hardware and the renderer's own "..." has no glyph in this cut. The
      // leave story is the safety footer's job anyway.
      std::snprintf(flow_.caption, sizeof(flow_.caption),
                    "This first time can take a while. It keeps working if you leave.");
      showFlow();
    }
  }

  flowStage(studyui::SyncStage::Download, nullptr);
  if (reviewCount > 0) flow_.safety = studyui::SyncSafety::ReviewsSafe;
  int decksUpdated = 0;
  if (!applyManifests(manifests, message, decksUpdated)) {
    const studyui::SyncSafety safety =
        decksUpdated > 0 ? studyui::SyncSafety::ReviewsSafePartialDecks : studyui::SyncSafety::ReviewsSafe;
    const bool stopped = message.rfind("Stopped.", 0) == 0;
    endSyncSession(stopped ? studyui::SyncVerdictKind::Neutral : studyui::SyncVerdictKind::Error, safety,
                   stopped ? "STOPPED" : "NOT SYNCED", message.c_str(),
                   stopped ? "Sync again to finish." : "The service may be busy. Try again in a few minutes.");
    return;
  }
  bridge_.lastSyncAt = static_cast<int64_t>(time(nullptr));
  study::saveBridgeState(bridge_);
  findDeckDirs();  // the bridge may have delivered a deck this card never had
  if (!sync_.failedDecks.empty()) {
    // The rest of the sync worked, so the reviews are away and the other
    // decks are current; saying SYNCED anyway would send the user looking
    // for a deck that was never built. The converter refuses a deck with
    // no cards of its own, and a deck renamed on the desktop side is simply
    // gone.
    char detail[192];
    const size_t failed = sync_.failedDecks.size();
    const bool others = decksUpdated > 0 || deckCount_ > static_cast<int>(failed);
    if (failed == 1) {
      std::snprintf(detail, sizeof(detail), "%s could not be built.%s", sync_.failedDecks.front().c_str(),
                    others ? " Everything else is up to date." : "");
    } else {
      std::snprintf(detail, sizeof(detail), "%u decks could not be built, starting with %s.%s",
                    static_cast<unsigned>(failed), sync_.failedDecks.front().c_str(),
                    others ? " Everything else is up to date." : "");
    }
    // The dropped-review count belongs here most of all: a deck deleted on
    // the desktop fails to build AND leaves every review of it with no card
    // to land on, so this verdict is the one that hides the biggest loss.
    flow_.factCount = 0;
    if (sync_.reviewsMissing > 0) {
      std::snprintf(flow_.factLines[flow_.factCount++], sizeof(flow_.factLines[0]), "%d SENT, %d HAD NO CARD IN ANKI",
                    reviewCount, sync_.reviewsMissing);
    } else if (reviewCount > 0) {
      std::snprintf(flow_.factLines[flow_.factCount++], sizeof(flow_.factLines[0]), "%d REVIEW%s SENT", reviewCount,
                    reviewCount == 1 ? "" : "S");
    }
    endSyncSession(studyui::SyncVerdictKind::Neutral, studyui::SyncSafety::ReviewsSafePartialDecks, "PART WAY", detail,
                   "Choose your decks again to drop it, or fix it in Anki.");
    return;
  }

  // The mirror exists now, so the question can finally be answered. Ask once,
  // then run the flow again: this second pass is the one that builds and
  // downloads what was chosen. Without the re-run the user would choose decks
  // and be told SYNCED with nothing on the card.
  if (!bridge_.choseDecks || pickerRequested_) {
    if (!runDeckPicker()) {
      pickerRequested_ = false;  // a cancel leaves no request behind
      return;                    // runDeckPicker already ended the session
    }
    pickerRequested_ = false;
    bridge_.choseDecks = true;
    study::saveBridgeState(bridge_);
    syncBusy_ = false;
    // The second pass is the one that fetches what was just chosen. Say so,
    // or four filled bars emptying and starting again reads as a retry after
    // a failure.
    secondPass_ = true;
    runSyncFlow();
    return;
  }
  secondPass_ = false;

  flow_.factCount = 0;
  if (reviewCount > 0) {
    // Corrected in place rather than added as a fourth line: there is room for
    // three, and the line that needs fixing is this one. "40 SENT" beside a
    // silent drop of 3 is the claim that misleads.
    if (sync_.reviewsMissing > 0) {
      std::snprintf(flow_.factLines[flow_.factCount++], sizeof(flow_.factLines[0]), "%d SENT, %d HAD NO CARD IN ANKI",
                    reviewCount, sync_.reviewsMissing);
    } else {
      std::snprintf(flow_.factLines[flow_.factCount++], sizeof(flow_.factLines[0]), "%d REVIEW%s SENT", reviewCount,
                    reviewCount == 1 ? "" : "S");
    }
  }
  if (decksUpdated > 0) {
    // Sending reviews moves the deck's fingerprint, so the bridge rebuilds it
    // and the reader fetches it back. Calling that UPDATED made every
    // review-only sync announce a change the user had not made.
    std::snprintf(flow_.factLines[flow_.factCount++], sizeof(flow_.factLines[0]),
                  reviewCount > 0 ? "%d DECK%s REBUILT WITH YOUR ANSWERS" : "%d DECK%s UPDATED", decksUpdated,
                  decksUpdated == 1 ? "" : "S");
  } else {
    std::snprintf(flow_.factLines[flow_.factCount++], sizeof(flow_.factLines[0]), "DECKS UP TO DATE");
  }
  struct tm local;
  const time_t now = time(nullptr);
  localtime_r(&now, &local);
  std::snprintf(flow_.factLines[flow_.factCount++], sizeof(flow_.factLines[0]), "LAST SYNC %02d:%02d", local.tm_hour,
                local.tm_min);
  endSyncSession(studyui::SyncVerdictKind::Success,
                 reviewCount > 0 ? studyui::SyncSafety::ReviewsSafe : studyui::SyncSafety::None, "SYNCED",
                 "This reader and your Anki are up to date.", nullptr, /*describeQueue=*/true);
}
