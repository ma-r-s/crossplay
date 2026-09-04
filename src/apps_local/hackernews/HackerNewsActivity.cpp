#include "HackerNewsActivity.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <WiFi.h>

#include <cstdio>

#include "../../SilentRestart.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../components/UITheme.h"
#include "../../network/HttpDownloader.h"
#include "../Shelf.h"
#include "../ShelfScreen.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxTheme.h"
#include "DevMode.h"

namespace {

// One request for the whole front page. `front_page` is what HN itself ranks,
// rather than `story` sorted by points, which would be a different list.
constexpr const char* kFrontPageUrl = "https://hn.algolia.com/api/v1/search?tags=front_page&hitsPerPage=30";

// One request for a whole thread, already nested.
constexpr const char* kItemUrlPrefix = "https://hn.algolia.com/api/v1/items/";

// The text extractor. It answers Markdown for an arbitrary page, which is the
// one job this device cannot do for itself: extracting readable prose from
// somebody's HTML is the actual hard problem, and there is no free open API
// that returns an article as text without a service like this in the middle.
//
// Worth being clear-eyed about the cost, because it is a real one: every
// article opened here tells this third party what is being read, and the app
// stops working the day they stop answering. That is why it is only ever
// reached for the article body. Everything that makes this app worth having --
// the front page, every comment, every thread -- comes from Algolia and would
// keep working if this line were deleted.
constexpr const char* kExtractorPrefix = "https://r.jina.ai/";

// The front page JSON is ~22KB. It goes to the card first so the TLS buffers
// are freed before ArduinoJson allocates, which is the same order the font
// downloader uses and for the same reason.
constexpr const char* kFrontPageTmp = "/hn_front.tmp";

// An article is fetched into RAM rather than to the card, because it is small
// and because the readability gate has to see the whole body before deciding
// anything. Bounded so a runaway page cannot decide the memory ceiling: the
// largest real article measured was 32KB.
constexpr size_t kMaxArticleBytes = 96u * 1024u;

constexpr int kMaxStories = 30;

}  // namespace

std::unique_ptr<Activity> HackerNewsActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<HackerNewsActivity>(renderer, mappedInput);
}

// --- Lifecycle -----------------------------------------------------------

void HackerNewsActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);

  // NOTHING TOUCHES THE RADIO HERE, and that is the feature.
  //
  // The picker used to come up before anything else, and backing out of it shut
  // the app -- so the SAVED shelf, the half that exists precisely for having no
  // network, could not be reached without one. That is the state every new
  // device is in, and everyone on a train.
  //
  // So the list opens at once, offline or not, and the first thing that
  // genuinely needs the network is what asks for it. See ensureConnected.
  library_.load();
  view_ = hn::ListView::FrontPage;
  phase_ = Phase::List;
  requestUpdate();
}

void HackerNewsActivity::onExit() {
  Activity::onExit();

  // The radio has to come down before the activity does. silentRestart() is
  // what the rest of the firmware uses to get the stack back to a clean state
  // after station mode; skipping it leaves the next app on a warm radio.
  // Not ours to put down if Developer Mode brought it up: this branch reboots
  // the device, and doing that every time Hacker News exits while dev mode
  // is on is indistinguishable from a crash.
  if (WiFi.getMode() != WIFI_MODE_NULL && !devmode::holdsRadio()) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
  Storage.remove(kFrontPageTmp);
}

void HackerNewsActivity::ensureConnected(const Pending what, const char* busyMessage) {
  // Asked of the radio every time rather than latched once.
  //
  // A latch ("we connected earlier") is wrong in the direction that matters: an
  // AP that drops while the reader is mid-article leaves the latch saying yes,
  // the fetch fails, and the picker -- the one control that could fix it -- is
  // never offered again. WL_CONNECTED is the honest question here because the
  // question IS "will an HTTP GET work", not "who owns this radio"; ownership
  // is a different question with a different answer (see devmode::holdsRadio in
  // onExit, and the linkradio notes on why WiFi.status() cannot answer it).
  if (WiFi.status() == WL_CONNECTED) {
    request(what, busyMessage);
    return;
  }

  // The picker's result decides whether the work ever happens, so the work is
  // remembered and not requested: requesting first would run it against a radio
  // that is not up.
  afterConnect_ = what;
  afterConnectMessage_ = busyMessage;

  // PHASE IS DELIBERATELY NOT TOUCHED HERE, and neither is busyMessage_.
  //
  // There was a Phase::Connecting with a "CONNECTING" screen and a comment
  // saying it was what the panel showed if the picker did not paint promptly.
  // It could never be drawn. pushActivity only sets pendingActivity; the swap
  // happens at the bottom of the SAME ActivityManager::loop() pass this call
  // returns into, so by the time the render task looks at currentActivity the
  // picker is already it. And the picker's own onEnter asks for the only
  // repaint in that frame. A slow picker leaves the PREVIOUS frame up, which is
  // the list or the article that was on screen -- not a blank panel.
  //
  // Leaving phase_ alone is also what makes coming back free: whatever was on
  // screen when the connection was asked for is still what phase_ says, so a
  // cancelled picker needs a repaint and nothing else. The Phase this restored
  // from went with it. And with no cross-task write here there is no phase_ or
  // busyMessage_ for render() to catch mid-change, so no RenderLock is owed:
  // afterConnect_ and afterConnectMessage_ are read by loop() only.
  //
  // The Back PRESS that may have been recorded this frame belongs to the screen
  // that is about to stop being on top. Left set, it survives the picker and
  // pairs with a RELEASE that arrives here afterwards: hold Back, tap something
  // that needs the network, let go inside the picker (which reads only the
  // press), cancel with a second Back, and that second release lands in loop()
  // with phase_ back at List -- which is shelf::leave(), the app shutting on
  // the way to the one screen that works with no network. Docs and this branch
  // both say that can never happen; this line is why.
  backPressSeen_ = false;

  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiChosen(!result.isCancelled); });
}

