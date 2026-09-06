#pragma once

// Wallpapers on the device. The thin layer: renderer, storage, input, shelf.
//
// The app is a face for the sleep system that already exists. It shows the BMPs
// in /wallpapers as a two-column grid of thumbnails, and a tap on one pins it as
// the sleep screen by copying it to /sleep.bmp -- the single image the sleep
// screen already checks first -- and switching the sleep mode to CUSTOM. The
// set wallpaper wears a thick border. SleepActivity is untouched.
//
// A thumbnail is a full 480x800 1-bit BMP box-downscaled into a cell on the
// device (the renderer's 1-bit blit cannot scale, so the app does it), decoded
// once per page and cached in RAM so moving the border does not re-read the SD.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../../network/CrossPointWebServer.h"
#include "../ui/ToyboxScreen.h"
#include "WallpapersCore.h"
#include "WallpapersScreens.h"

class WallpapersActivity final : public Activity {
 public:
  WallpapersActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Wallpapers", renderer, mappedInput) {}
  ~WallpapersActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // A wallpaper scaled down to a cell: a 1-bpp bit buffer (set bit = ink) plus
  // where it sits inside the cell once its aspect is fitted.
  struct Thumb {
    int16_t w = 0;
    int16_t h = 0;
    int16_t ox = 0;
    int16_t oy = 0;
    std::vector<uint8_t> bits;  // packed, (w+7)/8 bytes per row
    bool ok = false;
  };

  // What the screen is showing. Derived from what is on the card, never from a
  // remembered "have I offered already" flag: a stored value that decides what
  // you see turns a reproducible screen into a nondeterministic one
  // (invisible-saved-state-reads-as-nondeterminism).
  // Sheet, Confirm and Preview are the hold branch. Preview draws NO chrome at
  // all -- it is what the sleep screen puts on the glass, and a hint band over
  // it would be a preview of something that never appears.
  //
  // Help is absent on purpose: app/wallqr removed it with buildHelp when the QR
  // screen replaced it, and a member nothing sets is a branch nothing reaches.
  enum class View : uint8_t { Grid, Offer, Fetching, Notice, Add, Sheet, Confirm, Preview };
  View view_ = View::Grid;

  void scanLibrary();
  int builtInsPresent() const;  // how many of the built-in set are on the card
  void pickView();              // Grid or Offer, from the card alone
  void sweepPartFiles();        // drop incomplete copies left by a power cut
  void sweepPartFilesIn(const char* dirPath);
  void startSetDownload();  // ask for WiFi, then queue the fetch
  void onWifiChosen(bool connected);
  void runSetDownload();  // blocking: fetch the pack, then unpack it
  bool unpackSet();       // pack -> individual .bmp files, resumable
  void prewarmThumbs();   // build the thumbnail cache while the bar is still up
  void showNotice(const char* headline, const char* body, const char* actionLabel, freeink::ui::ActionId action);
  // What the CARD says is chosen, re-read after every commit. Never a
  // remembered intention: /sleep.bmp and /.sleep are what the sleep screen
  // reads, so they are what the picker reports (see docs/apps/wallpapers-shuffle.md).
  void loadSelection();
  bool isChosen(int index) const;
  void listShuffleDir(std::vector<std::string>& out) const;
  // Put the card into the one shape cardShapeFor() names for this many
  // wallpapers. The ONLY writer of /sleep.bmp, /.sleep and /wallpapers/.active,
  // so the invariant that they are never both live has one place to hold.
  bool commitSelection(const std::vector<std::string>& want);
  bool copyWallpaper(const std::string& src, const std::string& dst) const;
  std::string sourcePathFor(const std::string& name) const;
  bool fillShuffleDir(const std::vector<std::string>& want);
  void clearShuffleDir();
  void applySleepSettings();
  void toggleChosen(int index);  // a tap on a tile while choosing
  void openAdd();                // entry: get the radio, then serve
  void startAddServer();         // latch dev mode, bind, advertise, build the address
  void stopAddServer();          // and undo all four, in reverse
  void pollAddArrivals();        // has a wallpaper landed while the code was up?
  void computeWarning();
  // Whether the pinned wallpaper can reach the sleep screen under the settings
  // as they are RIGHT NOW, and the one line that says so. Both read SETTINGS
  // live, because the two settings involved are changed elsewhere (Settings,
  // and the web settings page) while this app is not looking. See #354.
  wallpapers::Reach sleepReach() const;
  bool sleepBlocked() const;
  // Not const: a line carrying a count is built into note_, which has to
  // outlive the paint. Every other line is a literal out of WallpapersCore.
  const char* currentSleepNote();
  bool setWallpaper(int index);  // choose exactly this one
  void openSheet(int index);     // a hold landed on this library index
  bool deleteWallpaper();        // remove the sheet's wallpaper from the card
  void renderPreview();          // the wallpaper at 1:1, nothing else on the panel
  int pageCount() const;         // over the whole library
  void clampPage();
  void ensureThumbsForPage();  // decode this page's cells if not cached
  Thumb decodeThumb(const std::string& path, int16_t cellW, int16_t cellH) const;
  // Cached decode: reads /wallpapers/.thumbs/<name>.thb when it still matches
  // the source and the cell size, otherwise decodes and writes it.
  Thumb thumbFor(const std::string& name, const std::string& path, int16_t cellW, int16_t cellH, int* decoded);
  void drawGrid(const wallpapersui::GridGeom& geom);
  void drawAddTile(const wallpapersui::GridGeom& geom, const freeink::ui::Rect& th);
  void drawGetSetTile(const wallpapersui::GridGeom& geom, const freeink::ui::Rect& th) const;
  int specialTiles() const;  // chrome tiles in front of the wallpapers
  void drawMarker(const freeink::ui::Rect& th) const;

