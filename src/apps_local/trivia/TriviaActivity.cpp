#include "TriviaActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../../CrossPointSettings.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../components/UITheme.h"
#include "../../network/HttpDownloader.h"
#include "../Shelf.h"
#include "../bridge/BridgeHttp.h"
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
// Published in the SAME release upload as pack.dat, so the two cannot skew.
// Fetched straight from GitHub rather than through a service: the device is not
// a browser, so the CORS problem that put api/firmware.js on the site does not
// apply to it, and a freshness check needing no service beats one that does.
constexpr const char* kManifestUrl = "https://github.com/ma-r-s/crossplay/releases/download/trivia-pack/pack.json";
// Which build this card holds, and the reports waiting to go up.
constexpr const char* kMetaPath = "/trivia/pack.meta";
constexpr const char* kReportsPath = "/trivia/reports.dat";

// Reports ride a request the device was making anyway, which is card #125's
// rule: the device never brings the radio up to report. The headers that
// identify it are attached by bridge::identify() and are the SAME ones every
// other service already receives -- nothing new about a reader is sent.
constexpr bridge::Endpoint kReportEndpoint = {
    "crossplay.ma-r-s.com",
    "TRIVIASYNC",
    "CROSSPLAY_SITE_URL",
    "/.crosspoint/trivia/.bridge-roots.pem",
};
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
  // pack.state is fixed width and must never grow; the report queue appends.
  // The difference is a property of the FILE, so it is set when the source is
  // attached rather than inferred at each write -- a source that decided per
  // call would let a bug in one file silently extend the other.
  void attachGrowing(HalFile& file) {
    attach(file);
    grow_ = true;
  }
  bool write(const uint32_t offset, const void* src, const uint32_t length) override {
    if (length == 0) return true;
    if (file_ == nullptr) return false;
    if (offset + length > size_) {
      if (!grow_) return false;
    }
    if (!file_->seekSet(offset)) return false;
    if (file_->write(static_cast<const uint8_t*>(src), length) != length) return false;
    if (offset + length > size_) size_ = offset + length;
    return true;
  }
  bool flush() override {
    if (file_ != nullptr) file_->flush();
    return true;
  }
  uint32_t size() const override { return size_; }

 private:
  HalFile* file_ = nullptr;
  uint32_t size_ = 0;
  bool grow_ = false;
};

HalFile g_packFile;
HalFile g_stateFile;
HalFile g_reportFile;
FileSource g_packSource;
FileSource g_stateSource;
FileSource g_reportSource;

// The rows of the WHY? list, and the two that are conditional.
//
// `us` only appears when US questions are OFF, because with the toggle off the
// chooser already skips marked records -- so a US question a player MEETS is
// one the pack failed to mark, which is a one-byte repair rather than a taste
// report. With the toggle on, the player asked for these and reporting one is
// not a defect. `giveaway` only appears in solo, because quizmaster draws no
// options for it to be about.
struct ReasonRow {
  const char* label;
  trivia::Reason reason;
  bool soloOnly;
  bool usOnly;
};
constexpr ReasonRow kReasonRows[] = {
    {"WRONG ANSWER", trivia::Reason::Wrong, false, false},
    {"MAKES NO SENSE", trivia::Reason::Nonsense, false, false},
    {"THE OPTIONS GIVE IT AWAY", trivia::Reason::Giveaway, true, false},
    {"MORE THAN ONE ANSWER FITS", trivia::Reason::Ambiguous, false, false},
    {"OUT OF DATE", trivia::Reason::Outdated, false, false},
    {"BROKEN TEXT", trivia::Reason::Broken, false, false},
    {"ONLY A LOCAL COULD KNOW", trivia::Reason::Regional, false, false},
    {"THIS IS A US QUESTION", trivia::Reason::Us, false, true},
    {"TOO HARD", trivia::Reason::Hard, false, false},
    {"TOO EASY", trivia::Reason::Easy, false, false},
};

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
  openReports(pack_.count(), g_packSource.size());
  chooser_.begin(pack_, state_, rng_);
  return true;
}