void HackerNewsActivity::onWifiChosen(const bool connected) {
  if (!connected) {
    // Declining to connect is not wanting out of the app. Back to whatever was
    // on screen when the connection was asked for -- which is a list whose
    // SAVED half is still readable, or a reader still holding its article.
    // phase_ still says which, because raising the picker never changed it.
    afterConnect_ = Pending::None;
    requestUpdate();
    return;
  }
  const Pending what = afterConnect_;
  afterConnect_ = Pending::None;
  request(what, afterConnectMessage_);
}

// --- Scheduling the slow parts -------------------------------------------

void HackerNewsActivity::request(const Pending what, const char* busyMessage) {
  // Nothing to do is not a reason to show a busy screen. loop() clears pending_
  // before it dispatches, so a Pending::None request would leave phase_ at Busy
  // with no work queued to move it off again: a "FETCHING" panel that is not
  // fetching and never stops, which is what a hang looks like. Unreachable
  // today -- afterConnect_ is only ever set to real work -- and cheaper to make
  // unrepresentable than to keep arguing.
  if (what == Pending::None) return;
  {
    RenderLock lock(*this);
    phase_ = Phase::Busy;
    busyMessage_ = busyMessage;
    pending_ = what;
  }
  // Paint the busy screen now. The fetch happens on the next loop pass, so the
  // panel is never blank while the radio works.
  requestUpdate();
}

// --- Input ---------------------------------------------------------------

