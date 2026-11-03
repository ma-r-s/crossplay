#include "RssActivity.h"

#include <ESPmDNS.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "../../DevMode.h"
#include "../../SilentRestart.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../components/UITheme.h"
#include "../../network/HttpDownloader.h"
#include "../../util/DeviceHostname.h"
#include "../../util/QrUtils.h"
#include "../Shelf.h"
#include "../ShelfScreen.h"
#include "../hackernews/HackerNewsCore.h"
#include "../hackernews/HackerNewsRows.h"
#include "../hackernews/HackerNewsScreens.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxText.h"
#include "../ui/ToyboxTheme.h"
#include "RssFeed.h"
#include "RssServer.h"

namespace fui = freeink::ui;

namespace {

// Plain files in a folder of their own, the way Hacker News keeps its shelf:
// readable in an editor, recoverable by hand, still there if the app is not.
constexpr const char* kDir = "/.crosspoint/rss";
constexpr const char* kFeedsPath = "/.crosspoint/rss/feeds.tsv";
constexpr const char* kPostsPath = "/.crosspoint/rss/posts.tsv";
constexpr const char* kReadPath = "/.crosspoint/rss/read.txt";
// One file per post the feed carried words for, named by the post's id.
constexpr const char* kBodiesDir = "/.crosspoint/rss/posts";

// Ceilings on what is read back off the card. Each is many times what the
// limits in RssCore.h can produce, so they only ever stop a damaged file.
constexpr size_t kMaxIndexBytes = 512u * 1024u;
constexpr size_t kMaxBodyBytes = 256u * 1024u;

// What a refused feed says, on the device and on the phone alike.
constexpr const char* kNoFeedThere = "No feed at that address.";

bool readText(const char* path, const size_t cap, std::string& out) {
  out.clear();
  if (!Storage.exists(path)) return false;
  return Storage.readFileToString("RSS", path, cap, out);
}

// Beside itself, then moved over the old one: openFileForWrite truncates at
// open, so writing in place would leave an empty list behind a lost write.
bool writeText(const char* path, const std::string& text) {
  Storage.ensureDirectoryExists(kDir);
  const std::string tmp = std::string(path) + ".tmp";
  {
    HalFile file;
    if (!Storage.openFileForWrite("RSS", tmp.c_str(), file)) return false;
    if (!text.empty() && file.write(text.data(), text.size()) != text.size()) {
      file.close();
      Storage.remove(tmp.c_str());
      return false;
    }
    file.flush();
  }
  if (!Storage.replaceFile(tmp.c_str(), path)) {
    Storage.remove(tmp.c_str());
    return false;
  }
  return true;
}

uint32_t nowOrZero() {
  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  return now > static_cast<int64_t>(rss::kClockFloor) ? static_cast<uint32_t>(now) : 0u;
}

}  // namespace

std::unique_ptr<Activity> RssActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<RssActivity>(renderer, mappedInput);
}

RssActivity::RssActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Rss", renderer, mappedInput) {}

RssActivity::~RssActivity() = default;

// --- Lifecycle -----------------------------------------------------------

void RssActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  loadAll();
  library_.load();
  tab_ = rssui::Tab::All;
  feedFilter_ = -1;
  phase_ = Phase::List;
  viewStale_ = true;
  // The list goes up first, from the card, so there is something to read
  // whatever the radio says. With feeds to read, the first loop pass asks for
  // the radio and reads them; with none, nothing touches the radio at all.
  refreshOnFirstPass_ = !feeds_.empty();
  LOG_INF("RSS", "opened: %d feeds, %d posts, %d read, %d saved", static_cast<int>(feeds_.size()),
          static_cast<int>(posts_.size()), static_cast<int>(read_.ids().size()),
          static_cast<int>(library_.articles().size()));
  requestUpdate();
}

