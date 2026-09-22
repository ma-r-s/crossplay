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
#include "../live/LiveStore.h"
#include "../ui/ToyboxScreen.h"
#include "WallpapersCore.h"
#include "WallpapersScreens.h"

// LIVE: a wallpaper slot fed from a website rather than from the card, mutually
// exclusive with the normal selection. Three arrangements for its tile were
// built behind one switch and rendered side by side; Mario picked the combined
// tile, which replaces + Add outright and is captioned "Your phone". The other
// two (a Live tile of its own after + Add, and no Live tile until it is set up)
// went with the macro that chose between them in the same commit.
//
// That tile keeps + Add's CELL so no learned pixel moves, and it is the only
// chrome tile in front of the library now. It leads to a destination naming
// BOTH routes (wallpapersui::buildPhone): this comment used to say the upload
// server kept its own way in through the offer screen's USE MY OWN PHOTO, and
// that was false -- the offer screen stops appearing the moment a reader has
// any wallpapers at all, so on every reader past its first fetch there was no
// route to the upload server. Walk the screens rather than grep for openAdd():
// the function survived the release that made it unreachable.

// Whether a Live slot has been set up on this device, and whether it is what
// the sleep screen shows right now. Compile-time, because this slice is the
// tile and not the plumbing, and a fixed answer is what makes each render
// reproducible.
#ifndef WALLPAPERS_LIVE_CONFIGURED
#define WALLPAPERS_LIVE_CONFIGURED 0
#endif
#ifndef WALLPAPERS_LIVE_ON
#define WALLPAPERS_LIVE_ON 0
#endif
// And whether a picture has just landed on the Add screen. Reaching that for
// real needs a phone posting a file to a server the simulator does not compile
// at all, so the one screen the upload route ends on would otherwise be the one
// screen no render could ever show. Read only inside the SIMULATOR branch of
// openAdd(), default off.
#ifndef WALLPAPERS_ADD_ARRIVED
#define WALLPAPERS_ADD_ARRIVED 0
#endif

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
  // Live is one of the two routes BEHIND that destination: the pairing code
  // before a phone is attached, the schedule and the senders afterwards. It
  // reaches the panel through wallpapersui::buildLive like every other screen
  // here.
  // Phone is the "Your phone" tile's destination: the two-route screen that
  // names what a phone can do with this reader. Live sits behind it now rather
  // than being it, and Add is reachable from it -- which is the whole of the
  // fix for a route that had gone missing from every reader with wallpapers on
  // it (see wallpapersui::buildPhone).
  enum class View : uint8_t { Grid, Offer, Fetching, Notice, Add, Sheet, Confirm, Preview, Phone, Live };
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
  void addAnother();             // put the code back after a picture has landed
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
  // One blit, two callers: the grid's cells and the Add screen's arrival. A
  // second copy of "a set bit is ink, offset by the fit" is a second thing to
  // get wrong, and this fork's recurring bug is the fix landing on one of two
  // identical paths (fix-the-twin-too).
  void drawThumbInto(const Thumb& t, const freeink::ui::Rect& box) const;
  void drawGrid(const wallpapersui::GridGeom& geom);
  void drawGetSetTile(const wallpapersui::GridGeom& geom, const freeink::ui::Rect& th, int slot) const;
  void drawLiveTile(const wallpapersui::GridGeom& geom, const freeink::ui::Rect& th, int slot) const;
  // Which chrome tile a combined index is, or None once the wallpapers start.
  // specialAt() and specialTiles() are one ordering read twice: the drawing and
  // the hit-test both go through them, so no cell can draw one thing and open
  // another -- the bug this fork has caught more often than any other.
  enum class SpecialTile : uint8_t { None, Live, GetSet };
  SpecialTile specialAt(int combined) const;
  int specialTiles() const;     // chrome tiles in front of the wallpapers
  bool liveConfigured() const;  // a Live slot exists on this device
  bool liveOn() const;          // and it is what the sleep screen shows, as the device booted
  void openPhone();             // the tile's destination: both routes named
  void openLive();              // one of them
  // What Live is doing RIGHT NOW, which is liveOn() until somebody presses the
  // screen's own toggle. Kept as a member because the tile's marker and the
  // screen's switch are two readings of one fact and came apart once already;
  // it is now WRITTEN THROUGH to the card by toggleLive(), so "on" outlives the
  // app being closed. Seeded in onEnter() from live::load().
  bool liveRunning_ = false;

  // Live's own state, loaded on entry and saved by whatever changes it. Held
  // here rather than re-read per render for the reason the sheet's isActive
  // flag is: render() runs on the other FreeRTOS task, and a std::string this
  // one reallocates under it is a read of freed memory.
  live::State liveState_;
  std::string livePollToken_;     // the pairing in flight, empty when there is none
  std::string liveCode_;          // "601 663" -- grouped for reading down a phone
  std::string liveStatus_;        // the one line under the code or the switch
  std::string liveNextCheck_;     // "In 6 hours"
  std::string liveScheduleNote_;  // "Every 6 hours"
  std::string livePhoneState_;    // the one line under LIVE on the Phone screen
  // Work the loop task does AFTER the paint, never inside a tap: both of these
  // block on the radio for seconds, and an activity that blocks inside route()
  // is the #306 family this app has already been bitten by twice.
  bool livePairQueued_ = false;
  bool liveCheckQueued_ = false;
  bool liveOffQueued_ = false;
  unsigned long livePollAt_ = 0;  // millis() of the next /api/pair/poll
  // WHEN THE CODE ON THE GLASS DIES, from the service's own expiresIn rather
  // than from a ten-minute literal typed beside it. 0 means there is no code.
  //
  // The service sweeps a pending code at CODE_TTL_S and the reader used to go
  // on polling and displaying it forever, so a screen left up for eleven
  // minutes showed six digits that could not be claimed, under a line
  // promising they last ten. The recovery the website now names -- "type the
  // six digits it is showing now" -- is only true if the digits it is showing
  // are live, so this is what makes that sentence true.
  unsigned long liveCodeDeadline_ = 0;
  // The QR's payload is DERIVED from liveCode_ at paint time rather than kept
  // beside it: two members are two things that can disagree, and a code on the
  // panel that the square beside it did not encode is this card's whole bug.
  void clearLiveCode();
  void armLiveCodeDeadline(int expiresIn);
  void startLivePairing();
  void pollLivePairing();
  void runLiveCheck();
  void runLiveOffReport();
  void toggleLive();
  void refreshLiveLines();
  void applyLiveSleepSettings();

  // The sender list, as the service last answered it.
  //
  // A fixed array rather than a vector for the reason LiveModel's is: it is
  // written on the loop task and read on the render task with no lock across
  // them, and a container reallocated under a paint has already bitten this app
  // once. The strings behind the model's `const char*`s are these, so they have
  // to outlive every paint that can see them.
  struct LiveSenderRow {
    std::string who;
    std::string since;  // "12 Sep", empty when the service had no usable stamp
    // The service's own handle for this phone: the first 16 chars of its token
    // hash, never the token. It is what revoke names, and it is the reason the
    // screen carries an INDEX instead -- one copy of this fact, here.
    std::string id;
  };
  LiveSenderRow liveSenders_[wallpapersui::LiveModel::kMaxSenders];
  int liveSenderCount_ = 0;
  // The service's own cap, as it last said it. Logged rather than drawn: the
  // number in a refusal is the service's sentence to write, not ours.
  int liveSenderMax_ = wallpapersui::LiveModel::kMaxSenders;
  // A join code is being minted or is up. Distinct from livePairQueued_,
  // because the two mint codes with opposite meanings: a setup code makes a new
  // fridge and a join code adds a phone to this one. Conflating them is how ADD
  // SOMEBODY would silently orphan the phone already sending.
  bool liveJoinQueued_ = false;
  bool liveJoining_ = false;
  // Which row the revoke confirm is about, or -1 for "no confirm up". A
  // sub-state of View::Live rather than a View of its own, so the face set, the
  // tap routing and the Back unwind all keep working without a second copy of
  // each.
  int liveRevokeIndex_ = -1;
  bool liveSendersQueued_ = false;
  bool liveRevokeQueued_ = false;
  void startLiveJoin();
  void refreshLiveSenders();
  void runLiveRevoke();
  // True while the screen is showing a six-digit code. ONE reading, because the
  // Activity binds the 82px cut from it and the screen draws from it.
  bool liveShowingCode() const;
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
  // The display name of the picture that landed and is now the sleep screen,
  // empty while nothing has. A MEMBER because the screen reads it as a
  // `const char*` and render() runs on the other FreeRTOS task, like every
  // other string on these screens. Cleared on the way into the screen rather
  // than remembered, because it is a fact about this visit.
  std::string addArrivedName_;
  // And the picture itself, decoded ONCE when it lands rather than per paint.
  //
  // NOT through thumbFor: that cache is keyed on the file name alone and its
  // staleness check includes the cell size, so asking it for this screen's
  // 232px square would evict the grid's prewarmed entry for the same file at
  // the grid's cell size -- and the next grid paint would decode again, and
  // evict this one back. Two screens sharing a one-entry-per-name cache at two
  // sizes do not share it, they take turns destroying it. decodeThumb reads and
  // returns without touching the card's cache at all.
  Thumb addArrivedThumb_;
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
