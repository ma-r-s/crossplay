#include "HackerNewsActivity.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>

#include "../../SilentRestart.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../components/UITheme.h"
#include "../../network/HttpDownloader.h"
#include "../Shelf.h"
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

  // Read the shelf before anything network happens: SAVED is the half of this
  // app that works with no connection at all, and it should be right the
  // moment the app opens rather than after a fetch nobody asked for.
  library_.load();

  phase_ = Phase::Connecting;
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiChosen(!result.isCancelled); });
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

void HackerNewsActivity::onWifiChosen(const bool connected) {
  if (!connected) {
    // Backing out of the picker is not the same as wanting out of the app. The
    // saved shelf is the half that works with no connection at all -- onEnter
    // loads it before anything network happens for exactly that reason -- and
    // routing this to shelf::leave() put the picker in front of it as a door
    // that needs a network to open. On a train, that made every saved article
    // unreachable: the text is on the card, and there was no way to it.
    if (!library_.articles().empty()) {
      showingSaved_ = true;
      buildSavedRows();
      phase_ = Phase::List;
      requestUpdate();
      return;
    }
    // Nothing saved: there is genuinely nothing to show without a network, so
    // leaving is still the honest answer.
    shelf::leave(renderer, mappedInput);
    return;
  }
  request(Pending::FrontPage, "FETCHING THE FRONT PAGE");
}

// --- Scheduling the slow parts -------------------------------------------

void HackerNewsActivity::request(const Pending what, const char* busyMessage) {
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
      showNotice("NO LUCK", "Could not reach Hacker News. Check the network and try again.", false);
    }
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Back walks out one layer at a time and never names where it lands; the
    // shelf owns the last step. See docs/shelf.md.
    if (phase_ == Phase::List || phase_ == Phase::Connecting) {
      shelf::leave(renderer, mappedInput);
    } else {
      // An article opened out of the library goes back to the library. Landing
      // on the front page instead loses the shelf you were working through,
      // and there is no way back to it but two more taps.
      if (readingSaved_) {
        showingSaved_ = true;
        buildSavedRows();
      }
      readingSaved_ = false;
      phase_ = Phase::List;
      requestUpdate();
    }
    return;
  }

  // Physical page keys do the same thing the footer arrows do, through the same
  // function. Two paths would drift, and the drift is invisible until somebody
  // uses the input you did not test.
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
  const bool next = mappedInput.wasReleased(MappedInputManager::Button::Down);
  const bool prev = mappedInput.wasReleased(MappedInputManager::Button::Up);
  if (phase_ == Phase::List && !stories_.empty() && (next || prev)) {
    const int count = static_cast<int>(stories_.size());
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
    case hnui::ActionOpenStory:
      if (showingSaved_) {
        // Off the card, so no network and no busy screen.
        openSavedArticle(event.value);
      } else {
        selected_ = event.value;
        request(Pending::Article, "OPENING");
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
        if (articleAvailable_) request(Pending::Article, "OPENING");
      } else {
        request(Pending::Comments, "FETCHING THE THREAD");
      }
      break;
    case hnui::ActionNotice:
      // The notice's only button is always the way onward to the comments.
      request(Pending::Comments, "FETCHING THE THREAD");
      break;
    case hnui::ActionSave:
      saveCurrentArticle();
      break;
    case hnui::ActionUnsave:
      unsaveCurrentArticle();
      break;
    case hnui::ActionShowSaved:
      showingSaved_ = true;
      topIndex_ = 0;
      buildSavedRows();
      requestUpdate();
      break;
    case hnui::ActionShowFrontPage:
      showingSaved_ = false;
      topIndex_ = 0;
      rowsFitted_ = false;
      requestUpdate();
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
  rowTitles_.clear();
  rowValues_.clear();
  rows_.clear();
  const JsonArrayConst hits = doc["hits"].as<JsonArrayConst>();
  stories_.reserve(kMaxStories);
  rowTitles_.reserve(kMaxStories);
  rowValues_.reserve(kMaxStories);
  rows_.reserve(kMaxStories);

  for (JsonObjectConst hit : hits) {
    if (static_cast<int>(stories_.size()) >= kMaxStories) break;
    hn::Story story;
    story.title = hit["title"] | "";
    if (story.title.empty()) continue;
    story.url = hit["url"] | "";
    story.author = hit["author"] | "";
    story.points = hit["points"] | 0;
    story.commentCount = hit["num_comments"] | 0;
    story.id = static_cast<uint32_t>(std::strtoul(hit["objectID"] | "0", nullptr, 10));
    // A story with no link is its own text, which is always readable here.
    story.mayBeReadable = story.url.empty() || hn::urlCanBeArticle(story.url);
    stories_.push_back(std::move(story));
  }

  for (const hn::Story& story : stories_) {
    // The bare comment count. The list is already HN's own ranking, so points
    // would be restating the order; how much discussion a story drew is the
    // thing the order does not tell you, and it is what decides whether to open
    // the thread. Spelled out it cost ten characters of every headline.
    char count[16];
    std::snprintf(count, sizeof(count), "%d", story.commentCount);
    rowTitles_.push_back(story.title);
    rowValues_.push_back(count);
  }
  // The rows themselves are assembled at paint time, because fitting a headline
  // to its width needs a draw target and there is none here.
  rowsFitted_ = false;

  LOG_INF("HN", "front page: %d stories", static_cast<int>(stories_.size()));
  selected_ = 0;
  topIndex_ = 0;
  phase_ = Phase::List;
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
               "back. The conversation is still here.",
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
               "is still here.",
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
  // Only an article is worth saving: a thread is a conversation that keeps
  // moving, and the words on screen are the article's.
  if (readingComments_ || readerUrl_.empty() || document_.empty()) return;
  if (!library_.save(readerUrl_, readerTitle_, document_)) {
    showNotice("NOT SAVED", "The card would not take it. There may be no room left.", false);
  }
  requestUpdate();
}

