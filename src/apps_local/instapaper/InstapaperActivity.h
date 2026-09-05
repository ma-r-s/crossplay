#pragma once

// Instapaper, read on the device.
//
// ---------------------------------------------------------------------------
// The shape of it, and the three decisions worth knowing.
//
// 1. Nothing slow happens on the render path. A network step is REQUESTED by
//    setting `step_` and asking for a repaint; the loop performs it on the
//    following pass, once the screen announcing it is already on the panel.
//    Upstream's font downloader does the opposite and pins the main loop for
//    forty seconds with no repaint and no input, which is indistinguishable
//    from a crash.
//
// 2. One network operation per loop pass, never a chain. A sync is start,
//    poll, poll, download, download, ... and each of those returns to the
//    loop, so Back always answers and the caption can say which article is
//    arriving. A single call that did the whole cycle would be a minute of
//    frozen panel.
//
// 3. The reading position IS the pager. Instapaper defines progress as the top
//    edge of the viewport over the article's length, which is topLine_ over
//    lineCount_ -- so there is no second model of where the reader is, and
//    nothing to keep in step.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../ui/ToyboxFormat.h"
#include "../ui/ToyboxScreen.h"
#include "../ui/ToyboxWrappedText.h"
#include "InstapaperIndex.h"
#include "InstapaperLibrary.h"
#include "InstapaperScreens.h"
#include "InstapaperSync.h"

class InstapaperActivity final : public Activity {
 public:
  InstapaperActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Instapaper", renderer, mappedInput) {}
  ~InstapaperActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void onExit() override;
  void render(RenderLock&&) override;

 private:
  // What is on the screen right now.
  enum class Phase : uint8_t {
    Queue,        // the reading list
    Reading,      // one article
    Busy,         // a network step is about to happen or is happening
    Notice,       // a verdict, an error, or an article this reader cannot show
    Pairing,      // the QR and the code
    PairConfirm,  // "is this you?", before anything is stored
  };

  // The next thing the loop should do. One step per pass; see decision 2.
  enum class Step : uint8_t {
    None,
    PairStart,
    PairPoll,
    SyncStart,
    SyncPoll,
    Download,
  };

  void onWifiChosen(bool connected);
  // Announce on screen, then act on the following pass.
  void request(Step next, const char* busyMessage);
  void needNetwork(Step next, const char* busyMessage);

  bool doPairStart();
  bool doPairPoll();
  bool doSyncStart();
  bool doSyncPoll();
  bool doDownload();
  void finishSync();

  void openArticle(int64_t id);
  void closeArticle();
  void archiveCurrent();
  void undoArchive();
  void turnPage(int delta);
  void showNotice(const char* headline, std::string message, bool unreadable = false);
  void showQueue();

  // Epoch seconds, or 0 when the clock has never been set. A progress stamp
  // from an unset clock is worse than none: the bridge would either drop it or
  // pass a wrong time to Instapaper, and Instapaper resolves conflicts by
  // time.
  static uint32_t nowOrZero();

  instapaper::Library library_;
  instapaper::BridgeState bridge_;
  instapaper::Sync sync_;

  // Pairing, in flight.
  std::string pairCode_;
  std::string pollToken_;
  std::string pendingToken_;
  std::string pendingUser_;

  // A sync, in flight.
  std::string jobId_;
  instapaper::SyncSummary summary_;
  std::vector<int64_t> toDownload_;
  size_t downloadIndex_ = 0;
  int downloaded_ = 0;
  int downloadFailures_ = 0;
  // When contact with the bridge was first lost, or 0 while polls are getting
  // through. See doSyncPoll: the job outlives a lost packet, so the sync does.
  uint32_t pollMissedSinceMs_ = 0;
  // Reading positions this sync PUT UP, counted where they are put up -- the
  // verdict screen has no other way to know. Archives are not counted here:
  // the summary comes back saying which ones the service really took, and that
  // is the honest number to say out loud.
  int sentPositions_ = 0;
  bool cancelDownload_ = false;
  char busyDetail_[48] = "";

  // Reading.
  int64_t openId_ = 0;
  std::string document_;
  // The article wrapped to the reader's width, kept between paints. It holds
  // no copy of the document and re-wraps itself whenever the panel, the cut or
  // the text stops matching what it wrapped; see ToyboxWrappedText.h for why
  // that is probed rather than announced.
  toybox::WrappedText wrap_;
  std::string readerTitle_;
  uint32_t topLine_ = 0;
  uint32_t lineCount_ = 0;
  uint16_t visibleLines_ = 0;
  // "%lu / %lu", sized from that format rather than from the page counts a
  // reasonable article produces.
  static constexpr int kPageLabelCap = 2 * toybox::kULongChars + toybox::literalChars(" / ") + 1;
  char pageLabel_[kPageLabelCap] = "";

  // The article ARCHIVE was last pressed on, while it can still be taken back:
  // until the next sync carries the intent up, until another article is
  // opened, or until the app is left. In memory only -- an offer to undo
  // something the reader no longer remembers doing is not a kindness.
  int64_t undoArchiveId_ = 0;

  std::string noticeHeadline_;
  std::string noticeMessage_;
  bool noticeUnreadable_ = false;

  // Row strings, owned here because fui::ListItem holds pointers rather than
  // copies. Rebuilt whenever the queue changes.
  std::vector<int64_t> rowIds_;
  std::vector<std::string> rowLabels_;
  std::vector<std::string> rowSubtitles_;
  std::vector<std::string> rowValues_;
  std::vector<freeink::ui::ListItem> rows_;
  bool rowsFitted_ = false;
  int topIndex_ = 0;
  int visibleRows_ = 0;
  char lastSyncLabel_[24] = "";

  Phase phase_ = Phase::Queue;
  Step step_ = Step::None;
  const char* busyMessage_ = "";

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;

  // SYNC on the queue and BACK TO THE LIST on a notice compute the same rect:
  // (16,732,448,52) on this panel, from two separate expressions that can only
  // agree by accident. They are the control that SUMMONS a verdict and the one
  // that DISMISSES it, on the same pixels.
  //
  // So a second tap across a phase change acts on a screen the user has not
  // read yet: sync, tap again because e-ink looks like nothing happened, and
  // the verdict -- "3 did not arrive; sync again" or the bridge's reason for
  // refusing -- is gone before it was seen. The reverse costs a spurious sync.
  //
  // Not fixed by moving the button: y=732 is the fork-wide primary-action band
  // that twenty screen files use, and the band is not the problem. Fixed by
  // ignoring taps for a moment after a screen becomes VISIBLE, stamped after
  // displayBuffer() rather than when the state changed, because what matters
  // is when a person could first have seen it.
  static constexpr uint32_t kSettleMs = 600;
  Phase lastShownPhase_ = Phase::Queue;
  uint32_t phaseShownAtMs_ = 0;
  bool everShown_ = false;
};
