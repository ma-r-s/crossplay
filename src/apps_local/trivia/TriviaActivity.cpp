#include "TriviaActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../../activities/network/WifiSelectionActivity.h"
#include "../../components/UITheme.h"
#include "../../network/HttpDownloader.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* kDir = "/trivia";
constexpr const char* kPackPath = "/trivia/pack.dat";
constexpr const char* kPartPath = "/trivia/pack.dat.part";
// A rolling PRERELEASE, so the OTA's releases/latest can never see it and a
// 6MB question pack never lands in a firmware update. Same arrangement as the
// xkcd archive; see docs/apps/trivia-pack-format.md.
constexpr const char* kPackUrl = "https://github.com/ma-r-s/crossplay/releases/download/trivia-pack/pack.dat";
constexpr const char* kStatePath = "/trivia/pack.state";
// One byte: the difficulty filter. It lived only in the activity, which is
// deleted on exit, so leaving Trivia and coming back silently put a player who
// had chosen Level 4 back on "Any" with nothing on screen to say so.
constexpr const char* kPrefsPath = "/trivia/prefs";

// A ByteSource over a HalFile. Every read seeks first: an index entry and the
// record it points at are in different places, so sequential reads are the
// exception rather than the rule.
class FileSource final : public trivia::WritableByteSource {
 public:
  void attach(HalFile& file) {
    file_ = &file;
    size_ = static_cast<uint32_t>(file.size());
  }
  bool read(const uint32_t offset, void* dst, const uint32_t length) override {
    if (length == 0) return true;
    if (file_ == nullptr || offset > size_ || offset + length > size_) return false;
    if (!file_->seekSet(offset)) return false;
    return file_->read(dst, length) == static_cast<int>(length);
  }
  bool write(const uint32_t offset, const void* src, const uint32_t length) override {
    if (length == 0) return true;
    if (file_ == nullptr || offset + length > size_) return false;  // state never grows
    if (!file_->seekSet(offset)) return false;
    return file_->write(static_cast<const uint8_t*>(src), length) == length;
  }
  bool flush() override {
    if (file_ != nullptr) file_->flush();
    return true;
  }
  uint32_t size() const override { return size_; }

 private:
  HalFile* file_ = nullptr;
  uint32_t size_ = 0;
};

HalFile g_packFile;
HalFile g_stateFile;
FileSource g_packSource;
FileSource g_stateSource;

}  // namespace

std::unique_ptr<Activity> TriviaActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<TriviaActivity>(renderer, mappedInput);
}

bool TriviaActivity::ensureState(const uint32_t count) {
  g_stateFile = Storage.open(kStatePath, O_RDWR);
  if (!g_stateFile.isOpen() || static_cast<uint32_t>(g_stateFile.size()) != count) {
    // Missing, or a DIFFERENT length from the pack because the pack was
    // replaced under it. Rewriting loses which questions have been seen, which
    // is the right trade: reading a stale byte for a question it does not
    // describe is worse.
    //
    // `!=`, not `<`. A pack that SHRANK left a longer state file, size() >= count
    // held, and every byte was reused against a pack whose record order had
    // changed -- so SEEN bits deprioritised arbitrary questions and, worse,
    // FLAGGED bits made arbitrary questions permanently unservable with no way
    // for the player to clear them. Both this call site and PackState::open
    // check the length; the guard is at the boundary as well as at the caller
    // because this is the only caller today and will not be the last.
    HalFile fresh;
    if (!Storage.openFileForWrite("TRIVIA", kStatePath, fresh)) {
      LOG_ERR("TRIVIA", "Cannot create %s", kStatePath);
      return false;
    }
    uint8_t zeros[256] = {};
    for (uint32_t written = 0; written < count;) {
      const uint32_t chunk = (count - written) < sizeof(zeros) ? (count - written) : sizeof(zeros);
      if (fresh.write(zeros, chunk) != chunk) {
        LOG_ERR("TRIVIA", "Short write creating %s", kStatePath);
        return false;
      }
      written += chunk;
    }
    fresh.flush();
    fresh.close();
    g_stateFile = Storage.open(kStatePath, O_RDWR);
  }
  if (!g_stateFile.isOpen()) return false;
  g_stateSource.attach(g_stateFile);
  return state_.open(g_stateSource, count);
}

