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

namespace fui = freeink::ui;

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

  // Nothing touches the radio here. The saved library needs no network, and a
  // read-later shelf you cannot open without connecting is not a read-later
  // shelf -- which is exactly what this app was until now: the picker came up
  // before anything, and cancelling it threw you out of the app entirely.
  //
  // So the list opens instantly, offline or not, and the first thing that
  // genuinely needs the network is what asks for it.
  library_.load();
  buildStoryRows();
  phase_ = Phase::List;
  requestUpdate();
}

void HackerNewsActivity::ensureConnected(const Pending what, const char* busyMessage) {
  if (link_ == Link::Connected) {
    request(what, busyMessage);
    return;
  }
  afterConnect_ = what;
  afterConnectMessage_ = busyMessage;
  returnPhase_ = phase_;
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
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
  Storage.remove(kFrontPageTmp);
}

void HackerNewsActivity::onWifiChosen(const bool connected) {
  if (!connected) {
    // Declining to connect is not leaving. Back to whatever was on screen when
    // the connection was asked for, which is usually a list they can still read
    // the saved half of.
    phase_ = returnPhase_;
    afterConnect_ = Pending::None;
    requestUpdate();
    return;
  }
  link_ = Link::Connected;
  const Pending what = afterConnect_;
  afterConnect_ = Pending::None;
  request(what, afterConnectMessage_);
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
      if (what == Pending::FrontPage) {
        // Back to the list rather than a full-screen notice: the saved half is
        // still there and still readable, and an error page would hide it.
        frontPageFailed_ = true;
        phase_ = Phase::List;
      } else {
        showNotice("NO LUCK", "Could not reach Hacker News. Check the network and try again.", false);
      }
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
      // Back lands on whichever half of the list this was opened from, so a
      // saved article does not drop the reader onto the front page.
      if (readingSaved_) {
        showingSaved_ = true;
        buildSavedRows();
      }
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

  const bool next = mappedInput.wasReleased(MappedInputManager::Button::Down);
  const bool prev = mappedInput.wasReleased(MappedInputManager::Button::Up);
  if (phase_ == Phase::List && !stories_.empty() && (next || prev)) {
    const int count = static_cast<int>(stories_.size());
    selected_ = (selected_ + (next ? 1 : count - 1)) % count;
    requestUpdate();
    return;
  }
  if (phase_ == Phase::List && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    ensureConnected(Pending::Article, "OPENING");
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
      selected_ = event.value;
      if (showingSaved_) {
        // Already on the card: no radio, no wait, no failure mode.
        openSavedArticle(selected_);
        requestUpdate();
      } else {
        readingSaved_ = false;
        ensureConnected(Pending::Article, "OPENING");
      }
      break;
    case hnui::ActionPagePrev:
      turnPage(-1);
      break;
    case hnui::ActionPageNext:
      turnPage(1);
      break;
    case hnui::ActionShowArticle:
      if (articleAvailable_) ensureConnected(Pending::Article, "OPENING");
      break;
    case hnui::ActionShowComments:
      ensureConnected(Pending::Comments, "FETCHING THE THREAD");
      break;
    case hnui::ActionSave:
      saveCurrentArticle();
      requestUpdate();
      break;
    case hnui::ActionUnsave:
      unsaveCurrentArticle();
      requestUpdate();
      break;
    case hnui::ActionShowFrontPage:
      showingSaved_ = false;
      selected_ = 0;
      topIndex_ = 0;
      buildStoryRows();
      requestUpdate();
      break;
    case hnui::ActionLoadFrontPage:
      frontPageFailed_ = false;
      ensureConnected(Pending::FrontPage, "FETCHING THE FRONT PAGE");
      break;
    case hnui::ActionShowSaved:
      library_.load();
      showingSaved_ = true;
      selected_ = 0;
      topIndex_ = 0;
      buildSavedRows();
      requestUpdate();
      break;
    case hnui::ActionNotice:
      // The notice's only button is always the way onward to the comments.
      ensureConnected(Pending::Comments, "FETCHING THE THREAD");
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
    // Titles arrive straight out of the JSON rather than through the comment
    // decoders, so they need the same fold: a headline is where curly quotes
    // are most common and where a missing glyph is most obvious.
    hn::foldTypography(story.title);
    story.url = hit["url"] | "";
    story.author = hit["author"] | "";
    story.points = hit["points"] | 0;
    story.commentCount = hit["num_comments"] | 0;
    story.id = static_cast<uint32_t>(std::strtoul(hit["objectID"] | "0", nullptr, 10));
    stories_.push_back(std::move(story));
  }

  buildStoryRows();

  LOG_INF("HN", "front page: %d stories", static_cast<int>(stories_.size()));
  selected_ = 0;
  topIndex_ = 0;
  showingSaved_ = false;
  phase_ = Phase::List;
  return !stories_.empty();
}