// Reads which build this card holds, then opens the outbound queue against it.
//
// Both halves fail benignly and on purpose. A missing or disagreeing pack.meta
// means "I hold a pack and do not know which build", which stops reports being
// QUEUED rather than stopping play: a report that cannot name its pack can
// never be resolved, and one that names the wrong pack is worse than none --
// it resolves through another build's index map and deletes a question nobody
// reported. HIDE still works throughout; only the outbound copy is withheld.
void TriviaActivity::openReports(const uint32_t count, const uint32_t packBytes) {
  meta_ = trivia::PackMeta{};
  HalFile metaFile = Storage.open(kMetaPath, O_RDONLY);
  if (metaFile.isOpen() && metaFile.size() > 0 && metaFile.size() < 512) {
    char text[512] = {};
    const int read = metaFile.read(text, static_cast<size_t>(metaFile.size()));
    if (read > 0) meta_ = trivia::parseMeta(text, static_cast<size_t>(read), count, packBytes);
  }
  if (!meta_.valid) {
    LOG_INF("TRIVIA", "Pack build unknown; reports are held until a sync settles it");
    return;
  }

  g_reportFile = Storage.open(kReportsPath, O_RDWR);
  if (!g_reportFile.isOpen()) {
    HalFile fresh;
    if (!Storage.openFileForWrite("TRIVIA", kReportsPath, fresh)) {
      LOG_ERR("TRIVIA", "Cannot create %s", kReportsPath);
      return;
    }
    fresh.flush();
    fresh.close();
    g_reportFile = Storage.open(kReportsPath, O_RDWR);
    if (!g_reportFile.isOpen()) return;
  }
  g_reportSource.attachGrowing(g_reportFile);
  const trivia::QueueOpen opened = reports_.open(g_reportSource, meta_.id, count);
  switch (opened) {
    case trivia::QueueOpen::Ready:
    case trivia::QueueOpen::Started:
      break;
    case trivia::QueueOpen::Foreign:
      // Undelivered reports about a DIFFERENT pack. They are kept exactly as
      // filed and sent under their own header; nothing new is queued until they
      // go, because re-labelling them would file them against questions they
      // were never about.
      LOG_INF("TRIVIA", "%u report(s) still pending for pack %s", static_cast<unsigned>(reports_.pending()),
              reports_.packId());
      break;
    case trivia::QueueOpen::Unusable:
      LOG_ERR("TRIVIA", "%s is not a readable report queue", kReportsPath);
      break;
  }
}

// One line for the SETTINGS row, saying what the card holds. Written here
// rather than in the screen because only the activity knows the pack.
void TriviaActivity::describePack(char* out, const size_t capacity) const {
  if (!pack_.isOpen()) {
    std::snprintf(out, capacity, "%s", "NO PACK ON THIS CARD YET");
    return;
  }
  if (!meta_.valid) {
    // Said plainly rather than hidden: an unknown build is why reports are not
    // being queued, and a player who is told nothing would report into a void.
    std::snprintf(out, capacity, "%u QUESTIONS. BUILD UNKNOWN UNTIL YOU SYNC", static_cast<unsigned>(pack_.count()));
    return;
  }
  std::snprintf(out, capacity, "%u QUESTIONS, BUILD %s", static_cast<unsigned>(pack_.count()), meta_.id);
}

// Fills the WHY? list, skipping the two rows that cannot apply. Returns how
// many rows were written.
int TriviaActivity::reasonRows(triviaui::ReasonModel& model) const {
  const bool solo = view_ == View::Solo || flagReturn_ == View::Solo;
  const bool usHidden = SETTINGS.triviaShowUsCentric == 0;
  int count = 0;
  for (const ReasonRow& row : kReasonRows) {
    if (count >= triviaui::ReasonModel::kMax) break;
    if (row.soloOnly && !solo) continue;
    if (row.usOnly && !usHidden) continue;
    model.label[count] = row.label;
    model.value[count] = static_cast<int>(row.reason);
    ++count;
  }
  model.count = count;
  return count;
}