void HackerNewsActivity::unsaveCurrentArticle() {
  if (readerUrl_.empty()) return;
  library_.remove(readerUrl_);
  // Removing what you are reading leaves the reader on something the shelf no
  // longer holds, so step back to the list rather than showing an article that
  // is gone.
  if (readingSaved_) {
    showingSaved_ = true;
    buildSavedRows();
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
  readerUrl_ = article.url;
  // Set AFTER showDocument, which does not know about the library: showDocument
  // is shared with the front-page path and clearing this there would make every
  // saved article return to the wrong shelf.
  showDocument(article.title.c_str(), false);
  readingSaved_ = true;
  articleAvailable_ = true;
  requestUpdate();
}

void HackerNewsActivity::buildSavedRows() {
  const auto& saved = library_.articles();
  rowTitles_.clear();
  rowLabels_.clear();
  rowValues_.clear();
  rows_.clear();
  rowTitles_.reserve(saved.size());
  rowValues_.reserve(saved.size());
  for (const hn::SavedArticle& article : saved) {
    rowTitles_.push_back(article.title);
    rowValues_.push_back(std::string());
  }
  // Left unfitted on purpose: render() fits labels to the row width it can
  // only measure with a draw target, the same way the front page does.
  rowsFitted_ = false;
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
    case Phase::Connecting:
    case Phase::Busy: {
      hnui::NoticeModel model;
      model.headline = busyMessage_;
      hnui::buildNotice(screen, model);
      what = "HN busy";
      break;
    }

    case Phase::List: {
      if (!rowsFitted_) {
        // Fit each headline to the width the component will actually give it,
        // breaking between words and marking what was dropped. Done once per
        // fetch rather than per paint: the answer only changes when the stories
        // or the fonts do.
        rowLabels_.clear();
        rowLabels_.reserve(stories_.size());
        rows_.clear();
        rows_.reserve(stories_.size());
        fui::TextStyle titleStyle = tokens.bodyText;
        titleStyle.maxLines = 2;
        const int16_t titleWidth = hnui::listTitleWidth(target, device, tokens);
        for (const std::string& title : rowTitles_) {
          rowLabels_.push_back(hnui::fitLines(target, title.c_str(), titleWidth, 2, titleStyle));
        }
        // A second pass, because a push_back into rowLabels_ can reallocate and
        // ListItem holds pointers rather than copies.
        for (size_t i = 0; i < rowLabels_.size(); ++i) {
          fui::ListItem row;
          row.label = rowLabels_[i].c_str();
          row.value = rowValues_[i].c_str();
          row.actionValue = static_cast<int16_t>(i);
          rows_.push_back(row);
        }
        rowsFitted_ = true;
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
        // Against the rows actually drawn, not against stories_: on the saved
        // shelf those are different lengths, and clamping to the wrong one
        // scrolls past the end or refuses to scroll at all.
        const int maxTop = static_cast<int>(rows_.size()) - visibleRows_;
        if (topIndex_ > maxTop) topIndex_ = maxTop < 0 ? 0 : maxTop;
        if (topIndex_ < 0) topIndex_ = 0;
      }
      hnui::ListModel model;
      model.items = rows_.empty() ? nullptr : rows_.data();
      model.count = static_cast<int>(rows_.size());
      model.selected = showingSaved_ ? -1 : selected_;
      model.topIndex = topIndex_;
      model.showingSaved = showingSaved_;
      if (showingSaved_) {
        model.title = "SAVED";
        if (rows_.empty()) {
          model.emptyHeadline = "NOTHING SAVED YET";
          // Measured, not guessed: centeredText does not wrap, and the old
          // wording was 620.6px against a 480px panel, so it cut mid-word to
          // "Open an article and tap the mar" -- in the ONLY place the app
          // says how to save anything. This is 345px in the 10pt face and
          // 415px in the 12pt, so it survives a face change too.
          model.emptyMessage = "Tap the mark on an article to keep it.";
        }
      }
      hnui::buildList(screen, model);
      what = showingSaved_ ? "HN saved" : "HN front page";
      break;
    }

    case Phase::Reading: {
      // Measured here because measuring needs a draw target, and measured from
      // the same rect the text is drawn into: readerBody() is the one function
      // that owns that rectangle, so a page turn cannot skip a line.
      const fui::Rect body = hnui::readerBody(device);
      const int16_t lineHeight = target.lineHeight(tokens.bodyText.font);
      visibleLines_ = fui::textAreaVisibleLines(body, lineHeight);
      lineCount_ = fui::textAreaMeasure(target, body.width, document_.c_str(), tokens.bodyText, 0).lineCount;

      const uint32_t pages = visibleLines_ > 0 ? (lineCount_ + visibleLines_ - 1) / visibleLines_ : 1;
      const uint32_t page = visibleLines_ > 0 ? topLine_ / visibleLines_ + 1 : 1;
      std::snprintf(pageLabel_, sizeof(pageLabel_), "%lu/%lu", static_cast<unsigned long>(page),
                    static_cast<unsigned long>(pages < 1 ? 1 : pages));

      hnui::ReaderModel model;
      // The story's own title, not COMMENTS/ARTICLE: which piece you are in
      // is the useful fact, and the footer's swap button already names the
      // mode. The builder fits it to the band.
      model.title = readerTitle_.c_str();
      model.text = document_.c_str();
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
      // A thread has nothing to save but the article it hangs off, so the mark
      // is absent there rather than offering to save the wrong thing.
      model.canSave = !readingComments_ && !readerUrl_.empty();
      model.saved = model.canSave && library_.contains(readerUrl_);
      hnui::buildReader(screen, model);
      what = "HN reader";
      break;
    }

    case Phase::Notice: {
      hnui::NoticeModel model;
      model.headline = noticeHeadline_.c_str();
      model.message = noticeMessage_.c_str();
      model.mark = noticeUnreadable_ ? &icon_unreadable_32 : nullptr;
      model.actionLabel = noticeUnreadable_ ? "READ THE COMMENTS" : nullptr;
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