void RssActivity::onExit() {
  stopPhone();
  Activity::onExit();
  // The radio comes down with the app, unless Developer Mode raised it: see
  // HackerNewsActivity::onExit, whose teardown this is.
  if (WiFi.getMode() != WIFI_MODE_NULL && !devmode::holdsRadio()) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

// --- The card ------------------------------------------------------------

void RssActivity::loadAll() {
  std::string text;
  feeds_.clear();
  posts_.clear();
  read_.clear();
  if (readText(kFeedsPath, kMaxIndexBytes, text) && !rss::parseFeeds(text, feeds_)) {
    LOG_ERR("RSS", "feeds.tsv is not in our format; leaving it alone");
  }
  if (readText(kPostsPath, kMaxIndexBytes, text) && !rss::parsePosts(text, posts_)) {
    LOG_ERR("RSS", "posts.tsv is not in our format; leaving it alone");
  }
  if (readText(kReadPath, kMaxIndexBytes, text)) rss::parseRead(text, read_);
  // Posts of a feed that is no longer here are not shown, whatever the file
  // says: a feed removed while the card was elsewhere must not linger.
  posts_ = rss::combine(feeds_, posts_, {}, {});
}

bool RssActivity::saveFeeds() {
  if (writeText(kFeedsPath, rss::serializeFeeds(feeds_))) return true;
  LOG_ERR("RSS", "could not write the feeds");
  return false;
}

bool RssActivity::savePosts() {
  if (writeText(kPostsPath, rss::serializePosts(posts_))) return true;
  LOG_ERR("RSS", "could not write the posts");
  return false;
}

void RssActivity::saveRead() {
  if (!writeText(kReadPath, rss::serializeRead(read_))) LOG_ERR("RSS", "could not write the read marks");
}

std::string RssActivity::bodyPath(const std::string& id, const rss::Body body) const {
  // The kind is in the name, so a post whose feed starts carrying its whole
  // text is written again rather than read back as the summary it once was.
  return std::string(kBodiesDir) + "/" + id + (body == rss::Body::Full ? ".txt" : ".sum");
}

void RssActivity::pruneBodies() {
  // Every body no post points at any more: posts that scrolled out of their
  // feed, feeds that were removed. Listed rather than tracked, so a file left
  // by an interrupted refresh goes too.
  std::vector<std::string> wanted;
  wanted.reserve(posts_.size());
  for (const rss::Post& post : posts_) {
    if (post.body == rss::Body::None) continue;
    const std::string path = bodyPath(post.id, post.body);
    wanted.push_back(path.substr(path.rfind('/') + 1));
  }
  std::sort(wanted.begin(), wanted.end());
  const std::vector<String> files = Storage.listFiles(kBodiesDir, rss::kMaxFeeds * rss::kPostsPerFeed * 2 + 64);
  int removed = 0;
  for (const String& name : files) {
    const std::string file(name.c_str());
    if (std::binary_search(wanted.begin(), wanted.end(), file)) continue;
    Storage.remove((std::string(kBodiesDir) + "/" + file).c_str());
    ++removed;
  }
  if (removed > 0) LOG_INF("RSS", "pruned %d post files", removed);
}

// --- The radio -----------------------------------------------------------

void RssActivity::ensureConnected(const Pending what) {
  // Asked of the radio every time rather than latched; see HackerNewsActivity.
  if (WiFi.status() == WL_CONNECTED) {
    request(what);
    return;
  }
  afterConnect_ = what;
  // The press that may have been seen this frame belongs to the screen that is
  // about to stop being on top. See HackerNewsActivity::ensureConnected.
  backPressSeen_ = false;
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           const Pending what = afterConnect_;
                           afterConnect_ = Pending::None;
                           if (result.isCancelled) {
                             // Declining is not wanting out. The list is still
                             // the one read last time, and it still reads.
                             LOG_INF("RSS", "no Wi-Fi; showing what is on the card");
                             requestUpdate();
                             return;
                           }
                           request(what);
                         });
}

void RssActivity::request(const Pending what) {
  if (what == Pending::None) return;
  if (what == Pending::Phone) {
    // Nothing slow: the screen it opens is its own announcement.
    pending_ = what;
    requestUpdate();
    return;
  }
  {
    RenderLock lock(*this);
    pending_ = what;
    phase_ = Phase::Busy;
    busyStoppable_ = false;
    busyMessage_.clear();
    switch (what) {
      case Pending::Refresh:
        busyHeadline_ = "REFRESHING";
        break;
      case Pending::Article:
        busyHeadline_ = "OPENING";
        break;
      default:
        busyHeadline_ = "JOINING";
        break;
    }
  }
  requestUpdate();
}

// --- Refreshing ----------------------------------------------------------

void RssActivity::pumpRefreshInput() {
  // The refresh holds the loop for as long as it runs, so nothing else is
  // sampling input: this is the one place in the app that pumps it, and only
  // while a refresh is in hand (docs/apps/rss.md, input-pump rule).
  mappedInput.update();
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) {
    stopRefresh_ = true;
    return;
  }
  int x = 0;
  int y = 0;
  if (!interactionsReady_ || !mappedInput.wasScreenTapped(x, y)) return;
  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(x);
  input.touchY = static_cast<int16_t>(y);
  if (interactions_.route(input).action == rssui::ActionStopRefresh) stopRefresh_ = true;
}

RssActivity::FetchResult RssActivity::fetchFeed(const std::string& url, const bool probe,
                                                std::vector<std::string>* discovered) {
  // Each post is decided the moment its closing tag arrives: kept or not, and
  // if kept, its words go to the card there and then. Nothing holds the feed.
  struct Sink final : rss::ItemSink {
    RssActivity* self = nullptr;
    std::string url;
    std::string key;
    rss::Keeper keeper;
    bool onItem(rss::Item& item) override {
      rss::Post post;
      std::string text;
      if (!rss::ingest(item, url, key, post, text)) return !keeper.saturated();
      const std::string id = post.id;
      const rss::Body body = post.body;
      std::string evicted;
      if (keeper.offer(std::move(post), evicted) && !text.empty()) {
        const std::string path = self->bodyPath(id, body);
        if (!Storage.exists(path.c_str()) && !writeText(path.c_str(), text)) {
          LOG_ERR("RSS", "could not keep the words of %s", id.c_str());
        }
      }
      return !keeper.saturated();
    }
  };

  FetchResult result;
  Sink sink;
  sink.self = this;
  sink.url = url;
  sink.key = rss::keyFor(url);
  Storage.ensureDirectoryExists(kBodiesDir);

  rss::Probe prober(&sink);
  rss::FeedParser parser(&sink);
  size_t total = 0;
  bool overLimit = false;
  const uint32_t started = millis();
  const bool fetched = HttpDownloader::fetchUrl(url, [&](const uint8_t* data, const size_t length) {
    total += length;
    const bool isFeed = probe ? prober.isFeed() : true;
    if (total > (isFeed ? rss::kMaxFeedBytes : rss::kMaxPageBytes)) {
      overLimit = true;
      return false;
    }
    if (busyStoppable_) {
      pumpRefreshInput();
      if (stopRefresh_) return false;
    }
    const char* bytes = reinterpret_cast<const char*>(data);
    return probe ? prober.feed(bytes, length) : parser.feed(bytes, length);
  });
  if (probe) {
    prober.finish();
  } else {
    parser.finish();
  }
  const rss::FeedParser& read = probe ? prober.parser() : parser;

  // A feed counts as read when it was one and anything usable came of it: the
  // whole of it, the part before the size limit or a broken tag, or the part
  // before the newest twenty were certain. A refresh stopped by hand mid-feed
  // does not count, so that feed keeps the posts it had.
  const bool usable = fetched || overLimit || read.stoppedBySink() || read.items() > 0;
  result.ok = read.isFeed() && usable && !(stopRefresh_ && !fetched && !read.stoppedBySink());
  result.title = read.title();
  result.posts = sink.keeper.take();
  if (probe && discovered != nullptr && !prober.isFeed()) {
    for (const std::string& href : prober.links().feeds()) discovered->push_back(rss::resolveUrl(url, href));
  }
  LOG_INF("RSS", "%s: %s, %d items, %d kept, %u bytes, %ums, status %d%s%s", url.c_str(),
          read.isFeed() ? "feed" : (probe ? "page" : "not a feed"), read.items(), static_cast<int>(result.posts.size()),
          static_cast<unsigned>(total), static_cast<unsigned>(millis() - started), HttpDownloader::lastStatus(),
          read.malformed() ? ", malformed" : "", overLimit ? ", over the limit" : "");
  return result;
}