void HackerNewsActivity::loop() {
  namespace fui = freeink::ui;

  // A Back RELEASE only means "go back" if this activity also saw the PRESS.
  //
  // WifiSelectionActivity acts on the press and every branch here acts on the
  // release, so one physical Back used to do two things: the picker cancelled
  // at the press, handed control back, and 77ms later the release arrived at an
  // activity that had never seen its other half and read it as "leave the
  // list". The app shut on the way to the one screen that works with no
  // network, so with no remembered Wi-Fi the saved shelf could not be reached
  // at all.
  //
  // Recorded before every early return below, so a fetch or a page key landing
  // on the same frame as the press cannot swallow it and leave Back dead.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) backPressSeen_ = true;

  // The deferred fetch, one pass after the screen that announces it. Taken
  // before anything else so a queued fetch cannot be starved by input.
  if (pending_ != Pending::None) {
    const Pending what = pending_;
    pending_ = Pending::None;
    bool ok = false;
    switch (what) {
      case Pending::FrontPage:
        ok = fetchFrontPage();
        break;
      case Pending::Article:
        ok = fetchArticle();
        break;
      case Pending::Comments:
        ok = fetchComments();
        break;
      case Pending::None:
        break;
    }
    if (!ok && phase_ == Phase::Busy) {
      if (what == Pending::FrontPage) {
        // Back to the list, where both segments are, rather than to a
        // full-screen notice: the failure is drawn as the list's own empty
        // state, so the SAVED shelf stays one tap away instead of being behind
        // an error screen.
        //
        // The list can always show this because the empty state is the only
        // thing that draws it, and the LOAD/TRY AGAIN rect is only ever drawn
        // on an empty one. The tap handler enforces that rather than assuming
        // it; see ActionLoadFrontPage.
        frontPageFailed_ = true;
        phase_ = Phase::List;
        view_ = hn::ListView::FrontPage;
      } else {
        // An ARTICLE or a THREAD that did not arrive, which is the more common
        // failure by far: on a train, every tap on a cached front page comes
        // here. It used to show this notice with no control at all -- no
        // button, no segments, no route to SAVED -- so the fix three lines
        // above went into one arm of this `if` and not into its twin, and the
        // twin is the arm people actually reach.
        //
        // The notice keeps its shape (the reader is gone either way, and going
        // straight back to the list would say nothing about why) but it now
        // carries the control every notice carries, and the same sentence the
        // list's own failure uses. Two screens, one fact, one promise.
        showNotice(hn::kUnreachableHeadline, hn::kUnreachableMessage, false);
      }
    }
    requestUpdate();
    return;
  }

  if (backPressSeen_ && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    backPressSeen_ = false;
    // Back walks out one layer at a time and never names where it lands; the
    // shelf owns the last step. See docs/shelf.md.
    //
    // There is no arm here for "the picker is up". There cannot be: only the
    // TOP activity's loop() runs, so nothing sets backPressSeen_ while the
    // picker is on top, and the result handler runs before this activity's next
    // loop() pass either way. The arm that existed tested a Phase::Connecting
    // that was never reachable from here, and the release it was written to
    // catch is caught in ensureConnected instead, by not carrying the press
    // across the picker at all.
    if (phase_ == Phase::List) {
      shelf::leave(renderer, mappedInput);
    } else {
      returnToList();
    }
    return;
  }

  // A vertical swipe pages whatever is on screen, and it is the first thing a
  // hand reaches for on a touch panel showing a scrollbar: a cold tester swiped
  // the story list, got a byte-identical screen, and read the list as stuck.
  //
  // Up carries the page upwards to the next one, the way the content moves
  // under a finger, and the same way round in the list and in the reader --
  // learn it once. Back is a LEFT-EDGE swipe and has already returned above, so
  // nothing here can swallow it.
  const MappedInputManager::SwipeDir swipe = mappedInput.wasSwipe();
  const bool swipeNext = swipe == MappedInputManager::SwipeDir::Up;
  const bool swipePrev = swipe == MappedInputManager::SwipeDir::Down;

  // Physical page keys do the same thing the footer arrows do, through the same
  // function. Two paths would drift, and the drift is invisible until somebody
  // uses the input you did not test.
  if (phase_ == Phase::Reading) {
    if (mappedInput.wasReleased(MappedInputManager::Button::PageForward) || swipeNext) {
      turnPage(1);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::PageBack) || swipePrev) {
      turnPage(-1);
      return;
    }
  }

  // The two side keys PAGE the story list. They are the device's only physical
  // buttons and the case labels them previous and next page.
  //
  // They used to move `selected_` one story at a time -- a row cursor that only
  // Confirm could open, and Confirm is PIN_UNASSIGNED on the X4 Pro and never
  // fires. So you could walk an inverted highlight down thirty stories and had
  // no way to open any of them. See docs/buttons.md.
  //
  // Paging moves the VIEW, not a selection: `selected_` is now only ever set by
  // tapping a story, which is the thing that opens it. Rows stay tappable, so a
  // button is never the only route.
  const bool next = mappedInput.wasReleased(MappedInputManager::Button::Down) || swipeNext;
  const bool prev = mappedInput.wasReleased(MappedInputManager::Button::Up) || swipePrev;
  if (phase_ == Phase::List && (next || prev)) {
    pageList(next ? 1 : -1);
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
    case hnui::ActionOpenStory:
      if (view_ == hn::ListView::Saved) {
        // Off the card, so no network and no busy screen.
        openSavedArticle(event.value);
      } else {
        selected_ = event.value;
        ensureConnected(Pending::Article, "OPENING");
      }
      break;
    case hnui::ActionPagePrev:
      turnPage(-1);
      break;
    case hnui::ActionPageNext:
      turnPage(1);
      break;
    case hnui::ActionSwapView:
      // One action, and the model decides which way it points. Two would let
      // the label and the effect disagree.
      if (readingComments_) {
        if (articleAvailable_) ensureConnected(Pending::Article, "OPENING");
      } else {
        ensureConnected(Pending::Comments, "FETCHING THE THREAD");
      }
      break;
    case hnui::ActionNotice:
      // The unreadable notice's button, and the way onward to the comments.
      ensureConnected(Pending::Comments, "FETCHING THE THREAD");
      break;
    case hnui::ActionNoticeBack:
      // Every other notice's button. The SAME function Back lands through, not
      // a second copy of its three lines: a saved article's notice has to
      // return to the SAVED shelf, and two routes to one landing is how one of
      // them starts going somewhere else.
      returnToList();
      break;
    case hnui::ActionSave:
      saveCurrentArticle();
      break;
    case hnui::ActionUnsave:
      unsaveCurrentArticle();
      break;
    case hnui::ActionShowSaved:
      view_ = hn::ListView::Saved;
      topIndex_ = 0;
      requestUpdate();
      break;
    case hnui::ActionShowFrontPage:
      // Nothing is rebuilt here, and that is the point: this case used to set
      // the flag and leave the other shelf's titles on screen. The paint sees
      // the view changed and sources the rows again.
      view_ = hn::ListView::FrontPage;
      topIndex_ = 0;
      requestUpdate();
      break;
    case hnui::ActionLoadFrontPage:
      // The only control in the app that asks for the network by itself, and
      // the only route to a front-page fetch.
      //
      // Guarded rather than argued. This rect is only ever DRAWN on an empty
      // front page, and the failure landing in loop() relies on that: it puts
      // the message in the list's empty state, which a list with rows in it
      // does not draw. But interactionsReady_ is still true from the empty
      // paint after a fetch has filled stories_ and asked for a repaint, so a
      // tap landing in that window routes here against a list that is no longer
      // empty -- and its failure would then be swallowed with nothing on
      // screen to say so. Cheaper to make the invariant true than to keep
      // asserting it in a comment.
      if (view_ != hn::ListView::FrontPage || !stories_.empty()) break;
      // Cleared first so a second failure repaints the same error rather than a
      // stale one.
      frontPageFailed_ = false;
      ensureConnected(Pending::FrontPage, "FETCHING THE FRONT PAGE");
      break;
    default:
      break;
  }
}

