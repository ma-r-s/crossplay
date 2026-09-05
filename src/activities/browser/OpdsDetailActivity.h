#pragma once
#include <OpdsParser.h>

#include <string>

#include "OpdsServerStore.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"

/**
 * Book details, shown between tapping a result and downloading it.
 *
 * The cover is whatever the catalog happens to serve: portrait, landscape,
 * tiny, or absent. Every case lands in a fixed box,
 * and a missing cover, a failed fetch and an unsupported format all draw the
 * same placeholder -- a timeout should not look like a different bug.
 */
class OpdsDetailActivity final : public Activity, private UiAppHost {
 public:
  OpdsDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, OpdsEntry entry, const OpdsServer& server,
                     std::string feedUrl);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static void screenTrampoline(UiScreen& screen, void* user);
  void buildScreen(UiScreen& screen);
  // Fixed destination box for the cover, whatever its source dimensions.
  void drawCover(UiScreen& screen, const freeink::ui::Rect& box);
  void drawPlaceholder(UiScreen& screen, const freeink::ui::Rect& box);
  void paintCover();

  // Shared with the browser's wait screen, which draws the same cached
  // cover: two copies of the BMP-then-decoder fallback would drift apart the
  // first time one of them learned a new format. Returns false when nothing
  // was drawn, so a caller that already reserved the space can fill it.
 public:
  static bool paintCoverFile(GfxRenderer& renderer, const std::string& path, const freeink::ui::Rect& box);

 private:
  static void downloadTrampoline(const freeink::ui::ActionEvent& event, void* user);
  void fetchCover();

  OpdsEntry entry;
  const OpdsServer& server;
  // The feed the entry came from; relative cover hrefs resolve against it.
  std::string feedUrl;
  std::string coverPath;
  std::string metaLine;
  bool coverAvailable = false;
  // The cover is an HTTP fetch that blocks for seconds. It must not run until
  // a frame has actually reached the panel, or the screen stays blank for the
  // whole fetch and the app reads as hung.
  bool coverPending = false;
  bool framePresented = false;
  // Written from inside the fetch's progress callback, which is the only code
  // that runs while the transfer blocks the loop. HttpDownloader reads
  // coverCancelled as its cancel flag and drops the partial file; loop() acts
  // on it once the abort unwinds.
  bool coverCancelled = false;
  bool coverGoHome = false;
  // Where buildScreen() reserved the cover. The image is painted after
  // renderUi() flushes the screen tree, which would otherwise paint over it.
  freeink::ui::Rect coverRect{};
};