void RssActivity::adopt(const std::vector<rss::Post>& fresh, const std::vector<std::string>& answered) {
  {
    RenderLock lock(*this);
    posts_ = rss::combine(feeds_, posts_, fresh, answered);
    read_.retainOnly(posts_);
    viewStale_ = true;
  }
  saveFeeds();
  savePosts();
  saveRead();
  pruneBodies();
}

void RssActivity::refreshAll() {
  stopRefresh_ = false;
  const uint32_t now = nowOrZero();
  std::vector<rss::Post> fresh;
  fresh.reserve(feeds_.size() * rss::kPostsPerFeed);
  std::vector<std::string> answered;
  const int count = static_cast<int>(feeds_.size());
  int failed = 0;
  for (int i = 0; i < count && !stopRefresh_; ++i) {
    {
      RenderLock lock(*this);
      phase_ = Phase::Busy;
      busyHeadline_ = "REFRESHING";
      char progress[24];
      std::snprintf(progress, sizeof(progress), "%d of %d", i + 1, count);
      busyMessage_ = feeds_[static_cast<size_t>(i)].title + "\n" + progress;
      busyStoppable_ = true;
    }
    requestUpdateAndWait();
    pumpRefreshInput();
    if (stopRefresh_) break;

    rss::Feed& feed = feeds_[static_cast<size_t>(i)];
    FetchResult result = fetchFeed(feed.url, false, nullptr);
    if (!result.ok) {
      // Not read: a refresh stopped mid-feed leaves the mark it had, and a
      // feed that did not answer is marked so the feeds list can say so.
      if (!stopRefresh_) {
        feed.failed = true;
        ++failed;
      }
      continue;
    }
    feed.failed = false;
    // A feed added before it had a name -- its host stands in -- takes the
    // name it gives itself.
    if (!result.title.empty() && feed.title == rss::hostOf(feed.url)) feed.title = result.title;
    rss::settleDates(result.posts, posts_, now);
    answered.push_back(rss::keyFor(feed.url));
    for (rss::Post& post : result.posts) fresh.push_back(std::move(post));
  }
  busyStoppable_ = false;
  LOG_INF("RSS", "refresh: %d of %d feeds read, %d failed%s", static_cast<int>(answered.size()), count, failed,
          stopRefresh_ ? ", stopped" : "");
  adopt(fresh, answered);
  stopRefresh_ = false;
  {
    RenderLock lock(*this);
    phase_ = Phase::List;
  }
}

// --- Input ---------------------------------------------------------------

void RssActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) backPressSeen_ = true;

  if (refreshOnFirstPass_) {
    refreshOnFirstPass_ = false;
    ensureConnected(Pending::Refresh);
    return;
  }

  if (pending_ != Pending::None) {
    const Pending what = pending_;
    pending_ = Pending::None;
    switch (what) {
      case Pending::Refresh:
        refreshAll();
        break;
      case Pending::Article:
        if (!fetchArticle()) {
          showNotice(hn::kUnreachableHeadline, "Could not reach the site. Saved posts still work.", false);
        }
        break;
      case Pending::Phone:
        startPhone();
        break;
      case Pending::AddFeed:
        addFeed();
        break;
      case Pending::None:
        break;
    }
    requestUpdate();
    return;
  }

  if (phase_ == Phase::Phone) servePhone();

  if (backPressSeen_ && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    backPressSeen_ = false;
    switch (phase_) {
      case Phase::Reading:
      case Phase::Notice:
      case Phase::Busy:
        returnToList();
        return;
      case Phase::Phone:
        stopPhone();
        returnToList();
        return;
      case Phase::List:
        // Back walks out one layer at a time: a narrowed list to the feeds, any
        // other tab to every post, every post out of the app.
        if (feedFilter_ >= 0) {
          showTab(rssui::Tab::Feeds);
        } else if (tab_ != rssui::Tab::All) {
          showTab(rssui::Tab::All);
        } else {
          shelf::leave(renderer, mappedInput);
        }
        return;
    }
  }

  // A vertical swipe pages, the same way round as in Hacker News.
  const MappedInputManager::SwipeDir swipe = mappedInput.wasSwipe();
  const bool next = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                    mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
                    swipe == MappedInputManager::SwipeDir::Up;
  const bool prev = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                    mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                    swipe == MappedInputManager::SwipeDir::Down;
  if (next || prev) {
    if (phase_ == Phase::Reading) {
      turnPage(next ? 1 : -1);
      return;
    }
    if (phase_ == Phase::List) {
      pageList(next ? 1 : -1);
      return;
    }
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
    case rssui::ActionOpenPost:
      if (tab_ == rssui::Tab::Saved) {
        openSaved(event.value);
      } else {
        openPost(event.value);
      }
      break;
    case rssui::ActionOpenFeed:
      openFeed(event.value);
      break;
    case rssui::ActionShowAll:
      showTab(rssui::Tab::All);
      break;
    case rssui::ActionShowFeeds:
      showTab(rssui::Tab::Feeds);
      break;
    case rssui::ActionShowSaved:
      showTab(rssui::Tab::Saved);
      break;
    case rssui::ActionAddFeeds:
      // The phone screen needs the radio, so the picker comes first when it is
      // down; the screen is drawn once there is an address to put in the code.
      ensureConnected(Pending::Phone);
      break;
    case rssui::ActionPhoneDone:
      stopPhone();
      returnToList();
      break;
    case hnui::ActionPagePrev:
      turnPage(-1);
      break;
    case hnui::ActionPageNext:
      turnPage(1);
      break;
    case hnui::ActionSave:
      saveCurrent();
      break;
    case hnui::ActionUnsave:
      unsaveCurrent();
      break;
    case hnui::ActionNoticeBack:
      returnToList();
      break;
    default:
      break;
  }
}

// --- Lists ---------------------------------------------------------------

void RssActivity::showTab(const rssui::Tab tab) {
  {
    RenderLock lock(*this);
    tab_ = tab;
    feedFilter_ = -1;
    topIndex_ = 0;
    selected_ = -1;
    phase_ = Phase::List;
    viewStale_ = true;
  }
  requestUpdate();
}

void RssActivity::openFeed(const int feedIndex) {
  if (feedIndex < 0 || feedIndex >= static_cast<int>(feeds_.size())) return;
  {
    RenderLock lock(*this);
    tab_ = rssui::Tab::Feeds;
    feedFilter_ = feedIndex;
    topIndex_ = 0;
    selected_ = -1;
    viewStale_ = true;
  }
  requestUpdate();
}

int RssActivity::feedIndexForKey(const std::string& key) const {
  for (size_t i = 0; i < feeds_.size(); ++i) {
    if (rss::keyFor(feeds_[i].url) == key) return static_cast<int>(i);
  }
  return -1;
}

std::string RssActivity::siteForSaved(const std::string& url) const {
  // The feed's own name when the post is still in one, which is what the list
  // of every post calls it; the host otherwise.
  for (const rss::Post& post : posts_) {
    if (post.link != url) continue;
    const int feed = feedIndexForKey(post.feed);
    if (feed >= 0) return feeds_[static_cast<size_t>(feed)].title;
  }
  return url.rfind("rss:", 0) == 0 ? std::string() : rss::hostOf(url);
}

void RssActivity::rebuildView() {
  view_.clear();
  if (tab_ == rssui::Tab::Saved) {
    const int count = static_cast<int>(library_.articles().size());
    view_.reserve(static_cast<size_t>(count));
    // Newest kept first.
    for (int i = count - 1; i >= 0; --i) view_.push_back(i);
  } else if (tab_ == rssui::Tab::Feeds && feedFilter_ < 0) {
    view_.reserve(feeds_.size());
    for (size_t i = 0; i < feeds_.size(); ++i) view_.push_back(static_cast<int>(i));
  } else {
    const std::string key = feedFilter_ >= 0 ? rss::keyFor(feeds_[static_cast<size_t>(feedFilter_)].url) : "";
    view_.reserve(posts_.size());
    for (size_t i = 0; i < posts_.size(); ++i) {
      if (key.empty() || posts_[i].feed == key) view_.push_back(static_cast<int>(i));
    }
  }
  viewStale_ = false;
}

void RssActivity::pageList(const int delta) {
  const int count = static_cast<int>(view_.size());
  if (count <= 0 || visibleRows_ <= 0) return;
  const int pages = shelfui::pageCountFor(count, visibleRows_);
  if (pages <= 1) return;
  topIndex_ = shelfui::pageStep(shelfui::pageFor(topIndex_, visibleRows_), pages, delta) * visibleRows_;
  requestUpdate();
}

// --- Reading -------------------------------------------------------------