// --- Fetching ------------------------------------------------------------

const hn::Story* HackerNewsActivity::currentStory() const {
  if (selected_ < 0 || selected_ >= static_cast<int>(stories_.size())) return nullptr;
  return &stories_[static_cast<size_t>(selected_)];
}

bool HackerNewsActivity::fetchFrontPage() {
  if (HttpDownloader::downloadToFile(kFrontPageUrl, kFrontPageTmp) != HttpDownloader::OK) {
    LOG_ERR("HN", "front page fetch failed");
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("HN", kFrontPageTmp, file)) {
    LOG_ERR("HN", "could not reopen the front page");
    Storage.remove(kFrontPageTmp);
    return false;
  }

  // The HTTP client is closed by now, so the TLS buffers are already back.
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  Storage.remove(kFrontPageTmp);
  if (err) {
    LOG_ERR("HN", "front page parse error: %s", err.c_str());
    return false;
  }

  stories_.clear();
  const JsonArrayConst hits = doc["hits"].as<JsonArrayConst>();
  stories_.reserve(kMaxStories);

  for (JsonObjectConst hit : hits) {
    if (static_cast<int>(stories_.size()) >= kMaxStories) break;
    hn::Story story;
    // Folded here rather than at the row, because this is where somebody
    // else's text becomes ours: Algolia sends real curly quotes and em dashes
    // in a headline, and the reading cut has no glyph for either, so they
    // draw as nothing at all. The URL is not folded -- it is a request, not a
    // sentence, and changing a character in it changes where it points.
    story.title = utf8FoldTypography(hit["title"] | "");
    if (story.title.empty()) continue;
    story.url = hit["url"] | "";
    story.author = utf8FoldTypography(hit["author"] | "");
    story.points = hit["points"] | 0;
    story.commentCount = hit["num_comments"] | 0;
    story.id = static_cast<uint32_t>(std::strtoul(hit["objectID"] | "0", nullptr, 10));
    // A story with no link is its own text, which is always readable here.
    story.mayBeReadable = story.url.empty() || hn::urlCanBeArticle(story.url);
    stories_.push_back(std::move(story));
  }

  // The rows are assembled at paint time, because fitting a headline to its
  // width needs a draw target and there is none here. Invalidated rather than
  // built: the view has not changed, so nothing else would notice that the
  // stories under it have.
  view_ = hn::ListView::FrontPage;
  rows_.invalidate();

  LOG_INF("HN", "front page: %d stories", static_cast<int>(stories_.size()));
  selected_ = 0;
  topIndex_ = 0;
  phase_ = Phase::List;
  // Cleared only where a front page actually arrived. Returning false here
  // leaves loop() to set it again, so an empty answer still reads as a failure.
  frontPageFailed_ = stories_.empty();
  return !stories_.empty();
}

