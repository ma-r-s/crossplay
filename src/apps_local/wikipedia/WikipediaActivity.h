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
  // Keep laying out the open article between renders.
  bool skipLoopDelay() override;

 private:
  enum class View : uint8_t { Search, Article, Contents, Install, Notice };
  struct Visit {
    uint32_t locator;
    int page;
  };
  static constexpr int kHistoryDepth = 8;
  // Below this many bytes of XHTML, headings flow with the text; above it each
  // top-level heading starts a fresh page.
  static constexpr size_t kFreshPageBytes = 24 * 1024;
  static constexpr unsigned long kHostWaitMs = 30UL * 60UL * 1000UL;

  void go(View next);
  void routeAction(int action, int value);
  void showNotice(const char* headline, const char* body, const char* actionLabel, freeink::ui::ActionId action);

  // search
  void refreshResults();
  void handleKey(int value);
  void openTitle(const std::string& title);
  void openRandom();
  freeink::ui::Rect keyboardRect() const;
  void drawKeyboard();

  // article
  bool openLocator(uint32_t locator, int page, const std::string& anchor);
  bool stageArticle(uint32_t locator);
  bool ensureBuilt();
  void closeArticle();
  void turnPage(int delta);
  void pushHistory();
  void popHistory();
  void refreshHeadingPages();
  int headingForPage(int page) const;
  void renderArticle(toybox::Screen& screen);
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
  // Whether the keyboard is drawn. Variant 1 keeps it up; the others raise it
  // on a tap of the field and drop it when the query is cleared.
  bool keyboardShown_ = true;
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

  // install
  wikiui::InstallModel::Stage stage_ = wikiui::InstallModel::Stage::Waiting;
  unsigned long stageAt_ = 0;
  bool usbActive_ = false;
  bool restartRequested_ = false;

  // notice
  std::string noticeHead_;
  std::string noticeBody_;
  const char* noticeAction_ = nullptr;
  freeink::ui::ActionId noticeActionId_ = 0;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