void HackerNewsActivity::buildStoryRows() {
  rowTitles_.clear();
  rowValues_.clear();
  rows_.clear();
  rowsFitted_ = false;
  rowTitles_.reserve(stories_.size());
  rowValues_.reserve(stories_.size());
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
}

bool HackerNewsActivity::fetchArticle() {
  const hn::Story* story = currentStory();
  if (story == nullptr) return false;

  articleAvailable_ = false;

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

  std::string articleTitle = extracted.title.empty() ? story->title : extracted.title;
  hn::foldTypography(articleTitle);
  blocks_.clear();
  for (const std::string& paragraph : hn::paragraphsFromMarkdown(extracted.body)) {
    addBlock(paragraph);
    addBlock("");
  }
  articleAvailable_ = true;
  readerUrl_ = story->url;
  readingSaved_ = false;
  library_.load();
  showDocument(story->title.c_str(), false);
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

  blocks_.clear();
  for (const hn::Comment& comment : comments) {
    // Shouted, because the whole thread is drawn in one face and one size:
    // weight on this device comes from size and inversion, and neither is
    // available inside a running page. A line of capitals reads as a name
    // rather than as the start of a sentence.
    std::string author;
    for (const char c : comment.author) {
      author += c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
    }
    addBlock(author, comment.depth, true);
    for (const std::string& paragraph : comment.paragraphs) {
      addBlock(paragraph, comment.depth);
    }
    // A gap at the comment's own depth, so its rule runs through it unbroken.
    addBlock("", comment.depth);
  }

  if (comments.empty()) {
    addBlock("Nobody has said anything yet.");
  } else if (scanner.truncated()) {
    char note[96];
    std::snprintf(note, sizeof(note), "Showing the first %d of %d comments.", static_cast<int>(comments.size()),
                  scanner.totalSeen());
    addBlock("");
    addBlock(note);
  }

  LOG_INF("HN", "thread: kept %d of %d comments, %d blocks", static_cast<int>(comments.size()), scanner.totalSeen(),
          static_cast<int>(blocks_.size()));
  showDocument(story->title.c_str(), true);
  return true;
}

// --- Reading -------------------------------------------------------------

void HackerNewsActivity::addBlock(std::string text, const int depth, const bool isAuthor) {
  Block block;
  block.text = std::move(text);
  block.depth = depth < hn::kMaxCommentDepth ? depth : hn::kMaxCommentDepth;
  block.isAuthor = isAuthor;
  blocks_.push_back(std::move(block));
}

void HackerNewsActivity::wrapBlocks(const freeink::ui::DrawTarget& target, const freeink::ui::DeviceContext& device,
                                    const freeink::ui::ThemeTokens& tokens) {
  lineText_.clear();
  lineMeta_.clear();
  linePtr_.clear();
  for (const Block& block : blocks_) {
    hnui::appendWrapped(target, device, tokens, block.text.c_str(), block.depth, block.isAuthor, lineText_, lineMeta_);
  }
  // A second pass, because a push_back into lineText_ can reallocate and the
  // model holds pointers rather than copies. Same rule as the story rows.
  linePtr_.reserve(lineText_.size());
  for (const std::string& line : lineText_) linePtr_.push_back(line.c_str());
  linesWrapped_ = true;
}

void HackerNewsActivity::showDocument(const char* title, const bool comments) {
  RenderLock lock(*this);
  readerTitle_ = title != nullptr ? title : "";
  readingComments_ = comments;
  topLine_ = 0;
  linesWrapped_ = false;
  phase_ = Phase::Reading;
  // Line counts need a draw target, which only exists inside render(). The
  // first paint fills them in; until then the label reads as one page, which is
  // what a document that has not been measured looks like.
  lineCount_ = 0;
  visibleLines_ = 0;
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
  const hn::Story* story = unreadable ? currentStory() : nullptr;
  showNotice(headline, story != nullptr ? story->title.c_str() : nullptr, message, unreadable);
}