void RssActivity::openPost(const int viewIndex) {
  if (viewIndex < 0 || viewIndex >= static_cast<int>(view_.size())) return;
  const int index = view_[static_cast<size_t>(viewIndex)];
  if (index < 0 || index >= static_cast<int>(posts_.size())) return;
  const rss::Post& post = posts_[static_cast<size_t>(index)];
  selected_ = viewIndex;
  openPostIndex_ = index;
  readingSaved_ = false;
  // Opened is read, whether or not the words then arrive: the dot answers
  // "have I looked at this", and a page that would not load has been looked at.
  if (read_.add(post.id)) saveRead();
  // The shelf's key: the post's own link, or one made from its id when the
  // feed gave none, so a post with no link can still be kept.
  readerKey_ = post.link.empty() ? "rss:" + post.id : post.link;

  if (post.body == rss::Body::Full) {
    std::string words;
    if (readText(bodyPath(post.id, post.body).c_str(), kMaxBodyBytes, words)) {
      showDocument(post, words, nullptr);
      return;
    }
    LOG_ERR("RSS", "the words of %s are missing from the card", post.id.c_str());
  }
  // The free half of Hacker News's gate: a link that cannot be an article is
  // not fetched.
  if (post.link.empty() || !hn::urlCanBeArticle(post.link)) {
    std::string summary;
    if (post.body == rss::Body::Summary && readText(bodyPath(post.id, post.body).c_str(), kMaxBodyBytes, summary)) {
      showDocument(post, summary, "That is all the feed carries, and its link is not a page this reader can open.");
      return;
    }
    showNotice("NOT READABLE HERE", "That post is a link to something only a browser can open.", true);
    return;
  }
  ensureConnected(Pending::Article);
}

bool RssActivity::fetchArticle() {
  if (openPostIndex_ < 0 || openPostIndex_ >= static_cast<int>(posts_.size())) return false;
  const rss::Post post = posts_[static_cast<size_t>(openPostIndex_)];
  std::string summary;
  const bool haveSummary =
      post.body == rss::Body::Summary && readText(bodyPath(post.id, post.body).c_str(), kMaxBodyBytes, summary);

  std::string response;
  bool overLimit = false;
  const bool ok = HttpDownloader::fetchUrl(hn::kExtractorPrefix + post.link,
                                           [&response, &overLimit](const uint8_t* data, const size_t len) {
                                             if (response.size() + len > hn::kMaxArticleBytes) {
                                               overLimit = true;
                                               return false;
                                             }
                                             response.append(reinterpret_cast<const char*>(data), len);
                                             return true;
                                           });
  if (!ok && !overLimit) {
    LOG_ERR("RSS", "article fetch failed for %s", post.link.c_str());
    if (haveSummary) {
      showDocument(post, summary, "That is all the feed carries. The rest of the post needs a connection.");
      return true;
    }
    return false;
  }
  const hn::Extracted extracted = hn::splitExtractorResponse(response);
  // The paid half of the gate, Hacker News's own: only counting the prose tells
  // a page from the error sentence a browser-only site answers with.
  if (!hn::readsAsProse(extracted.body)) {
    LOG_INF("RSS", "gate rejected %s (%d prose chars)", post.link.c_str(), hn::proseChars(extracted.body));
    if (haveSummary) {
      showDocument(post, summary, "That is all the feed carries. The page itself would not come through as text.");
      return true;
    }
    showNotice("NOT READABLE HERE",
               "That page came back with no article in it. Whatever is there needs a browser to see.", true);
    return true;
  }
  std::string words;
  for (const std::string& paragraph : hn::paragraphsFromMarkdown(extracted.body)) {
    if (!words.empty()) words += "\n\n";
    words += paragraph;
  }
  showDocument(post, words, nullptr);
  return true;
}

void RssActivity::showDocument(const rss::Post& post, const std::string& words, const char* note) {
  RenderLock lock(*this);
  // The headline again, because the band cuts a long one; then whose it is and
  // when, which nothing else on the reader says.
  document_ = post.title;
  document_ += "\n\n";
  const int feed = feedIndexForKey(post.feed);
  std::string byline = feed >= 0 ? feeds_[static_cast<size_t>(feed)].title : rss::hostOf(post.link);
  const std::string date = rss::formatDate(post.date);
  if (!date.empty()) byline += byline.empty() ? date : ", " + date;
  if (!byline.empty()) {
    document_ += byline;
    document_ += "\n\n";
  }
  document_ += words;
  if (note != nullptr) {
    document_ += "\n\n";
    document_ += note;
  }
  readerTitle_ = post.title;
  topLine_ = 0;
  lineCount_ = 0;
  visibleLines_ = 0;
  saveFailedNotice_ = false;
  phase_ = Phase::Reading;
}

void RssActivity::openSaved(const int viewIndex) {
  if (viewIndex < 0 || viewIndex >= static_cast<int>(view_.size())) return;
  const auto& saved = library_.articles();
  const int index = view_[static_cast<size_t>(viewIndex)];
  if (index < 0 || index >= static_cast<int>(saved.size())) return;
  const hn::SavedArticle& article = saved[static_cast<size_t>(index)];
  std::string text;
  if (!library_.readArticle(article, text)) {
    showNotice("NOT THERE", "That post is in the list but its words are missing from the card.", false);
    return;
  }
  RenderLock lock(*this);
  selected_ = viewIndex;
  // Saved exactly as it was shown -- headline, byline and all -- so it is
  // shown again as it is.
  document_ = utf8FoldTypography(text);
  readerTitle_ = article.title;
  readerKey_ = article.url;
  readingSaved_ = true;
  topLine_ = 0;
  lineCount_ = 0;
  visibleLines_ = 0;
  saveFailedNotice_ = false;
  phase_ = Phase::Reading;
}

void RssActivity::saveCurrent() {
  if (readerKey_.empty() || document_.empty()) return;
  if (!library_.save(readerKey_, readerTitle_, document_)) {
    // Said over the page rather than instead of it; see Hacker News's card #40.
    saveFailedNotice_ = true;
    requestUpdate();
    return;
  }
  saveFailedNotice_ = false;
  viewStale_ = true;
  requestUpdate();
}

