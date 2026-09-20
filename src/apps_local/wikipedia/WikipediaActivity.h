#pragma once

// WIKIPEDIA: the pack on the card, read like a book.
//
// The activity is the thin layer: it owns the pack, the query, the article the
// book engine is laying out, the history and the install hand-over. The format
// is WikipediaCore.h (freestanding), the card side is WikipediaPack.h, the
// drawing is WikipediaScreens.cpp (freestanding). Design and decisions:
// docs/apps/wikipedia-plan.md; the pack: docs/apps/wikipedia-pack-format.md.

#include <Epub/Page.h>
#include <Epub/Section.h>
#include <FreeInkUIGfxRenderer.h>
#include <PaintClock.h>

#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "WikipediaCore.h"
#include "WikipediaPack.h"
#include "WikipediaScreens.h"

class WikipediaActivity final : public Activity {
 public:
  WikipediaActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Wikipedia", renderer, mappedInput) {}
  ~WikipediaActivity() override;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // The card is the host's while the install screen shows; a deep sleep there
  // is a chip reset with the host mid-write.
  bool preventAutoSleep() override { return view_ == View::Install; }
  // While the card is handed to a USB host nothing outside this loop may
  // navigate: ActivityManager's home gesture would exit through onExit with
  // no restart, and the card stays detached until someone power-cycles.
  bool requiresExclusiveStorageLoop() const override { return view_ == View::Install; }
  // Keep laying out the open article between renders.
  bool skipLoopDelay() override;

 private:
  enum class View : uint8_t { Search, Article, Contents, Install, Notice };
  struct Visit {
    uint32_t locator;
    int page;
    // Carried so the cue can name the article on the way BACK too: the card
    // keeps 32 staged articles and the trail is 8 deep, so a step back can
    // land on one that was evicted, which is a fresh open with a cue.
    std::string title;
  };
  static constexpr int kHistoryDepth = 8;
  // Below this many bytes of XHTML, headings flow with the text; above it each
  // top-level heading starts a fresh page.
  static constexpr size_t kFreshPageBytes = 24 * 1024;
  // A title longer than this does not fit the band whole; the page keeps its h1.
  static constexpr size_t kBandTitleBytes = 56;
  static constexpr unsigned long kHostWaitMs = 30UL * 60UL * 1000UL;
  static constexpr unsigned long kCableOutMs = 2000;

  void go(View next);
  void routeAction(int action, int value);
  void showNotice(const char* headline, const char* body, const char* actionLabel, freeink::ui::ActionId action);
  // A notice that names the title it is about: "There is no article called "X"".
  void noticeAbout(const char* headline, const char* fmt, const std::string& title);

  // search
  void refreshResults();
  void handleKey(int value);
  void openTitle(const std::string& title);
  void openRandom();
  freeink::ui::Rect keyboardRect() const;
  void drawKeyboard();

  // article
  // The title comes from the caller because every path into an article has
  // one before the article is read (the index entry, the recent row, the
  // link), and the cue needs it BEFORE the read rather than after.
  bool openLocator(uint32_t locator, int page, const std::string& anchor, const std::string& title);
  bool stageArticle(uint32_t locator);
  bool ensureBuilt();
  void closeArticle();
  void turnPage(int delta);
  void pushHistory();
  void popHistory();
  void refreshHeadingPages();
  int headingForPage(int page) const;
  void renderArticle(toybox::Screen& screen);
  // Creates the section against a body rect, from render() or from the cue.
  void ensureSection(const freeink::ui::Rect& body);
  // The work the cue's waveform pays for: staging and laying out while the
  // panel is busy. Does nothing when no cue is flying.
  void layOutUnderCue();
  void saveState();
  void pruneCache();

  // install
  void enterInstall();
  void leaveInstall();

  View view_ = View::Search;
  wikipedia::Pack pack_;
  wikipedia::State state_;
  bool packOpen_ = false;
  std::string footer_;
  std::string partsLine_;

  // search
  std::string query_;
  std::vector<wikipedia::IndexEntry> results_;
  bool shifted_ = false;
  bool symbols_ = false;
  // The keyboard rises on a tap of the field and goes down with the X or Back.
  bool keyboardShown_ = false;
  freeink::ui::InteractionBuffer<56> kbInteractions_;
  paintclock::RevealGate kbGate_;

  // article
  uint32_t locator_ = 0;
  wikipedia::Article article_;
  std::string cacheDir_;
  std::unique_ptr<Section> section_;
  std::vector<PageLink> links_;
  int linkMarginLeft_ = 0;
  int linkMarginTop_ = 0;
  int targetPage_ = 0;
  std::string pendingAnchor_;
  std::vector<int> headingPages_;  // page of each heading, -1 while unknown
  bool headingPagesFinal_ = false;
  std::vector<Visit> history_;
  bool buildFailed_ = false;
  int contentsFirst_ = 0;
  int contentsShown_ = 0;  // rows the last contents render fitted
  // Row of state_.recent behind each drawn recent row (the trail skips the
  // article CONTINUE names).
  int recentRows_[wikiui::kMaxRecent] = {};

  // install
  wikiui::InstallModel::Stage stage_ = wikiui::InstallModel::Stage::Waiting;
  unsigned long stageAt_ = 0;
  bool usbActive_ = false;
  unsigned long cableOutAt_ = 0;
  bool restartRequested_ = false;

  // notice
  std::string noticeHead_;
  std::string noticeBody_;
  const char* noticeAction_ = nullptr;
  freeink::ui::ActionId noticeActionId_ = 0;

  toybox::Interactions interactions_;
  // Painted before an article is read and laid out, deferred so the panel's
  // waveform runs while that work happens. render() lands it.
  void paintOpeningCue(const std::string& title);
  bool cuePainted_ = false;
  // The body rect the cue's own chrome produced, so the layout under the
  // waveform is made for the same viewport render() will draw into.
  freeink::ui::Rect cueBody_{};
  bool buildLogged_ = false;
  // Layout time, wherever it ran: under the cue or in render().
  uint32_t buildMs_ = 0;
  bool stagedFresh_ = false;
  uint32_t stageMs_ = 0;
  bool interactionsReady_ = false;
};
