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
#include "../ui/ToyboxScreen.h"
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
  std::string readerTitle_;
  uint32_t topLine_ = 0;
  uint32_t lineCount_ = 0;
  uint16_t visibleLines_ = 0;
  char pageLabel_[16] = "";

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
};