  // Which wallpaper the sheet, the confirm and the preview are about. Held as a
  // NAME as well as an index because the index is a position in a list that
  // deleting, uploading and page-turning all renumber, and a stale index would
  // delete the wrong picture. The name is re-resolved to an index at the moment
  // of the delete and the delete refuses if it no longer resolves.
  int sheetIndex_ = -1;
  std::string sheetFile_;    // the file name, the identity that survives a re-sort
  std::string sheetName_;    // its display name, for the two screens' headline
  std::string sheetDetail_;  // the confirm's consequence sentence(s)
  // Settled by openSheet on the LOOP task. render() runs on the other task with
  // no lock between them, so it reads this rather than indexing names_, which
  // deleteWallpaper clears and reallocates underneath it.
  bool sheetIsActive_ = false;

  std::vector<std::string> names_;  // library file names, sorted
  // The chosen set, as it is on the card. Holds the pinned name when one
  // wallpaper is chosen and the contents of /.sleep when several are -- INCLUDING
  // any whose library file has since been deleted, because those copies still
  // take their turn on the glass and a count that skipped them would understate
  // what the sleep screen does.
  std::vector<std::string> chosen_;
  int activeIndex_ = -1;         // which name is pinned, or -1
  int builtInsMissing_ = 0;      // how many of the built-in set are not on the card
  bool warningPending_ = false;  // the free-space walk, deferred until after the first paint
  bool painted_ = false;         // the panel has shown something at least once
  int page_ = 0;
  int fetchDone_ = 0;
  int fetchTotal_ = 0;
  bool fetchCancel_ = false;
  int fetchPhase_ = 0;  // 0 fetch, 1 unpack, 2 thumbnails -- thirds of one bar
  bool fetchQueued_ = false;
  std::string noticeHead_;
  std::string noticeBody_;
  const char* noticeAction_ = nullptr;
  freeink::ui::ActionId noticeActionId_ = 0;
  std::unique_ptr<CrossPointWebServer> addServer_;
  int addBefore_ = 0;   // library size when the code went up
  int addArrived_ = 0;  // how many have landed since
  bool addWaitingWifi_ = false;
  unsigned long addLastPoll_ = 0;
  // Whether THIS screen is the one holding dev mode's yield. The LinkRadio
  // shape: what makes the pause/resume pairing correct at runtime is this flag,
  // not the 1:1 source count host-tests/release can see (its own comment says
  // it cannot see reachability at all). One pause, one resume, one owner.
  bool addDevPaused_ = false;

  // The screen owns the radio while the code is up, and the user is looking at
  // their PHONE -- nothing here counts as activity, so the 10-minute auto-sleep
  // (minimum 1) would reset the chip mid-upload. The two other owners of this
  // same server both carry this line; this is the third (fix-the-twin-too).
  bool preventAutoSleep() override { return addServer_ && addServer_->isRunning(); }
  std::string addStatus_;
  std::string addQrUrl_;  // what the CODE carries: always the numeric address
  std::string addUrl_;  // printed large: the name when it resolves, else the address     // http://crossplay.local/w --
                        // what the QR encodes and the screen prints
  std::string addAltUrl_;  // http://<ip>/w -- the fallback, printed under it, never encoded
  std::string rightLabel_;
  std::string warning_;
  // The last selection's outcome, kept so the strip can report what it changed
  // behind the user's back. The CHOICE, not a rendered sentence: the sentence
  // also has to carry any standing caveat, and a caveat is only knowable from
  // SETTINGS at paint time. Holding a string here is what let the first version
  // suppress a caveat for a whole app session (#354, twice over).
  bool selectedThisSession_ = false;
  wallpapers::SleepChoice lastChoice_;
  // Choose-a-set mode: what a tap on a tile means. RAM only and reset on every
  // onEnter, because it is a mode the user is in, not a fact about the card --
  // an invisible saved value that decides what a tap does is how a reproducible
  // screen becomes a nondeterministic one.
  bool choosing_ = false;
  // /sleep.bmp present while /.sleep also holds files: one picture is showing
  // and a set is inert behind it. Read off the card on entry, because nothing
  // in SETTINGS can see it and this app is not its only cause.
  bool shadowedSet_ = false;
  // The last free-space walk's raw answer, so an add can apply the precondition
  // at its own floor without a second FAT cluster walk on the input path.
  bool freeKnown_ = false;
  uint64_t freeBytes_ = 0;
  std::string note_;  // the hint strip's line, when it carries a number

  // The current page's thumbnails, one per on-page slot (perPage entries;
  // trailing empty slots have ok = false).
  std::vector<Thumb> thumbs_;
  int cachedPage_ = -1;
  int cachedPerPage_ = -1;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;

  // The grid is hit-tested against geometry and never reaches route(), so its
  // taps are gated on the page and the pinned wallpaper -- state loop() settles
  // before render() runs. See Activity::surfaceMeaning().
  uint32_t surfaceMeaning() const override;
};
