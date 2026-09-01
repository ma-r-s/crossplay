#include "InstapaperActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "../../SilentRestart.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../components/UITheme.h"
#include "../../util/QrUtils.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxText.h"
#include "../ui/ToyboxTheme.h"
#include "DevMode.h"

namespace {

namespace fui = freeink::ui;

// Below this, the clock has never been set: the device boots at the epoch and
// a reading position stamped there would either be dropped by the bridge or,
// worse, believed by Instapaper. The same floor Study uses.
constexpr int64_t kClockFloor = 1700000000;

// How long to wait between polls of a running job. The bridge fetches and
// converts a handful of articles, so this is seconds rather than tens of them,
// and each poll is one HTTP round trip on a battery.
constexpr uint32_t kPollIntervalMs = 1500;

}  // namespace

std::unique_ptr<Activity> InstapaperActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<InstapaperActivity>(renderer, mappedInput);
}

uint32_t InstapaperActivity::nowOrZero() {
  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  return now > kClockFloor ? static_cast<uint32_t>(now) : 0u;
}

// --- Lifecycle -----------------------------------------------------------

void InstapaperActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);

  library_.load();
  library_.loadBridgeState(bridge_);
  rowsFitted_ = false;
  // Opens on the queue, offline. Everything the reader already has is
  // readable with no radio at all, which is the point of a read-later app: a
  // train, a plane, and no network is the normal case rather than the failure.
  phase_ = Phase::Queue;
  LOG_INF("INSTA", "opened: %d articles, %s", static_cast<int>(library_.articles().size()),
          bridge_.paired ? "paired" : "not paired");
  // Without this the app enters, loads its queue, logs that it did, and draws
  // NOTHING: the panel keeps showing the shelf it was opened from. Nothing in
  // a build or a log says so -- the log line above is what made it look like
  // the app had run. See docs/building-apps.md.
  requestUpdate();
}

void InstapaperActivity::onExit() {
  Activity::onExit();
  // The radio has to come down before the activity does. Not ours to put down
  // if Developer Mode brought it up: silentRestart() reboots, and doing that
  // every time this app exits while dev mode is on is indistinguishable from a
  // crash.
  if (WiFi.getMode() != WIFI_MODE_NULL && !devmode::holdsRadio()) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

// --- Scheduling the slow parts -------------------------------------------

void InstapaperActivity::request(const Step next, const char* busyMessage) {
  {
    RenderLock lock(*this);
    phase_ = Phase::Busy;
    busyMessage_ = busyMessage;
    step_ = next;
  }
  // Paint the busy screen now. The work happens on the next loop pass, so the
  // panel is never blank while the radio works.
  requestUpdate();
}

void InstapaperActivity::needNetwork(const Step next, const char* busyMessage) {
  if (WiFi.status() == WL_CONNECTED) {
    request(next, busyMessage);
    return;
  }
  // The picker is a child activity, and its result decides whether the step
  // ever happens. Requesting the step first and connecting after would run it
  // against a radio that is not up yet.
  step_ = next;
  busyMessage_ = busyMessage;
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiChosen(!result.isCancelled); });
}

void InstapaperActivity::onWifiChosen(const bool connected) {
  if (!connected) {
    // They backed out of the picker, so they did not want to sync. The queue
    // still works, which is why this returns there rather than leaving.
    step_ = Step::None;
    showQueue();
    return;
  }
  request(step_, busyMessage_);
}

void InstapaperActivity::showQueue() {
  {
    // Scoped, and the repaint is outside it: that is the shape every other
    // caller in this fork uses, and holding a render lock across the call that
    // asks for a render is not a shape worth being the exception to.
    RenderLock lock(*this);
    phase_ = Phase::Queue;
    rowsFitted_ = false;
  }
  requestUpdate();
}

void InstapaperActivity::showNotice(const char* headline, std::string message, const bool unreadable) {
  {
    RenderLock lock(*this);
    noticeHeadline_ = headline;
    noticeMessage_ = std::move(message);
    noticeUnreadable_ = unreadable;
    phase_ = Phase::Notice;
  }
  requestUpdate();
}

// --- The loop ------------------------------------------------------------