bool TriviaActivity::openPack() {
  if (!Storage.openFileForRead("TRIVIA", kPackPath, g_packFile)) {
    LOG_ERR("TRIVIA", "No pack at %s", kPackPath);
    return false;
  }
  g_packSource.attach(g_packFile);
  if (!pack_.open(g_packSource)) {
    LOG_ERR("TRIVIA", "%s is not a readable trivia pack", kPackPath);
    return false;
  }
  if (!ensureState(pack_.count())) return false;
  chooser_.begin(pack_, state_, rng_);
  return true;
}

namespace {

int loadDifficulty() {
  HalFile f = Storage.open(kPrefsPath, O_RDONLY);
  if (!f.isOpen() || f.size() < 1) return 0;
  uint8_t b = 0;
  if (f.read(&b, 1) != 1) return 0;
  // A file written by a future build, or a corrupt byte, must not select a
  // difficulty that filters every question out of the pack.
  return (b <= trivia::kDifficulties) ? static_cast<int>(b) : 0;
}

void saveDifficulty(const int difficulty) {
  HalFile f;
  if (!Storage.openFileForWrite("TRIVIA", kPrefsPath, f)) return;
  const uint8_t b = static_cast<uint8_t>(difficulty);
  f.write(&b, 1);
}

}  // namespace

void TriviaActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);

  rng_.seed(static_cast<uint32_t>(millis()) | 1u);
  difficulty_ = loadDifficulty();

  if (openPack()) {
    view_ = View::Menu;
    LOG_INF("TRIVIA", "Pack open: %u questions", static_cast<unsigned>(pack_.count()));
  } else {
    showNotice("NO QUESTIONS",
               "There is no question pack on this card yet. Connect to WiFi and fetch one; it is about 6MB.",
               "GET THE QUESTIONS", triviaui::ActionGetPack);
  }

  flashOnNextPaint_ = true;
  requestUpdate();
}

void TriviaActivity::onExit() {
  g_packFile.close();
  g_stateFile.close();
  Activity::onExit();
}

void TriviaActivity::showNotice(const char* headline, const char* body, const char* actionLabel,
                                const freeink::ui::ActionId action) {
  std::snprintf(noticeHead_, sizeof(noticeHead_), "%s", headline);
  // Guarded because the natural call is the dangerous one: reading the current
  // body, deciding to keep it, and passing it straight back makes this an
  // snprintf of a buffer onto itself, which is undefined behaviour for
  // overlapping copies. Aliasing means "leave it as it is", so skipping is
  // also the right answer semantically, not merely the safe one.
  if (body != noticeBody_) {
    std::snprintf(noticeBody_, sizeof(noticeBody_), "%s", body);
  }
  noticeAction_ = actionLabel;
  noticeActionId_ = action;
  view_ = View::Notice;
  interactionsReady_ = false;
  flashOnNextPaint_ = true;
  requestUpdate();
}

// One asset, to a .part name, renamed only once it is whole -- so a torn
// download can never masquerade as a corrupt pack. The app simply finds no
// pack, exactly as before the attempt.
//
// Synchronous: loop() is blocked but the render task is not, so progress paints
// through requestUpdateAndWait(). The input pump in the progress callback is
// the sanctioned exception to the one-pump rule -- nothing else pumps while
// this blocks, and without it Back could not cancel a multi-minute download.
void TriviaActivity::onWifiChosen(const bool connected) {
  if (!connected) {
    // Cancelling the picker is a decision, not a failure: say what did not
    // happen and leave the button that starts it again.
    showNotice("NO WIFI", "The pack needs WiFi to download. The card is unchanged.", "TRY AGAIN",
               triviaui::ActionGetPack);
    return;
  }
  // Queued rather than run here: the download blocks for minutes, and this is
  // the result handler of the activity that is still unwinding.
  downloadQueued_ = true;
}

