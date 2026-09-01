#pragma once

// Hacker News, read on the device.
//
// ---------------------------------------------------------------------------
// The shape of it, and the two decisions worth knowing.
//
// 1. Every network round trip is one request. The front page is one GET to
//    Algolia's search endpoint and a whole comment thread is one GET to its
//    items endpoint, already nested. The official Firebase API needs a request
//    per item, so the same two screens would cost 31 and 58 round trips over
//    TLS. See HackerNewsCore.h.
//
// 2. Nothing slow happens on the render path. A fetch is *requested* by setting
//    `pending_` and asking for a repaint; the loop performs it on the following
//    pass, once the loading screen is already on the panel. Upstream's font
//    downloader does the opposite and pins the main loop for forty seconds with
//    no repaint and no input, which is indistinguishable from a crash and shows
//    up in the log as `New max loop duration: 39864 ms`.
// ---------------------------------------------------------------------------
//
// The reading itself is deliberately not clever. An article and a thread both
// become one flat, wrapped document that pages a screenful at a time, because
// that is what the panel is good at.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "HackerNewsCore.h"
#include "HackerNewsLibrary.h"
#include "HackerNewsRows.h"
#include "HackerNewsScreens.h"

class HackerNewsActivity final : public Activity {
 public:
  HackerNewsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("HackerNews", renderer, mappedInput) {}
  ~HackerNewsActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void onExit() override;
  void render(RenderLock&&) override;

 private:
  // What is on the screen right now.
  enum class Phase : uint8_t {
    Connecting,  // the Wi-Fi picker is up as a child activity
    Busy,        // a fetch is about to happen or is happening
    List,        // the front page
    Reading,     // an article or a thread
    Notice,      // an error, or a link this device cannot read
  };

  // What the next loop pass should fetch. Set alongside a repaint request so
  // the work always starts after the screen showing it has been drawn.
  enum class Pending : uint8_t { None, FrontPage, Article, Comments };

  void onWifiChosen(bool connected);
  // Leaving the app when nothing is saved, showing the saved shelf when
  // something is. One decision with two callers, because it has two.
  void leaveOrShowSaved();
  void request(Pending what, const char* busyMessage);

  bool fetchFrontPage();
  bool fetchArticle();
  bool fetchComments();

  // Reading state, shared by an article and a thread because they are the same
  // object once the words are extracted.
  void showDocument(const char* title, bool comments);
  // The saved shelf. save/unsave act on whatever the reader currently holds;
  // openSavedArticle restores one from the card without going near the network.
  void saveCurrentArticle();
  void unsaveCurrentArticle();
  void openSavedArticle(int index);
  void repage();
  void turnPage(int delta);
  // One page of story rows, in either direction, wrapping at both ends.
  //
  // The one place the side keys and a swipe agree on what a page is, through
  // the same arithmetic the shelf pages by. It counts the rows that were DRAWN
  // rather than the stories that were fetched: the saved shelf is a different
  // length from the front page, and paging it by the front page's count either
  // did nothing or jumped to wherever the paint clamped it back to.
  void pageList(int delta);
  void showNotice(const char* headline, const char* message, bool unreadable);

  const hn::Story* currentStory() const;

  std::vector<hn::Story> stories_;
  int selected_ = 0;
  int topIndex_ = 0;
  // How many story rows fit, measured in render() and read by loop() so the
  // side keys can page. Zero until the list has been drawn once.
  int visibleRows_ = 0;

  // The flattened document the reader draws, and where in it we are.
  std::string document_;
  std::string readerTitle_;
  // The article's own URL, which is the library's key. Held because the reader
  // has to be able to say whether what it is showing is saved, and a title is
  // not unique.
  std::string readerUrl_;
  bool readingComments_ = false;
  // Which half of the library the list is showing. Stored as the same type the
  // rows record themselves as built for, so the two cannot drift: see
  // HackerNewsRows.h for the bug that cost.
  hn::ListView view_ = hn::ListView::FrontPage;
  // Whether the reader was opened out of the library rather than off the front
  // page. Back honours it: a saved article returns to the shelf it came from.
  bool readingSaved_ = false;
  hn::Library library_;
  bool articleAvailable_ = false;
  uint32_t topLine_ = 0;
  uint32_t lineCount_ = 0;
  uint16_t visibleLines_ = 0;
  char pageLabel_[16] = "";

  std::string noticeHeadline_;
  std::string noticeMessage_;
  bool noticeUnreadable_ = false;

  // The strings the list draws, owned here because fui::ListItem holds pointers
  // rather than copies, and tagged with the shelf they came from.
  hn::Rows rows_;
  // Rebuilt from rows_.labels whenever those are refitted, for the same
  // pointer-stability reason.
  std::vector<freeink::ui::ListItem> listItems_;

  // Whether the Back press that a release belongs to arrived while this
  // activity was on top. See loop(): the Wi-Fi picker cancels on the press.
  bool backPressSeen_ = false;

  Phase phase_ = Phase::Connecting;
  Pending pending_ = Pending::None;
  const char* busyMessage_ = "";

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