void InstapaperActivity::loop() {
  // The deferred step, one pass after the screen that announces it. Taken
  // before anything else so a queued step cannot be starved by input.
  if (step_ != Step::None && phase_ == Phase::Busy) {
    const Step what = step_;
    step_ = Step::None;
    bool ok = true;
    switch (what) {
      case Step::PairStart:
        ok = doPairStart();
        break;
      case Step::PairPoll:
        ok = doPairPoll();
        break;
      case Step::SyncStart:
        ok = doSyncStart();
        break;
      case Step::SyncPoll:
        ok = doSyncPoll();
        break;
      case Step::Download:
        ok = doDownload();
        break;
      case Step::None:
        break;
    }
    (void)ok;
    requestUpdate();
    return;
  }

  // Back is read BEFORE the pairing poll, because that poll sleeps: checking
  // it afterwards would make walking away from the QR take up to a poll
  // interval to answer, which on a screen people stare at reads as a hang.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (phase_) {
      case Phase::Reading:
        closeArticle();
        break;
      case Phase::Pairing:
      case Phase::PairConfirm:
        // Walking away from a pairing is worth telling the bridge about: the
        // code stops being claimable, and a registration the confirm screen
        // declined is revoked rather than left as a device nobody holds.
        sync_.pairAbandon(pollToken_, pendingToken_);
        pollToken_.clear();
        pendingToken_.clear();
        pairCode_.clear();
        showQueue();
        break;
      case Phase::Notice:
      case Phase::Busy:
        showQueue();
        break;
      case Phase::Queue:
        shelf::leave(renderer, mappedInput);
        break;
    }
    return;
  }

  // Pairing polls on its own screen rather than on the busy one: the QR has to
  // stay on the panel the whole time somebody is scanning it.
  //
  // Repainted only when the poll CHANGED something, and that is the important
  // half twice over. Repainting every interval would flash a QR code on e-ink
  // once a second; and the first version repainted only on failure, so the
  // confirm screen -- the one screen this whole handshake exists to show --
  // was reached and never drawn.
  if (phase_ == Phase::Pairing && !pollToken_.empty()) {
    delay(kPollIntervalMs);
    const Phase before = phase_;
    doPairPoll();
    if (phase_ != before) requestUpdate();
    return;
  }

  // Physical page keys do the same thing the footer arrows do, through the
  // same function. Two paths would drift, and the drift is invisible until
  // somebody uses the input nobody tested.
  if (phase_ == Phase::Reading) {
    if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
      turnPage(1);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
      turnPage(-1);
      return;
    }
  }

  // The two side keys PAGE the queue. They are the device's only physical
  // buttons; a row cursor would need Confirm, which is an unassigned pin on
  // the X4 Pro and never fires. See docs/buttons.md.
  const bool next = mappedInput.wasReleased(MappedInputManager::Button::Down);
  const bool prev = mappedInput.wasReleased(MappedInputManager::Button::Up);
  if (phase_ == Phase::Queue && !rows_.empty() && (next || prev)) {
    const int count = static_cast<int>(rows_.size());
    const int perPage = visibleRows_;
    if (perPage > 0) {
      const int pages = (count + perPage - 1) / perPage;
      const int page = topIndex_ / perPage;
      // Wraps: a page key that stops working at the last page reads as broken.
      topIndex_ = ((page + (next ? 1 : pages - 1)) % pages) * perPage;
      requestUpdate();
    }
    return;
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady_) return;

  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions_.route(input);

  switch (event.action) {
    case instapaperui::ActionOpenArticle:
      if (event.value >= 0 && event.value < static_cast<int>(rowIds_.size())) {
        selected_ = event.value;
        openArticle(rowIds_[static_cast<size_t>(event.value)]);
      }
      break;
    case instapaperui::ActionSync:
      if (bridge_.paired) {
        needNetwork(Step::SyncStart, "SYNCING");
      } else {
        needNetwork(Step::PairStart, "GETTING A CODE");
      }
      break;
    case instapaperui::ActionPagePrev:
      turnPage(-1);
      break;
    case instapaperui::ActionPageNext:
      turnPage(1);
      break;
    case instapaperui::ActionArchive:
      archiveCurrent();
      break;
    case instapaperui::ActionPairConfirm:
      if (!pendingToken_.empty()) {
        bridge_.paired = true;
        bridge_.token = pendingToken_;
        library_.saveBridgeState(bridge_);
        pendingToken_.clear();
        pollToken_.clear();
        LOG_INF("INSTA", "paired");
        needNetwork(Step::SyncStart, "SYNCING");
      }
      break;
    case instapaperui::ActionNotice:
      showQueue();
      break;
    default:
      break;
  }
}

