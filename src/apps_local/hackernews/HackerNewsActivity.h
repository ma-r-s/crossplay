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
  void buildSavedRows();
  void repage();
  void turnPage(int delta);
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
  // Which half of the library the list is showing.
  bool showingSaved_ = false;
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

  // Row labels, owned here because fui::ListItem holds pointers rather than
  // copies. Parallel to stories_ and rebuilt with it.
  std::vector<std::string> rowTitles_;  // as Hacker News wrote them
  std::vector<std::string> rowLabels_;  // fitted to the row, ellipsised
  std::vector<std::string> rowValues_;  // the comment count
  bool rowsFitted_ = false;
  std::vector<freeink::ui::ListItem> rows_;

  Phase phase_ = Phase::Connecting;
  Pending pending_ = Pending::None;
  const char* busyMessage_ = "";

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
