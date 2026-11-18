#pragma once

// RSS: the sites you read, newest first, as text.
//
// ---------------------------------------------------------------------------
// The shape of it, and the decisions worth knowing.
//
// 1. Feeds are read when the app opens, over Wi-Fi, and at no other time.
//    Nothing runs in the background and nothing runs asleep. What was read
//    last time is on the card, so the list opens with no radio at all and the
//    refresh only replaces it.
//
// 2. A post the feed carried whole is read off the card. A post the feed only
//    linked to goes through the same text extractor Hacker News uses, and the
//    same readability gate decides whether what came back is an article. When
//    it is not, the feed's own summary is what there is.
//
// 3. Feeds are added from a phone, the way Notes takes its typing: the reader
//    joins Wi-Fi, serves one page, and shows a QR code. The phone page asks;
//    this activity answers from its own loop (RssDesk.h says why).
//
// 4. No server of ours and no account. The reader fetches each feed from its
//    own site, and nothing else is involved but the extractor in (2).
//
// The three-way split every app here uses: RssCore and RssFeed are the rules
// and the parsing (host-tests/rss), RssScreens lays out (host-tests/ui), and
// this file keeps what needs hardware: the radio, the card and the panel.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../hackernews/HackerNewsLibrary.h"
#include "../ui/ToyboxFormat.h"
#include "../ui/ToyboxScreen.h"
#include "../ui/ToyboxWrappedText.h"
#include "RssCore.h"
#include "RssDesk.h"
#include "RssScreens.h"

class RssServer;

class RssActivity final : public Activity {
 public:
  // Out of line, both of them, because the server is only declared here.
  RssActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~RssActivity() override;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void onExit() override;
  void render(RenderLock&&) override;
  // Not while a refresh or a fetch is under way, and not while a phone may be
  // talking to the page: a reader that sleeps mid-sentence looks broken.
  bool preventAutoSleep() override { return phase_ == Phase::Busy || phase_ == Phase::Phone; }
  bool skipLoopDelay() override { return phase_ == Phase::Phone; }

 private:
  enum class Phase : uint8_t {
    Busy,     // a refresh, an article, or adding a feed
    List,     // posts, feeds or saved, by tab_
    Reading,  // one post
    Notice,   // a refusal, with a way back
    Phone,    // the QR code and the page behind it
  };
  // What the next loop pass does. Set alongside a repaint so the work starts
  // after the screen announcing it is on the panel.
  enum class Pending : uint8_t { None, Refresh, Article, Phone, AddFeed };

  // --- The radio -------------------------------------------------------
  // Runs `what` once the radio is up, raising the picker first when it is not.
  // Declining the picker leaves things as they were: the list still reads.
  void ensureConnected(Pending what);
  void request(Pending what);

  // --- Feeds -----------------------------------------------------------
  void loadAll();
  bool saveFeeds();
  bool savePosts();
  void saveRead();
  // Every feed, one after another, in this one loop pass, painting between
  // them. Back or STOP ends it after the feed in hand; what was read stands.
  void refreshAll();
  struct FetchResult {
    bool ok = false;
    int status = 0;
    std::string title;
    std::vector<rss::Post> posts;
  };
  // One feed read off the socket into posts and body files. `probe` reads an
  // address that may be a page instead, and fills `discovered` with the feeds
  // the page names.
  FetchResult fetchFeed(const std::string& url, bool probe, std::vector<std::string>* discovered);
  // Folds freshly read posts into the list (rss::combine), then writes the
  // feeds, the posts and the read marks, and drops bodies nothing points at.
  void adopt(const std::vector<rss::Post>& fresh);
  void pruneBodies();
  // While a feed downloads: pumps input for STOP during a refresh, serves the
  // phone page during an add. True abandons the download.
  bool pollDuringFetch();
  // A body this read wrote and a newer post has pushed out again.
  void dropBodyWrittenNow(const std::string& id, std::vector<std::string>& written);
  // Pumps input while a refresh holds the loop, and answers Back, Home and a
  // tap on STOP. See docs/apps/rss.md for why this one place may do that.
  void pumpRefreshInput();