// --- Pairing -------------------------------------------------------------

bool InstapaperActivity::doPairStart() {
  instapaper::Sync::PairStart start;
  std::string message;
  if (!sync_.pairStart(start, message)) {
    showNotice("NO LUCK", message);
    return false;
  }
  RenderLock lock(*this);
  pairCode_ = start.code;
  pollToken_ = start.pollToken;
  phase_ = Phase::Pairing;
  return true;
}

bool InstapaperActivity::doPairPoll() {
  std::string username;
  std::string token;
  std::string message;
  const int result = sync_.pairPoll(pollToken_, username, token, message);
  if (result == 0) return true;  // still pending; the QR stays up
  if (result < 0) {
    pollToken_.clear();
    showNotice("THAT CODE IS GONE", message.empty() ? "Ask for a fresh one." : message);
    return false;
  }
  // Delivered. Nothing is stored until a button on THIS device is pressed:
  // that is what stops a phished QR pairing a stranger's account to a reader
  // its owner never touched, and what makes a shoulder-surfed code visible.
  RenderLock lock(*this);
  pendingToken_ = token;
  pendingUser_ = username;
  phase_ = Phase::PairConfirm;
  return true;
}

// --- Syncing -------------------------------------------------------------

bool InstapaperActivity::doSyncStart() {
  library_.load();
  std::vector<int64_t> archive;
  for (const instapaper::Article& a : library_.articles()) {
    if (a.archivePending) archive.push_back(a.id);
  }

  // A clock that has never been set must not stamp a reading position. The
  // index keeps the progress either way -- it is only the timestamp that is
  // withheld, and without one the bridge leaves Instapaper's own value alone.
  std::vector<instapaper::Article> have = library_.articles();
  if (nowOrZero() == 0) {
    for (instapaper::Article& a : have) a.progressAt = 0;
  }

  std::string message;
  if (!sync_.syncStart(bridge_, have, archive, jobId_, message)) {
    if (sync_.unpaired) {
      // The pairing is dead. Clearing it is what stops every future SYNC
      // repeating this same refusal forever.
      bridge_ = instapaper::BridgeState{};
      library_.clearBridgeState();
    }
    showNotice("NOT SYNCED", message);
    return false;
  }
  request(Step::SyncPoll, "SYNCING");
  return true;
}

bool InstapaperActivity::doSyncPoll() {
  std::string message;
  const std::string status = sync_.syncStatus(bridge_, jobId_, summary_, message);
  if (status == "done") {
    // The whole merge is one pure function, and it decides what to fetch and
    // what to delete. See InstapaperIndex.h.
    library_.load();
    const instapaper::MergePlan plan =
        instapaper::mergeSummary(library_.articles(), instapaper::deliveredArticles(summary_), summary_.deleteIds,
                                 summary_.archived, library_.presentIds());
    for (const int64_t id : plan.drop) library_.removeArticle(id);
    // The index is saved BEFORE the downloads, on purpose. A download that
    // fails leaves a row whose text is missing, and the next sync's
    // presentIds() puts it straight back on the download list. The opposite
    // order loses the metadata for anything that did arrive.
    library_.saveIndex();

    toDownload_ = plan.download;
    downloadIndex_ = 0;
    downloaded_ = 0;
    downloadFailures_ = 0;
    if (toDownload_.empty()) {
      finishSync();
      return true;
    }
    request(Step::Download, "FETCHING ARTICLES");
    return true;
  }
  if (status == "running" || status == "queued") {
    delay(kPollIntervalMs);
    request(Step::SyncPoll, "SYNCING");
    return true;
  }
  if (sync_.unpaired) {
    bridge_ = instapaper::BridgeState{};
    library_.clearBridgeState();
  }
  showNotice("NOT SYNCED", message.empty() ? "The service stopped answering. Try again." : message);
  return false;
}