// The FLAGGED bit and the outbound copy, in that order. The bit is what the
// player asked for and must land even when the queue cannot take the report.
void TriviaActivity::fileReport(const uint32_t index, const trivia::Reason reason) {
  state_.setFlag(index, trivia::kFlagged);
  if (reports_.isOpen() && meta_.valid && std::strcmp(reports_.packId(), meta_.id) == 0) {
    reports_.add(index, reason);
  }
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
  // Cleared on EVERY notice, so the extra row cannot survive onto a screen that
  // did not ask for it. A caller that wants WHY?/UNDO sets them AFTER this
  // call; anything else gets a notice with one button, as before.
  noticeSecond_ = nullptr;
  noticeSecondId_ = 0;
  noticeThird_ = nullptr;
  noticeThirdId_ = 0;
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

// SYNC: send what is queued, then ask what is published.
//
// THE ORDER IS THE DESIGN, not a preference. Reports go up FIRST, under the
// pack id their own queue header carries. Fetching first would mean a pack
// update could land while reports about the previous build were still on the
// card, and the pack format's own residual (a same-count replacement keeps
// pack.state) means nothing on the device could see that those indices now name
// different questions. Uploading first closes that window entirely.
//
// The busy screen is painted BEFORE the work, one loop pass earlier, which is
// XkcdActivity's pattern rather than a re-derivation of it: the panel is never
// blank while the radio is up.
void TriviaActivity::runSync() {
  showNotice("SYNCING", "Sending what you reported.", nullptr, 0);
  requestUpdateAndWait();

  uint32_t delivered = 0;
  const uint32_t pending = reports_.isOpen() ? reports_.pending() : 0;
  if (pending > 0) {
    // Compact JSON, built by hand: a document this shape does not earn a
    // serialiser, and the endpoint's own reader is a few lines for the same
    // reason. The pack id comes from the QUEUE, never from the card -- that is
    // what makes a queue filed against an older build still resolvable.
    std::string body;
    body.reserve(64 + pending * 24);
    body += "{\"pack\":\"";
    body += reports_.packId();
    body += "\",\"count\":";
    char num[16];
    std::snprintf(num, sizeof(num), "%u", static_cast<unsigned>(reports_.packCount()));
    body += num;
    body += ",\"reports\":[";
    uint32_t written = 0;
    for (uint32_t i = reports_.sent(); i < reports_.count(); ++i) {
      uint32_t index = 0;
      trivia::Reason reason = trivia::Reason::None;
      if (!reports_.entry(i, index, reason)) continue;
      if (index == trivia::kWithdrawnIndex) continue;  // taken back before it went
      if (written > 0) body += ',';
      std::snprintf(num, sizeof(num), "%u", static_cast<unsigned>(index));
      body += "{\"i\":";
      body += num;
      if (reason != trivia::Reason::None) {
        body += ",\"r\":\"";
        body += trivia::reasonName(reason);
        body += '"';
      }
      body += '}';
      ++written;
    }
    body += "]}";

    std::string response;
    std::string message;
    const int status =
        written == 0 ? 200
                     : bridge::request(kReportEndpoint, "POST", "/api/trivia", "",
                                       reinterpret_cast<const uint8_t*>(body.data()), body.size(), response, message);
    if (status >= 200 && status < 300) {
      // Marks the entries that were BUILT INTO THIS REQUEST as sent, not the
      // whole queue: a report filed while the request was in flight sits after
      // this point and must survive its success.
      reports_.markSent(reports_.count());
      delivered = written;
      LOG_INF("TRIVIA", "Sent %u report(s)", static_cast<unsigned>(delivered));
    } else {
      // Nothing is marked, so they go again next time. A failed send must never
      // cost a report: the player cannot file it twice.
      LOG_ERR("TRIVIA", "Report upload failed (%d): %s", status, message.c_str());
    }
  }

  showNotice("SYNCING", "Checking for a newer pack.", nullptr, 0);
  requestUpdateAndWait();

  std::string manifestText;
  trivia::PackManifest published;
  if (HttpDownloader::fetchUrl(kManifestUrl, manifestText)) {
    published = trivia::parseManifest(manifestText.c_str(), manifestText.size());
  }

  char body[192];
  switch (trivia::compare(meta_, published)) {
    case trivia::Freshness::Current:
      std::snprintf(body, sizeof(body), "You have the newest questions. %u report%s sent.",
                    static_cast<unsigned>(delivered), delivered == 1 ? "" : "s");
      showNotice("UP TO DATE", body, "BACK", triviaui::ActionCloseSettings);
      break;
    case trivia::Freshness::Newer:
    case trivia::Freshness::Unknown:
      // Says what it will cost BEFORE spending it. A sync that only spins is
      // what card #253 calls honest and useless, and this pack is minutes over
      // WiFi -- the size is the whole reason a player would rather not.
      std::snprintf(body, sizeof(body), "A newer pack is ready: %u questions, %u MB. This takes a few minutes.",
                    static_cast<unsigned>(published.count), static_cast<unsigned>(published.bytes / 1000000u));
      showNotice("NEWER PACK", body, "GET IT", triviaui::ActionGetPack);
      break;
    case trivia::Freshness::NoManifest:
      std::snprintf(body, sizeof(body), "Could not check for a newer pack. %u report%s sent.",
                    static_cast<unsigned>(delivered), delivered == 1 ? "" : "s");
      showNotice("NOT CHECKED", body, "BACK", triviaui::ActionCloseSettings);
      break;
  }
  requestUpdate();
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
  // US-centric questions are hidden unless the player opted in from Settings.
  const bool allowUsCentric = SETTINGS.triviaShowUsCentric != 0;
  if (!chooser_.next(index, solo, difficulty_, allowUsCentric)) {
    LOG_ERR("TRIVIA", "No question available (difficulty %d, solo %d, us %d)", difficulty_, solo ? 1 : 0,
            allowUsCentric ? 1 : 0);
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
      // Switched on the row rather than on "0, 1, or anything else". The old
      // trailing else cycled the difficulty for every value that was not 0 or
      // 1, so the SETTINGS row added beside DIFFICULTY would silently have been
      // a second difficulty control.
      //
      // And NO `default:`. A default that breaks is the same silence in a new
      // coat: the next row added to MenuRow would compile and do nothing. With
      // every enumerator listed, -Wswitch makes it a build failure instead, and
      // this tree is built -Wall -Wextra -Werror. Count is named for that
      // reason alone; it is not a row.
      switch (static_cast<triviaui::MenuRow>(value)) {
        case triviaui::MenuRow::Quizmaster:
          go(View::Quizmaster);
          deal();
          break;
        case triviaui::MenuRow::Solo:
          score_.reset();
          go(View::Solo);
          deal();
          break;
        case triviaui::MenuRow::Difficulty:
          // Stays on the front door: a per-session mood, not a preference.
          difficulty_ = (difficulty_ + 1) % (trivia::kDifficulties + 1);
          saveDifficulty(difficulty_);
          requestUpdate();
          break;
        case triviaui::MenuRow::Settings:
          // Rendered once here rather than on every paint: the pack does not
          // change while the screen is up, and building it per paint would put
          // an snprintf in the render path for a line that never moves.
          describePack(packLine_, sizeof(packLine_));
          go(View::Settings);
          break;
        case triviaui::MenuRow::Count:
          break;
      }
      break;
    case triviaui::ActionSettingsRow:
      switch (static_cast<triviaui::SettingRow>(value)) {
        case triviaui::SettingRow::UsCentric:
          // Written straight back through the settings object that owns it.
          // The value and its stored key "triviaShowUsCentric" did not move
          // when the UI did (card #311): a device that already has this saved
          // keeps what its owner chose, and the web settings API keeps working.
          SETTINGS.triviaShowUsCentric = SETTINGS.triviaShowUsCentric ? 0 : 1;
          SETTINGS.saveToFile();
          break;
        case triviaui::SettingRow::Sync:
          // The radio first, exactly as ActionGetPack does. Entering the TLS
          // stack with WiFi never started is a panic rather than a failure --
          // the socket layer takes a mutex that does not exist yet.
          WiFi.mode(WIFI_STA);
          startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                                 [this](const ActivityResult& result) {
                                   if (result.isCancelled) return;
                                   // Queued rather than run here: the sync
                                   // blocks for a long time and pumps input
                                   // itself, which must not happen while an
                                   // action is still being routed.
                                   syncQueued_ = true;
                                   requestUpdate();
                                 });
          return;
        case triviaui::SettingRow::Hidden:
          if (state_.flaggedCount() > 0) {
            // Shows every hidden question again, and withdraws the reports that
            // have not gone yet. A report already sent is a fact on the far end
            // and is NOT withdrawn: un-hiding here cannot reach into what was
            // already delivered, and pretending it could would leave the two
            // silently disagreeing.
            uint32_t restored = 0;
            for (uint32_t i = 0; i < state_.count(); ++i) {
              if (!state_.flagged(i)) continue;
              state_.clearFlag(i, trivia::kFlagged);
              reports_.withdraw(i);
              ++restored;
            }
            LOG_INF("TRIVIA", "Un-hid %u question(s)", static_cast<unsigned>(restored));
          }
          break;
        case triviaui::SettingRow::Count:
          break;
      }
      requestUpdate();
      break;
    case triviaui::ActionCloseSettings:
      go(View::Menu);
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
      // The whole curation loop. One tap files it, with no reason: Mario's rule
      // on card #257 is that a report with no reason is still a report, and
      // demanding a category is how you get no reports. WHY? is the optional
      // second tap, on its own screen.
      if (haveQuestion_) {
        fileReport(current_, trivia::Reason::None);
        reasonIndex_ = current_;
        reasonIndexValid_ = true;
        LOG_INF("TRIVIA", "Flagged question %u (queued %u)", static_cast<unsigned>(current_),
                static_cast<unsigned>(reports_.pending()));
        // Say what happened. Before this the question simply changed, which is
        // exactly what NEXT does, so a cold tester could not tell the button
        // had any effect at all and stopped pressing it.
        flagReturn_ = view_;
        char body[160];
        std::snprintf(body, sizeof(body), "That question will not come back. %u hidden so far.",
                      static_cast<unsigned>(state_.flaggedCount()));
        showNotice("HIDDEN", body, "NEXT QUESTION", triviaui::ActionNext);
        // Both on their own row, so NEXT QUESTION keeps the rect and the centre
        // it has on every other notice.
        noticeSecond_ = "WHY?";
        noticeSecondId_ = triviaui::ActionWhy;
        noticeThird_ = "UNDO";
        noticeThirdId_ = triviaui::ActionUnhide;
      }
      break;

    case triviaui::ActionWhy:
      if (reasonIndexValid_) go(View::Reason);
      break;

    case triviaui::ActionReasonRow: {
      // The row carries the reason's WIRE value, never its position, so the
      // list can be reordered or a row hidden without changing what a report
      // means.
      if (reasonIndexValid_ && value > 0 && value < static_cast<int>(trivia::Reason::Count)) {
        reports_.setReason(reasonIndex_, static_cast<trivia::Reason>(value));
        LOG_INF("TRIVIA", "Reason %d on question %u", value, static_cast<unsigned>(reasonIndex_));
      }
      go(View::Notice);
      break;
    }

    case triviaui::ActionCloseReason:
      go(View::Notice);
      break;

    case triviaui::ActionUnhide:
      // The undo the HIDE button never had. Clears the bit AND withdraws the
      // queued report, so a mis-tap costs nothing on either side.
      if (reasonIndexValid_) {
        state_.clearFlag(reasonIndex_, trivia::kFlagged);
        reports_.withdraw(reasonIndex_);
        reasonIndexValid_ = false;
        LOG_INF("TRIVIA", "Un-hid question %u", static_cast<unsigned>(reasonIndex_));
        go(flagReturn_);
        deal();
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
  if (syncQueued_) {
    syncQueued_ = false;
    runSync();
    return;
  }

  // Back, which on this board is the left-edge swipe: wasBackGesture() is
  // folded into Button::Back, so this one read serves both the gesture and the
  // key on boards that have one. It has to be read HERE, before the early
  // return below -- the whole of card #250 is that this loop() returned on
  // "no tap" and a swipe is not a tap, so the gesture reached nothing and
  // Trivia was the one app in apps_local with no way back out of its front
  // door. The target per screen is trivia::backFrom(); see TriviaCore.h.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (trivia::backFrom(view_, pack_.isOpen())) {
      case trivia::Back::LeaveApp:
        shelf::leave(renderer, mappedInput);
        return;
      case trivia::Back::EndRound:
        // The END button's own handler, not a copy of it.
        routeAction(triviaui::ActionQuit, 0);
        return;
      case trivia::Back::ToMenu:
        go(View::Menu);
        return;
      case trivia::Back::ToNotice:
        // The reason list is a modal over the HIDDEN notice, and the report is
        // already filed by the time it is on screen. Every way off it lands
        // back on that notice, so the round is never lost by backing out.
        go(View::Notice);
        return;
    }
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
      model.secondLabel = noticeSecond_;
      model.secondAction = noticeSecondId_;
      model.thirdLabel = noticeThird_;
      model.thirdAction = noticeThirdId_;
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
    case View::Reason: {
      triviaui::ReasonModel model;
      reasonRows(model);
      triviaui::buildReasons(screen, model);
      break;
    }
    case View::Settings: {
      triviaui::SettingsModel model;
      // Read straight from the settings object rather than mirrored into a
      // member, so there is one copy of this fact and no second one to drift.
      // It is NOT a defence against a concurrent web write: rendering here is
      // notification-driven and a write from the browser raises no
      // requestUpdate(), so this screen would show the old value until the next
      // tap either way.
      model.usCentric = SETTINGS.triviaShowUsCentric != 0;
      model.hidden = state_.flaggedCount();
      model.pending = reports_.isOpen() ? reports_.pending() : 0;
      model.packLine = packLine_;
      triviaui::buildSettings(screen, model);
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