void HackerNewsActivity::showNotice(const char* headline, const char* story, const char* message,
                                    const bool unreadable) {
  RenderLock lock(*this);
  // A notice about a story leads with the story: it is the content, and it is
  // the one thing the reader needs to recognise what they just tapped. What
  // went wrong becomes the state line under it.
  noticeStory_ = story != nullptr ? story : "";
  noticeHeadline_ = headline;
  noticeMessage_ = message;
  noticeUnreadable_ = unreadable;
  phase_ = Phase::Notice;
}

void HackerNewsActivity::drawRowLabels(fui::GfxRendererTarget& target, const fui::DeviceContext& device,
                                       const fui::ThemeTokens& tokens, const int count, const int selected,
                                       const int topIndex) {
  if (rowLabels_.empty()) return;

  // The same band and row height the component was handed, so a label cannot
  // land on a row other than the one it belongs to.
  const fui::Rect band = hnui::listBand(device);
  const int16_t rowHeight = hnui::listRowHeight(target, tokens);
  const int16_t gap = tokens.listRowGap;
  const uint16_t visible = fui::listVisibleRows(band, rowHeight, gap);
  const int16_t width = hnui::listTitleWidth(target, device, tokens);

  for (uint16_t slot = 0; slot < visible; ++slot) {
    const int index = topIndex + slot;
    if (index >= count || index >= static_cast<int>(rowLabels_.size())) break;

    target.setFont(fui::GfxRendererTarget::FONT_BODY, rowFonts_[static_cast<size_t>(index)]);
    fui::TextStyle style = tokens.bodyText;
    style.maxLines = 2;
    // The selected row is filled black, so its headline has to be paper. There
    // is no grey on this panel and no third state to get wrong.
    style.color = index == selected ? fui::Color::White : fui::Color::Black;

    const fui::Rect where = fui::makeRect(static_cast<int16_t>(band.x + tokens.listSidePadding),
                                          static_cast<int16_t>(band.y + slot * (rowHeight + gap)), width, rowHeight);
    target.text(where, rowLabels_[static_cast<size_t>(index)].c_str(), style);
  }
  target.setFont(fui::GfxRendererTarget::FONT_BODY, toybox::kReadingFontId);
}

// --- The library ---------------------------------------------------------

void HackerNewsActivity::saveCurrentArticle() {
  if (readerUrl_.empty() || blocks_.empty()) return;
  library_.load();

  std::string text;
  for (const Block& block : blocks_) {
    text += block.text;
    text += "\n";
  }
  if (!library_.save(readerUrl_, readerTitle_, text)) {
    showNotice("COULD NOT SAVE", readerTitle_.c_str(), "There was no room on the card, or it is not writable.", false);
  }
}

void HackerNewsActivity::unsaveCurrentArticle() {
  if (!library_.remove(readerUrl_)) return;

  // Reading it from the library and then removing it leaves the reader on
  // something no longer in the list, so it goes back to where the list is.
  if (readingSaved_) {
    buildSavedRows();
    selected_ = 0;
    topIndex_ = 0;
    phase_ = Phase::List;
  }
}

bool HackerNewsActivity::openSavedArticle(const int index) {
  const std::vector<hn::SavedArticle>& saved = library_.articles();
  if (index < 0 || index >= static_cast<int>(saved.size())) return false;
  const hn::SavedArticle& article = saved[static_cast<size_t>(index)];

  std::string text;
  if (!library_.readArticle(article, text)) {
    showNotice("NOT ON THE DEVICE", article.title.c_str(), "The words for this one are missing from the card.", false);
    return true;
  }

  blocks_.clear();
  size_t i = 0;
  while (i < text.size()) {
    const size_t nl = text.find('\n', i);
    const size_t end = nl == std::string::npos ? text.size() : nl;
    addBlock(text.substr(i, end - i));
    i = end == text.size() ? text.size() : nl + 1;
  }
  readerUrl_ = article.url;
  readingSaved_ = true;
  articleAvailable_ = true;
  showDocument(article.title.c_str(), false);
  return true;
}

void HackerNewsActivity::buildSavedRows() {
  rowTitles_.clear();
  rowLabels_.clear();
  rowValues_.clear();
  rows_.clear();
  rowsFitted_ = false;
  rowTitles_.reserve(library_.articles().size());
  rowValues_.reserve(library_.articles().size());
  for (const hn::SavedArticle& article : library_.articles()) {
    rowTitles_.push_back(article.title);
    // No footnote: the comment count belongs to the front page and means
    // nothing once an article is on the device, and there is no second state
    // for a saved row to be in.
    rowValues_.emplace_back();
  }
}