bool HackerNewsActivity::fetchArticle() {
  const hn::Story* story = currentStory();
  if (story == nullptr) return false;

  articleAvailable_ = false;
  // The library's key. Held from the fetch rather than derived later, because
  // by the time the reader asks whether this article is saved the story it came
  // from may no longer be the selected one.
  readerUrl_ = story->url;
  readingSaved_ = false;

  // A story that is its own text (Ask HN, Show HN with a body) has no article
  // to fetch and no link to judge. Its words live with its comments.
  if (story->url.empty()) {
    return fetchComments();
  }

  // The free half of the gate. A PDF or a page that is only JavaScript cannot
  // become an article however it is fetched, so it is not fetched: the reader
  // gets the answer at once instead of waiting to be told no.
  if (!hn::urlCanBeArticle(story->url)) {
    showNotice("NOT READABLE HERE",
               "That link is a PDF, a video, or a page that only a browser can open. There is no article text to bring "
               "back. The conversation is still here, and you can keep that for later.",
               true);
    return true;
  }

  std::string response;
  std::string url = kExtractorPrefix;
  url += story->url;
  bool overLimit = false;
  const bool ok = HttpDownloader::fetchUrl(url, [&response, &overLimit](const uint8_t* data, const size_t len) {
    if (response.size() + len > kMaxArticleBytes) {
      overLimit = true;
      return false;
    }
    response.append(reinterpret_cast<const char*>(data), len);
    return true;
  });
  if (!ok && !overLimit) {
    LOG_ERR("HN", "article fetch failed");
    return false;
  }

  const hn::Extracted extracted = hn::splitExtractorResponse(response);

  // The paid half of the gate, and the whole reason this app can promise an
  // article rather than a blank page. Both interesting failures arrive as HTTP
  // 200: a PDF extracts to an empty body, a JavaScript page to an error
  // sentence. Only counting the prose separates them from a real post.
  if (!hn::readsAsProse(extracted.body)) {
    LOG_INF("HN", "gate rejected %s (%d prose chars)", story->url.c_str(), hn::proseChars(extracted.body));
    showNotice("NOT READABLE HERE",
               "That page came back with no article in it. Whatever is there needs a browser to see. The conversation "
               "is still here, and you can keep that for later.",
               true);
    return true;
  }

  document_ = extracted.title.empty() ? story->title : extracted.title;
  document_ += "\n\n";
  for (const std::string& paragraph : hn::paragraphsFromMarkdown(extracted.body)) {
    document_ += paragraph;
    document_ += "\n\n";
  }
  articleAvailable_ = true;
  showDocument(extracted.title.empty() ? story->title.c_str() : extracted.title.c_str(), false);
  return true;
}

bool HackerNewsActivity::fetchComments() {
  const hn::Story* story = currentStory();
  if (story == nullptr) return false;

  // The thread's own key in the library, taken here for the reason the
  // article's is taken in fetchArticle: by the time the reader asks whether
  // what it is showing has been kept, the selected story may have moved on.
  // Hacker News's item page rather than the story's link, so an article and its
  // discussion are two entries and neither can overwrite the other -- and so a
  // post with no link of its own still has a key.
  readerUrl_ = hn::savedThreadUrl(story->id);
  readingSaved_ = false;

  std::vector<hn::Comment> comments;
  hn::CommentScanner scanner(comments, {});

  char url[96];
  std::snprintf(url, sizeof(url), "%s%lu", kItemUrlPrefix, static_cast<unsigned long>(story->id));

  // Scanned off the socket, never assembled: the biggest thread measured was
  // 249KB of JSON for 146KB of text, and this device should not hold either.
  const bool ok = HttpDownloader::fetchUrl(url, [&scanner](const uint8_t* data, const size_t len) {
    return scanner.feed(reinterpret_cast<const char*>(data), len);
  });
  if (!ok) {
    LOG_ERR("HN", "thread fetch failed");
    return false;
  }

  // The story first, so a thread opened from a list of thirty says what it is
  // about before it says who is talking.
  document_ = story->title;
  document_ += "\n\n";

  for (const hn::Comment& comment : comments) {
    // Depth as a quote gutter rather than as indentation. Indentation only
    // survives on a paragraph's first line once the text is wrapped, so it
    // reads as a typo; a marker on the author line survives everything and says
    // the same thing.
    for (int i = 0; i < comment.depth; ++i) document_ += '>';
    if (comment.depth > 0) document_ += ' ';
    // Shouted, because the whole thread is one wrapped document in one style
    // and there is no second weight to reach for -- weight on this device comes
    // from size and inversion, and a textArea has one of each. Case is the only
    // signal left, and it is enough: a line of capitals reads as a name and not
    // as the start of a sentence.
    for (const char c : comment.author) {
      document_ += c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
    }
    document_ += "\n\n";
    for (const std::string& paragraph : comment.paragraphs) {
      document_ += paragraph;
      document_ += "\n\n";
    }
  }

  if (comments.empty()) {
    document_ = story->url.empty() ? "No comments yet.\n" : "No comments on this one yet.\n";
  } else if (scanner.truncated()) {
    char note[80];
    std::snprintf(note, sizeof(note), "\nShowing the first %d of %d comments.\n", static_cast<int>(comments.size()),
                  scanner.totalSeen());
    document_ += note;
  }

  LOG_INF("HN", "thread: kept %d of %d comments, %d bytes", static_cast<int>(comments.size()), scanner.totalSeen(),
          static_cast<int>(document_.size()));
  showDocument(story->title.c_str(), true);
  return true;
}