bool InstapaperActivity::doDownload() {
  if (downloadIndex_ >= toDownload_.size()) {
    finishSync();
    return true;
  }
  const int64_t id = toDownload_[downloadIndex_++];
  const instapaper::Article* article = library_.find(id);
  if (article != nullptr) {
    std::string message;
    const uint32_t bytes = instapaper::deliveredBytes(summary_, id);
    const std::string part = library_.partPathFor(id);
    if (sync_.downloadToPart(bridge_, *article, bytes, part, &cancelDownload_, message)) {
      // Renamed only once the whole file is on the card, so a failed or
      // interrupted download can never be read as an article.
      if (library_.commitPart(id)) {
        ++downloaded_;
      } else {
        ++downloadFailures_;
      }
    } else {
      library_.discardPart(id);
      ++downloadFailures_;
      LOG_ERR("INSTA", "article %lld did not arrive: %s", static_cast<long long>(id), message.c_str());
    }
  }
  std::snprintf(busyDetail_, sizeof(busyDetail_), "%d of %d", static_cast<int>(downloadIndex_),
                static_cast<int>(toDownload_.size()));
  request(Step::Download, "FETCHING ARTICLES");
  return true;
}

void InstapaperActivity::finishSync() {
  bridge_.lastSyncAt = static_cast<int64_t>(std::time(nullptr));
  library_.saveBridgeState(bridge_);
  rowsFitted_ = false;
  topIndex_ = 0;

  // The verdict names what a reader would otherwise discover later and blame
  // on the device: an article that could not be prepared, one that did not
  // arrive, and the ones still to come. Saying SYNCED and leaving a gap is
  // the failure this paragraph exists to avoid.
  char body[192];
  int used = std::snprintf(body, sizeof(body), "%d new or updated.", downloaded_);
  if (summary_.withheld > 0 && used > 0 && used < static_cast<int>(sizeof(body))) {
    used += std::snprintf(body + used, sizeof(body) - used, " %d more next sync.", summary_.withheld);
  }
  if (summary_.failed > 0 && used > 0 && used < static_cast<int>(sizeof(body))) {
    used += std::snprintf(body + used, sizeof(body) - used, " %d Instapaper could not prepare.", summary_.failed);
  }
  if (downloadFailures_ > 0 && used > 0 && used < static_cast<int>(sizeof(body))) {
    std::snprintf(body + used, sizeof(body) - used, " %d did not arrive; sync again.", downloadFailures_);
  }
  const bool clean = summary_.failed == 0 && downloadFailures_ == 0;
  showNotice(clean ? "SYNCED" : "SYNCED, MOSTLY", body);
}

// --- Reading -------------------------------------------------------------

void InstapaperActivity::openArticle(const int64_t id) {
  const instapaper::Article* article = library_.find(id);
  if (article == nullptr) return;

  if (!library_.readArticle(id, document_) || document_.empty()) {
    showNotice("NOT ON THE CARD", "This one did not finish downloading. Sync again to fetch it.");
    return;
  }
  if (!article->renderable) {
    // Said at the moment it is known, on its own screen, rather than marked in
    // the list: the list would be claiming something about text it has not
    // opened. The Hacker News gate's lesson, applied to a different failure.
    showNotice("NOT SHOWABLE HERE", "This article is written in a script this reader has no letters for.", true);
    return;
  }

  RenderLock lock(*this);
  openId_ = id;
  readerTitle_ = article->title;
  phase_ = Phase::Reading;
  // topLine_ is placed in render(), where the line count is finally known:
  // measuring needs a draw target. Marked with a sentinel so that placement
  // happens exactly once per opening.
  topLine_ = 0;
  lineCount_ = 0;
  requestUpdate();
}