// --- Drawing -------------------------------------------------------------

void HackerNewsActivity::render(RenderLock&&) {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  // The reading cut in the body slot. Hacker News is a page of text, not a
  // board, and at the 20px UI cut a 480px panel holds about 28 characters a
  // line: an article became forty page taps and half the headlines on the front
  // page could not finish.
  //
  // The small slot differs by screen. The list wants the dense cut for its
  // footnote counts and has no buttons; every other screen wants Jersey on its
  // buttons and has no footnote. Three slots is a working set, so this is one
  // assignment rather than a fourth face.
  fui::GfxRendererTarget target =
      toybox::makeTarget(renderer, phase_ == Phase::List ? toybox::readingFaces() : toybox::readerFaces());
  const fui::DeviceContext device = target.deviceContext();
  const fui::ThemeTokens tokens = toybox::themeTokens();
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, device, noInput, interactions_);
  toybox::Screen screen(frame, tokens);

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
        // Per row, the largest cut the headline actually fits in. Big first; if
        // it would have to be cut, try the small one; if that still would, keep
        // the small one and let it end in an ellipsis. Both cuts get two lines.
        //
        // Measured by rebinding the body slot, because measureText works on
        // slots rather than faces -- and the label is drawn by us afterwards
        // for the same reason: ListProps carries one label style for the whole
        // list, so per-row type cannot go through the component at all.
        static constexpr int kRowCuts[] = {toybox::kReadingFontId, toybox::kReadingSmallFontId};
        rowFonts_.clear();
        rowFonts_.reserve(rowTitles_.size());
        for (const std::string& title : rowTitles_) {
          std::string best;
          int bestCut = kRowCuts[0];
          for (const int cut : kRowCuts) {
            target.setFont(fui::GfxRendererTarget::FONT_BODY, cut);
            best = hnui::fitLines(target, title.c_str(), titleWidth, 2, titleStyle);
            bestCut = cut;
            const size_t length = best.size();
            if (length < 3 || best.compare(length - 3, 3, "...") != 0) break;
          }
          rowLabels_.push_back(best);
          rowFonts_.push_back(bestCut);
        }
        target.setFont(fui::GfxRendererTarget::FONT_BODY, toybox::kReadingFontId);
        // A second pass, because a push_back into rowLabels_ can reallocate and
        // ListItem holds pointers rather than copies.
        for (size_t i = 0; i < rowLabels_.size(); ++i) {
          fui::ListItem row;
          // Blank: the component lays out the row, its border, its selection
          // fill and its hit rect, and we paint the headline over it at this
          // row's own cut.
          row.label = "";
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
      topIndex_ = static_cast<int>(
          fui::listTopIndexFor(static_cast<int16_t>(selected_), static_cast<uint16_t>(topIndex_),
                               fui::listVisibleRows(hnui::listBand(device), rowHeight, tokens.listRowGap),
                               static_cast<uint16_t>(stories_.size())));
      hnui::ListModel model;
      model.showingSaved = showingSaved_;
      if (!showingSaved_ && stories_.empty()) {
        // Two different screens, and collapsing them is how a working app reads
        // as a broken one. Nothing fetched yet is an invitation; a fetch that
        // failed is an error. The headline is the hit target either way, which
        // is the front-door rule: the commonest tap is the largest thing on the
        // screen and needs no button.
        model.emptyHeadline = frontPageFailed_ ? "NO LUCK" : "TAP TO LOAD";
        model.emptyMessage = frontPageFailed_ ? "Could not reach Hacker News. Tap to try again."
                                              : "The front page needs a connection. Saved articles do not.";
        model.emptyAction = hnui::ActionLoadFrontPage;
      } else if (showingSaved_ && library_.empty()) {
        // The normal state of a new device, so it says what it is. An error
        // here would be a lie and would read as a broken feature.
        // Two words, because the display cut is wide and "NOTHING SAVED YET"
        // came out as "NOTHING SAVED Y" on a 480px panel.
        model.emptyHeadline = "NOTHING SAVED";
        model.emptyMessage = "Open a story and tap the mark to keep it here for offline reading.";
      }
      model.items = rows_.empty() ? nullptr : rows_.data();
      model.count = static_cast<int>(rows_.size());
      model.selected = selected_;
      model.topIndex = topIndex_;
      hnui::buildList(screen, model);
      drawRowLabels(target, device, tokens, model.count, selected_, topIndex_);
      what = "HN front page";
      break;
    }

    case Phase::Reading: {
      // Wrapping needs a draw target, so it happens here rather than at fetch
      // time, and once per document rather than per paint.
      if (!linesWrapped_) wrapBlocks(target, device, tokens);

      visibleLines_ = hnui::readerVisibleLines(target, device, tokens);
      lineCount_ = static_cast<uint32_t>(lineText_.size());

      const uint32_t pages = visibleLines_ > 0 ? (lineCount_ + visibleLines_ - 1) / visibleLines_ : 1;
      const uint32_t page = visibleLines_ > 0 ? topLine_ / visibleLines_ + 1 : 1;
      std::snprintf(pageLabel_, sizeof(pageLabel_), "%lu/%lu", static_cast<unsigned long>(page),
                    static_cast<unsigned long>(pages < 1 ? 1 : pages));

      // Fitted against the bold title cut the band actually sets it in, and
      // ellipsised rather than clipped: a headline that stops mid-word reads as
      // a rendering fault.
      //
      // One line. The band is 76px and two lines of the bold cut overflow it --
      // the second was drawn straight through the rule underneath. A title cut
      // to one line still says which story you are in, which is its whole job.
      const bool canSaveHere = !readingComments_ && articleAvailable_ && !readerUrl_.empty();
      // Step the cut down before cutting the words. At the bold 16 most story
      // titles overflowed the band and were ellipsised to three or four words;
      // at 12 nearly all of them fit whole, and a smaller headline that says
      // what the story is beats a bigger one that does not.
      //
      // Done by rebinding the title slot rather than by measuring against a
      // second slot, because the small slot carries the button face and the
      // footer must stay Jersey. Three slots is a working set: rebinding one
      // mid-render is a single assignment.
      // Largest first, and a smaller cut earns a second line: this band is 76px,
      // which two lines of the 16px cut overflow and two of the 12px cut do not.
      // Without that the step-down buys almost nothing, because a long headline
      // needs about 530px and the panel is 480 wide -- no single line can hold
      // it at any size we have.
      struct BandCut {
        int font;
        int lines;
      };
      static constexpr BandCut kBandCuts[] = {{toybox::kReadingBoldFontId, 1}, {toybox::kReadingBoldSmallFontId, 2}};
      int bandLines = 1;
      for (const BandCut& cut : kBandCuts) {
        target.setFont(fui::GfxRendererTarget::FONT_TITLE, cut.font);
        const int16_t width = hnui::readerTitleWidth(target, device, tokens, canSaveHere, pageLabel_);
        readerTitleFitted_ = hnui::fitLines(target, readerTitle_.c_str(), width, cut.lines, tokens.titleText);
        bandLines = cut.lines;
        const size_t length = readerTitleFitted_.size();
        if (length < 3 || readerTitleFitted_.compare(length - 3, 3, "...") != 0) break;
      }

      hnui::ReaderModel model;
      model.title = readerTitleFitted_.c_str();
      model.titleLines = bandLines;
      model.lineText = linePtr_.empty() ? nullptr : linePtr_.data();
      model.lineMeta = lineMeta_.empty() ? nullptr : lineMeta_.data();
      model.lineCount = static_cast<int>(linePtr_.size());
      model.topLine = topLine_;
      model.pageLabel = pageLabel_;
      model.showingComments = readingComments_;
      model.articleAvailable = articleAvailable_;
      model.commentsAvailable = true;
      model.canPagePrev = topLine_ > 0;
      model.canPageNext = lineCount_ > topLine_ + visibleLines_;
      // Only an article can be kept; a thread is a conversation that will have
      // moved on by the time it is read again.
      model.canSave = canSaveHere;
      model.saved = model.canSave && library_.contains(readerUrl_);
      hnui::buildReader(screen, model);
      what = "HN reader";
      break;
    }

    case Phase::Notice: {
      hnui::NoticeModel model;
      if (noticeStory_.empty()) {
        model.headline = noticeHeadline_.c_str();
      } else {
        // Fitted the same way a story row is, against the display cut this
        // screen sets it in.
        fui::TextStyle headline = tokens.titleText;
        headline.maxLines = 2;
        noticeFitted_ = hnui::fitLines(target, noticeStory_.c_str(),
                                       static_cast<int16_t>(device.width - 2 * toybox::kMargin), 2, headline);
        model.headline = noticeFitted_.c_str();
        model.state = noticeHeadline_.c_str();
      }
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