// --- Reading -------------------------------------------------------------

void HackerNewsActivity::showDocument(const char* title, const bool comments) {
  RenderLock lock(*this);
  readerTitle_ = title != nullptr ? title : "";
  readingComments_ = comments;
  topLine_ = 0;
  phase_ = Phase::Reading;
  // Line counts need a draw target, which only exists inside render(). The
  // first paint fills them in; until then the label reads as one page, which is
  // what a document that has not been measured looks like.
  lineCount_ = 0;
  visibleLines_ = 0;
}

void HackerNewsActivity::saveCurrentArticle() {
  // Whatever is on the page, keyed by whatever it is. A thread used to be
  // refused here on the grounds that a conversation keeps moving -- but the
  // stories worth taking on a train are exactly the ones whose page will not
  // render, and for those the conversation is all there is. Refusing it meant
  // the only stories that could not be kept were the ones with most reason to
  // be. What is written to the card is the words that are on screen, which is a
  // snapshot either way.
  if (readerUrl_.empty() || document_.empty()) return;
  const std::string title = readingComments_ ? hn::savedThreadTitle(readerTitle_) : readerTitle_;
  if (!library_.save(readerUrl_, title, document_)) {
    showNotice("NOT SAVED", "The card would not take it. There may be no room left.", false);
  }
  // The shelf gained a row while the view did not change, which is the one
  // staleness rowsStale() cannot see on its own.
  rows_.invalidate();
  requestUpdate();
}

void HackerNewsActivity::unsaveCurrentArticle() {
  if (readerUrl_.empty()) return;
  library_.remove(readerUrl_);
  rows_.invalidate();
  // Removing what you are reading leaves the reader on something the shelf no
  // longer holds, so step back to the list rather than showing an article that
  // is gone.
  if (readingSaved_) {
    view_ = hn::ListView::Saved;
    readingSaved_ = false;
    phase_ = Phase::List;
  }
  requestUpdate();
}

void HackerNewsActivity::openSavedArticle(const int index) {
  const auto& saved = library_.articles();
  if (index < 0 || index >= static_cast<int>(saved.size())) return;
  const hn::SavedArticle& article = saved[static_cast<size_t>(index)];
  if (!library_.readArticle(article, document_)) {
    showNotice("NOT THERE", "That article is in the list but its text is missing from the card.", false);
    requestUpdate();
    return;
  }
  // Articles saved from today on are folded before they are written, because
  // the fold happens where the text enters. One saved before this existed is
  // not, and the reading cut has no glyph for what it carries, so it would keep
  // its holes for as long as it stayed on the card. Folding on the read costs
  // one pass over a document that is about to be word-wrapped anyway, and needs
  // no migration.
  document_ = utf8FoldTypography(document_);
  readerUrl_ = article.url;
  // Set AFTER showDocument, which does not know about the library: showDocument
  // is shared with the front-page path and clearing this there would make every
  // saved article return to the wrong shelf.
  showDocument(article.title.c_str(), false);
  readingSaved_ = true;
  articleAvailable_ = true;
  requestUpdate();
}

void HackerNewsActivity::returnToList() {
  // An article opened out of the library goes back to the library. Landing on
  // the front page instead loses the shelf you were working through, and there
  // is no way back to it but two more taps.
  if (readingSaved_) {
    view_ = hn::ListView::Saved;
  }
  readingSaved_ = false;
  phase_ = Phase::List;
  requestUpdate();
}

void HackerNewsActivity::pageList(const int delta) {
  // The rows the last paint DREW, not the stories the last fetch returned. The
  // saved shelf draws a different number of rows from the front page, so
  // paging it by stories_.size() moved topIndex_ somewhere the paint then
  // clamped back -- a key that did nothing on the shelf, or jumped.
  const int count = static_cast<int>(listItems_.size());
  if (count <= 0 || visibleRows_ <= 0) return;
  const int pages = shelfui::pageCountFor(count, visibleRows_);
  // A list that fits on one page has nowhere to step to. Moving it anyway would
  // be a page turn that changed nothing, which reads as a dead input.
  if (pages <= 1) return;
  topIndex_ = shelfui::pageStep(shelfui::pageFor(topIndex_, visibleRows_), pages, delta) * visibleRows_;
  requestUpdate();
}