void InstapaperActivity::closeArticle() {
  // The reading position, in Instapaper's own terms: the top edge of the
  // viewport as a share of the article's length. Reaching the end counts as
  // finished rather than as "topLine_/lineCount_", which would report about
  // 90% for an article somebody just read to the last word.
  instapaper::Article* article = library_.find(openId_);
  if (article != nullptr && lineCount_ > 0) {
    const float progress = (topLine_ + visibleLines_ >= lineCount_)
                               ? 1.0f
                               : static_cast<float>(topLine_) / static_cast<float>(lineCount_);
    const uint32_t stamp = nowOrZero();
    // Only ever forward: a reader who pages back to check something has not
    // un-read the article, and Instapaper resolves conflicts by timestamp, so
    // sending a smaller value with a newer stamp would drag the phone
    // backwards too.
    //
    // An unset clock withholds the TIMESTAMP, not the position. The position
    // is still worth having -- it is what reopens this article where it was
    // left -- and without a stamp the bridge simply does not offer it to
    // Instapaper, which is the honest outcome for a device that cannot say
    // when anything happened.
    if (progress > article->progress) {
      article->progress = progress;
      article->progressAt = stamp;
      article->progressDirty = stamp > 0;
      library_.saveIndex();
    }
  }
  openId_ = 0;
  document_.clear();
  document_.shrink_to_fit();
  showQueue();
}

void InstapaperActivity::archiveCurrent() {
  // The id is taken BEFORE closing, because closeArticle() clears it -- and
  // closing first is what banks the reading position, so an article archived
  // half-read still tells Instapaper where the reader got to.
  const int64_t id = openId_;
  if (id == 0) return;
  closeArticle();

  instapaper::Article* article = library_.find(id);
  if (article == nullptr) return;
  // Recorded here, sent on the next sync. The intent survives a reboot and is
  // re-sent until the bridge confirms it, which is safe because archiving an
  // already-archived bookmark is a no-op on Instapaper's side. The row
  // disappears from the queue immediately, because "did I press it?" is worse
  // than a row that comes back if the sync fails -- and it cannot come back:
  // the flag is on the card.
  article->archivePending = true;
  library_.saveIndex();
  rowsFitted_ = false;
  showQueue();
}

void InstapaperActivity::turnPage(const int delta) {
  if (phase_ != Phase::Reading || visibleLines_ == 0) return;
  const uint32_t span = visibleLines_;
  const uint32_t maxTop = lineCount_ > span ? lineCount_ - span : 0;
  if (delta > 0) {
    topLine_ = topLine_ + span > maxTop ? maxTop : topLine_ + span;
  } else {
    topLine_ = topLine_ > span ? topLine_ - span : 0;
  }
  requestUpdate();
}

// --- Rendering -----------------------------------------------------------