void TriviaActivity::runPackDownload() {
  // exists() first, because SdFat's mkdir returns FALSE for a directory that
  // is already there. Treating that as failure meant the second attempt at a
  // download could never succeed: the first one creates /trivia, and every run
  // after it reports NO ROOM on a card with gigabytes free.
  //
  // Found on hardware and invisible in the simulator, whose SD is an ordinary
  // host directory where mkdir on an existing path succeeds. Every other
  // caller in this fork already does it this way -- StudyActivity, ScreenshotUtil,
  // BookmarkFile -- and this was the one that invented its own.
  if (!Storage.exists(kDir) && !Storage.mkdir(kDir)) {
    showNotice("NO ROOM", "Could not create /trivia on the card. Is the card in, and writable?", "TRY AGAIN",
               triviaui::ActionGetPack);
    return;
  }

  // Ask the card how much room is left BEFORE writing ~6MB to it, because this
  // app is not the only thing on the card. A card filled by a trivia pack is a
  // card where Study's review log cannot be written, and that failure loses
  // answers rather than refusing -- the cost of overfilling lands on a different
  // app, silently, later.
  //
  // The decision itself lives in TriviaCore so it can be tested: this file
  // includes WiFi.h and cannot be built on the host at all.
  uint64_t freeNow = 0;
  const bool queryOk = Storage.freeBytes(freeNow);
  switch (trivia::roomFor(queryOk, freeNow, trivia::kPackFreeFloorBytes)) {
    case trivia::Room::Unknown:
      // NOT the same screen as NO ROOM: freeBytes() returns false for "could not
      // answer" and never for "full". Saying the card is full when we do not know
      // that would be the same conflation the HAL call exists to prevent.
      // The body must describe what the BUTTON does. The first version said the
      // card "may need re-seating" beside a button marked TRY AGAIN: the
      // instruction and the only available action were about different things,
      // and it sent people to fiddle with hardware for a fault that is usually
      // transient. Not asserting anything about the physical slot, which this
      // code cannot know and nobody has checked.
      showNotice("CAN'T TELL",
                 "The card did not answer when asked how much room is left, so nothing was written. "
                 "Trying again usually works.",
                 "TRY AGAIN", triviaui::ActionGetPack);
      return;
    case trivia::Room::TooSmall: {
      // Local buffer: showNotice snprintf()s body INTO noticeBody_, so passing
      // noticeBody_ as the body argument would be an overlapping self-copy.
      char body[160];
      // Says what would make the retry succeed. Reporting both numbers and then
      // offering only TRY AGAIN told the user they were stuck while implying
      // the button might help; it is the only action on screen, so the sentence
      // has to name the thing that makes it work.
      std::snprintf(body, sizeof(body),
                    "The questions need about %u MB free and the card has %u MB. "
                    "Delete something from the card, then try again. Nothing was written.",
                    static_cast<unsigned>(trivia::kPackFreeFloorBytes >> 20), static_cast<unsigned>(freeNow >> 20));
      showNotice("NO ROOM", body, "TRY AGAIN", triviaui::ActionGetPack);
      return;
    }
    case trivia::Room::Ok:
      break;
  }

  g_packFile.close();
  g_stateFile.close();
  downloadCancel_ = false;

  size_t lastPainted = 0;
  const auto progress = [this, &lastPainted](const size_t got, const size_t total) {
    // Every ~1MB: each paint is an e-ink refresh, and finer steps would spend
    // more time refreshing than downloading.
    if (got - lastPainted >= 1024u * 1024u || (total > 0 && got == total)) {
      lastPainted = got;
      if (total > 0) {
        std::snprintf(noticeBody_, sizeof(noticeBody_), "Fetching the questions: %u of %u MB. Back stops it.",
                      static_cast<unsigned>(got >> 20), static_cast<unsigned>(total >> 20));
      } else {
        std::snprintf(noticeBody_, sizeof(noticeBody_), "Fetching the questions: %u MB so far. Back stops it.",
                      static_cast<unsigned>(got >> 20));
      }
      requestUpdateAndWait();
    }
    mappedInput.update();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) downloadCancel_ = true;
    if (mappedInput.wasHomeGesture()) downloadCancel_ = true;
  };

  std::snprintf(noticeHead_, sizeof(noticeHead_), "%s", "FETCHING");
  std::snprintf(noticeBody_, sizeof(noticeBody_), "%s", "Starting. Back stops it.");
  noticeAction_ = nullptr;
  requestUpdateAndWait();

  const auto err = HttpDownloader::downloadToFile(kPackUrl, kPartPath, progress, &downloadCancel_);
  if (err != HttpDownloader::OK) {
    Storage.remove(kPartPath);
    // Every one of these offers TRY AGAIN. A screen that reports a failure and
    // gives you nothing to press is a dead end -- the user's only way out is to
    // leave the app, and nothing on screen says so. Get Books shipped exactly
    // that and Mario found it on the device.
    if (err == HttpDownloader::ABORTED) {
      showNotice("STOPPED", "Download stopped. Nothing was kept.", "TRY AGAIN", triviaui::ActionGetPack);
    } else if (err == HttpDownloader::FILE_ERROR) {
      showNotice("CARD TROUBLE", "The card would not take the file. Nothing was kept.", "TRY AGAIN",
                 triviaui::ActionGetPack);
    } else {
      showNotice("NO ANSWER", "The download did not answer. The card is unchanged.", "TRY AGAIN",
                 triviaui::ActionGetPack);
    }
    return;
  }

  Storage.remove(kPackPath);  // a half pack from an earlier era must not block the rename
  if (!Storage.rename(kPartPath, kPackPath)) {
    showNotice("CARD TROUBLE", "Downloaded, but the card refused the final rename.", "TRY AGAIN",
               triviaui::ActionGetPack);
    return;
  }

  // The state file describes the OLD pack. openPack rewrites it when the
  // question count no longer matches, which loses which questions have been
  // seen -- the right trade against reading a stale byte for a question it
  // does not describe.
  if (!openPack()) {
    showNotice("BAD PACK", "Downloaded, but the pack did not open.", "TRY AGAIN", triviaui::ActionGetPack);
    return;
  }
  char body[96];
  std::snprintf(body, sizeof(body), "%u questions on the card. Ready when you are.",
                static_cast<unsigned>(pack_.count()));
  showNotice("READY", body, "PLAY", triviaui::ActionMenuRow);
  selected_ = -1;
}