void HackerNewsActivity::turnPage(const int delta) {
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

void HackerNewsActivity::showNotice(const char* headline, const char* message, const bool unreadable) {
  RenderLock lock(*this);
  noticeHeadline_ = headline;
  noticeMessage_ = message;
  noticeUnreadable_ = unreadable;
  phase_ = Phase::Notice;
}

// --- Drawing -------------------------------------------------------------

void HackerNewsActivity::render(RenderLock&&) {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  // The reading cut in the body slot. Hacker News is a page of text, not a
  // board, and at the 20px UI cut a 480px panel holds about 28 characters a
  // line: an article became forty page taps and half the headlines on the front
  // page could not finish.
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, toybox::readingFaces());
  const fui::DeviceContext device = target.deviceContext();
  const fui::ThemeTokens& tokens = toybox::themeTokens();
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, device, noInput, interactions_);
  toybox::Screen screen(frame);

  const char* what = "Hacker News";

  switch (phase_) {
    case Phase::Busy: {
      hnui::NoticeModel model;
      model.headline = busyMessage_;
      // No control, deliberately: there is nothing to decide while a fetch is
      // in flight, and the next loop pass replaces this screen either way.
      hnui::buildNotice(screen, model);
      what = "HN busy";
      break;
    }

    case Phase::List: {
      // THE ONE PLACE THE ROWS AND THE VIEW ARE RECONCILED. Every path that
      // changes which shelf is showing now only sets view_; this notices. The
      // bug that made it so was a path that set the flag and forgot the rows,
      // and the screen it produced -- one shelf's headlines over the other
      // shelf's indices -- looked entirely correct.
      //
      // Safe to source here despite render() and loop() being separate tasks:
      // a fetch that rewrites stories_ leaves phase_ at Busy, so this case is
      // not the one drawing while that happens.
      if (hn::rowsStale(rows_, view_)) {
        hn::buildRows(rows_, view_, stories_, library_.articles());
      }
      if (!rows_.fitted) {
        // Fit each headline to the width the component will actually give it,
        // breaking between words and marking what was dropped. Done once per
        // rebuild rather than per paint: the answer only changes when the rows
        // or the fonts do.
        rows_.labels.clear();
        rows_.labels.reserve(rows_.size());
        listItems_.clear();
        listItems_.reserve(rows_.size());
        fui::TextStyle titleStyle = tokens.bodyText;
        titleStyle.maxLines = 2;
        const int16_t titleWidth = hnui::listTitleWidth(target, device, tokens);
        for (const std::string& title : rows_.titles) {
          rows_.labels.push_back(hnui::fitLines(target, title.c_str(), titleWidth, 2, titleStyle));
        }
        // A second pass, because a push_back into labels can reallocate and
        // ListItem holds pointers rather than copies.
        for (size_t i = 0; i < rows_.labels.size(); ++i) {
          fui::ListItem row;
          row.label = rows_.labels[i].c_str();
          row.value = rows_.values[i].c_str();
          row.actionValue = static_cast<int16_t>(i);
          listItems_.push_back(row);
        }
        rows_.fitted = true;
      }

      // The same row height the list will draw with, from the same function.
      // Computing it a second time here is how a list scrolls by a different
      // number of rows than it shows.
      const int16_t rowHeight = hnui::listRowHeight(target, tokens);
      // Cached for loop(), which pages with the side keys and cannot measure:
      // listVisibleRows needs a draw target. The shelf caches rowsPerPage the
      // same way and for the same reason.
      visibleRows_ = fui::listVisibleRows(hnui::listBand(device), rowHeight, tokens.listRowGap);
      // topIndex_ is no longer derived from the selection. It was
      // listTopIndexFor(selected_, ...), which scrolled the view to keep a row
      // cursor visible -- and that cursor is gone. Paging owns the view now, so
      // deriving it here would fight the page keys. It only needs clamping.
      if (visibleRows_ > 0) {
        // Onto a PAGE BOUNDARY, not onto the last screenful of rows. Against
        // the rows actually drawn, too, not against stories_: on the saved
        // shelf those are different lengths.
        //
        // Clamping to `count - visibleRows_` looks like the same thing and is
        // not, whenever the last page is a short one: it rewrites topIndex_ to
        // a value the pager could never have produced, and the next step back
        // is computed from that. A 14-row shelf pages 0, 6, 12; the old clamp
        // stored 8 instead of 12, so forward, forward, back left the shelf on
        // page ONE.
        //
        // The list component still fills its last page from `count - visible`
        // for DRAWING, which is its own behaviour and every list in the fork
        // shares it. The difference is that the page we are on is now ours to
        // remember rather than something read back out of the paint.
        const int pages = shelfui::pageCountFor(static_cast<int>(listItems_.size()), visibleRows_);
        const int maxTop = pages > 0 ? (pages - 1) * visibleRows_ : 0;
        if (topIndex_ > maxTop) topIndex_ = maxTop;
        if (topIndex_ < 0) topIndex_ = 0;
      }
      const bool saved = view_ == hn::ListView::Saved;
      hnui::ListModel model;
      model.items = listItems_.empty() ? nullptr : listItems_.data();
      model.count = static_cast<int>(listItems_.size());
      model.selected = saved ? -1 : selected_;
      model.topIndex = topIndex_;
      model.showingSaved = saved;
      if (saved) model.title = "SAVED";
      if (listItems_.empty()) {
        // Both shelves, from one place. An empty front page is now the ordinary
        // opening screen -- the radio is down until something asks for it -- so
        // it needs a sentence and a way onward exactly as the shelf does.
        //
        // MEASURE ANY WORDING HERE IN THE FACE THE CALL SITE RESOLVES TO, not
        // the one its name suggests: theme().smallText -> kUiFont ->
        // FONT_SLOT_BODY, which under readingFaces() is kReadingFontId,
        // notoserif_14 and not a UI face. A message that measured a comfortable
        // 345px in ubuntu_10 -- a face this screen never uses -- is 511.8px
        // here and shipped cut as "Tap the mark on an article to ke".
        const hn::EmptyState empty = hn::emptyState(view_, frontPageFailed_);
        model.emptyHeadline = empty.headline;
        model.emptyMessage = empty.message;
        model.emptyActionLabel = empty.actionLabel;
        if (empty.actionLabel != nullptr) model.emptyAction = hnui::ActionLoadFrontPage;
      }
      hnui::buildList(screen, model);
      what = saved ? "HN saved" : "HN front page";
      break;
    }

    case Phase::Reading: {
      // Measured here because measuring needs a draw target, and measured from
      // the same rect the text is drawn into: readerBody() is the one function
      // that owns that rectangle, so a page turn cannot skip a line.
      //
      // Through wrap_ rather than textAreaMeasure(), which wrapped the whole
      // document on every single paint with not even a branch to hang a cache
      // on. An article is long and a flattened comment thread is longer; both
      // were re-wrapped twice per page turn. Instapaper was reported first and
      // this is the same bug in the same shape. See ToyboxWrappedText.h.
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
      // The story's own title, not COMMENTS/ARTICLE: which piece you are in
      // is the useful fact, and the footer's swap button already names the
      // mode. The builder fits it to the band.
      model.title = readerTitle_.c_str();
      model.topLine = topLine_;
      model.pageLabel = pageLabel_;
      model.showingComments = readingComments_;
      // Coming back to an article is only offered when there was one. Going to
      // the comments always is.
      // A saved article does not know its own thread: SavedArticle::id is
      // derived from the URL so the shelf can name a file, not the HN item id
      // the comments fetch needs. currentStory() would answer from stories_ --
      // the FRONT PAGE list, at whatever index was last selected -- so tapping
      // COMMENTS on a saved article served the front page's top thread instead.
      // Wrong comments are worse than none, so the control is dimmed here.
      model.swapAvailable = readingSaved_ ? false : (readingComments_ ? articleAvailable_ : true);
      model.canPagePrev = topLine_ > 0;
      model.canPageNext = lineCount_ > topLine_ + visibleLines_;
      // Over whatever is on the page. readerUrl_ is the key for the piece being
      // read -- the story's link for an article, Hacker News's item page for a
      // thread -- so the mark can never claim one was kept because the other
      // was. Empty only until the first fetch has answered.
      model.canSave = !readerUrl_.empty();
      model.saved = model.canSave && library_.contains(readerUrl_);
      // The count the panel was drawn from; see the twin in
      // InstapaperActivity.cpp. No reading position goes anywhere from here,
      // but the page label and the forward control were both computed from
      // the count taken before the drawing.
      lineCount_ = hnui::buildReader(screen, model, bodyText);
      if (lineCount_ != measured) requestUpdate();
      what = "HN reader";
      break;
    }

    case Phase::Notice: {
      hnui::NoticeModel model;
      model.headline = noticeHeadline_.c_str();
      model.message = noticeMessage_.c_str();
      model.mark = noticeUnreadable_ ? &icon_unreadable_32 : nullptr;
      // EVERY notice carries a control, and the rule for which lives in the
      // screens layer where a host test can ask it directly. It used to be
      // `unreadable ? "READ THE COMMENTS" : nullptr` written out here, so the
      // four notices that are not about an unreadable link -- a failed article,
      // a failed thread, a card that would not take a save, a saved file whose
      // text has gone missing -- drew no button at all. This screen has no
      // segments and nothing under it, so each was a dead end whose only exit
      // is a left-edge swipe the screen never mentions, with the SAVED shelf on
      // the far side of it.
      const hnui::NoticeControl control = hnui::noticeControl(noticeUnreadable_);
      model.actionLabel = control.label;
      model.action = control.action;
      hnui::buildNotice(screen, model);
      what = "HN notice";
      break;
    }
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, what);

  const auto labels = mappedInput.mapLabels("Back", phase_ == Phase::List ? "Open" : "", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