void InstapaperActivity::render(RenderLock&&) {
  renderer.clearScreen();
  // The reading cut in the body slot. This app is a page of text, not a board,
  // and at the 20px UI cut a 480px panel holds about 28 characters a line.
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, toybox::readingFaces());
  const fui::DeviceContext device = target.deviceContext();
  const fui::ThemeTokens& tokens = toybox::themeTokens();
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, device, noInput, interactions_);
  toybox::Screen screen(frame);

  const char* what = "Instapaper";

  switch (phase_) {
    case Phase::Busy: {
      instapaperui::NoticeModel model;
      model.headline = busyMessage_;
      model.message = busyDetail_;
      instapaperui::buildNotice(screen, model);
      what = "Instapaper busy";
      break;
    }

    case Phase::Notice: {
      instapaperui::NoticeModel model;
      model.headline = noticeHeadline_.c_str();
      model.message = noticeMessage_.c_str();
      if (noticeUnreadable_) model.mark = &icon_unreadable_32;
      model.actionLabel = "BACK TO THE LIST";
      instapaperui::buildNotice(screen, model);
      what = "Instapaper notice";
      break;
    }

    case Phase::Pairing: {
      const fui::Rect qr = instapaperui::buildPairQr(screen, pairCode_.c_str());
      QrUtils::drawQrCode(renderer, Rect{qr.x, qr.y, qr.width, qr.height},
                          instapaper::Sync::pairUrl(pairCode_));
      what = "Instapaper pairing";
      break;
    }

    case Phase::PairConfirm: {
      const instapaperui::PairConfirmLayout layout = instapaperui::buildPairConfirm(screen);
      // The READING cut, not the display cut, and two lines rather than one.
      // An Instapaper username is an email address: one long unbreakable
      // token, which at the display cut does not fit on any line of this panel
      // and cannot be broken between words because it has none. Drawn large it
      // came out as "..." -- a confirmation screen with no account on it.
      //
      // Two lines of the reading cut hold about fifty characters, which is
      // every real address, and fitLines now cuts mid-token rather than
      // vanishing if one ever exceeds that.
      fui::TextStyle name = tokens.bodyText;
      name.align = fui::TextAlign::Center;
      name.color = fui::Color::Black;
      name.maxLines = 2;
      const std::string shown =
          toybox::fitLines(target, pendingUser_.c_str(), layout.username.width, 2, name);
      target.text(layout.username, shown.c_str(), name);
      screen.frame().hit(layout.pill, instapaperui::ActionPairConfirm, 0);
      what = "Instapaper confirm";
      break;
    }

    case Phase::Reading: {
      // Measured here because measuring needs a draw target, and measured from
      // the same rect the text is drawn into: readerBody() is the one function
      // that owns that rectangle, so a page turn cannot skip a line and the
      // progress sent to Instapaper is computed against what was really shown.
      const fui::Rect body = instapaperui::readerBody(device);
      const int16_t lineHeight = target.lineHeight(tokens.bodyText.font);
      visibleLines_ = fui::textAreaVisibleLines(body, lineHeight);
      const uint32_t measured =
          fui::textAreaMeasure(target, body.width, document_.c_str(), tokens.bodyText, 0).lineCount;

      if (lineCount_ == 0 && measured > 0) {
        // First paint of this opening: put the reader back where they were.
        // Placed here rather than in openArticle() because the line count does
        // not exist until something has measured the text.
        lineCount_ = measured;
        const instapaper::Article* article = library_.find(openId_);
        if (article != nullptr && article->progress > 0.0f && article->progress < 1.0f) {
          const uint32_t span = visibleLines_ > 0 ? visibleLines_ : 1;
          const uint32_t maxTop = lineCount_ > span ? lineCount_ - span : 0;
          const uint32_t restored = static_cast<uint32_t>(article->progress * static_cast<float>(lineCount_));
          topLine_ = restored > maxTop ? maxTop : restored;
        }
      } else {
        lineCount_ = measured;
      }

      const uint32_t pages = visibleLines_ > 0 ? (lineCount_ + visibleLines_ - 1) / visibleLines_ : 1;
      const uint32_t page = visibleLines_ > 0 ? topLine_ / visibleLines_ + 1 : 1;
      std::snprintf(pageLabel_, sizeof(pageLabel_), "%lu / %lu", static_cast<unsigned long>(page),
                    static_cast<unsigned long>(pages < 1 ? 1 : pages));

      instapaperui::ReaderModel model;
      model.title = readerTitle_.c_str();
      model.text = document_.c_str();
      model.topLine = topLine_;
      model.pageLabel = pageLabel_;
      model.canPagePrev = topLine_ > 0;
      model.canPageNext = visibleLines_ > 0 && topLine_ + visibleLines_ < lineCount_;
      instapaperui::buildReader(screen, model);
      what = "Instapaper reader";
      break;
    }

    case Phase::Queue: {
      if (!rowsFitted_) {
        // Fit each title to the width the component will actually give it,
        // breaking between words and marking what was dropped. Done once per
        // change rather than per paint: the answer only moves when the queue
        // or the fonts do.
        library_.load();
        const std::vector<const instapaper::Article*> shown = instapaper::visible(library_.articles());
        rowIds_.clear();
        rowLabels_.clear();
        rowSubtitles_.clear();
        rowValues_.clear();
        rows_.clear();

        fui::TextStyle titleStyle = tokens.bodyText;
        titleStyle.maxLines = 1;
        const int16_t titleWidth = instapaperui::queueTitleWidth(target, device, tokens);
        for (const instapaper::Article* article : shown) {
          rowIds_.push_back(article->id);
          const char* title = article->title.empty() ? article->domain.c_str() : article->title.c_str();
          rowLabels_.push_back(toybox::fitLines(target, title, titleWidth, 1, titleStyle));
          char subtitle[64];
          std::snprintf(subtitle, sizeof(subtitle), "%u min%s%s", static_cast<unsigned>(article->minutes),
                        article->domain.empty() ? "" : ", ", article->domain.c_str());
          rowSubtitles_.push_back(subtitle);
          char value[8] = "";
          if (article->progress >= 0.995f) {
            std::snprintf(value, sizeof(value), "DONE");
          } else if (article->progress > 0.01f) {
            std::snprintf(value, sizeof(value), "%d%%", static_cast<int>(article->progress * 100.0f + 0.5f));
          }
          rowValues_.push_back(value);
        }
        // A second pass, because a push_back can reallocate and ListItem holds
        // pointers rather than copies.
        rows_.reserve(rowLabels_.size());
        for (size_t i = 0; i < rowLabels_.size(); ++i) {
          fui::ListItem row;
          row.label = rowLabels_[i].c_str();
          row.subtitle = rowSubtitles_[i].c_str();
          row.value = rowValues_[i].c_str();
          row.actionValue = static_cast<int16_t>(i);
          rows_.push_back(row);
        }

        // The clock time and nothing else. "SYNCED 14:32" and "NEVER SYNCED"
        // both pushed INSTAPAPER off its own header band, and the renderer
        // marks that overflow with U+2026 -- which these cuts have no glyph
        // for, so the title simply read "INSTAPA" with nothing to say it had
        // been cut. Found by looking at the render; a build cannot see it and
        // neither can a log.
        //
        // Measured afterwards, against a 448px band interior, and the numbers
        // are worth keeping because they say how little room there is:
        //
        //   INSTAPAPER @toybox_30                        292.8px
        //     + "SYNCED 14:32"                           487.6px  OVERFLOWS
        //     + "NEVER SYNCED"                           511.4px  OVERFLOWS
        //     + "14:32"                                  366.3px  fits, 82 spare
        //
        // The trap in taking that measurement: the right label draws with the
        // theme's smallText, and ToyboxTokens sets smallText.font = kUiFont --
        // the BODY slot, which under readingFaces() is reading_serif_14 and
        // not the small cut it reads like. Measured in toybox_10 the same
        // string comes to 110px and every combination "fits", which contradicts
        // what the panel actually did. Measure in the face the call site
        // RESOLVES, not the one its name suggests.
        //
        // An empty label is right rather than a placeholder: a queue with
        // articles in it has synced by definition, so "never" only ever
        // appears beside an empty list that already says what to do.
        if (bridge_.lastSyncAt > 0) {
          const time_t when = static_cast<time_t>(bridge_.lastSyncAt);
          struct tm parts {};
          localtime_r(&when, &parts);
          std::snprintf(lastSyncLabel_, sizeof(lastSyncLabel_), "%02d:%02d", parts.tm_hour, parts.tm_min);
        } else {
          lastSyncLabel_[0] = '\0';
        }
        rowsFitted_ = true;
      }

      // The same row height the list will draw with, from the same function.
      // Computing it a second time here is how a list scrolls by a different
      // number of rows than it shows.
      const int16_t rowHeight = instapaperui::queueRowHeight(target, tokens);
      visibleRows_ = fui::listVisibleRows(instapaperui::queueBand(device), rowHeight, tokens.listRowGap);
      if (visibleRows_ > 0) {
        const int maxTop = static_cast<int>(rows_.size()) - visibleRows_;
        if (topIndex_ > maxTop) topIndex_ = maxTop < 0 ? 0 : maxTop;
        if (topIndex_ < 0) topIndex_ = 0;
      }

      instapaperui::QueueModel model;
      model.items = rows_.empty() ? nullptr : rows_.data();
      model.count = static_cast<int>(rows_.size());
      model.selected = selected_;
      model.topIndex = topIndex_;
      model.lastSync = lastSyncLabel_;
      instapaperui::buildQueue(screen, model);
      what = "Instapaper queue";
      break;
    }
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, what);

  // The two side keys do different things on the two screens that use them, so
  // the hints have to say which. Everything else here is reached by tapping,
  // because four of the six logical buttons are unassigned pins on the X4 Pro
  // and a control only a key can reach is a control nobody can reach.
  const bool reading = phase_ == Phase::Reading;
  const auto labels = mappedInput.mapLabels("Back", "", reading ? "Prev" : "Up", reading ? "Next" : "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