  // --- Lists -----------------------------------------------------------
  void showTab(rssui::Tab tab);
  void openFeed(int feedIndex);
  // The rows of whatever the list is showing, as indices: into posts_ for a
  // posts list, into feeds_ for the feeds, into the saved shelf for saved.
  void rebuildView();
  void pageList(int delta);
  int feedIndexForKey(const std::string& key) const;
  std::string siteForSaved(const std::string& url) const;

  // --- Reading ---------------------------------------------------------
  // The row a tap named, as an index into posts_ or the saved shelf; -1 when it
  // names nothing.
  int viewAt(int viewIndex);
  void openPost(int viewIndex);
  bool fetchArticle();
  void showDocument(const rss::Post& post, const std::string& words, const char* note);
  // The post being opened, from its feed's summary, when the radio was
  // declined: what there is, said to be all there is.
  void showSummaryOffline();
  void openSaved(int viewIndex);
  void saveCurrent();
  void unsaveCurrent();
  void turnPage(int delta);
  void returnToList();
  void showNotice(const char* headline, const char* message, bool unreadable);
  std::string bodyPath(const std::string& id, rss::Body body) const;

  // --- The phone -------------------------------------------------------
  void startPhone();
  void stopPhone();
  void servePhone();
  void addFeed();
  void removeFeed(const rss::Request& request);
  void publishToDesk();
  // An address that would have the reader fetch from itself (see
  // rss::isLocalAddress), or its own address on the network.
  bool pointsAtThisReader(const std::string& url) const;
  std::string feedCountLine() const;

  // --- State -----------------------------------------------------------
  std::vector<rss::Feed> feeds_;
  std::vector<rss::Post> posts_;
  rss::ReadSet read_;
  hn::Library library_{"/.crosspoint/rss/saved"};

  rssui::Tab tab_ = rssui::Tab::All;
  // The feed the posts list is narrowed to, or -1 for every feed.
  int feedFilter_ = -1;
  std::vector<int> view_;
  bool viewStale_ = true;
  int topIndex_ = 0;
  int visibleRows_ = 0;
  // The row last opened in the list on screen, drawn inverted, or -1.
  int selected_ = -1;
  // The strings the list window draws, owned here because fui::ListItem holds
  // pointers. Rebuilt every paint for the rows on the page and no others.
  std::vector<std::string> labels_;
  std::vector<std::string> values_;
  std::vector<freeink::ui::ListItem> items_;
  std::string bandTitle_;

  // The post being read.
  int openPostIndex_ = -1;
  std::string document_;
  toybox::WrappedText wrap_;
  std::string readerTitle_;
  // The saved shelf's key for what is on screen: the post's link, or an
  // address made from its id when the feed gave it none.
  std::string readerKey_;
  bool readingSaved_ = false;
  bool saveFailedNotice_ = false;
  uint32_t topLine_ = 0;
  uint32_t lineCount_ = 0;
  uint16_t visibleLines_ = 0;
  static constexpr int kPageLabelCap = 2 * toybox::kULongChars + toybox::literalChars("/") + 1;
  char pageLabel_[kPageLabelCap] = "";

  std::string noticeHeadline_;
  std::string noticeMessage_;
  bool noticeUnreadable_ = false;

  std::string busyHeadline_;
  std::string busyMessage_;
  bool busyStoppable_ = false;
  bool stopRefresh_ = false;

  Phase phase_ = Phase::List;
  Pending pending_ = Pending::None;
  Pending afterConnect_ = Pending::None;
  // Opening on a refresh is asked for in the first loop pass, not in onEnter,
  // because the picker is an activity and onEnter is too early to start one.
  bool refreshOnFirstPass_ = false;
  // See HackerNewsActivity: a Back RELEASE only means "go back" when this
  // activity also saw the press, because the picker acts on the press.
  bool backPressSeen_ = false;

  // A list on the card that is there and could not be read. Nothing is written
  // over it for as long as this app is open.
  bool cardRefused_ = false;

  rss::Desk desk_;
  std::unique_ptr<RssServer> server_;
  bool devPaused_ = false;
  std::string phoneUrl_;
  std::string phoneReadable_;
  std::string phoneStatus_;
  rss::Request addRequest_;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