void TriviaActivity::go(const View next) {
  view_ = next;
  interactionsReady_ = false;
  flashOnNextPaint_ = true;  // a mode change is a page turn, so spend the flash
  requestUpdate();
}

void TriviaActivity::deal() {
  const bool solo = view_ == View::Solo;
  revealed_ = false;
  chosen_ = -1;
  haveQuestion_ = false;

  uint32_t index = 0;
  if (!chooser_.next(index, solo, difficulty_)) {
    LOG_ERR("TRIVIA", "No question available (difficulty %d, solo %d)", difficulty_, solo ? 1 : 0);
    return;
  }
  if (!pack_.read(index, question_)) {
    LOG_ERR("TRIVIA", "Unreadable record %u", static_cast<unsigned>(index));
    return;
  }
  current_ = index;
  haveQuestion_ = true;
  state_.setFlag(index, trivia::kSeen);
  if (solo) trivia::buildChoices(question_, rng_, choices_);
  requestUpdate();
}

void TriviaActivity::routeAction(const int action, const int value) {
  switch (action) {
    case triviaui::ActionGetPack:
      // The radio first. Entering the TLS stack with WiFi never started is not
      // a failed download, it is a panic: the socket layer takes a mutex that
      // does not exist yet and FreeRTOS asserts on the null handle
      // (xQueueSemaphoreTake, queue.c:1709). The notice this button sits under
      // has always said "connect to WiFi and fetch one"; nothing did.
      WiFi.mode(WIFI_STA);
      startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                             [this](const ActivityResult& result) { onWifiChosen(!result.isCancelled); });
      break;
    case triviaui::ActionMenuRow:
      if (view_ == View::Notice) {  // the READY notice's PLAY button
        go(View::Menu);
        break;
      }
      selected_ = value;
      if (value == 0) {
        go(View::Quizmaster);
        deal();
      } else if (value == 1) {
        score_.reset();
        go(View::Solo);
        deal();
      } else {
        difficulty_ = (difficulty_ + 1) % (trivia::kDifficulties + 1);
        saveDifficulty(difficulty_);
        requestUpdate();
      }
      break;
    case triviaui::ActionQuit: {
      // Quizmaster keeps no score -- it is a person reading to a room, and
      // score_ belongs to solo. Ending there returns to the menu rather than
      // reporting a total that was never counted, or worse, a stale one left
      // over from an earlier solo round.
      if (view_ == View::Quizmaster) {
        go(View::Menu);
        break;
      }
      // Ending the round IS the summary. The two findings -- "cannot leave the
      // quiz" and "no final score" -- were one omission: with no deliberate way
      // to stop, there was no moment at which a score could be shown, so the
      // only exit (the HOME key) also threw the result away.
      //
      // Reuses the notice machinery, whose ActionMenuRow already returns to the
      // menu from a Notice, rather than adding a fifth View for one screen.
      char body[160];
      if (score_.asked == 0) {
        std::snprintf(body, sizeof(body), "%s", "No questions answered, so nothing to score.");
      } else {
        std::snprintf(body, sizeof(body), "You got %d of %d.", score_.right, score_.asked);
      }
      showNotice("ROUND OVER", body, "BACK TO MENU", triviaui::ActionMenuRow);
      break;
    }
    case triviaui::ActionReveal:
      revealed_ = true;
      requestUpdate();
      break;
    case triviaui::ActionNext:
      // From the HIDDEN notice, put the mode back first: deal() reads view_ to
      // decide whether it needs a question with distractors.
      if (view_ == View::Notice) go(flagReturn_);
      deal();
      break;
    case triviaui::ActionOption:
      if (chosen_ < 0 && value >= 0 && value < trivia::kOptions) {
        chosen_ = value;
        score_.record(value == choices_.correct);
        requestUpdate();
      }
      break;
    case triviaui::ActionFlag:
      // The whole curation loop: a question judged bad at a bar is never served
      // again, and the index is read back off the card into verdicts.tsv.
      if (haveQuestion_) {
        state_.setFlag(current_, trivia::kFlagged);
        LOG_INF("TRIVIA", "Flagged question %u", static_cast<unsigned>(current_));
        // Say what happened. Before this the question simply changed, which is
        // exactly what NEXT does, so a cold tester could not tell the button
        // had any effect at all and stopped pressing it.
        flagReturn_ = view_;
        char body[160];
        std::snprintf(body, sizeof(body), "That question will not come back. %u hidden so far.",
                      static_cast<unsigned>(state_.flaggedCount()));
        showNotice("HIDDEN", body, "NEXT QUESTION", triviaui::ActionNext);
      }
      break;
    default:
      break;
  }
}