void RssActivity::unsaveCurrent() {
  if (readerKey_.empty()) return;
  saveFailedNotice_ = false;
  library_.remove(readerKey_);
  viewStale_ = true;
  // Removing what you are reading out of the shelf leaves nothing to read.
  if (readingSaved_) {
    readingSaved_ = false;
    phase_ = Phase::List;
    selected_ = -1;
  }
  requestUpdate();
}

void RssActivity::turnPage(const int delta) {
  if (phase_ != Phase::Reading || visibleLines_ == 0) return;
  saveFailedNotice_ = false;
  const uint32_t span = visibleLines_;
  const uint32_t maxTop = lineCount_ > span ? lineCount_ - span : 0;
  if (delta > 0) {
    topLine_ = topLine_ + span > maxTop ? maxTop : topLine_ + span;
  } else {
    topLine_ = topLine_ > span ? topLine_ - span : 0;
  }
  requestUpdate();
}

void RssActivity::returnToList() {
  {
    RenderLock lock(*this);
    phase_ = Phase::List;
    readingSaved_ = false;
    saveFailedNotice_ = false;
    // The dot that went out when the post was opened, and a save or a removal
    // made while reading, both change the rows.
    viewStale_ = true;
  }
  requestUpdate();
}

void RssActivity::showNotice(const char* headline, const char* message, const bool unreadable) {
  {
    RenderLock lock(*this);
    noticeHeadline_ = headline;
    noticeMessage_ = message;
    noticeUnreadable_ = unreadable;
    phase_ = Phase::Notice;
  }
  requestUpdate();
}

// --- The phone -----------------------------------------------------------

void RssActivity::publishToDesk() { desk_.publish(feeds_, posts_); }

void RssActivity::startPhone() {
  // Developer Mode holds port 80 for as long as its toggle is on. Two binds on
  // one port fail in a way that reads as a broken screen, so it yields while
  // this screen is up; every way off this screen goes through stopPhone().
  devmode::pause();
  devPaused_ = true;
  desk_.clear();
  publishToDesk();
  server_ = makeUniqueNoThrow<RssServer>(desk_);
  if (!server_ || !server_->begin()) {
    stopPhone();
    showNotice("NO LUCK", "The reader could not open its web server. Try again in a moment.", false);
    return;
  }
#ifdef SIMULATOR
  // No radio here, so no name and no address to read off one. The page itself
  // really is served, on the host's own port.
  const bool mdnsUp = false;
  const std::string dotted = "127.0.0.1";
#else
  MDNS.end();
  const bool mdnsUp = MDNS.begin(devicehost::mdnsName());
  const std::string dotted = std::string(WiFi.localIP().toString().c_str());
#endif
  // The code carries the address, always; the name depends on a responder
  // that can fail to start. See NotesActivity::startPhone.
  phoneUrl_ = "http://" + dotted + "/r";
  phoneReadable_ = mdnsUp ? std::string("http://") + devicehost::mdnsName() + ".local/r" : phoneUrl_;
  char status[32];
  std::snprintf(status, sizeof(status), feeds_.size() == 1 ? "%d FEED" : "%d FEEDS", static_cast<int>(feeds_.size()));
  phoneStatus_ = status;
  RenderLock lock(*this);
  phase_ = Phase::Phone;
  interactionsReady_ = false;
  requestUpdate();
}

void RssActivity::stopPhone() {
  if (server_) {
    server_->stop();
    server_.reset();
#ifndef SIMULATOR
    MDNS.end();
#endif
  }
  desk_.clear();
  if (devPaused_) {
    devPaused_ = false;
    devmode::resume();
  }
}

void RssActivity::servePhone() {
  if (!server_) return;
  for (int i = 0; i < 8 && server_; ++i) server_->handleClient();
  rss::Request next;
  if (!desk_.take(next)) return;
  if (next.kind == rss::Request::Kind::Remove) {
    removeFeed(next);
    return;
  }
  // Adding fetches the feed, which takes seconds, so the screen says so first
  // and the fetch happens on the next pass.
  addRequest_ = next;
  {
    RenderLock lock(*this);
    const std::string host = rss::hostOf(rss::normalizeAddress(next.text));
    phoneStatus_ = "ADDING " + (host.empty() ? next.text : host);
  }
  pending_ = Pending::AddFeed;
  requestUpdate();
}

void RssActivity::removeFeed(const rss::Request& request) {
  const auto at = std::find_if(feeds_.begin(), feeds_.end(),
                               [&request](const rss::Feed& feed) { return feed.url == request.text; });
  if (at == feeds_.end()) {
    desk_.finish(request.job, false, "That feed is not on the reader.");
    return;
  }
  const std::string title = at->title;
  {
    RenderLock lock(*this);
    feeds_.erase(at);
    feedFilter_ = -1;
    if (tab_ == rssui::Tab::Feeds) topIndex_ = 0;
    phoneStatus_ = "REMOVED " + title;
  }
  // Its posts go with it; adopt() drops the posts of a feed no longer here.
  adopt({}, {});
  publishToDesk();
  desk_.finish(request.job, true, "Removed " + title + ".");
  LOG_INF("RSS", "removed %s", request.text.c_str());
  requestUpdate();
}

