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
#include "../ui/ToyboxTheme.h"  // GfxRendererTarget, for the per-row label pass
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

  // Whether the radio has been brought up in this session. Not a guess at
  // whether the internet exists -- only whether we have already asked, so the
  // picker appears once and never again.
  enum class Link : uint8_t { Down, Connected };

  void onWifiChosen(bool connected);
  // Runs `what` if the radio is already up, otherwise raises the picker and
  // runs it afterwards. Every network action goes through here, so there is one
  // place that decides when a connection is asked for.
  void ensureConnected(Pending what, const char* busyMessage);
  void request(Pending what, const char* busyMessage);

  bool fetchFrontPage();
  void buildStoryRows();
  // Draws the story headlines over the rows the component laid out. Ours
  // because each row may be set at a different cut, and a list component has
  // one label style for the whole list.
  void drawRowLabels(freeink::ui::GfxRendererTarget& target, const freeink::ui::DeviceContext& device,
                     const freeink::ui::ThemeTokens& tokens, int count, int selected, int topIndex);
  // The saved library, which needs no account and no network.
  void saveCurrentArticle();
  void unsaveCurrentArticle();
  bool openSavedArticle(int index);
  void buildSavedRows();
  bool fetchArticle();
  bool fetchComments();

  // Reading state, shared by an article and a thread because they are the same
  // object once the words are extracted.
  void showDocument(const char* title, bool comments);
  void addBlock(std::string text, int depth = 0, bool isAuthor = false);
  void wrapBlocks(const freeink::ui::DrawTarget& target, const freeink::ui::DeviceContext& device,
                  const freeink::ui::ThemeTokens& tokens);
  void repage();
  void turnPage(int delta);
  void showNotice(const char* headline, const char* message, bool unreadable);
  void showNotice(const char* headline, const char* story, const char* message, bool unreadable);

  const hn::Story* currentStory() const;

  std::vector<hn::Story> stories_;
  hn::Library library_;
  bool showingSaved_ = false;
  // Set while the reader is showing something opened from the library rather
  // than from the front page, so Back knows which list to return to.
  bool readingSaved_ = false;
  // The story the reader is on, kept because a saved article has no row in
  // stories_ to look it up from.
  std::string readerUrl_;
  int selected_ = 0;
  int topIndex_ = 0;

  // What the reader draws: the story as depth-tagged paragraphs, plus the
  // wrapped lines those become. Paragraphs are kept because wrapping needs a
  // draw target, which only exists inside render().
  struct Block {
    std::string text;
    int depth = 0;
    bool isAuthor = false;
  };
  std::vector<Block> blocks_;
  std::vector<std::string> lineText_;
  std::vector<const char*> linePtr_;
  std::vector<hnui::ReaderLine> lineMeta_;
  bool linesWrapped_ = false;

  std::string readerTitle_;
  std::string readerTitleFitted_;  // that title, cut to the band's two lines
  bool readingComments_ = false;
  bool articleAvailable_ = false;
  uint32_t topLine_ = 0;
  uint32_t lineCount_ = 0;
  uint16_t visibleLines_ = 0;
  char pageLabel_[16] = "";

  std::string noticeHeadline_;
  std::string noticeStory_;   // the story a notice is about, if any
  std::string noticeFitted_;  // that title, cut to the display cut's two lines
  std::string noticeMessage_;
  bool noticeUnreadable_ = false;

  // Row labels, owned here because fui::ListItem holds pointers rather than
  // copies. Parallel to stories_ and rebuilt with it.
  std::vector<std::string> rowTitles_;  // as Hacker News wrote them
  std::vector<std::string> rowLabels_;  // fitted to the row, ellipsised
  std::vector<int> rowFonts_;           // the cut each was fitted at
  std::vector<std::string> rowValues_;  // the comment count
  bool rowsFitted_ = false;
  std::vector<freeink::ui::ListItem> rows_;

  Phase phase_ = Phase::Connecting;
  Pending pending_ = Pending::None;
  Link link_ = Link::Down;
  Pending afterConnect_ = Pending::None;
  const char* afterConnectMessage_ = "";
  // Where to land if the picker is cancelled. Backing out of a connection
  // returns you to what you were looking at; it is not a way out of the app.
  Phase returnPhase_ = Phase::List;
  // Set once a front page fetch has actually failed, so the empty list can tell
  // "not loaded yet" from "tried and could not", which are different screens.
  bool frontPageFailed_ = false;
  const char* busyMessage_ = "";

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