void TriviaActivity::loop() {
  // Started from the action handler rather than inside it: the download blocks
  // for minutes and pumps input itself, which must not happen while an action
  // is still being routed.
  if (downloadQueued_) {
    downloadQueued_ = false;
    runPackDownload();
    return;
  }

  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  // Touch only, like the rest of this fork's games. The app never blocks the
  // loop, so it must not call mappedInput.update(): the shell already did.
  if (!input.touchReleased || !interactionsReady_) return;

  const fui::ActionEvent action = interactions_.route(input);
  routeAction(static_cast<int>(action.action), static_cast<int>(action.value));
}

void TriviaActivity::render(RenderLock&&) {
  renderer.clearScreen();

  // Faces per view, not per app. The clue is a page of prose and wants the
  // reading serif; the front door is a menu and must look like every other
  // menu in the fork, which is Jersey. proseMenuFaces exists for exactly this
  // shape -- a headline with a sentence under it -- because at the 20px UI cut
  // a sentence runs off the panel and is truncated with an ellipsis Jersey
  // does not carry, so the line simply stops. See ToyboxTheme.h.
  const bool prose = view_ == View::Quizmaster || view_ == View::Solo || view_ == View::Notice;
  fui::GfxRendererTarget target =
      toybox::makeTarget(renderer, prose ? toybox::readingChromeFaces() : toybox::proseMenuFaces());
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);

  switch (view_) {
    case View::Notice: {
      triviaui::NoticeModel model;
      model.headline = noticeHead_;
      model.body = noticeBody_;
      model.actionLabel = noticeAction_;
      model.action = noticeActionId_;
      triviaui::buildNotice(screen, model);
      break;
    }
    case View::Menu: {
      triviaui::MenuModel model;
      model.selected = selected_;
      model.difficulty = difficulty_;
      model.packCount = pack_.count();
      model.seenCount = state_.seenCount();
      triviaui::buildMenu(screen, model);
      break;
    }
    case View::Quizmaster: {
      triviaui::QuestionModel model;
      if (haveQuestion_) {
        model.clue = question_.clue();
        model.difficulty = question_.difficulty();
        if (revealed_) model.answer = question_.answer();
      } else {
        model.clue = "No question available. Try another difficulty.";
      }
      triviaui::buildQuestion(screen, model);
      break;
    }
    case View::Solo: {
      triviaui::ChoiceModel model;
      model.asked = score_.asked;
      model.right = score_.right;
      model.chosen = chosen_;
      if (haveQuestion_) {
        model.clue = question_.clue();
        model.difficulty = question_.difficulty();
        model.correct = choices_.correct;
        for (int i = 0; i < trivia::kOptions; ++i) model.option[i] = choices_.option[i];
      } else {
        model.clue = "No multiple-choice question available at this difficulty.";
      }
      triviaui::buildChoice(screen, model);
      break;
    }
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Trivia");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(flashOnNextPaint_ ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  flashOnNextPaint_ = false;
}