void RssActivity::addFeed() {
  const rss::Request job = addRequest_;
  const auto refuse = [this, &job](const std::string& message, const char* status) {
    desk_.finish(job.job, false, message);
    RenderLock lock(*this);
    phoneStatus_ = status;
    phase_ = Phase::Phone;
  };

  const std::string address = rss::normalizeAddress(job.text);
  if (address.empty()) return refuse("That is not a web address.", "NOT AN ADDRESS");
  if (static_cast<int>(feeds_.size()) >= rss::kMaxFeeds) {
    return refuse("That is thirty feeds already. Remove one first.", "THIRTY ALREADY");
  }
  const auto known = [this](const std::string& url) {
    return std::any_of(feeds_.begin(), feeds_.end(), [&url](const rss::Feed& feed) { return feed.url == url; });
  };
  if (known(address)) return refuse("That feed is already on the reader.", "ALREADY THERE");

  // One request for either kind of address: a feed is read on the spot, a page
  // is read as far as the feed it names.
  std::vector<std::string> discovered;
  std::string feedUrl = address;
  FetchResult result = fetchFeed(address, true, &discovered);
  if (!result.ok && !discovered.empty()) {
    feedUrl = discovered.front();
    if (known(feedUrl)) return refuse("That site's feed is already on the reader.", "ALREADY THERE");
    result = fetchFeed(feedUrl, false, nullptr);
  }
  if (!result.ok) {
    const bool answered = HttpDownloader::lastStatus() >= 200 && HttpDownloader::lastStatus() < 300;
    return refuse(answered ? kNoFeedThere : "The site did not answer.", answered ? "NO FEED THERE" : "NO ANSWER");
  }

  rss::Feed feed;
  feed.url = feedUrl;
  feed.title = result.title.empty() ? rss::hostOf(feedUrl) : result.title;
  {
    RenderLock lock(*this);
    feeds_.push_back(feed);
  }
  rss::settleDates(result.posts, posts_, nowOrZero());
  adopt(result.posts, {rss::keyFor(feedUrl)});
  publishToDesk();
  desk_.finish(job.job, true, "Added " + feed.title + ".");
  LOG_INF("RSS", "added %s as %s with %d posts", job.text.c_str(), feedUrl.c_str(),
          static_cast<int>(result.posts.size()));
  RenderLock lock(*this);
  phoneStatus_ = "ADDED " + feed.title;
  phase_ = Phase::Phone;
}

// --- Drawing -------------------------------------------------------------

void RssActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const toybox::Faces faces = phase_ == Phase::Reading ? toybox::readerFaces() : toybox::readingFaces();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, faces);
  const fui::DeviceContext device = target.deviceContext();
  const fui::ThemeTokens& tokens = toybox::themeTokens();
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, device, noInput, interactions_);
  toybox::Screen screen(frame);
  const char* what = "RSS";

  switch (phase_) {
    case Phase::Busy: {
      hnui::NoticeModel model;
      model.band = "RSS";
      model.headline = busyHeadline_.c_str();
      model.message = busyMessage_.c_str();
      // A refresh can take a while across many feeds, and the left-edge swipe
      // that is Back is not written anywhere, so the screen carries the way
      // out. A single fetch is over before a finger could reach it.
      if (busyStoppable_) {
        model.actionLabel = "STOP";
        model.action = rssui::ActionStopRefresh;
      }
      hnui::buildNotice(screen, model);
      what = "RSS busy";
      break;
    }

    case Phase::List: {
      if (viewStale_) rebuildView();
      const bool feedsList = tab_ == rssui::Tab::Feeds && feedFilter_ < 0;
      const bool saved = tab_ == rssui::Tab::Saved;
      const int16_t rowHeight =
          feedsList ? rssui::feedRowHeight(target, tokens) : rssui::postRowHeight(target, tokens);
      const fui::Rect band = feedsList ? rssui::feedsBand(device) : rssui::postsBand(device);
      visibleRows_ = fui::listVisibleRows(band, rowHeight, tokens.listRowGap);
      const int count = static_cast<int>(view_.size());
      if (visibleRows_ > 0) {
        // Onto a page boundary, never onto "the last screenful"; see the
        // same clamp in HackerNewsActivity::render for the bug that cost.
        const int pages = shelfui::pageCountFor(count, visibleRows_);
        const int maxTop = pages > 0 ? (pages - 1) * visibleRows_ : 0;
        topIndex_ = std::min(std::max(topIndex_, 0), maxTop);
      }

      // Only the rows this page shows are built, fitted and handed over.
      const int first = topIndex_;
      const int last = std::min(count, topIndex_ + std::max(visibleRows_, 1));
      const int window = std::max(0, last - first);
      labels_.assign(static_cast<size_t>(window), std::string());
      values_.assign(static_cast<size_t>(window), std::string());
      items_.assign(static_cast<size_t>(window), fui::ListItem{});
      fui::TextStyle titleStyle = tokens.bodyText;
      titleStyle.maxLines = feedsList ? 1 : 2;
      const fui::TextStyle footnote = rssui::footnoteStyle(tokens);
      const int16_t titleWidth =
          feedsList ? rssui::feedTitleWidth(target, device, tokens) : rssui::postTitleWidth(target, device, tokens);
      for (int i = 0; i < window; ++i) {
        const size_t slot = static_cast<size_t>(i);
        const int index = view_[static_cast<size_t>(first + i)];
        fui::ListItem& item = items_[slot];
        item.actionValue = static_cast<int16_t>(first + i);
        if (feedsList) {
          const rss::Feed& feed = feeds_[static_cast<size_t>(index)];
          labels_[slot] = toybox::fitLines(target, feed.title.c_str(), titleWidth, 1, titleStyle);
          const int unread = rss::unreadCount(posts_, read_, rss::keyFor(feed.url));
          if (feed.failed) {
            values_[slot] = "FAILED";
          } else if (unread > 0) {
            values_[slot] = std::to_string(unread);
          }
        } else if (saved) {
          const hn::SavedArticle& article = library_.articles()[static_cast<size_t>(index)];
          labels_[slot] = toybox::fitLines(target, article.title.c_str(), titleWidth, 2, titleStyle);
          values_[slot] = toybox::fitLines(target, siteForSaved(article.url).c_str(), rssui::postSiteWidth(), 1,
                                           footnote);
          item.icon = rssui::readMark();
        } else {
          const rss::Post& post = posts_[static_cast<size_t>(index)];
          labels_[slot] = toybox::fitLines(target, post.title.c_str(), titleWidth, 2, titleStyle);
          const int feed = feedIndexForKey(post.feed);
          const std::string site = feed >= 0 ? feeds_[static_cast<size_t>(feed)].title : std::string();
          values_[slot] = toybox::fitLines(target, site.c_str(), rssui::postSiteWidth(), 1, footnote);
          item.icon = read_.contains(post.id) ? rssui::readMark() : rssui::unreadMark();
        }
      }
      // A second pass, because the strings above must stop moving before a
      // ListItem points at them.
      for (int i = 0; i < window; ++i) {
        items_[static_cast<size_t>(i)].label = labels_[static_cast<size_t>(i)].c_str();
        items_[static_cast<size_t>(i)].value = values_[static_cast<size_t>(i)].c_str();
      }

      rssui::ListModel model;
      model.tab = tab_;
      if (feedFilter_ >= 0) {
        bandTitle_ = feeds_[static_cast<size_t>(feedFilter_)].title;
        model.title = bandTitle_.c_str();
        model.narrowed = true;
      } else {
        model.title = feedsList ? "FEEDS" : (saved ? "SAVED" : "RSS");
      }
      model.items = items_.empty() ? nullptr : items_.data();
      model.count = count;
      model.windowFirst = first;
      model.windowCount = window;
      model.topIndex = topIndex_;
      model.selected = selected_;
      if (count == 0) {
        const rssui::Empty empty = rssui::emptyFor(tab_, !feeds_.empty(), feedFilter_ >= 0);
        model.emptyHeadline = empty.headline;
        model.emptyMessage = empty.message;
        model.emptyActionLabel = empty.actionLabel;
        model.emptyAction = empty.action;
      }
      if (feedsList) {
        rssui::buildFeeds(screen, model);
        what = "RSS feeds";
      } else {
        rssui::buildPosts(screen, model);
        what = saved ? "RSS saved" : "RSS posts";
      }
      break;
    }

    case Phase::Reading: {
      const fui::Rect body = hnui::readerBody(device);
      const int16_t lineHeight = target.lineHeight(tokens.bodyText.font);
      visibleLines_ = fui::textAreaVisibleLines(body, lineHeight);
      hnui::ReaderBody bodyText;
      bodyText.text = document_.c_str();
      bodyText.style = tokens.bodyText;
      bodyText.wrap = &wrap_;
      lineCount_ = hnui::readerLineCount(target, device, bodyText);
      const uint32_t measured = lineCount_;
      const uint32_t pages = visibleLines_ > 0 ? (lineCount_ + visibleLines_ - 1) / visibleLines_ : 1;
      const uint32_t page = visibleLines_ > 0 ? topLine_ / visibleLines_ + 1 : 1;
      std::snprintf(pageLabel_, sizeof(pageLabel_), "%lu/%lu", static_cast<unsigned long>(page),
                    static_cast<unsigned long>(pages < 1 ? 1 : pages));

      hnui::ReaderModel model;
      model.title = readerTitle_.c_str();
      model.topLine = topLine_;
      model.pageLabel = pageLabel_;
      model.hasSwap = false;
      model.canPagePrev = topLine_ > 0;
      model.canPageNext = lineCount_ > topLine_ + visibleLines_;
      model.canSave = !readerKey_.empty();
      model.saved = model.canSave && library_.contains(readerKey_);
      model.saveNotice = saveFailedNotice_ ? "Not saved: the card is full." : nullptr;
      lineCount_ = hnui::buildReader(screen, model, bodyText);
      if (lineCount_ != measured) requestUpdate();
      what = "RSS reader";
      break;
    }

    case Phase::Notice: {
      hnui::NoticeModel model;
      model.band = "RSS";
      model.headline = noticeHeadline_.c_str();
      model.message = noticeMessage_.c_str();
      model.mark = noticeUnreadable_ ? &icon_unreadable_32 : nullptr;
      // Every notice here goes back to the list: there are no comments to
      // offer the way Hacker News's unreadable notice does.
      const hnui::NoticeControl control = hnui::noticeControl(false);
      model.actionLabel = control.label;
      model.action = control.action;
      hnui::buildNotice(screen, model);
      what = "RSS notice";
      break;
    }

    case Phase::Phone: {
      rssui::PhoneModel model;
      model.url = phoneUrl_.c_str();
      model.readable = phoneReadable_.c_str();
      model.status = phoneStatus_.c_str();
      const fui::Rect qr = rssui::buildPhone(screen, model);
      QrUtils::drawQrCode(renderer, Rect{qr.x, qr.y, qr.width, qr.height}, phoneUrl_);
      what = "RSS phone";
      break;
    }
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, what);
  const auto labels = mappedInput.mapLabels("Back", "", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
