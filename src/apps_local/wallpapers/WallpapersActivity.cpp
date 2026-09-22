#include "WallpapersActivity.h"

#include <ESPmDNS.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "../../../lib/GfxRenderer/FontCacheManager.h"
#include "../../CrossPointSettings.h"
#include "../../DevMode.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../network/HttpDownloader.h"
#include "../../util/DeviceHostname.h"
#include "../../util/QrUtils.h"
#include "../Shelf.h"
#include "../live/LiveBridge.h"
#include "../live/LiveEngine.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxText.h"
#include "../ui/ToyboxTheme.h"
#include "Bitmap.h"
#include "WallpapersCore.h"

namespace fui = freeink::ui;

namespace {
// WallpapersCore mirrors CrossPointSettings::SLEEP_SCREEN_MODE so the sleep-
// reachability rules are freestanding and host-testable. This is the seam where
// the mirror is checked: a value that moves upstream fails the build here
// rather than silently teaching the picker to say the wrong sentence.
static_assert(wallpapers::kSleepDark == CrossPointSettings::SLEEP_SCREEN_MODE::DARK, "sleep mode mirror drifted");
static_assert(wallpapers::kSleepLight == CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT, "sleep mode mirror drifted");
static_assert(wallpapers::kSleepCustom == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM, "sleep mode mirror drifted");
static_assert(wallpapers::kSleepCover == CrossPointSettings::SLEEP_SCREEN_MODE::COVER, "sleep mode mirror drifted");
static_assert(wallpapers::kSleepCoverCustom == CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM,
              "sleep mode mirror drifted");
static_assert(wallpapers::kSleepBlank == CrossPointSettings::SLEEP_SCREEN_MODE::BLANK, "sleep mode mirror drifted");
static_assert(wallpapers::kSleepQuickResume == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME,
              "sleep mode mirror drifted");
static_assert(wallpapers::kSleepTransparentCustom == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM,
              "sleep mode mirror drifted");
static_assert(wallpapers::kSleepModeCount == CrossPointSettings::SLEEP_SCREEN_MODE::SLEEP_SCREEN_MODE_COUNT,
              "a sleep screen mode was added upstream; WallpapersCore's mirror and its rules must be updated");

constexpr int kMaxLibrary = 256;
constexpr size_t kNameMax = 128;
constexpr size_t kCopyChunk = 4096;

// ---------------------------------------------------------------------------
// The thumbnail cache.
//
// A thumbnail is a full 480x800 1-bit BMP read off the card and box-downscaled
// (the renderer's 1-bit blit cannot scale). cachedPage_ resets on every
// onEnter, so before this the app re-did that work on EVERY open and on every
// page turn -- harmless on an empty card, and the whole cost once 21 wallpapers
// are on it.
//
// Cached per wallpaper, not per set, so one changed or deleted file invalidates
// its own thumb and nothing else. The key is the source's SIZE plus a sample of
// its bytes plus the cell size: every device wallpaper is exactly 48062 bytes,
// so size alone cannot see a replaced file, and a cell-size change (the hint
// gap moved it) must invalidate everything. The sample is three 64-byte reads
// rather than a full hash, because hashing the whole file would cost what the
// decode costs and save nothing.
constexpr char kThumbDir[] = "/wallpapers/.thumbs";
constexpr uint32_t kThumbMagic = 0x31485457;  // "WTH1"

struct ThumbHeader {
  uint32_t magic;
  uint32_t sourceBytes;
  uint32_t sampleHash;
  int16_t cellW, cellH, w, h, ox, oy;
};

uint32_t sampleHashOf(HalFile& f, const uint64_t size) {
  uint32_t h = 2166136261u;
  const uint64_t offsets[3] = {62, size / 2, size > 128 ? size - 128 : 0};
  uint8_t buf[64];
  for (const uint64_t off : offsets) {
    if (!f.seekSet(static_cast<size_t>(off))) continue;
    const int got = f.read(buf, sizeof(buf));
    for (int i = 0; i < got; ++i) {
      h ^= buf[i];
      h *= 16777619u;
    }
  }
  return h;
}

// The selection marker's dimensions live in WallpapersScreens.cpp beside
// kMarkerRoom, the clearance they have to fit inside.

// A page dot: a small square, filled for the current page.
constexpr int16_t kDotSize = 12;
constexpr int16_t kDotGap = 10;

// The Live tile's double frame: a thick outer rect with a hairline inside it.
constexpr int16_t kLiveFrameWeight = 3;
constexpr int16_t kLiveFrameInset = 5;

// The tile folds Live and + Add into one control, so it says whose picture
// arrives here rather than what the slot is called. The caption comes from
// WallpapersScreens because the hint strip's sentence names the same tile, and
// the two must not be able to drift apart (see liveTileCaption).

// The address the panel prints and the QR encodes is wallpapersui::kLiveAddress,
// and liveLink() is the only thing that builds a link out of it. Declared beside
// the screen that draws it rather than here, so the printed line and the encoded
// square cannot come apart.

// The screen's array and the transport's are one number, checked by the
// compiler rather than by whoever edits one of them.
//
// Neither is the RULE. THE SERVICE IS THE ONE THAT DECIDES how many phones a
// reader may have: it refuses the fifth before a code is minted, in its own
// sentence, and it answers its own cap on every /api/senders. These two are the
// size of the array that has to be able to hold a full answer. A drawing limit
// is not a limit -- a fifth sender the service allowed would exist, could write
// to this fridge, and would be invisible on the one screen that can revoke it,
// which is why live::listSenders logs loudly if the service ever says more.
static_assert(wallpapersui::LiveModel::kMaxSenders == live::kMaxSenders,
              "the Live screen and the Live transport disagree about how many senders fit");
// Ordered 8x8 Bayer thresholds. A thumbnail is an AREA AVERAGE of the source
// re-dithered at thumbnail size: a 50% threshold collapses a dense engraving
// into a flat blob ("grey mush"), while re-dithering keeps its tone as texture,
// which is the difference between a picker that looks designed and one that
// looks broken. Ordered rather than error diffusion, the house rule: error
// diffusion is what makes flat fields look dirty.
constexpr uint8_t kBayer8[64] = {
    0,  48, 12, 60, 3,  51, 15, 63, 32, 16, 44, 28, 35, 19, 47, 31, 8,  56, 4,  52, 11, 59,
    7,  55, 40, 24, 36, 20, 43, 27, 39, 23, 2,  50, 14, 62, 1,  49, 13, 61, 34, 18, 46, 30,
    33, 17, 45, 29, 10, 58, 6,  54, 9,  57, 5,  53, 42, 26, 38, 22, 41, 25, 37, 21,
};

// Extract a 2-bpp pixel (0=black .. 3=white) from readNextRow's packed output.
inline uint8_t px2(const uint8_t* data, int x) {
  return static_cast<uint8_t>((data[x >> 2] >> (6 - ((x & 3) * 2))) & 0x3);
}
}  // namespace

std::unique_ptr<Activity> WallpapersActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<WallpapersActivity>(renderer, mappedInput);
}

void WallpapersActivity::onEnter() {
  // Timed per phase because "the app takes ten seconds to open" is not a
  // diagnosis, and this fork's recurring failure is fixing the thing in front
  // of you rather than the thing that is slow. One line names the cost.
  const uint32_t tEnter = millis();
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  const uint32_t tFonts = millis();
  Storage.mkdir(wallpapers::kLibraryDir);
  sweepPartFiles();
  const uint32_t tSweep = millis();
  scanLibrary();
  const uint32_t tScan = millis();
  loadSelection();
  const uint32_t tActive = millis();
  // The free-space probe is NOT here any more. HalStorage::freeBytes() walks the
  // FAT cluster chain (SDCardManager::refreshFreeClusters: "seconds on a large
  // card"), it is cached for 20s afterwards, and it was the only thing on this
  // path that could take seconds -- which is exactly the shape Mario saw: ten
  // seconds once, fast on every open inside the TTL, on an EMPTY card where no
  // thumbnail work exists at all. Ten seconds of blank screen before the offer
  // appears is the "reads as crashed" failure one layer earlier than the paint-
  // before-block work, so the screen is painted first and the walk happens
  // after, in loop(), with content already on the glass.
  warning_.clear();
  selectedThisSession_ = false;
  choosing_ = false;
  warningPending_ = true;
  // Live, from the card. load() returning false means no file, which is
  // indistinguishable from a reader that was never paired and is treated as
  // exactly that -- liveState_ is left at its defaults, so every "is it set
  // up?" below answers no without a special case for the missing file.
  live::load(liveState_);
  liveRunning_ = liveOn();
  livePollToken_.clear();
  clearLiveCode();
  liveStatus_.clear();
  // The list is the SERVICE's answer and this app has not asked yet. Starting
  // empty rather than from anything remembered is what stops the screen
  // claiming, for one paint, that a phone revoked while the app was closed can
  // still send.
  liveSenderCount_ = 0;
  liveRevokeIndex_ = -1;
  liveJoining_ = false;
  for (LiveSenderRow& row : liveSenders_) row = LiveSenderRow{};
  refreshLiveLines();
  LOG_INF("WALL", "onEnter %ums: fonts=%u sweep=%u scan=%u active=%u (free-space deferred)", tActive - tEnter,
          tFonts - tEnter, tSweep - tFonts, tScan - tSweep, tActive - tScan);
  // Open on the page holding the set wallpaper, so the border is on screen.
  cachedPage_ = -1;
  page_ = 0;
  // Grid or Offer, decided by what is on the card. An empty grid is never a
  // state this app shows.
  pickView();
  requestUpdate();
}

void WallpapersActivity::scanLibrary() {
  names_.clear();
  auto dir = Storage.open(wallpapers::kLibraryDir);
  if (!dir || !dir.isDirectory()) return;
  auto name = makeUniqueNoThrow<char[]>(kNameMax);
  if (!name) {
    LOG_ERR("WALL", "OOM: wallpaper name buffer");
    return;
  }
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) continue;
    entry.getName(name.get(), kNameMax);
    if (!wallpapers::isSupportedWallpaper(std::string_view{name.get()})) continue;
    names_.emplace_back(name.get());
    if (static_cast<int>(names_.size()) >= kMaxLibrary) break;
  }
  // A user's own wallpapers first, then the built-ins. NOT a plain sort any
  // more, so the built-in count below counts rather than binary_searching -- a
  // binary_search over this order would silently return wrong answers.
  std::sort(names_.begin(), names_.end(),
            [](const std::string& a, const std::string& b) { return wallpapers::sortsBefore(a, b); });

  int have = 0;
  for (const std::string& n : names_) {
    if (wallpapers::isBuiltInFile(n)) ++have;
  }
  builtInsMissing_ = static_cast<int>(wallpapers::builtInCount()) - have;
}

void WallpapersActivity::listShuffleDir(std::vector<std::string>& out) const {
  out.clear();
  auto dir = Storage.open(wallpapers::kShuffleDir);
  if (!dir || !dir.isDirectory()) return;
  auto name = makeUniqueNoThrow<char[]>(kNameMax);
  if (!name) {
    LOG_ERR("WALL", "OOM: shuffle name buffer");
    return;
  }
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) continue;
    entry.getName(name.get(), kNameMax);
    if (!wallpapers::isSupportedWallpaper(std::string_view{name.get()})) continue;
    out.emplace_back(name.get());
    if (static_cast<int>(out.size()) >= kMaxLibrary) break;
  }
}

// What the CARD says is chosen. Not a remembered intention and not a hint file:
// /sleep.bmp and /.sleep are what SleepActivity reads, so they are what the
// picker reports. See docs/apps/wallpapers-shuffle.md.
void WallpapersActivity::loadSelection() {
  activeIndex_ = -1;
  chosen_.clear();
  shadowedSet_ = false;

  // The pin is checked FIRST because renderCustomSleepScreen checks it first:
  // while it is there it is the only thing that shows, whatever else is on the
  // card. Deliberately NOT gated on sleepScreen == CUSTOM (#354) -- what is
  // pinned is a fact about the card, and whether it reaches the glass is a fact
  // about two settings that the hint strip says in words.
  if (Storage.exists(wallpapers::kPinnedSleep)) {
    std::vector<std::string> shadowed;
    listShuffleDir(shadowed);
    // A set behind a pin. This app is not the only way to get here:
    // BmpViewerActivity's "set sleep cover" writes /sleep.bmp straight from the
    // file browser, the File Transfer page can drop one at the root, and a
    // power cut during a one-to-many transition leaves one behind. Reported
    // rather than silently repaired: those are the user's files, and the strip
    // has a sentence for exactly this.
    shadowedSet_ = !shadowed.empty();
    char marker[kNameMax] = {};
    if (Storage.readFileToBuffer(wallpapers::kActiveMarker, marker, sizeof(marker)) == 0) return;
    for (char* p = marker; *p; ++p) {
      if (*p == '\n' || *p == '\r') {
        *p = '\0';
        break;
      }
    }
    for (int i = 0; i < static_cast<int>(names_.size()); ++i) {
      if (wallpapers::sameFileName(names_[static_cast<size_t>(i)], marker)) {
        activeIndex_ = i;
        chosen_.push_back(names_[static_cast<size_t>(i)]);
        break;
      }
    }
    return;
  }

  // No pin: the set IS the directory. Every file counts, including one whose
  // library original has since been deleted -- that copy still takes its turn
  // on the glass, and a count that skipped it would understate what the sleep
  // screen does. Files the user put here themselves are adopted for the same
  // reason: they are what the sleep screen shows.
  listShuffleDir(chosen_);
  if (chosen_.size() == 1) {
    for (int i = 0; i < static_cast<int>(names_.size()); ++i) {
      if (wallpapers::sameFileName(names_[static_cast<size_t>(i)], chosen_[0])) {
        activeIndex_ = i;
        break;
      }
    }
  }
}

bool WallpapersActivity::isChosen(const int index) const {
  if (index < 0 || index >= static_cast<int>(names_.size())) return false;
  const std::string& name = names_[static_cast<size_t>(index)];
  for (const std::string& c : chosen_) {
    if (wallpapers::sameFileName(c, name)) return true;
  }
  return false;
}

wallpapers::Reach WallpapersActivity::sleepReach() const {
  const bool quickResumeOnTimeout =
      SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  return wallpapers::reachOfPinnedSleep(SETTINGS.sleepScreen, quickResumeOnTimeout);
}

bool WallpapersActivity::sleepBlocked() const {
  const wallpapers::Reach reach = sleepReach();
  return reach == wallpapers::Reach::BlockedByMode || reach == wallpapers::Reach::BlockedByQuickResume;
}

const char* WallpapersActivity::currentSleepNote() {
  const wallpapers::Reach reach = sleepReach();
  const int chosen = static_cast<int>(chosen_.size());

  // What a selection changed BEHIND the user's back outranks everything below:
  // it is the only voice that fact has, and stripLineAfterSelection already
  // carries any standing caveat with it so nothing is lost by letting it win
  // (#354 was a caveat suppressed by a line that had no room for both).
  if (selectedThisSession_ && (lastChoice_.tookOverMode || lastChoice_.clearedQuickResume)) {
    return wallpapers::stripLineAfterSelection(lastChoice_, reach).text;
  }

  // Choosing, a real set, or a set hidden behind a pin: the set line owns the
  // strip, and it already orders the caveat and the contradiction ahead of any
  // count it might otherwise print.
  if (choosing_ || chosen >= 2 || shadowedSet_) {
    const wallpapers::ShuffleLine line = wallpapers::shuffleStripLine(choosing_, chosen, shadowedSet_, reach);
    if (line.text == nullptr) return nullptr;
    if (!line.wantsCount) return line.text;
    // The only line that allocates, and it is a member so it outlives the
    // paint. host-tests/wallcaption measures the sentence with the widest count
    // this app can produce in front of it.
    note_ = std::to_string(chosen);
    note_ += ' ';
    note_ += line.text;
    return note_.c_str();
  }

  // One wallpaper, or none. Nothing pinned: the strip already carries "Tap one
  // to set your sleep screen.", and telling someone their non-existent
  // wallpaper is blocked would be noise.
  if (activeIndex_ < 0) return nullptr;
  if (selectedThisSession_) return wallpapers::stripLineAfterSelection(lastChoice_, reach).text;
  return wallpapers::reachHint(reach);
}

void WallpapersActivity::computeWarning() {
  warning_.clear();
  uint64_t free = 0;
  const bool ok = Storage.freeBytes(free);
  // Kept RAW rather than as a verdict: an add applies the same three-outcome
  // precondition at its own floor (kAddFloorBytes), and it must not trigger a
  // second FAT cluster walk to do it -- that walk is what put ten seconds of
  // blank screen on this app's open path, and a tap is the same input path.
  freeKnown_ = ok;
  freeBytes_ = ok ? free : 0;
  switch (wallpapers::roomFor(ok, free, wallpapers::kCardFloorBytes)) {
    case wallpapers::Room::Ok:
      break;
    case wallpapers::Room::TooFull:
      // Short enough to survive the hint strip. The longer form
      // ("... New wallpapers or books may not save.") did not: it overflowed
      // the 446px line in the face the strip resolves and was cut mid-phrase.
      // host-tests/wallcaption measures this string with the rest of them --
      // found by measuring rather than by looking.
      warning_ = "Card is low on space. Saves may fail.";
      break;
    case wallpapers::Room::Unknown:
      warning_ = "Could not check card space.";
      break;
  }
}

int WallpapersActivity::pageCount() const {
  const int per = wallpapersui::gridGeom(toybox::makeTarget(renderer).deviceContext()).perPage;
  if (names_.empty() || per <= 0) return 1;
  // specialTiles(), not a literal 1. drawGrid and the hit-test both use it, and
  // it is 2 whenever any built-in is missing -- so a literal 1 here promised
  // fewer pages than the grid draws and the LAST wallpaper became unreachable
  // at every library size where (1 + N) % perPage == 0. Pre-existing, and this
  // app now has two brand-new routes into that state: deleting a built-in, and
  // a set the user pages through to build.
  //
  // The arithmetic is freestanding so the three readers of "how many tiles are
  // there" can be walked against each other rather than compared by eye.
  return wallpapersui::pageCountFor(specialTiles(), static_cast<int>(names_.size()), per);
}

void WallpapersActivity::clampPage() {
  const int pages = pageCount();
  if (page_ < 0) page_ = 0;
  if (page_ >= pages) page_ = pages - 1;
}

// Copy one wallpaper onto the card, through a .part name renamed only when the
// whole file is written.
//
// Writing straight to the destination truncates whatever was there on the first
// byte, so a card that fills (or a power cut) halfway leaves a SHORT file where
// a good one used to be. That is not a file the sleep screen skips:
// Bitmap::parseHeaders seeks to bfOffBits and never checks that the pixel data
// is complete, and SleepActivity::findNextValidSleepImage accepts a file on
// exactly that check -- so a truncated 480x800 image is a VALID sleep image and
// gets drawn, half-rendered, on every sleep, with nothing on screen to say why.
// The rename is the only thing between a failed copy and a permanently broken
// sleep screen.
bool WallpapersActivity::copyWallpaper(const std::string& src, const std::string& dst) const {
  const std::string part = dst + ".part";
  Storage.remove(part.c_str());

  HalFile in;
  if (!Storage.openFileForRead("WALL", src, in)) {
    LOG_ERR("WALL", "Cannot open wallpaper %s", src.c_str());
    return false;
  }
  HalFile out;
  if (!Storage.openFileForWrite("WALL", part, out)) {
    LOG_ERR("WALL", "Cannot open %s for write", part.c_str());
    return false;
  }
  auto buffer = makeUniqueNoThrow<uint8_t[]>(kCopyChunk);
  if (!buffer) {
    LOG_ERR("WALL", "OOM: copy buffer");
    return false;
  }
  uint64_t copied = 0;
  for (;;) {
    const int got = in.read(buffer.get(), kCopyChunk);
    if (got < 0) {
      LOG_ERR("WALL", "Read error copying wallpaper");
      out.close();
      in.close();
      Storage.remove(part.c_str());
      return false;
    }
    if (got == 0) break;
    if (out.write(buffer.get(), static_cast<size_t>(got)) != static_cast<size_t>(got)) {
      LOG_ERR("WALL", "Write error copying wallpaper (card full?)");
      out.close();
      in.close();
      Storage.remove(part.c_str());
      return false;
    }
    copied += static_cast<uint64_t>(got);
  }
  out.close();
  in.close();

  // The size is knowable and exact, so check it rather than trusting that the
  // writes returned what they claimed.
  if (copied != wallpapers::kWallpaperFileBytes) {
    LOG_ERR("WALL", "Copied wallpaper is %u bytes, expected %u -- not swapping it in", static_cast<unsigned>(copied),
            static_cast<unsigned>(wallpapers::kWallpaperFileBytes));
    Storage.remove(part.c_str());
    return false;
  }

  Storage.remove(dst.c_str());
  if (!Storage.rename(part.c_str(), dst.c_str())) {
    LOG_ERR("WALL", "Card refused the final rename of %s", dst.c_str());
    Storage.remove(part.c_str());
    return false;
  }
  return true;
}

// Where a chosen name can be read from. The library first; a member whose
// library original was deleted is still on the card as its own copy, and that
// copy is what keeps it in the rotation.
std::string WallpapersActivity::sourcePathFor(const std::string& name) const {
  const std::string inLibrary = std::string(wallpapers::kLibraryDir) + "/" + name;
  if (Storage.exists(inLibrary.c_str())) return inLibrary;
  return std::string(wallpapers::kShuffleDir) + "/" + name;
}

void WallpapersActivity::clearShuffleDir() {
  std::vector<std::string> have;
  listShuffleDir(have);
  for (const std::string& n : have) {
    const std::string path = std::string(wallpapers::kShuffleDir) + "/" + n;
    Storage.remove(path.c_str());
    LOG_INF("WALL", "Removed from the set: %s", n.c_str());
  }
}

// Make /.sleep hold exactly `want`. ADDS first, so a copy that fails leaves the
// set exactly as it was rather than as a shorter one nobody chose.
bool WallpapersActivity::fillShuffleDir(const std::vector<std::string>& want) {
  Storage.mkdir(wallpapers::kShuffleDir);
  std::vector<std::string> have;
  listShuffleDir(have);
  const auto holds = [](const std::vector<std::string>& list, const std::string& n) {
    for (const std::string& s : list) {
      if (wallpapers::sameFileName(s, n)) return true;
    }
    return false;
  };
  for (const std::string& n : want) {
    if (holds(have, n)) continue;
    if (!copyWallpaper(sourcePathFor(n), std::string(wallpapers::kShuffleDir) + "/" + n)) return false;
  }
  for (const std::string& n : have) {
    if (holds(want, n)) continue;
    Storage.remove((std::string(wallpapers::kShuffleDir) + "/" + n).c_str());
  }
  return true;
}

// The two settings that decide whether the sleep system ever draws what this
// app wrote (#354). Both slots -- the pin and the set -- are reached through
// renderCustomSleepScreen, so one rule covers them.
//
// Keeping the timeout quick-resume flag ON used to be this app's way of not
// making wake slower, and the price was the whole feature: while that flag is
// on, the IDLE sleep -- the ordinary one -- short-circuits above the
// sleep-screen switch and no wallpaper can appear. The user tapped a picture;
// the picture wins, and the picker says what that cost.
//
// The rule and its consequences are proved in host-tests/wallpapers, which is
// where they can be walked over all eight modes rather than reasoned about.
void WallpapersActivity::applySleepSettings() {
  const bool quickResumeOnTimeout =
      SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  const wallpapers::SleepChoice choice = wallpapers::choiceForSetWallpaper(SETTINGS.sleepScreen, quickResumeOnTimeout);
  // Written only when it actually moves. The set is committed one tap at a
  // time, and an unconditional SETTINGS.saveToFile() would put a JSON write on
  // every one of them for a value that has not changed since the first.
  if (SETTINGS.sleepScreen != choice.sleepScreenMode || quickResumeOnTimeout != choice.quickResumeAfterTimeout) {
    SETTINGS.sleepScreen = choice.sleepScreenMode;
    SETTINGS.quickResumeSleepScreen = choice.quickResumeAfterTimeout
                                          ? CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT
                                          : CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;
    SETTINGS.saveToFile();
    LOG_INF("WALL", "sleep settings changed by a selection: mode %s -> %s, quick resume on timeout %d -> %d",
            wallpapers::sleepScreenModeName(choice.previousMode),
            wallpapers::sleepScreenModeName(choice.sleepScreenMode), quickResumeOnTimeout ? 1 : 0,
            choice.quickResumeAfterTimeout ? 1 : 0);
  }
  lastChoice_ = choice;
  selectedThisSession_ = true;
}

// Put the card into the ONE shape cardShapeFor() names for this many
// wallpapers. The only writer of /sleep.bmp, /.sleep and /wallpapers/.active,
// so "the pin and the set are never both live" has one place to hold.
//
// The order inside each branch is chosen so that a power cut mid-commit leaves
// a picture the user CHOSE on the glass rather than nothing: the slot that wins
// is written before the slot that loses is cleared. That can leave both
// populated for an instant, which is the same state BmpViewerActivity can
// create at any time, and loadSelection() reports it in words either way.
bool WallpapersActivity::commitSelection(const std::vector<std::string>& want) {
  const wallpapers::CardShape shape = wallpapers::cardShapeFor(static_cast<int>(want.size()));

  if (shape.pinned) {
    if (!copyWallpaper(sourcePathFor(want[0]), wallpapers::kPinnedSleep)) return false;
    clearShuffleDir();
    HalFile marker;
    if (Storage.openFileForWrite("WALL", wallpapers::kActiveMarker, marker)) {
      marker.write(reinterpret_cast<const uint8_t*>(want[0].c_str()), want[0].size());
      marker.close();
    }
  } else if (shape.shuffled) {
    if (!fillShuffleDir(want)) return false;
    Storage.remove(wallpapers::kPinnedSleep);
    // The hint has nothing left to name, and a stale name beside a set is the
    // second source that can disagree with the directory. Deleting it is
    // cheaper than reconciling it.
    Storage.remove(wallpapers::kActiveMarker);
  } else {
    Storage.remove(wallpapers::kPinnedSleep);
    clearShuffleDir();
    Storage.remove(wallpapers::kActiveMarker);
  }

  // Live and a chosen wallpaper are mutually exclusive, and this is where that
  // stops being a sentence in the design doc.
  //
  // Both write /sleep.bmp. Left on, Live would overwrite the picture the user
  // just tapped at its next wake -- a picker that marks a wallpaper the device
  // then does not show, which is card #354 exactly, arriving a few hours late
  // and therefore looking like nothing the picker did. So choosing a wallpaper
  // turns Live OFF, and the strip says so.
  //
  // The TOKEN is kept. Turning Live off is not disconnecting the phone, and
  // making the owner re-pair down a telephone because they liked a wallpaper
  // for an afternoon would be a punishment for using the app.
  if (!want.empty() && liveState_.on) {
    liveState_.on = false;
    liveRunning_ = false;
    liveStatus_.clear();
    live::save(liveState_);
    // AND SAY SO, once, the way the Live screen's own toggle does.
    //
    // This half was missing and the gap was visible to the one person the
    // feature is for. The device half is real -- the card says off, and
    // live::decide returns no timer at all for a reader that is off, so it
    // genuinely stops waking. But the SERVICE learns what a reader is doing
    // only when the reader speaks, and a reader that has stopped waking never
    // speaks again. So the website counted down to a check that would not
    // happen and then reported a reader it had not heard from -- which is what
    // a flat battery and a router that moved also look like. It has an honest
    // state for this ("Live is off on the reader. Your drawing is saved and
    // appears the moment Live is switched back on.") and nothing on this path
    // could reach it.
    //
    // QUEUED, like every other radio step here: the card is already written, so
    // nothing depends on the call getting out, and a failure costs a courtesy
    // rather than a fact. A check queued behind it would tell the service "on"
    // last and undo this, so it goes.
    if (liveState_.paired()) {
      liveCheckQueued_ = false;
      liveOffQueued_ = true;
    }
    LOG_INF("WALL", "a wallpaper was chosen, so Live is off; its pairing is kept");
  }

  // Nothing chosen leaves the sleep mode alone. It is still CUSTOM with no file,
  // which falls through to the user's own /sleep and then to the default screen
  // -- and reverting a mode the user may have set deliberately, because they
  // unchecked their last wallpaper, would be the app deciding more than it was
  // asked to.
  if (!want.empty()) applySleepSettings();
  loadSelection();
  LOG_INF("WALL", "selection committed: %d chosen (pin=%d set=%d)", static_cast<int>(want.size()), shape.pinned ? 1 : 0,
          shape.files);
  return true;
}

bool WallpapersActivity::setWallpaper(const int index) {
  if (index < 0 || index >= static_cast<int>(names_.size())) return false;
  return commitSelection({names_[static_cast<size_t>(index)]});
}

// A tap on a tile while choosing: add or remove one wallpaper, committed to the
// card before the function returns. There is nothing held in RAM to lose, and
// no cancel to get wrong -- DONE and Back therefore mean the same thing.
void WallpapersActivity::toggleChosen(const int index) {
  if (index < 0 || index >= static_cast<int>(names_.size())) return;
  const std::string& name = names_[static_cast<size_t>(index)];

  std::vector<std::string> want;
  want.reserve(chosen_.size() + 1);
  bool removing = false;
  for (const std::string& c : chosen_) {
    if (wallpapers::sameFileName(c, name)) {
      removing = true;
      continue;
    }
    want.push_back(c);
  }
  if (!removing) {
    // The three-outcome precondition, at the floor for the ONE file this tap
    // writes. Read from the LAST walk rather than a fresh one: freeBytes()
    // walks the FAT cluster chain, and this is the input path.
    //
    // Unknown proceeds here rather than refusing, unlike the download's
    // precondition. The doctrine's premise is that proceeding on Unknown costs
    // the user whose card is already in trouble -- and this write is
    // transactional (.part, size-checked, renamed), so a card that cannot take
    // it loses nothing and says so. Refusing on a card whose free-space walk
    // merely failed would make the picker unusable on it.
    if (freeKnown_ && wallpapers::roomFor(true, freeBytes_, wallpapers::kAddFloorBytes) == wallpapers::Room::TooFull) {
      showNotice("NOT ENOUGH ROOM",
                 "Your set was not changed. Free some space on the card and try again -- each wallpaper is about "
                 "48 KB.",
                 "OK", wallpapersui::ActionDismiss);
      return;
    }
    want.push_back(name);
  }

  if (!commitSelection(want)) {
    warningPending_ = true;
    showNotice("COULD NOT CHANGE THE SET",
               "Your sleep screen is unchanged. The card may be full, or the file may be damaged.", "OK",
               wallpapersui::ActionDismiss);
    return;
  }
  // The free number moved. Re-armed rather than re-walked: loop() runs it after
  // the next paint is on the glass.
  warningPending_ = true;
  requestUpdate();
}

WallpapersActivity::Thumb WallpapersActivity::decodeThumb(const std::string& path, int16_t cellW, int16_t cellH) const {
  Thumb t;
  HalFile file;
  if (!Storage.openFileForRead("WALL", path, file)) return t;
  Bitmap bmp(file);
  if (bmp.parseHeaders() != BmpReaderError::Ok) return t;

  const int sw = bmp.getWidth();
  const int sh = bmp.getHeight();
  if (sw <= 0 || sh <= 0) return t;
  const bool topDown = bmp.isTopDown();

  // Fit the wallpaper's aspect inside the cell (contain), centred.
  const float scale = std::min(static_cast<float>(cellW) / sw, static_cast<float>(cellH) / sh);
  int dw = std::max(1, static_cast<int>(sw * scale));
  int dh = std::max(1, static_cast<int>(sh * scale));
  if (dw > cellW) dw = cellW;
  if (dh > cellH) dh = cellH;
  t.w = static_cast<int16_t>(dw);
  t.h = static_cast<int16_t>(dh);
  t.ox = static_cast<int16_t>((cellW - dw) / 2);
  t.oy = static_cast<int16_t>((cellH - dh) / 2);

  const int bytesPerRow = (dw + 7) / 8;
  t.bits.assign(static_cast<size_t>(bytesPerRow) * dh, 0);

  auto data = makeUniqueNoThrow<uint8_t[]>((sw + 3) / 4);
  auto rowBuffer = makeUniqueNoThrow<uint8_t[]>(bmp.getRowBytes());
  std::vector<uint16_t> acc(static_cast<size_t>(dw), 0);
  std::vector<uint16_t> cnt(static_cast<size_t>(dw), 0);
  if (!data || !rowBuffer) return t;

  auto flush = [&](int dy) {
    if (dy < 0 || dy >= dh) return;
    uint8_t* outRow = t.bits.data() + static_cast<size_t>(bytesPerRow) * dy;
    for (int dx = 0; dx < dw; ++dx) {
      if (cnt[dx] == 0) continue;
      // How much of the source box was ink, 0..255, then dithered against the
      // ordered matrix so a half-dark box becomes texture rather than a blob.
      const int ink = static_cast<int>(acc[dx]) * 255 / static_cast<int>(cnt[dx]);
      const int threshold = (static_cast<int>(kBayer8[(dy & 7) * 8 + (dx & 7)]) * 255) / 64;
      if (ink > threshold) outRow[dx >> 3] |= static_cast<uint8_t>(0x80 >> (dx & 7));
    }
    std::fill(acc.begin(), acc.end(), 0);
    std::fill(cnt.begin(), cnt.end(), 0);
  };

  int curDy = -1;
  for (int sy = 0; sy < sh; ++sy) {
    if (bmp.readNextRow(data.get(), rowBuffer.get()) != BmpReaderError::Ok) break;
    const int srcRow = topDown ? sy : (sh - 1 - sy);
    int dy = srcRow * dh / sh;
    if (dy >= dh) dy = dh - 1;
    if (dy != curDy) {
      if (curDy >= 0) flush(curDy);
      curDy = dy;
    }
    for (int sx = 0; sx < sw; ++sx) {
      const int dx = sx * dw / sw;
      if (dx >= dw) continue;
      // px2: 0=black .. 3=white; treat the darker half as ink.
      if (px2(data.get(), sx) <= 1) acc[static_cast<size_t>(dx)]++;
      cnt[static_cast<size_t>(dx)]++;
    }
  }
  if (curDy >= 0) flush(curDy);
  t.ok = true;
  return t;
}

void WallpapersActivity::ensureThumbsForPage() {
  const wallpapersui::GridGeom geom = wallpapersui::gridGeom(toybox::makeTarget(renderer).deviceContext());
  if (cachedPage_ == page_ && cachedPerPage_ == geom.perPage && !thumbs_.empty()) return;

  const uint32_t tThumbs = millis();
  int decoded = 0;
  thumbs_.assign(static_cast<size_t>(geom.perPage), Thumb{});
  const int base = page_ * geom.perPage;
  // specialTiles(), not a literal 1: the decode and the draw must agree about
  // which wallpaper is in a cell, and when they did not, one tile drew its
  // neighbour's picture and the next drew a decode-failure cross.
  const int specials = specialTiles();
  for (int slot = 0; slot < geom.perPage; ++slot) {
    const int combined = base + slot;
    if (combined < specials) continue;  // the chrome tiles have no thumbnail
    const int idx = combined - specials;
    if (idx >= static_cast<int>(names_.size())) break;
    std::string path = std::string(wallpapers::kLibraryDir) + "/" + names_[static_cast<size_t>(idx)];
    thumbs_[static_cast<size_t>(slot)] =
        thumbFor(names_[static_cast<size_t>(idx)], path, geom.cellW, geom.cellH, &decoded);
  }
  LOG_INF("WALL", "thumbs page=%d decoded=%d (rest from cache) in %ums", page_, decoded, millis() - tThumbs);
  cachedPage_ = page_;
  cachedPerPage_ = geom.perPage;
}

// Cache hit or a decode plus a write. `decoded` counts only the real decodes so
// the log says which of the two happened.
WallpapersActivity::Thumb WallpapersActivity::thumbFor(const std::string& name, const std::string& path,
                                                       const int16_t cellW, const int16_t cellH, int* decoded) {
  uint64_t sourceBytes = 0;
  uint32_t sample = 0;
  {
    HalFile src;
    if (Storage.openFileForRead("WALL", path, src)) {
      sourceBytes = static_cast<uint64_t>(src.size());
      sample = sampleHashOf(src, sourceBytes);
      src.close();
    }
  }

  const std::string cachePath = std::string(kThumbDir) + "/" + name + ".thb";
  HalFile cf;
  if (Storage.openFileForRead("WALL", cachePath, cf)) {
    ThumbHeader head{};
    if (cf.read(&head, sizeof(head)) == static_cast<int>(sizeof(head)) && head.magic == kThumbMagic &&
        head.sourceBytes == sourceBytes && head.sampleHash == sample && head.cellW == cellW && head.cellH == cellH &&
        head.w > 0 && head.h > 0) {
      Thumb t;
      t.w = head.w;
      t.h = head.h;
      t.ox = head.ox;
      t.oy = head.oy;
      const size_t bytes = static_cast<size_t>((head.w + 7) / 8) * static_cast<size_t>(head.h);
      t.bits.resize(bytes);
      if (cf.read(t.bits.data(), bytes) == static_cast<int>(bytes)) {
        t.ok = true;
        cf.close();
        return t;
      }
    }
    cf.close();
  }

  if (decoded != nullptr) ++(*decoded);
  Thumb t = decodeThumb(path, cellW, cellH);
  if (!t.ok) return t;

  // Best effort: a cache that cannot be written must never break the picture.
  if (!Storage.exists(kThumbDir)) Storage.mkdir(kThumbDir);
  const std::string part = cachePath + ".part";
  HalFile out;
  if (Storage.openFileForWrite("WALL", part, out)) {
    ThumbHeader head{};
    head.magic = kThumbMagic;
    head.sourceBytes = static_cast<uint32_t>(sourceBytes);
    head.sampleHash = sample;
    head.cellW = cellW;
    head.cellH = cellH;
    head.w = t.w;
    head.h = t.h;
    head.ox = t.ox;
    head.oy = t.oy;
    const bool wrote =
        out.write(&head, sizeof(head)) == sizeof(head) && out.write(t.bits.data(), t.bits.size()) == t.bits.size();
    out.close();
    if (wrote) {
      Storage.remove(cachePath.c_str());
      if (!Storage.rename(part.c_str(), cachePath.c_str())) Storage.remove(part.c_str());
    } else {
      Storage.remove(part.c_str());
    }
  }
  return t;
}

// Build every thumbnail now, while the user is already watching a progress bar
// and expects to wait. Without this the cost merely MOVES to the first open,
// which is the screen that has to feel instant. Cancelling is honoured: a
// half-warmed cache is not wrong, only less warm, and the rest fills in lazily.
void WallpapersActivity::prewarmThumbs() {
  const wallpapersui::GridGeom geom = wallpapersui::gridGeom(toybox::makeTarget(renderer).deviceContext());
  fetchPhase_ = 2;
  fetchDone_ = 0;
  fetchTotal_ = static_cast<int>(names_.size());
  const uint32_t t0 = millis();
  int built = 0;
  for (size_t i = 0; i < names_.size(); ++i) {
    if (fetchCancel_) break;
    const std::string path = std::string(wallpapers::kLibraryDir) + "/" + names_[i];
    thumbFor(names_[i], path, geom.cellW, geom.cellH, &built);
    fetchDone_ = static_cast<int>(i) + 1;
    if ((i % 5) == 0 || i + 1 == names_.size()) {
      mappedInput.update();
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) fetchCancel_ = true;
      requestUpdateAndWait();
    }
  }
  LOG_INF("WALL", "prewarmed %d thumbnails in %ums", built, millis() - t0);
  // The grid's own cache is per-page and still cold; let it re-read from disk.
  cachedPage_ = -1;
}

// A set bit is ink, placed at the offset the aspect fit worked out. A thumbnail
// that would not decode gets the cross rather than a blank box: an empty frame
// where a picture belongs reads as a crash, which cold testers have twice
// reported on this fork.
void WallpapersActivity::drawThumbInto(const Thumb& t, const fui::Rect& box) const {
  if (!t.ok) {
    renderer.drawLine(box.x, box.y, box.right() - 1, box.bottom() - 1, true);
    renderer.drawLine(box.x, box.bottom() - 1, box.right() - 1, box.y, true);
    return;
  }
  const int bytesPerRow = (t.w + 7) / 8;
  for (int y = 0; y < t.h; ++y) {
    const uint8_t* row = t.bits.data() + static_cast<size_t>(bytesPerRow) * y;
    for (int x = 0; x < t.w; ++x) {
      if (row[x >> 3] & (0x80 >> (x & 7))) renderer.drawPixel(box.x + t.ox + x, box.y + t.oy + y, true);
    }
  }
}

void WallpapersActivity::drawGrid(const wallpapersui::GridGeom& geom) {
  const int base = page_ * geom.perPage;
  const int specials = specialTiles();
  const int total = specials + static_cast<int>(names_.size());

  for (int slot = 0; slot < geom.perPage; ++slot) {
    const int combined = base + slot;
    if (combined >= total) break;
    const fui::Rect th = wallpapersui::thumbRect(geom, slot);
    switch (specialAt(combined)) {
      case SpecialTile::Live:
        // One tile does both jobs, so it takes cell 0 and + Add's destination
        // rather than sitting beside it.
        drawLiveTile(geom, th, slot);
        continue;
      case SpecialTile::GetSet:
        // PARTIAL: the user has their own wallpapers but not the built-in set,
        // so the offer stays on screen as a tile rather than vanishing because
        // one wallpaper exists. It retires itself when the set is complete.
        drawGetSetTile(geom, th, slot);
        continue;
      case SpecialTile::None:
        break;
    }
    const int idx = combined - specials;
    const Thumb& t = thumbs_[static_cast<size_t>(slot)];
    drawThumbInto(t, th);

    // A hairline on every cell so a mostly-white wallpaper still reads as a
    // framed tile. This is not the selection signal: it is on every cell.
    renderer.drawRect(th.x, th.y, th.width, th.height, 1, true);
    if (isChosen(idx)) drawMarker(th);

    // Caption (variants with one): the file name, fitted so it never truncates
    // into a missing glyph.
    if (geom.captionH > 0) {
      const fui::Rect cap = wallpapersui::captionRect(geom, slot);
      fui::GfxRendererTarget target = toybox::makeTarget(renderer);
      fui::TextStyle style = toybox::themeTokens().smallText;
      // smallText maps to the BODY cut; the caption wants the actual small cut
      // (toybox_10) so it fits the caption row instead of towering over it.
      style.font = fui::FONT_SLOT_SMALL;
      style.align = fui::TextAlign::Center;
      style.color = fui::Color::Black;
      // A real name, not the file name: a picker showing "blake-door.bmp" cut
      // mid-word looks unfinished. When the long form does not fit the cell we
      // fall back to a shorter NAME rather than an ellipsis, and log which was
      // used so the fit is measured rather than eyeballed.
      const wallpapers::DisplayName name = wallpapers::displayName(names_[static_cast<size_t>(idx)]);
      std::string fitted = toybox::fitLines(target, name.full.c_str(), cap.width, 1, style);
      if (fitted != name.full) {
        fitted = toybox::fitLines(target, name.brief.c_str(), cap.width, 1, style);
        LOG_DBG("WALL", "caption fell back to brief: %s -> %s", name.full.c_str(), fitted.c_str());
      }
      if (fitted != name.full && fitted != name.brief) {
        LOG_ERR("WALL", "caption STILL cut: %s", fitted.c_str());
      }
      target.text(cap, fitted.c_str(), style);
    }
  }

  // Page dots, when the library spans more than one page.
  const int pages = pageCount();
  if (pages > 1) {
    const int16_t totalW = static_cast<int16_t>(pages * kDotSize + (pages - 1) * kDotGap);
    const fui::Rect panel = toybox::makeTarget(renderer).deviceContext().screen();
    int16_t x = static_cast<int16_t>((panel.width - totalW) / 2);
    for (int p = 0; p < pages; ++p) {
      if (p == page_) {
        renderer.fillRect(x, geom.pageDotsY, kDotSize, kDotSize, true);
      } else {
        renderer.drawRect(x, geom.pageDotsY, kDotSize, kDotSize, 1, true);
      }
      x = static_cast<int16_t>(x + kDotSize + kDotGap);
    }
  }
}

// How many chrome tiles sit in front of the wallpapers. The Live tile always,
// and one more while the built-in set is incomplete. Read by the drawing, the
// hit-test, the thumbnail decode and the page count, because a grid whose
// readers disagree about what is in a cell opens the wrong thing -- the bug
// this fork has caught more often than any other.
int WallpapersActivity::specialTiles() const {
  int n = 1;  // the Live tile, set up or not
  if (builtInsMissing_ > 0) n += 1;
  return n;
}

// The same ordering, resolved rather than counted. Walked in place instead of
// compared against literals so the two answers cannot drift: adding a tile
// above changes both.
WallpapersActivity::SpecialTile WallpapersActivity::specialAt(const int combined) const {
  if (combined < 0 || combined >= specialTiles()) return SpecialTile::None;
  int at = 0;
  // Cell 0, which is where + Add used to be. The Live tile inherited the cell
  // rather than taking one beside it, because a grid with both would have put a
  // second control where people had already learned one
  // (same-pixel-different-action).
  if (combined == at++) return SpecialTile::Live;
  return SpecialTile::GetSet;
}

// Read from the card, not from a build flag. WALLPAPERS_LIVE_CONFIGURED and
// WALLPAPERS_LIVE_ON survive only as the SCREENSHOT harness's way of forcing
// either half of the Live screen without a service to pair against: a
// non-default value overrides the store, and the default (0) means "ask the
// card", which is what every real device does.
bool WallpapersActivity::liveConfigured() const {
#if WALLPAPERS_LIVE_CONFIGURED != 0
  return true;
#else
  return liveState_.paired();
#endif
}

// Live and a chosen wallpaper are mutually exclusive, so "on" implies "set up"
// and the tile takes the ordinary selection marker rather than a second mark
// beside it.
bool WallpapersActivity::liveOn() const {
#if WALLPAPERS_LIVE_ON != 0
  return liveConfigured();
#else
  return liveConfigured() && liveState_.on;
#endif
}

// The Live slot. The frame is DOUBLE -- a 3px outer rect and a hairline inset
// kLiveFrameInset inside it -- because a wallpaper wears one hairline and the
// other chrome tiles a single thick rect, so two concentric rules are the only
// edge on this grid that cannot be mistaken for a plate's own border.
void WallpapersActivity::drawLiveTile(const wallpapersui::GridGeom& geom, const fui::Rect& th, const int slot) const {
  renderer.drawRect(th.x, th.y, th.width, th.height, kLiveFrameWeight, true);
  renderer.drawRect(static_cast<int16_t>(th.x + kLiveFrameInset), static_cast<int16_t>(th.y + kLiveFrameInset),
                    static_cast<int16_t>(th.width - kLiveFrameInset * 2),
                    static_cast<int16_t>(th.height - kLiveFrameInset * 2), 1, true);

  const int cx = th.x + th.width / 2;
  const int cy = th.y + th.height / 2;
  if (!liveConfigured()) {
    // The combined tile keeps + Add's plus while there is nothing to show: it
    // is still the control that puts the first thing here, and the affordance
    // is one people have already learned on this grid.
    const int len = th.width * 2 / 5;
    const int wgt = std::max(6, th.width / 12);
    renderer.fillRect(cx - len / 2, cy - wgt / 2, len, wgt, true);
    renderer.fillRect(cx - wgt / 2, cy - len / 2, wgt, len, true);
  } else {
    // Three stacked bars standing in for the last message: FILLED once there is
    // one, OUTLINED while there is not. The outline is the empty state, and it
    // is a shape waiting to be filled rather than an absence -- a bare tile
    // inside a heavy frame reads as a wallpaper that failed to decode, which is
    // what the diagonal cross already means two cells away.
    const int barW = th.width * 3 / 5;
    const int barH = std::max(8, th.width / 12);
    const int top = cy - (barH * 3 + barH * 2) / 2;
    static constexpr int kRunTenths[3] = {10, 8, 6};  // ragged right, so the block reads as prose
    for (int i = 0; i < 3; ++i) {
      const int w = barW * kRunTenths[i] / 10;
      const int y = top + i * barH * 2;
      if (liveConfigured()) {
        renderer.fillRect(cx - barW / 2, y, w, barH, true);
      } else {
        renderer.drawRect(cx - barW / 2, y, w, barH, 1, true);
      }
    }
  }

  // liveRunning_, not liveOn(): the Live screen's toggle is what the user just
  // pressed, and a tile that kept reporting the compile-time stub would say the
  // opposite of the screen they came back from. One bool read by both.
  if (liveRunning_) drawMarker(th);

  // The caption through captionRect, like every other tile on the grid. Hand
  // placing it would put this one a few pixels off the row its neighbours sit
  // on, and the marker's clearance is in that arithmetic too.
  const fui::Rect cap = wallpapersui::captionRect(geom, slot);
  if (cap.width <= 0) return;
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  fui::TextStyle style = toybox::themeTokens().smallText;
  style.font = fui::FONT_SLOT_SMALL;
  style.align = fui::TextAlign::Center;
  style.color = fui::Color::Black;
  style.maxLines = 1;
  target.text(cap, wallpapersui::liveTileCaption(), style);
}

void WallpapersActivity::drawGetSetTile(const wallpapersui::GridGeom& geom, const fui::Rect& th, const int slot) const {
  renderer.drawRect(th.x, th.y, th.width, th.height, 3, true);
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  fui::TextStyle style = toybox::themeTokens().smallText;
  style.font = fui::FONT_SLOT_SMALL;
  style.align = fui::TextAlign::Center;
  style.color = fui::Color::Black;
  style.maxLines = 2;

  char label[48];
  // One line of text, wrapped by width. An embedded newline is not a break
  // this renderer honours: it vanished and joined the words into "GET THE21".
  //
  // No plural: fmtwidth cannot bound a "%s" that switches word, and the count
  // beside a fixed noun says the same thing (the trivia and add-screen
  // precedent). The one-missing case was unreachable until the hold sheet could
  // delete a built-in, and it read "GET THE 1 BUILT-INS".
  std::snprintf(label, sizeof(label), "GET %d MISSING", builtInsMissing_);
  const fui::Rect box =
      fui::makeRect(th.x + 6, static_cast<int16_t>(th.y + th.height / 2 - 30), static_cast<int16_t>(th.width - 12), 60);
  target.text(box, label, style);

  // The slot, not a literal 1: this tile sits after the Live tile, and its
  // caption has to follow it along the row rather than pin itself to a cell.
  const fui::Rect cap = wallpapersui::captionRect(geom, slot);
  fui::TextStyle capStyle = style;
  capStyle.maxLines = 1;
  target.text(cap, "Tap to fetch", capStyle);
}

void WallpapersActivity::drawMarker(const fui::Rect& th) const {
  // Four corner brackets in the cell's padding. The rectangles come from
  // wallpapersui::markerRects so the shape the panel draws is the same shape
  // host-tests/wallcaption proves clear of the artwork and of every caption:
  // a marker whose geometry lived only here could drift from its own proof.
  // Nothing inside a picture looks like this, which is why the mark cannot be
  // mistaken for the artwork's own frame -- several plates carry real borders.
  const wallpapersui::MarkerRects m = wallpapersui::markerRects(th);
  for (const fui::Rect& r : m.r) renderer.fillRect(r.x, r.y, r.width, r.height, true);
}

// The whole set as one asset. Twenty-one separate downloads would be 42 TLS
// handshakes, because GitHub redirects release assets to a CDN host and
// HttpDownloader opens a fresh connection per hop with no session reuse: about
// a minute of dead time against roughly ten seconds for one file. Per-file
// "resume" would not have paid for it either, since downloadToFile has no Range
// support and deletes its destination before the first byte arrives.
//
// The pack needs no format: every wallpaper is exactly kWallpaperFileBytes, so
// it is a bare concatenation, image i lives at i * kWallpaperFileBytes, and the
// count is the file size divided by it. Built by tools_local/wallpapers/build_pack.py
// in the same order as the built-in table, which host-tests/wallpack asserts.
// The name is devicehost::mdnsName(), shared with the two activities that
// advertise it, so this screen cannot print a name the device does not answer
// to. It used to be a third copy of the literal "crossplay" here.
//
// Sharing the NAME is not sharing the LIFECYCLE: restartMdns lives in an
// anonymous namespace in CrossPointWebServerActivity.cpp and the web-server
// activity runs MDNS.end() in its own onExit, so mDNS is definitively not
// running when this screen opens. PR 1 calls MDNS.begin itself.

// Version 4, ECC_LOW, BYTE mode. Not the 114 QrUtils believes (that is the
// alphanumeric figure, and every URL with a lowercase letter is byte mode).
constexpr size_t kQrByteSafeLen = 78;

constexpr const char* kPackUrl = "https://github.com/ma-r-s/crossplay/releases/download/wallpapers/wallpapers.dat";
constexpr const char* kPackPart = "/wallpapers.dat.part";

// The HAL exposes a size only through an open file, and the difference between
// "absent" and "there but the wrong length" is the whole resume rule, so it is
// worth the open. A short file is a torn write, and Bitmap would accept it.

uint64_t fileSizeOf(const std::string& path) {
  HalFile f;
  if (!Storage.openFileForRead("WALL", path, f)) return 0;
  const uint64_t n = static_cast<uint64_t>(f.size());
  f.close();
  return n;
}

// A power cut mid-unpack leaves <name>.bmp.part behind. They are unambiguously
// ours and unambiguously incomplete, so they go on entry. Nothing else is
// touched: a .bmp of an unexpected length might be a wallpaper the user made
// elsewhere, and deleting a user's file to tidy up is not this app's call.
// Incomplete copies left by a power cut. Swept from EVERY place this app writes
// one, which is three: the library (the download's unpack), the set directory,
// and the card root beside the pinned file. A .part is skipped by
// findNextValidSleepImage on its extension, so one is inert rather than
// dangerous -- but it is 48KB of a card whose free space this app warns about.
void WallpapersActivity::sweepPartFiles() {
  const std::string rootPart = std::string(wallpapers::kPinnedSleep) + ".part";
  if (Storage.exists(rootPart.c_str())) {
    Storage.remove(rootPart.c_str());
    LOG_INF("WALL", "Swept incomplete %s", rootPart.c_str());
  }
  sweepPartFilesIn(wallpapers::kShuffleDir);
  sweepPartFilesIn(wallpapers::kLibraryDir);
}

void WallpapersActivity::sweepPartFilesIn(const char* dirPath) {
  auto dir = Storage.open(dirPath);
  if (!dir) return;
  std::vector<std::string> stale;
  for (;;) {
    auto entry = dir.openNextFile();
    if (!entry) break;
    char nameBuf[kNameMax] = {};
    entry.getName(nameBuf, sizeof(nameBuf));
    entry.close();
    const std::string name(nameBuf);
    if (name.size() > 5 && name.compare(name.size() - 5, 5, ".part") == 0) stale.push_back(name);
  }
  dir.close();
  for (const std::string& name : stale) {
    const std::string path = std::string(dirPath) + "/" + name;
    Storage.remove(path.c_str());
    LOG_INF("WALL", "Swept incomplete %s", path.c_str());
  }
}

void WallpapersActivity::showNotice(const char* headline, const char* body, const char* actionLabel,
                                    const fui::ActionId action) {
  noticeHead_ = headline;
  noticeBody_ = body;
  noticeAction_ = actionLabel;
  noticeActionId_ = action;
  view_ = View::Notice;
  interactionsReady_ = false;
  requestUpdate();
}

int WallpapersActivity::builtInsPresent() const {
  return static_cast<int>(wallpapers::builtInCount()) - builtInsMissing_;
}

// The screen is a function of the card, with no remembered "already offered"
// flag to go stale or to make two identical cards show different things.
void WallpapersActivity::pickView() { view_ = names_.empty() ? View::Offer : View::Grid; }

// A hold landed on a wallpaper. Everything the three screens behind this need is
// SNAPSHOT here, in loop(), while the index is still the one the finger meant:
// render() must not go looking things up in names_, and the delete must not
// trust an index that uploading, deleting or re-sorting can renumber underneath
// it. The file name is the identity that survives all three.
void WallpapersActivity::openSheet(const int index) {
  if (index < 0 || index >= static_cast<int>(names_.size())) return;
  sheetIndex_ = index;
  sheetFile_ = names_[static_cast<size_t>(index)];
  sheetName_ = wallpapers::displayName(sheetFile_).full;
  // Both screens behind this SAY something about the sleep screen -- the sheet
  // "This one is on your sleep screen now.", the confirm "It stays on your
  // sleep screen until you pick another." -- so the flag they read has to mean
  // the wallpaper actually REACHES the glass, not merely that it wears the
  // grid's marker.
  //
  // Those used to be the same thing. #354 deliberately un-gated loadSelection()
  // from sleepScreen == CUSTOM, because the marker was wrong in both
  // directions; activeIndex_ is now set under DARK, LIGHT, BLANK and
  // quick-resume too, where the pinned picture never appears. The marker is
  // qualified for the grid by the hint strip, which these two screens do not
  // carry, so they qualify it here instead. Neither card had this alone: it is
  // this branch's sentences meeting that card's wider activeIndex_.
  // isChosen(), not index == activeIndex_. #305 made the selection a SET, and a
  // member of a live set genuinely is on the sleep screen -- it just takes its
  // turn. Asking for the pinned index would under-claim for every member of
  // every set, on the screen that is about to delete one. isChosen answers
  // correctly in all three shapes: the pin (only that one), a set (all of its
  // members), and a set hidden behind a stray pin (only the pin, which is the
  // one actually showing).
  sheetIsActive_ = wallpapers::saysOnSleepScreen(isChosen(index), sleepReach());
  sheetDetail_ = wallpapers::deleteConsequence(wallpapers::isBuiltInFile(sheetFile_), sheetIsActive_);
  view_ = View::Sheet;
  interactionsReady_ = false;
  LOG_INF("WALL", "hold sheet for %s (index %d, active %d, chosen %d, blocked %d, shadowed %d)", sheetFile_.c_str(),
          index, activeIndex_, static_cast<int>(chosen_.size()), static_cast<int>(sleepBlocked()),
          static_cast<int>(shadowedSet_));
  requestUpdate();
}

// The one destructive thing this app does.
//
// Resolves the NAME rather than reusing sheetIndex_. An index is a position in
// a sorted list, and this app re-sorts: scanLibrary() runs on every onEnter and
// after every delete, and deleting a built-in also flips specialTiles() 1 -> 2
// so every cell renumbers. A name that no longer resolves means the file is
// already gone, and doing nothing is right -- deleting "whatever is at slot 4
// now" is exactly the bug this app is shaped to avoid.
//
// Today no upload can land WHILE the sheet is up: addServer_ runs only in
// View::Add and stopAddServer() is on the way out of it. That is a fact about
// the current wiring, not a property, which is why this resolves the name
// anyway rather than resting on it.
//
// /sleep.bmp is NOT touched. It is a self-contained copy, so a deleted
// wallpaper stays on the sleep screen until another is chosen; the confirm says
// so (wallpapers::deleteConsequence), because a user who believed otherwise
// would delete a second time looking for an effect that never comes.
bool WallpapersActivity::deleteWallpaper() {
  if (sheetFile_.empty()) return false;
  const auto found = std::find(names_.begin(), names_.end(), sheetFile_);
  if (found == names_.end()) {
    LOG_INF("WALL", "delete: %s is no longer in the library; nothing to do", sheetFile_.c_str());
    return false;
  }
  const std::string path = std::string(wallpapers::kLibraryDir) + "/" + sheetFile_;
  if (!Storage.remove(path.c_str())) {
    LOG_ERR("WALL", "Card refused to delete %s", path.c_str());
    return false;
  }
  // The thumbnail cache is keyed on the file NAME, so a later upload reusing the
  // name would otherwise be drawn from the deleted picture's cache entry until
  // something else invalidated it. thumbFor's staleness check is the source's
  // byte count plus a three-read content sample plus the cell size -- not the
  // mtime -- so a same-named replacement is caught by its CONTENT and this
  // removal is belt to that brace rather than the only guard. Removing it costs
  // one decode; leaving it costs the wrong picture.
  const std::string cache = std::string(kThumbDir) + "/" + sheetFile_ + ".thb";
  Storage.remove(cache.c_str());

  // The pin marker names a file that is gone. loadSelection() already fails to
  // match it, so the grid is right today -- but a later upload with the SAME
  // name would match it again and wear the "in use" border while /sleep.bmp
  // still holds the deleted picture. The marker is a hint about the library;
  // when its subject leaves the library the hint is stale, not just unmatched.
  char marker[kNameMax] = {};
  if (Storage.readFileToBuffer(wallpapers::kActiveMarker, marker, sizeof(marker)) > 0) {
    for (char* c = marker; *c; ++c) {
      if (*c == '\n' || *c == '\r') {
        *c = '\0';
        break;
      }
    }
    if (sheetFile_ == marker) Storage.remove(wallpapers::kActiveMarker);
  }
  LOG_INF("WALL", "deleted %s", path.c_str());

  // Re-derive everything from the card. builtInsMissing_ changes here whenever
  // the deleted file was a built-in, and that flips specialTiles() from 1 to 2
  // -- every wallpaper shifts one cell along. Nothing may act on the old
  // numbering, which is why this is a full rescan and not an erase from names_.
  sheetIndex_ = -1;
  sheetFile_.clear();
  scanLibrary();
  loadSelection();
  clampPage();
  cachedPage_ = -1;
  pickView();
  return true;
}

// The wallpaper at 1:1, and nothing else. No chrome, no button hints, no border:
// what is on the panel here is what the sleep screen puts there, which is the
// whole question Mario asked ("a way to preview the wallpaper without turning
// off the device"). Anything drawn over it would be answering a different one.
//
// The placement arithmetic is SleepActivity's non-oversize branch, reproduced
// rather than called: calculateBitmapPlacement is file-local to SleepActivity
// (upstream's file, not ours to widen). It is the identity for every wallpaper
// this app ships or produces -- kWallpaperFileBytes pins them all at 480x800,
// exactly the panel -- so the two agree on every file that can be here. A
// stray hand-copied BMP of another size centres instead of cropping; it is
// still the picture, and it is not a file any path in this app creates.
void WallpapersActivity::renderPreview() {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();

  // drawBitmap is a SILENT NO-OP while the font cache is scanning
  // (GfxRenderer.cpp:1365). Asking first, because the alternative is a cleared
  // panel with nothing on it and a `drawn = true` derived from parseHeaders
  // rather than from the draw -- a blank screen this fork has twice had cold
  // testers read as a crash.
  const FontCacheManager* fonts = renderer.getFontCacheManager();
  const bool canDraw = fonts == nullptr || !fonts->isScanning();

  const std::string path = std::string(wallpapers::kLibraryDir) + "/" + sheetFile_;
  HalFile file;
  bool drawn = false;
  if (canDraw && Storage.openFileForRead("WALL", path, file)) {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      const int w = bitmap.getWidth();
      const int h = bitmap.getHeight();
      const int x = w <= pageWidth ? (pageWidth - w) / 2 : 0;
      const int y = h <= pageHeight ? (pageHeight - h) / 2 : 0;
      renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0.0f, 0.0f);
      // The cover filter, because the sheet PROMISES "exactly as the sleep
      // screen draws it" and renderBitmapSleepScreen inverts here
      // (SleepActivity.cpp:631). The setting is user-reachable, and without
      // this the one user who chose it is shown the negative of their
      // wallpaper and told it is the real thing.
      if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
        renderer.invertScreen();
      }
      drawn = true;
    }
    file.close();
  }

  // A Frame with nothing built empties the hit table, so the sheet's DELETE
  // cannot still be routable while a full-screen picture covers it. It also
  // gives the one failure path here somewhere to put words: a blank panel reads
  // as a crash, twice confirmed by cold testers in this fork.
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, toybox::readingChromeFaces());
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(target, device, noInput, interactions_);
  toybox::Screen surface(frame, toybox::themeTokens());
  if (!drawn) {
    LOG_ERR("WALL", "preview: cannot draw %s", path.c_str());
    wallpapersui::NoticeModel model;
    model.headline = "CANNOT PREVIEW";
    model.body = "This file could not be opened as a wallpaper. Tap anywhere to go back.";
    wallpapersui::buildNotice(surface, model);
  }
  interactionsReady_ = false;
  // HALF_REFRESH, the same waveform every sleep-screen paint uses
  // (SleepActivity.cpp:609,643). The default is FAST, and under FAST a dense
  // 1-bit plate ghosts the screen it replaced and reads lower-contrast than the
  // real thing -- so the preview would differ from the sleep screen in the one
  // way a preview exists to rule out. The notice path takes it too, so the
  // failure and the picture do not paint with different waveforms.
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  painted_ = true;
}

// The Live screen. No radio and nothing to start: the pairing happens on the
// website and this reader would only talk to it at the next scheduled check, so
// opening the screen is a view change and nothing else. That is what makes it
// safe to reach from a tap with no WiFi picker in front of it.
bool WallpapersActivity::liveShowingCode() const { return !liveConfigured() || liveJoining_; }

// The tile's destination. It names both things a phone can do with this reader
// and does neither of them: no radio, no server, no code. Cheap enough to be
// the first screen, which is what lets the two routes be equals rather than one
// of them being "the screen" and the other a button on it.
void WallpapersActivity::openPhone() {
  view_ = View::Phone;
  // The Live line, settled here on the loop task. buildPhone reads it as a
  // pointer into livePhoneState_ and render() runs on the other task, so it
  // cannot be composed inside the paint.
  //
  // Through the SAME two readings refreshLiveLines uses -- liveConfigured()
  // and liveRunning_, not the store's own fields -- because the screenshot
  // harness makes those disagree by design and a line that took the other
  // answer would contradict the screen one tap away.
  live::Schedule schedule = liveState_.schedule();
  schedule.paired = liveConfigured();
  schedule.on = liveRunning_;
  livePhoneState_ = schedule.paired ? live::scheduleNote(schedule) : wallpapersui::phoneLiveIdle();
  interactionsReady_ = false;
  requestUpdate();
}

void WallpapersActivity::openLive() {
  view_ = View::Live;
  interactionsReady_ = false;
  // The headline counts DOWN, so it is stale the moment it is not recomputed.
  // Ten minutes spent in the grid used to be ten minutes of drift on the one
  // figure this screen exists to show.
  refreshLiveLines();
  // Nothing from a previous visit is on this screen. A confirm left standing
  // would come back naming a row this visit's list has not even fetched yet,
  // and a join code left up would hide the list behind a code nobody asked for.
  liveRevokeIndex_ = -1;
  liveJoining_ = false;
  // Nothing has happened on this visit yet. Without this the screen opens
  // carrying the last visit's report -- and after a wallpaper was chosen in
  // between, "A new message arrived." is a sentence about a sleep screen that
  // now belongs to something else.
  liveStatus_.clear();
  // A reader with no phone yet needs a code, and a code is a network round
  // trip. QUEUED rather than called: the screen is painted first with "Asking
  // Live for a code.", and the request runs from loop() behind it. Called here
  // the app would sit on a blank panel for the length of a TLS handshake, which
  // is the one thing a silent screen is reliably mistaken for.
  if (!liveState_.paired() && livePollToken_.empty()) {
    clearLiveCode();
    liveStatus_ = wallpapersui::liveStatusLine(wallpapersui::LiveStatus::AskingForCode);
    livePairQueued_ = true;
  }
  // And a paired reader fetches the sender list, queued behind the paint for the
  // same reason the code is. The list is the screen's whole second half, and it
  // is the service's answer rather than the card's: a phone revoked from
  // another reader, or one added since the last visit, is only knowable by
  // asking.
  if (liveState_.paired()) liveSendersQueued_ = true;
  requestUpdate();
}

// POST /api/pair/start, then poll until a browser claims the code.
void WallpapersActivity::startLivePairing() {
  live::PairStart start;
  std::string message;
  // The radio, before the request. Without this the call goes to the
  // transport with no network under it and the device panics on a null
  // semaphore, which is what pressing this tile did in v1.13.10.
  live::engine::RadioLease radio(message);
  if (!radio.held()) {
    liveStatus_ = message;
    requestUpdate();
    return;
  }
  if (!live::pairStart(start, message)) {
    clearLiveCode();
    liveStatus_ = message;
    requestUpdate();
    return;
  }
  livePollToken_ = start.pollToken;
  // Grouped three and three, which is the ONE thing anybody has to do with it:
  // read it down a telephone to somebody on another continent. The digits are
  // the service's; the space is ours.
  liveCode_ = start.code;
  if (liveCode_.size() == 6) liveCode_.insert(3, " ");
  // BUILT from the code rather than typed beside it. A link holding its own
  // copy points at the previous code the moment this one changes, and nothing
  // on the screen would show it (derived-facts-written-as-literals).
  armLiveCodeDeadline(start.expiresIn);
  liveStatus_ = wallpapersui::liveStatusLine(wallpapersui::LiveStatus::WaitingForPhone);
  livePollAt_ = millis() + 3000;
  requestUpdate();
}

// The code and its deadline are ONE fact and they go together, always.
//
// There is no separate link member any more: render() derives the QR's payload
// from this same string, so the digits on the glass and the square under them
// cannot name different numbers however this is cleared. They used to be two
// members with two independent fallbacks, which is how the screen came to
// print one code and encode another.
void WallpapersActivity::clearLiveCode() {
  liveCode_.clear();
  liveCodeDeadline_ = 0;
}

// THE SERVICE'S OWN expiresIn, not a ten-minute literal. It was parsed and
// logged and read by nothing at all, so the reader showed a dead code for as
// long as somebody left the screen up.
void WallpapersActivity::armLiveCodeDeadline(const int expiresIn) {
  // A second off the end, so the re-mint starts before the code the panel is
  // showing can be refused. A service that sent no figure gets the value it
  // documents; a pairing screen with no deadline at all is the state this
  // exists to remove.
  const unsigned long seconds = expiresIn > 1 ? static_cast<unsigned long>(expiresIn - 1) : 599UL;
  liveCodeDeadline_ = millis() + seconds * 1000UL;
}

void WallpapersActivity::pollLivePairing() {
  std::string token;
  std::string fridgeId;
  std::string message;
  // The radio, before the request. Without this the call goes to the
  // transport with no network under it and the device panics on a null
  // semaphore, which is what pressing this tile did in v1.13.10.
  live::engine::RadioLease radio(message);
  if (!radio.held()) {
    liveStatus_ = message;
    requestUpdate();
    return;
  }
  const int got = live::pairPoll(livePollToken_, token, fridgeId, message);
  livePollAt_ = millis() + 3000;
  if (got == 0) return;  // still waiting; the screen already says so
  if (got < 0) {
    livePollToken_.clear();
    clearLiveCode();
    liveStatus_ = message;
    requestUpdate();
    return;
  }
  livePollToken_.clear();
  // A JOIN code was claimed: somebody else's phone can now send to the fridge
  // this reader already had. Nothing about THIS reader changed -- same token,
  // same fridge, same picture -- so none of the setup below runs. Turning Live
  // on again, forcing the sleep setting again and dropping the user's chosen
  // wallpaper again would all be a second pairing's worth of side effects for
  // an act that added a contact.
  if (liveJoining_) {
    liveJoining_ = false;
    clearLiveCode();
    liveStatus_ = wallpapersui::liveStatusLine(wallpapersui::LiveStatus::Connected);
    // Straight back to the list, and the list is re-asked rather than guessed
    // at: the new phone's NAME comes from its own browser and only the service
    // knows it.
    liveSendersQueued_ = true;
    interactionsReady_ = false;
    requestUpdate();
    return;
  }
  liveState_.deviceToken = token;
  liveState_.fridgeId = fridgeId;
  // The code did its job and is spent. Dropped HERE rather than left to fall
  // off the screen on its own: it carries the deadline that re-mints an
  // expired code, and /api/pair/start makes a NEW FRIDGE -- so a deadline left
  // armed past a successful pairing is a reader that walks away from the
  // fridge it just joined.
  clearLiveCode();
  // Pairing turns Live ON. Somebody who walked through a six-digit code on a
  // telephone has said what they want; a paired reader that then showed nothing
  // until a second switch was found would be the feature failing at the exact
  // moment it succeeded.
  liveState_.on = true;
  liveRunning_ = true;
  live::save(liveState_);
  // The headline is computed from the SCHEDULE, and the schedule was unpaired
  // until the line above. Without this the paired screen arrives with its
  // largest element blank -- nextCheckPhrase answers "" for a reader that is
  // not paired, and liveNextCheck_ still holds what onEnter worked out -- at
  // the exact moment the feature succeeds.
  refreshLiveLines();
  applyLiveSleepSettings();
  liveStatus_ = wallpapersui::liveStatusLine(wallpapersui::LiveStatus::Connected);
  // And fetch immediately, from loop(). The first thing a person does after
  // pairing is look at the screen. The list is asked for in the same breath:
  // the phone that just claimed the code IS the first sender, and a paired
  // screen whose sender list said nobody could would be wrong about the one
  // thing that had just happened.
  liveCheckQueued_ = true;
  liveSendersQueued_ = true;
  interactionsReady_ = false;
  requestUpdate();
}

void WallpapersActivity::runLiveCheck() {
  bool arrived = false;
  std::string message;
  const bool ok = live::engine::checkNow(liveState_, arrived, message);
  if (!ok) {
    // A 401 cleared the pairing inside checkNow. Falling back to the unpaired
    // half of this screen is the honest thing to draw, and the sentence says
    // why rather than leaving a code to appear from nowhere.
    liveRunning_ = liveState_.on && liveState_.paired();
    if (!liveState_.paired()) {
      // The 401 path. Its own sentence is a full one and this line is a single
      // fitted row, so the SHORT enumerated form is what the panel gets; the
      // long one is already in the log. Every other failure keeps the message
      // it came with, including a service's verbatim refusal.
      liveStatus_ = wallpapersui::liveStatusLine(wallpapersui::LiveStatus::Disconnected);
      clearLiveCode();
      // The list belonged to a fridge this reader can no longer open. Kept on
      // the screen it would be four names the device cannot revoke, behind a
      // confirm that would 401 -- access shown and not removable, which is the
      // one failure the whole revoke path exists to avoid.
      liveSenderCount_ = 0;
      liveRevokeIndex_ = -1;
      liveJoining_ = false;
      livePairQueued_ = true;
    } else {
      liveStatus_ = message;
    }
  } else if (arrived) {
    liveStatus_ = wallpapersui::liveStatusLine(wallpapersui::LiveStatus::NewMessage);
  } else {
    liveStatus_ = wallpapersui::liveStatusLine(wallpapersui::LiveStatus::NothingNew);
  }
  refreshLiveLines();
  interactionsReady_ = false;
  requestUpdate();
}

// POST /api/pair/join: a code that adds a phone to THIS fridge.
//
// Emphatically not /api/pair/start, which mints a NEW fridge. Wiring ADD
// SOMEBODY to that one would have handed the browser a different fridge and
// silently orphaned both the phone already sending and the picture already on
// the glass, with nothing on any screen saying so.
void WallpapersActivity::startLiveJoin() {
  live::PairStart start;
  std::string message;
  // The radio, before the request. Without this the call goes to the
  // transport with no network under it and the device panics on a null
  // semaphore, which is what pressing this tile did in v1.13.10.
  live::engine::RadioLease radio(message);
  if (!radio.held()) {
    liveStatus_ = message;
    requestUpdate();
    return;
  }
  if (!live::pairJoin(liveState_.deviceToken, start, message)) {
    // The refusal at four phones arrives here as the SERVICE's own sentence,
    // and it is drawn verbatim. The device does not get to reword a decision
    // somebody else made, and it does not hold its own copy of the cap to
    // pre-empt it with (BridgeHttp.h, and LiveModel::kMaxSenders says the same).
    liveJoining_ = false;
    clearLiveCode();
    liveStatus_ = message;
    interactionsReady_ = false;
    requestUpdate();
    return;
  }
  livePollToken_ = start.pollToken;
  liveCode_ = start.code;
  if (liveCode_.size() == 6) liveCode_.insert(3, " ");
  armLiveCodeDeadline(start.expiresIn);
  liveStatus_ = wallpapersui::liveStatusLine(wallpapersui::LiveStatus::WaitingForPhone);
  livePollAt_ = millis() + 3000;
  interactionsReady_ = false;
  requestUpdate();
}

// GET /api/senders. The list on the screen is the service's answer and nothing
// else: there is no cached copy on the card to fall out of step with it.
void WallpapersActivity::refreshLiveSenders() {
  live::SenderList list;
  std::string message;
  // The radio, before the request. Without this the call goes to the
  // transport with no network under it and the device panics on a null
  // semaphore, which is what pressing this tile did in v1.13.10.
  live::engine::RadioLease radio(message);
  if (!radio.held()) {
    liveStatus_ = message;
    requestUpdate();
    return;
  }
  if (!live::listSenders(liveState_.deviceToken, list, message)) {
    // The list is left EXACTLY as it was rather than emptied. An empty list is
    // a sentence about this reader ("nobody can send to it"), and a failed
    // request is not evidence for it -- drawing one would tell the user their
    // phones were gone because the Wi-Fi was.
    liveStatus_ = message;
    interactionsReady_ = false;
    requestUpdate();
    return;
  }
  liveSenderMax_ = list.max;
  liveSenderCount_ = list.count;
  for (int i = 0; i < wallpapersui::LiveModel::kMaxSenders; ++i) {
    if (i >= list.count) {
      liveSenders_[i] = LiveSenderRow{};
      continue;
    }
    liveSenders_[i].who = list.items[i].name;
    // FORMATTED HERE, on the loop task, never inside the paint: a line built
    // during a render is a line no test can walk, and a std::string built there
    // is a dangling pointer by the time the screen tree reads it.
    liveSenders_[i].since = live::shortDate(list.items[i].pairedAt);
    liveSenders_[i].id = list.items[i].id;
  }
  // A revoke confirm standing over a row the refreshed list no longer has is a
  // confirm about somebody else. Dropped rather than re-pointed.
  if (liveRevokeIndex_ >= liveSenderCount_) liveRevokeIndex_ = -1;
  interactionsReady_ = false;
  requestUpdate();
}

// POST /api/senders/revoke. Destructive, remote, and silent to the person it
// happens to -- which is why it only ever runs after the confirm that names
// them, and why it refuses to run against a row that is no longer there.
void WallpapersActivity::runLiveRevoke() {
  const int index = liveRevokeIndex_;
  liveRevokeIndex_ = -1;
  if (index < 0 || index >= liveSenderCount_ || liveSenders_[index].id.empty()) {
    // The list moved under the confirm. Nothing is revoked, because the only
    // thing this could do instead is revoke whoever is at that position NOW,
    // and that is the wrong person by definition.
    LOG_ERR("WALL", "the revoke's row is gone; nothing was removed");
    liveSendersQueued_ = true;
    interactionsReady_ = false;
    requestUpdate();
    return;
  }
  int remaining = -1;
  std::string message;
  const std::string id = liveSenders_[index].id;
  // The radio, before the request. Without this the call goes to the
  // transport with no network under it and the device panics on a null
  // semaphore, which is what pressing this tile did in v1.13.10.
  live::engine::RadioLease radio(message);
  if (!radio.held()) {
    liveStatus_ = message;
    requestUpdate();
    return;
  }
  if (!live::revokeSender(liveState_.deviceToken, id, remaining, message)) {
    liveStatus_ = message;
    // Re-asked anyway. A failed revoke leaves the screen's idea of who can send
    // unproven, and the honest thing is the service's answer rather than the
    // list we happened to be holding.
    liveSendersQueued_ = true;
    interactionsReady_ = false;
    requestUpdate();
    return;
  }
  liveStatus_ = wallpapersui::liveStatusLine(wallpapersui::LiveStatus::Removed);
  // REFETCHED, never patched locally. `remaining` is the service's count and
  // the list has to match it; a list edited in place here would be this
  // device's opinion of an answer only the service has.
  liveSendersQueued_ = true;
  interactionsReady_ = false;
  requestUpdate();
}

void WallpapersActivity::toggleLive() {
  liveRunning_ = !liveRunning_;
  liveState_.on = liveRunning_;
  live::save(liveState_);
  // Turning Live ON is a sleep-screen choice, so it makes the same settings
  // change tapping a wallpaper makes. Without it the toggle would say "your
  // phone is on your sleep screen" over a black panel, because
  // SETTINGS.sleepScreen defaults to DARK and DARK never reads /sleep.bmp
  // (WallpapersCore.h:179, and card #354 is the picker having done exactly
  // this). Turning it OFF changes nothing: the setting belongs to whatever
  // owns the sleep screen next, and a toggle that reached over and reset it
  // would undo a wallpaper the user chose afterwards.
  if (liveRunning_) {
    applyLiveSleepSettings();
    // The other half of the same exclusivity. /wallpapers/.active is what makes
    // a tile wear the marker and what makes the grid say a wallpaper is on the
    // sleep screen; Live is about to overwrite /sleep.bmp, so leaving the
    // marker would have the picker assert something Live has just made false.
    // The wallpaper FILES are untouched -- only the claim goes.
    Storage.remove(wallpapers::kActiveMarker);
    clearShuffleDir();
    chosen_.clear();
    loadSelection();
    // AND CHECK NOW. Somebody who just switched this on has said what they
    // want, and the alternative is a blank wait of up to a whole interval
    // before anything happens -- on a daily cadence, a day. It also tells the
    // service Live is on again, because a check-in carries X-Live-On, so the
    // website stops saying "off on the reader" in the same breath.
    if (liveState_.paired()) {
      // And the OTHER branch's job goes, always. loop() drains the check
      // before the off-report, so a pair of them left standing would tell the
      // service "off" last whichever way the switch ended up -- and the
      // website would then say "Live is off on the reader" about a reader that
      // is on, until the next check-in, which can be a week.
      liveOffQueued_ = false;
      liveStatus_ = wallpapersui::liveStatusLine(wallpapersui::LiveStatus::Checking);
      liveCheckQueued_ = true;
    }
  } else if (liveState_.paired()) {
    // AND SAY SO, once, on the way out. Without it the only evidence the
    // website gets is silence, and silence already means a flat battery or a
    // router that moved -- neither of which it can tell apart from this, and
    // both of which it would have to guess at for half a day before saying
    // anything. Queued like every other radio step here: the toggle is already
    // written to the card, so nothing depends on the call getting out.
    liveCheckQueued_ = false;
    liveStatus_ = wallpapersui::liveStatusLine(wallpapersui::LiveStatus::TellingLiveOff);
    liveOffQueued_ = true;
  }
  refreshLiveLines();
  interactionsReady_ = false;
  requestUpdate();
}

// POST /api/off, and its failure costs nothing.
//
// The card already says Live is off and the sleep path already reads the card,
// so this call changes nothing on the device. All it buys is the website being
// able to say "off on the reader" instead of counting down to a check that will
// not happen, and the page has an honest fallback for its absence: the deadline
// passes and it reports a reader it has not heard from, which is the truth.
void WallpapersActivity::runLiveOffReport() {
  // ONLY THE LIVE SCREEN CAN SHOW THE DIFFERENCE, so only it is repainted.
  //
  // This report is queued from two places now: the Live screen's own toggle,
  // where the status line under the controls is what changes, and
  // commitSelection, which runs from a tap on the GRID. Nothing on the grid
  // draws liveStatus_ and the tile's marker went out with the selection that
  // queued this, so an unconditional repaint there is 0.3-2s of an e-ink panel
  // flashing to show exactly what it was already showing -- and it lands a
  // second or two after the tap, which reads as the app doing something of its
  // own.
  const bool visible = view_ == View::Live;
  std::string message;
  live::engine::RadioLease radio(message);
  if (!radio.held()) {
    // Not an error. The user asked for Live to stop, and it has stopped; what
    // did not happen is a courtesy to a website.
    LOG_INF("WALL", "no radio to tell Live this reader is off; the website will work it out");
    liveStatus_.clear();
    if (visible) {
      interactionsReady_ = false;
      requestUpdate();
    }
    return;
  }
  live::reportOff(liveState_.deviceToken, message);
  liveStatus_.clear();
  if (visible) {
    interactionsReady_ = false;
    requestUpdate();
  }
}

// The same call the picker makes, through the same WallpapersCore rules, so
// there is one place that knows what putting a picture on the sleep screen
// costs and one sentence that reports it.
void WallpapersActivity::applyLiveSleepSettings() { applySleepSettings(); }

void WallpapersActivity::refreshLiveLines() {
  // Both lines come out of live::, not out of this function, and the move is
  // the fix rather than tidying. The next check used to be printed from the
  // INTERVAL here, so it said "In about 24 hours" whether the last check was a
  // minute ago or twenty-three hours ago -- which is why the screen's two
  // largest facts read as one fact typed twice. live::nextCheckPhrase runs
  // live::decide, the same arithmetic that arms the timer on the way into
  // sleep, so the headline and the schedule cannot disagree; and both live
  // where host-tests/live can walk every band of them.
  live::Schedule schedule = liveState_.schedule();
  // PAIRED AS THE SCREEN UNDERSTANDS IT, not as the store does. The two differ
  // by design under WALLPAPERS_LIVE_CONFIGURED, the screenshot harness's way of
  // forcing the paired half with no service to pair against -- and a screen
  // drawn paired while these lines were computed unpaired is a blank headline,
  // which is exactly what the harness would have rendered.
  schedule.paired = liveConfigured();
  schedule.on = liveRunning_;
  liveScheduleNote_ = live::scheduleNote(schedule);
  liveNextCheck_ = live::nextCheckPhrase(schedule, static_cast<int64_t>(std::time(nullptr)));
}

// The address the phone opens. Station mode only: the hotspot has no NAT and a
// captive-portal DNS that answers every name with this device, so a phone joined
// to it has no internet and both iOS and Android offer to drop back to cellular
// -- mid-upload, on the one screen that cannot survive it.
void WallpapersActivity::openAdd() {
#ifdef SIMULATOR
  // A PLATFORM gate, not a feature gate: the simulator has no radio to bring up
  // and does not compile CrossPointWebServer at all, so the real path cannot
  // run here. Without this the screen became unreachable headlessly the moment
  // it started requiring WiFi -- and a screen that cannot be rendered cannot be
  // reviewed, which is how every layout defect in this app was found. The
  // address is representative so the layout is measured against a real one.
  addQrUrl_ = "http://192.168.1.42/w";
  addUrl_ = std::string("http://") + devicehost::mdnsName() + ".local/w";
  addAltUrl_ = "http://192.168.1.42/w";
  addBefore_ = static_cast<int>(names_.size());
  addArrived_ = 0;
  addArrivedName_.clear();
  addArrivedThumb_ = Thumb{};
#if WALLPAPERS_ADD_ARRIVED != 0
  // THE STATE A TAP SCRIPT CANNOT PLAY INTO. Reaching it for real needs a
  // phone on the same Wi-Fi posting a file to a server the simulator does not
  // compile, so without this the one screen the whole route ends on could never
  // be rendered -- and a screen that cannot be rendered cannot be reviewed,
  // which is how every layout defect in this app was found. Same harness stub
  // as WALLPAPERS_LIVE_CONFIGURED, default off, and it forces nothing on a
  // device: SIMULATOR already guards it.
  //
  // It runs the REAL commit, not a pretend one, so what the screen reports and
  // what the card says are the same two facts a real arrival produces --
  // including Live coming off the sleep screen.
  //
  // AND IT WILL ONLY PICK A FILE THE ROUTE COULD HAVE MADE. The first version
  // took names_[0], which on a seeded card is a built-in with an editorial name
  // -- so the render showed "kids-on-the-beach is on your sleep screen" for a
  // route that renames every upload w0007.bmp and throws the phone's name away.
  // The picture looked convincing and the sentence was one no reader can
  // produce. If the card holds no upload-shaped file the stub does nothing and
  // says why, because a harness that quietly substitutes something plausible is
  // the thing it exists to avoid.
  int arrival = -1;
  for (size_t i = 0; i < names_.size() && arrival < 0; ++i) {
    if (wallpapers::isUploadName(names_[i])) arrival = static_cast<int>(i);
  }
  if (arrival < 0) {
    LOG_ERR("WALL",
            "WALLPAPERS_ADD_ARRIVED is on but the card holds no %s-shaped file; "
            "seed one or this screen renders its waiting state",
            wallpapers::uploadFileName(1).c_str());
  } else if (setWallpaper(arrival)) {
    const int16_t side = wallpapersui::addPictureSide();
    addArrivedThumb_ = decodeThumb(sourcePathFor(names_[static_cast<size_t>(arrival)]), side, side);
    addArrivedName_ = wallpapers::displayName(names_[static_cast<size_t>(arrival)]).full;
    addArrived_ = 1;
  }
#endif
  view_ = View::Add;
  interactionsReady_ = false;
  requestUpdate();
  return;
#else
  // The radio first, and the picker ONLY if it is needed. WifiSelectionActivity
  // has no already-connected short-circuit of its own -- startWifiScan() runs
  // WiFi.disconnect() on every path -- so launching it unconditionally would
  // show a redundant chooser AND drop a working association. Four other apps
  // guard it exactly this way.
  if (WiFi.status() == WL_CONNECTED) {
    startAddServer();
    return;
  }
  addWaitingWifi_ = true;
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           addWaitingWifi_ = false;
                           if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                             showNotice("NO WIFI", "Adding a wallpaper from your phone needs WiFi. Nothing changed.",
                                        "BACK", wallpapersui::ActionDismiss);
                             return;
                           }
                           startAddServer();
                         });
#endif
}

void WallpapersActivity::startAddServer() {
  // Dev mode holds 80, 81 and UDP 8134 for as long as its toggle is on, and
  // Mario keeps a device on it. Two binds on one port fail in a way that reads
  // as "the screen is broken", so dev mode yields for as long as this screen is
  // up -- the same latch WifiSelectionActivity and the File Transfer screen
  // already take.
  devmode::pause();
  addDevPaused_ = true;

  // Every failure below leaves through stopAddServer(), so the yield is
  // released in exactly ONE place no matter which way this goes wrong. Three
  // resumes hung off three early returns read 1:1 to nobody and are how a latch
  // ends up held after the path nobody tested.
  addServer_ = makeUniqueNoThrow<CrossPointWebServer>(CrossPointWebServer::Surface::WallpapersOnly);
  if (!addServer_) {
    stopAddServer();
    showNotice("OUT OF MEMORY", "There was not enough memory to start. Nothing changed.", "BACK",
               wallpapersui::ActionDismiss);
    return;
  }
  addServer_->begin();
  if (!addServer_->isRunning()) {
    stopAddServer();
    showNotice("COULD NOT START", "The reader could not open its web server. Try again in a moment.", "TRY AGAIN",
               wallpapersui::ActionRetry);
    return;
  }

  // mDNS is ours to start. restartMdns() lives in an anonymous namespace in
  // CrossPointWebServerActivity.cpp so nothing outside that file can call it,
  // and that activity runs MDNS.end() in its own onExit -- so it is definitely
  // not running when this screen opens.
  MDNS.end();
  const bool mdnsUp = MDNS.begin(devicehost::mdnsName());
  if (!mdnsUp) LOG_DBG("WALL", "mDNS did not start; the code carries the address, which does not need it");

  const std::string dotted = std::string(WiFi.localIP().toString().c_str());
  const std::string ipUrl = "http://" + dotted + "/w";
  const std::string nameUrl = std::string("http://") + devicehost::mdnsName() + ".local/w";

  // THE CODE CARRIES THE ADDRESS, ALWAYS. It is generated from WiFi.localIP()
  // at the moment of drawing and depends on no service, so the only way it can
  // be wrong is DHCP moving this device in the seconds between the paint and
  // the scan. The name depends on a responder that can fail to start -- and
  // this function ALREADY KNEW when it had -- so encoding it was putting a
  // detected fault into the one element the user cannot read. The phone would
  // have said "cannot find server" and the prose would have blamed their WiFi.
  addQrUrl_ = ipUrl;

  // The name is the half worth BOOKMARKING -- it survives reboots, WiFi
  // reconnects and DHCP moves -- so it goes where a human reads it. When the
  // responder did not start it is not printed at all: an address that cannot
  // resolve is worse than one line fewer.
  addUrl_ = mdnsUp ? nameUrl : ipUrl;
  addAltUrl_ = mdnsUp ? ipUrl : std::string();

  // Card #352: QrUtils sizes its code from the ALPHANUMERIC table, so a payload
  // past 78 bytes in byte mode draws a code that cannot scan, silently, at every
  // layer. The address is ~24 bytes so this never fires; it stays because an
  // unscannable code is the worst outcome a screen whose whole promise is
  // "point your camera at it" can have.
  if (addQrUrl_.size() > kQrByteSafeLen)
    LOG_ERR("WALL", "address too long for a scannable code: %s", addQrUrl_.c_str());

  addBefore_ = static_cast<int>(names_.size());
  addArrived_ = 0;
  addArrivedName_.clear();
  addArrivedThumb_ = Thumb{};
  view_ = View::Add;
  interactionsReady_ = false;
  requestUpdate();
}

// SEND ANOTHER, on the arrangement where the arrival takes the square. The
// server is already running and the radio is already held, so this is a view
// change and nothing else -- which is why it is not ActionAddOwn, whose job
// starts with the WiFi picker and can end on the no-WiFi notice.
//
// The baseline moves to what is on the card NOW, so the count under the code
// answers "how many since I pressed this" rather than carrying the last
// picture's total forward into a second sentence about it.
void WallpapersActivity::addAnother() {
  addBefore_ = static_cast<int>(names_.size());
  addArrived_ = 0;
  addArrivedName_.clear();
  addArrivedThumb_ = Thumb{};
  interactionsReady_ = false;
  requestUpdate();
}

void WallpapersActivity::stopAddServer() {
  if (addServer_) {
    addServer_->stop();
    addServer_.reset();
    MDNS.end();
  }
  // Guarded by the flag rather than by whether a server exists: the
  // out-of-memory path never got one, and resuming a yield this screen does not
  // hold drops the count out from under whoever does.
  if (addDevPaused_) {
    addDevPaused_ = false;
    devmode::resume();
  }
  // Whatever arrived is kept; only a torn transfer leaves a .part, and the
  // sweep that already exists removes those.
  sweepPartFiles();
}

void WallpapersActivity::pollAddArrivals() {
  if (!addServer_) return;
  // The upload handler writes the file and says nothing to the app, so the
  // screen learns by looking. The bridges poll at this cadence for the same
  // reason.
  // A member, not a function-local static: a static would outlive the activity
  // and carry the last poll's timestamp into the NEXT visit, so re-entering
  // this screen within the interval would miss the first arrival for up to a
  // second and a half with nothing to explain the delay.
  static constexpr unsigned long kPollMs = 1500;
  const unsigned long now = millis();
  if (now - addLastPoll_ < kPollMs) return;
  addLastPoll_ = now;

  // SWAPPED OUT, not copied: the old list is needed to say which name is new,
  // and a copy of every name on the card every 1.5 seconds would be a per-poll
  // allocation for an answer that is wanted once. A swap costs nothing and the
  // old vector is destroyed on the way out of this function either way.
  std::vector<std::string> before;
  before.swap(names_);
  const int was = static_cast<int>(before.size());
  scanLibrary();
  const int isNow = static_cast<int>(names_.size());
  if (isNow == was) return;
  addArrived_ = isNow - addBefore_;
  if (addArrived_ < 0) addArrived_ = 0;

  // WHAT LANDED GOES ON THE SLEEP SCREEN. The person opened this route to put
  // their own picture on the glass, and it used to end with the picture merely
  // filed: on a reader with Live running, the panel then went on showing what
  // the website sends. commitSelection is what makes that impossible -- it is
  // the only writer of /sleep.bmp and it takes Live off on the way through --
  // so choosing here is also the whole of "do not leave Live selected".
  //
  // WHICH one is wallpapers::lastNewName's, freestanding so the step that
  // decides which picture gets pinned can be walked by a host suite rather than
  // only by driving the simulator.
  const int fresh = wallpapers::lastNewName(before, names_);
  if (fresh >= 0) {
    const std::string file = names_[static_cast<size_t>(fresh)];
    if (!setWallpaper(fresh)) {
      // NOT a hint on a strip this screen does not draw. warningPending_ feeds
      // computeWarning(), which reaches the panel only through the GRID's hint
      // band -- so on the Add screen a card that refused the copy looked
      // exactly like nothing having arrived, while the picture WAS on the card
      // and the sleep screen was not the one it names. Every other failure in
      // this file goes through showNotice and so does this one.
      loadSelection();
      computeWarning();
      cachedPage_ = -1;
      LOG_ERR("WALL", "uploaded wallpaper %s could not be set; card full?", file.c_str());
      showNotice("IT ARRIVED, BUT",
                 "Your picture is on the reader and your sleep screen is unchanged. The card may be full. Pick it "
                 "from the grid to try again.",
                 "OK", wallpapersui::ActionDismiss);
      return;
    }
    // DECODED HERE, on the loop task, at the size the screen will place. A
    // decode is an SD read and a downscale; inside a paint it is work on the
    // wrong task, and the window where it has produced nothing is a 232px
    // square of blank paper, which this fork's own notes say reads as a crash.
    const int16_t side = wallpapersui::addPictureSide();
    Thumb picture = decodeThumb(sourcePathFor(file), side, side);
    const std::string shown = wallpapers::displayName(file).full;
    // ONE LOCK ROUND THE PUBLICATION. render() runs on the other FreeRTOS task
    // and holds this same mutex for the whole paint; without it the second
    // upload of a session reassigns a std::string and a vector the paint is
    // reading, which is a read of freed memory rather than a torn word. The
    // poll that does it fires every 1.5s for as long as this screen is up.
    {
      RenderLock lock(*this);
      addArrivedName_ = shown;
      addArrivedThumb_ = std::move(picture);
    }
    LOG_INF("WALL", "an uploaded wallpaper is now the sleep screen: %s", file.c_str());
  }

  loadSelection();
  computeWarning();
  cachedPage_ = -1;
  requestUpdate();
}

void WallpapersActivity::startSetDownload() {
  // The radio first: entering the TLS stack with WiFi never started fails in a
  // way whose message says nothing about WiFi.
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiChosen(!result.isCancelled); });
}

void WallpapersActivity::onWifiChosen(const bool connected) {
  if (!connected) {
    showNotice("NO WIFI", "The wallpapers need WiFi to download. The card is unchanged.", "TRY AGAIN",
               wallpapersui::ActionRetry);
    return;
  }
  // Queued, not run here: this is the result handler of an activity that is
  // still unwinding, and the fetch blocks.
  fetchQueued_ = true;
}

void WallpapersActivity::runSetDownload() {
  // exists() first: mkdir returns false for a directory that is already there,
  // and treating that as failure means every attempt after the first reports a
  // full card. Trivia shipped exactly that bug.
  if (!Storage.exists(wallpapers::kLibraryDir) && !Storage.mkdir(wallpapers::kLibraryDir)) {
    showNotice("NO ROOM", "Could not create the wallpapers folder on the card. Is the card in, and writable?",
               "TRY AGAIN", wallpapersui::ActionRetry);
    return;
  }

  // Sized for the WHOLE set plus the floor that protects other apps, not for one
  // file: a check that passes for one wallpaper and fills the card at number
  // nine costs Study its review log, silently, later.
  uint64_t freeNow = 0;
  const bool queryOk = Storage.freeBytes(freeNow);
  switch (wallpapers::roomFor(queryOk, freeNow, wallpapers::kPackFloorBytes)) {
    case wallpapers::Room::Unknown:
      // NOT the same screen as NO ROOM: freeBytes() returns false for "could not
      // answer", never for "full", and saying the card is full when we do not
      // know that is the conflation the call exists to prevent.
      showNotice("CAN'T TELL",
                 "The card did not answer when asked how much room is left, so nothing was written. "
                 "Trying again usually works.",
                 "TRY AGAIN", wallpapersui::ActionRetry);
      return;
    case wallpapers::Room::TooFull: {
      char body[192];
      std::snprintf(body, sizeof(body),
                    "The wallpapers need about %u MB free and the card has %u MB. "
                    "Delete something from the card, then try again. Nothing was written.",
                    static_cast<unsigned>(wallpapers::kPackFloorBytes >> 20), static_cast<unsigned>(freeNow >> 20));
      showNotice("NO ROOM", body, "TRY AGAIN", wallpapersui::ActionRetry);
      return;
    }
    case wallpapers::Room::Ok:
      break;
  }

  fetchCancel_ = false;
  fetchDone_ = 0;
  fetchPhase_ = 0;
  fetchTotal_ = static_cast<int>(wallpapers::kBuiltInCount);
  view_ = View::Fetching;
  interactionsReady_ = false;
  // requestUpdateAndWait, not requestUpdate: a plain request is DEFERRED and
  // never reaches the render task while a blocking call sits in the same call
  // stack, so the app would freeze on the previous screen for the whole
  // transfer. This is the #306 family; PR #123 is the reference.
  requestUpdateAndWait();

  size_t lastPainted = 0;
  const auto progress = [this, &lastPainted](const size_t got, const size_t total) {
    // Every ~200KB: five honest steps across a ~1MB pack. Each paint is an
    // e-ink refresh, so finer steps would spend longer refreshing than fetching.
    if (got - lastPainted >= 200u * 1024u || (total > 0 && got == total)) {
      lastPainted = got;
      const uint64_t per = wallpapers::kWallpaperFileBytes;
      fetchDone_ = static_cast<int>(got / (per > 0 ? per : 1));
      if (fetchDone_ > fetchTotal_) fetchDone_ = fetchTotal_;
      requestUpdateAndWait();
    }
    // The sanctioned exception to the one-pump rule: nothing else pumps while
    // this blocks, and without it Back could not stop a download at all.
    mappedInput.update();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) fetchCancel_ = true;
    if (mappedInput.wasHomeGesture()) fetchCancel_ = true;
    if (fetchCancel_) {
      fetchCancel_ = true;
      requestUpdateAndWait();
    }
  };

  const auto err = HttpDownloader::downloadToFile(kPackUrl, kPackPart, progress, &fetchCancel_);
  if (err != HttpDownloader::OK) {
    Storage.remove(kPackPart);
    // Every one of these offers TRY AGAIN. A screen that reports a failure and
    // gives nothing to press is a dead end whose only exit is unmarked.
    if (err == HttpDownloader::ABORTED) {
      showNotice("STOPPED", "Download stopped. Nothing was kept, and the card is unchanged.", "TRY AGAIN",
                 wallpapersui::ActionRetry);
    } else if (err == HttpDownloader::FILE_ERROR) {
      showNotice("CARD TROUBLE", "The card would not take the file. Nothing was kept.", "TRY AGAIN",
                 wallpapersui::ActionRetry);
    } else if (HttpDownloader::lastStatus() == 404) {
      // A 404 is not a network problem, and saying "did not answer" for one
      // sends people to check their WiFi for a fault that is ours: the asset is
      // not published. lastStatus() exists precisely because "failed to fetch"
      // reads the same for a dead server and for a server that answered.
      showNotice("NOT THERE YET",
                 "The wallpapers are not on the server yet. That is a problem at our end, not with your "
                 "WiFi or your card. Nothing was written.",
                 "TRY AGAIN", wallpapersui::ActionRetry);
    } else {
      showNotice("NO ANSWER", "The download did not answer. The card is unchanged.", "TRY AGAIN",
                 wallpapersui::ActionRetry);
    }
    return;
  }

  if (!unpackSet()) return;

  Storage.remove(kPackPart);
  scanLibrary();
  prewarmThumbs();
  loadSelection();
  computeWarning();
  page_ = 0;
  cachedPage_ = -1;
  pickView();
  interactionsReady_ = false;
  requestUpdate();
}

// Pack -> one .bmp per wallpaper. Resumable for free: an image already on the
// card at exactly the right size is skipped, so a torn unpack costs seconds of
// SD work on retry rather than another download.
bool WallpapersActivity::unpackSet() {
  // Second phase, same bar, its second third.
  fetchPhase_ = 1;
  fetchDone_ = 0;
  HalFile pack;
  if (!Storage.openFileForRead("WALL", kPackPart, pack)) {
    showNotice("CARD TROUBLE", "The download arrived but could not be read back.", "TRY AGAIN",
               wallpapersui::ActionRetry);
    return false;
  }

  auto buffer = makeUniqueNoThrow<uint8_t[]>(kCopyChunk);
  if (!buffer) {
    showNotice("OUT OF MEMORY", "Not enough memory to unpack the wallpapers.", "TRY AGAIN", wallpapersui::ActionRetry);
    return false;
  }

  const size_t count = wallpapers::builtInCount();
  for (size_t i = 0; i < count; ++i) {
    if (fetchCancel_) {
      pack.close();
      showNotice("STOPPED", "Stopped. The wallpapers that already arrived are on the card.", "TRY AGAIN",
                 wallpapersui::ActionRetry);
      return false;
    }

    const std::string target = std::string(wallpapers::kLibraryDir) + "/" + wallpapers::builtInStem(i) + ".bmp";
    if (fileSizeOf(target) == wallpapers::kWallpaperFileBytes) {
      // Already here and the right length: skip the bytes and move on.
      pack.seekCur(static_cast<size_t>(wallpapers::kWallpaperFileBytes));
      fetchDone_ = static_cast<int>(i) + 1;
      continue;
    }

    const std::string part = target + ".part";
    HalFile out;
    if (!Storage.openFileForWrite("WALL", part, out)) {
      pack.close();
      showNotice("CARD TROUBLE", "The card would not take a wallpaper. The ones already written are kept.", "TRY AGAIN",
                 wallpapersui::ActionRetry);
      return false;
    }

    uint64_t left = wallpapers::kWallpaperFileBytes;
    bool ok = true;
    while (left > 0) {
      const size_t want = left < kCopyChunk ? static_cast<size_t>(left) : kCopyChunk;
      const int got = pack.read(buffer.get(), want);
      if (got <= 0 || out.write(buffer.get(), static_cast<size_t>(got)) != static_cast<size_t>(got)) {
        ok = false;
        break;
      }
      left -= static_cast<uint64_t>(got);
    }
    out.close();

    if (!ok || left != 0) {
      Storage.remove(part.c_str());
      pack.close();
      showNotice("CARD TROUBLE", "A wallpaper did not write completely. The ones already written are kept.",
                 "TRY AGAIN", wallpapersui::ActionRetry);
      return false;
    }

    Storage.remove(target.c_str());
    if (!Storage.rename(part.c_str(), target.c_str())) {
      Storage.remove(part.c_str());
      pack.close();
      showNotice("CARD TROUBLE", "The card refused to name a wallpaper. The ones already written are kept.",
                 "TRY AGAIN", wallpapersui::ActionRetry);
      return false;
    }

    fetchDone_ = static_cast<int>(i) + 1;
    // Every few files, not every file: 21 e-ink refreshes would take longer
    // than the unpack they are reporting on.
    if ((i % 5) == 0 || i + 1 == count) {
      mappedInput.update();
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) fetchCancel_ = true;
      requestUpdateAndWait();
    }
  }

  pack.close();
  return true;
}

void WallpapersActivity::loop() {
  // The deferred free-space walk, once the panel already shows something.
  // Guarded on painted_ rather than run straight from onEnter: rendering is
  // notification-driven, so a blocking call in the same breath as
  // requestUpdate() would run BEFORE the render task ever painted and the
  // deferral would buy nothing (the #306 shape).
  if (warningPending_ && painted_) {
    warningPending_ = false;
    const uint32_t t0 = millis();
    computeWarning();
    LOG_INF("WALL", "free-space probe took %ums", millis() - t0);
    if (!warning_.empty()) requestUpdate();
    return;
  }

  // Started here rather than in the action handler: the fetch blocks for a
  // while and pumps input itself, which must not happen while a tap is still
  // being routed (the Trivia precedent, and the whole of the #306 family).
  if (fetchQueued_) {
    fetchQueued_ = false;
    runSetDownload();
    return;
  }

  // Live's three network steps, all of them behind painted_ for the reason the
  // free-space walk is: rendering is notification-driven, so a blocking call in
  // the same breath as requestUpdate() runs BEFORE the render task paints, and
  // the user waits on a radio in front of a blank panel.
  if (painted_ && livePairQueued_) {
    livePairQueued_ = false;
    startLivePairing();
    return;
  }
  if (painted_ && liveJoinQueued_) {
    liveJoinQueued_ = false;
    startLiveJoin();
    return;
  }
  if (painted_ && liveCheckQueued_) {
    liveCheckQueued_ = false;
    runLiveCheck();
    return;
  }
  if (painted_ && liveOffQueued_) {
    liveOffQueued_ = false;
    runLiveOffReport();
    return;
  }
  // THE CODE ON THE GLASS OUTLIVED THE CODE ON THE SERVICE. A pending code
  // dies at the service's CODE_TTL_S and this screen went on showing it and
  // polling for it forever, so a reader left here for eleven minutes offered
  // six digits that could not be claimed under a line promising they last ten.
  // Re-minted rather than reported, because there is nothing for a person to
  // do about it and the screen has a perfectly good way to show a new one.
  //
  // GUARDED ON THE SCREEN REALLY SHOWING A CODE, through the same predicate
  // the paint reads. Without that, a reader that pairs and then sits on its
  // own paired screen for ten minutes reaches this branch with the deadline
  // its setup code left behind -- and /api/pair/start MINTS A NEW FRIDGE, so
  // it would quietly walk away from the fridge the user had just connected,
  // every ten minutes, with nothing on the screen saying so.
  if (painted_ && view_ == View::Live && liveShowingCode() && liveCodeDeadline_ != 0 &&
      static_cast<long>(millis() - liveCodeDeadline_) >= 0) {
    clearLiveCode();
    livePollToken_.clear();
    liveStatus_ = wallpapersui::liveStatusLine(liveJoining_ ? wallpapersui::LiveStatus::AskingToShare
                                                            : wallpapersui::LiveStatus::AskingForCode);
    if (liveJoining_) {
      liveJoinQueued_ = true;
    } else {
      livePairQueued_ = true;
    }
    interactionsReady_ = false;
    requestUpdate();
    return;
  }
  // The revoke before the refresh, always: they are queued together by
  // runLiveRevoke's own caller order and the list must be re-asked AFTER the
  // phone is gone, never in front of it.
  if (painted_ && liveRevokeQueued_) {
    liveRevokeQueued_ = false;
    runLiveRevoke();
    return;
  }
  if (painted_ && liveSendersQueued_) {
    liveSendersQueued_ = false;
    refreshLiveSenders();
    return;
  }
  if (painted_ && view_ == View::Live && !livePollToken_.empty() && static_cast<long>(millis() - livePollAt_) >= 0) {
    pollLivePairing();
    return;
  }

  // The server only answers while this screen is up, and it answers from the
  // app's own loop -- there is no task behind it.
  if (addServer_ && addServer_->isRunning()) {
    for (int i = 0; i < 8 && addServer_->isRunning(); ++i) addServer_->handleClient();
    pollAddArrivals();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Back unwinds the hold branch one step at a time. From the confirm it goes
    // to the sheet, NOT to the grid: Back on a confirm means "not that", and
    // dropping the user two screens back would make them start the hold again
    // to reach the preview they were actually after.
    if (view_ == View::Confirm || view_ == View::Preview) {
      view_ = View::Sheet;
      interactionsReady_ = false;
      requestUpdate();
      return;
    }
    if (view_ == View::Sheet) {
      sheetIndex_ = -1;
      sheetFile_.clear();
      pickView();
      requestUpdate();
      return;
    }
    // Live unwinds its two sub-states one step at a time, the way the hold
    // branch does. Back on a confirm means "not that", and Back on a join code
    // means "never mind, show me the list again" -- dropping the user to the
    // grid from either would make them find the screen again to reach what they
    // were actually looking at.
    if (view_ == View::Live && liveRevokeIndex_ >= 0) {
      liveRevokeIndex_ = -1;
      interactionsReady_ = false;
      requestUpdate();
      return;
    }
    if (view_ == View::Live && liveJoining_) {
      // What this drops is the POLL, so a code claimed after the user walked
      // away does not pair a phone onto a screen nobody is looking at. The
      // code itself is left to expire on its own; /api/pair/abandon would end
      // it sooner and this screen does not call it yet.
      liveJoining_ = false;
      livePollToken_.clear();
      clearLiveCode();
      liveStatus_.clear();
      liveSendersQueued_ = true;
      interactionsReady_ = false;
      requestUpdate();
      return;
    }
    // Otherwise Live has nothing to unwind: no server, no radio, no
    // half-finished pairing. Back goes to the screen it was opened from, which
    // is the two-route one and not the grid -- the same one-step-at-a-time rule
    // the hold branch follows, and the reason somebody who chose LIVE by
    // mistake does not have to find the tile again to choose the other.
    if (view_ == View::Live) {
      openPhone();
      return;
    }
    if (view_ == View::Phone) {
      pickView();
      requestUpdate();
      return;
    }
    // View::Help is gone with buildHelp (app/wallqr): the QR screen replaced it.
    // Add returns to the GRID rather than to the screen it was opened from,
    // and that is the exception to the rule above rather than a miss: the
    // thing you just did on it was put a wallpaper on the card, and the grid
    // is where that wallpaper is. It is also reachable from the offer screen,
    // where pickView() is the only right answer.
    if (view_ == View::Notice || view_ == View::Add) {
      stopAddServer();
      pickView();
      requestUpdate();
      return;
    }
    // Back and DONE do the same thing, and that is the point: every toggle is
    // already on the card, so there is no uncommitted set for the two exits to
    // disagree about. A modal whose two ways out mean different things is the
    // ambiguity this design does not have. Checked after the screens above,
    // which are their own views: Back closes the screen you can see first.
    if (choosing_) {
      choosing_ = false;
      requestUpdate();
      return;
    }
    stopAddServer();
    shelf::leave(renderer, mappedInput);
    return;
  }

  // The preview has no controls at all, on purpose: it is the sleep screen, and
  // the way out is anywhere on it. Said on the sheet before it opens, because
  // this screen has nowhere to say it.
  if (view_ == View::Preview) {
    int px = 0;
    int py = 0;
    if (!mappedInput.wasScreenTapped(px, py)) return;
    view_ = View::Sheet;
    interactionsReady_ = false;
    requestUpdate();
    return;
  }
  // The Offer, Notice, Sheet and Confirm screens carry real buttons, so their
  // taps go through Interactions rather than the grid's geometry hit-test.
  // Interactions::route() refuses a tap routed against a table the panel has
  // not shown yet, which is what stops a tap aimed at the screen underneath
  // from landing on the one that replaced it during a 0.3-2s e-ink repaint.
  if (view_ == View::Offer || view_ == View::Notice || view_ == View::Sheet || view_ == View::Confirm ||
      view_ == View::Phone || view_ == View::Live || view_ == View::Add) {
    int ax = 0;
    int ay = 0;
    if (!mappedInput.wasScreenTapped(ax, ay) || !interactionsReady_) return;
    fui::InputSnapshot input{};
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(ax);
    input.touchY = static_cast<int16_t>(ay);
    const fui::ActionEvent action = interactions_.route(input);
    switch (action.action) {
      case wallpapersui::ActionGetSet:
      case wallpapersui::ActionRetry:
        startSetDownload();
        return;
      case wallpapersui::ActionAddOwn:
        // Two screens carry this: the offer's USE MY OWN PHOTO, and SEND A
        // PICTURE on the "Your phone" destination. One id, one destination.
        openAdd();
        return;
      case wallpapersui::ActionAddAnother:
        addAnother();
        return;
      case wallpapersui::ActionLiveOpen:
        openLive();
        return;
      case wallpapersui::ActionLiveToggle:
        // Written through to the card. What the screen says, what the tile's
        // marker says and what the sleep path reads are now one stored fact.
        toggleLive();
        return;
      case wallpapersui::ActionLiveCheck:
        // QUEUED, not run. This blocks on the radio for seconds and pumps no
        // input while it does; running it inside route() is the #306 family
        // this app has already been bitten by twice.
        liveStatus_ = wallpapersui::liveStatusLine(wallpapersui::LiveStatus::Checking);
        liveCheckQueued_ = true;
        interactionsReady_ = false;
        requestUpdate();
        return;
      case wallpapersui::ActionLiveAdd:
        // A second code against the SAME fridge, through /api/pair/join.
        // Queued like every other radio step here, and the screen flips to its
        // code half first so the wait has something to say for itself.
        liveJoining_ = true;
        clearLiveCode();
        livePollToken_.clear();
        liveStatus_ = wallpapersui::liveStatusLine(wallpapersui::LiveStatus::AskingToShare);
        liveJoinQueued_ = true;
        interactionsReady_ = false;
        requestUpdate();
        return;
      case wallpapersui::ActionLiveSender:
        // Opens the confirm and removes NOTHING. The value is the row index,
        // which is all a screen is allowed to carry: the id that decides whose
        // access is destroyed lives in one place, here.
        if (action.value >= 0 && action.value < liveSenderCount_) {
          liveRevokeIndex_ = action.value;
          liveStatus_.clear();
          interactionsReady_ = false;
          requestUpdate();
        }
        return;
      case wallpapersui::ActionLiveKeep:
        liveRevokeIndex_ = -1;
        interactionsReady_ = false;
        requestUpdate();
        return;
      case wallpapersui::ActionLiveRevoke:
        // QUEUED, not run: it blocks on the radio and pumps no input, which
        // inside route() is the #306 family this app has been bitten by twice.
        // liveRevokeIndex_ survives until runLiveRevoke consumes it.
        liveRevokeQueued_ = true;
        interactionsReady_ = false;
        requestUpdate();
        return;
      case wallpapersui::ActionDismiss:
        pickView();
        requestUpdate();
        return;
      case wallpapersui::ActionPreview:
        view_ = View::Preview;
        interactionsReady_ = false;
        requestUpdate();
        return;
      case wallpapersui::ActionDelete:
        // Opens the question. Deletes nothing -- which is what makes this the
        // safe half of the pair the confirm reuses the pixels of.
        view_ = View::Confirm;
        interactionsReady_ = false;
        requestUpdate();
        return;
      case wallpapersui::ActionKeep:
        view_ = View::Sheet;
        interactionsReady_ = false;
        requestUpdate();
        return;
      case wallpapersui::ActionConfirmDelete:
        // A failed delete used to discard its bool and repaint the identical
        // confirm: the panel flashed and came back unchanged, with no way to
        // tell a card that refused from a touch that was dropped. Every other
        // failure in this app goes through showNotice, and this is the one the
        // user is watching hardest (a-silent-screen-reads-as-a-crash).
        if (!deleteWallpaper()) {
          showNotice("NOT DELETED",
                     "The card would not remove this wallpaper, or it is already gone. It is still here.", "OK",
                     wallpapersui::ActionDismiss);
          return;
        }
        interactionsReady_ = false;
        requestUpdate();
        return;
      default:
        return;
    }
  }
  if (view_ != View::Grid) return;

  const int pages = pageCount();
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (page_ + 1 < pages) {
      ++page_;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (page_ > 0) {
      --page_;
      requestUpdate();
    }
    return;
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady_) return;

  // The header chip is the only hit region the grid registers; everything below
  // it is geometry. Routed FIRST so a tap on the band cannot fall through to a
  // cell, and gated on the surface like every cell is: the chip is the one
  // control on this screen whose label changes, so a tap that left the finger
  // against the previous frame must not act on the new one.
  {
    fui::InputSnapshot chipInput{};
    chipInput.touchReleased = true;
    chipInput.touchX = static_cast<int16_t>(tapX);
    chipInput.touchY = static_cast<int16_t>(tapY);
    if (interactions_.route(chipInput).action == wallpapersui::ActionChoose) {
      if (!surfaceRevealed()) {
        LOG_INF("WALL", "chip tap refused: surface not yet seen");
        return;
      }
      choosing_ = !choosing_;
      LOG_INF("WALL", "choose-a-set mode %s with %d chosen", choosing_ ? "on" : "off",
              static_cast<int>(chosen_.size()));
      requestUpdate();
      return;
    }
  }

  const wallpapersui::GridGeom geom = wallpapersui::gridGeom(toybox::makeTarget(renderer).deviceContext());

  // A page dot?
  if (pages > 1) {
    const int16_t totalW = static_cast<int16_t>(pages * kDotSize + (pages - 1) * kDotGap);
    const fui::Rect panel = toybox::makeTarget(renderer).deviceContext().screen();
    int16_t x = static_cast<int16_t>((panel.width - totalW) / 2);
    for (int p = 0; p < pages; ++p) {
      // A generous vertical band so the small dots are easy to hit.
      if (tapX >= x - kDotGap / 2 && tapX < x + kDotSize + kDotGap / 2 && tapY >= geom.pageDotsY - 16 &&
          tapY < geom.pageDotsY + kDotSize + 16) {
        if (p != page_) {
          page_ = p;
          requestUpdate();
        }
        return;
      }
      x = static_cast<int16_t>(x + kDotSize + kDotGap);
    }
  }

  // A grid cell? The first cell of page 0 is the + Add a wallpaper tile.
  const int slot = wallpapersui::cellAt(geom, tapX, tapY);
  if (slot < 0) return;
  if (!surfaceRevealed()) {
    // Still refused for a REAL remap (a page turn or the library changing under
    // the finger), never for a moved selection any more. Logged because "my tap
    // did nothing" is otherwise indistinguishable from a dropped touch, and this
    // is the line that tells the two apart on hardware.
    LOG_INF("WALL", "tap refused: surface not yet seen (a real remap, not the marker)");
    return;
  }
  const int combined = page_ * geom.perPage + slot;
  const int specials = specialTiles();
  const int total = specials + static_cast<int>(names_.size());
  if (combined >= total) return;
  // The chrome tiles are not wallpapers and have no sheet, but they are on the
  // same grid and a user who has learned "hold a tile for options" will hold
  // cell 0 first. Neither action is destructive -- one opens the Live screen,
  // the other a ~1MB fetch -- but both are the same failure this
  // whole change exists to prevent: the hold firing the thing the user was
  // reaching past. Silence is the right answer; the hold is repeatable.
  //
  // They keep their meaning in choose-a-set mode too -- a big black "+" that
  // does nothing because of a mode you are in is worse than one that works --
  // and they leave the mode behind, because they leave the grid. Nothing is
  // lost by that: the set is already on the card.
  const bool held = mappedInput.tapWasHeldLong();
  switch (specialAt(combined)) {
    case SpecialTile::Live:
      if (held) return;
      choosing_ = false;
      // The CAPTION is what settles where the tap goes, and the cell says
      // "Your phone" -- which is a destination, not an action. It used to open
      // Live directly, on the reasoning that the phone route IS Live and the
      // local upload server keeps its own way in through the offer screen's
      // USE MY OWN PHOTO. That second half was false: the offer screen stops
      // appearing the moment a reader has any wallpapers, so for everybody
      // past their first fetch the upload had no way in at all.
      openPhone();
      return;
    case SpecialTile::GetSet:
      if (held) return;
      choosing_ = false;
      startSetDownload();
      return;
    case SpecialTile::None:
      break;
  }
  const int idx = combined - specials;
  // FOUR RULES MEET HERE, from three cards, and the order is the whole point.
  // They compose; none is dropped.
  //
  // 1. #365: a hold the SDK's classifier missed arrives as an ORDINARY TAP --
  //    wasTouchTap has no duration gate -- and a tap on this grid changes the
  //    sleep screen. tapWasHeldLong() is the only discriminator and it must be
  //    asked before any branch that can act.
  // 2. #305: while choosing, a tap is membership; and with a set already live,
  //    a tap opens that set for editing rather than collapsing it to one
  //    wallpaper. A stray tap must never undo several taps of work.
  // 3. #354: "already the sleep screen" is only a reason to do nothing when it
  //    can actually BE the sleep screen. When the settings block it -- or a
  //    stray pin is hiding a set -- tapping the marked wallpaper again is
  //    exactly what a person does after nothing happened, and it must repair
  //    that rather than be swallowed.
  // 4. #354: a failed change says so, instead of doing nothing at all.
  //
  // Any of 1 to 3 expressed as a "return early" beside the others would
  // silently win. They are all in cellAction instead, which the host suite
  // walks over every combination: a HOLD on a blocked-and-marked wallpaper
  // still opens the sheet, a TAP on it still repairs the settings, and neither
  // is reachable while a set is being built.
  switch (wallpapers::cellAction(held, choosing_, idx, activeIndex_, static_cast<int>(chosen_.size()), sleepBlocked(),
                                 shadowedSet_)) {
    case wallpapers::CellAction::Sheet:
      openSheet(idx);
      return;
    case wallpapers::CellAction::None:
      return;
    case wallpapers::CellAction::Toggle:
      // Entering the mode IS the tap's answer when a set was already live: the
      // strip has said "Tap to change." and this is that change.
      choosing_ = true;
      toggleChosen(idx);
      return;
    case wallpapers::CellAction::Set:
      break;
  }
  if (setWallpaper(idx)) {
    requestUpdate();
    return;
  }
  // A failed pin used to do NOTHING: no notice, no repaint, nothing on the
  // panel at all, while the reasons (card full, unreadable file, a card that
  // refused the rename) went only to the log. setWallpaper leaves /sleep.bmp
  // untouched on every one of those paths, so the honest report is that the
  // sleep screen did not change.
  //
  // The free-space walk is re-armed rather than run here: it can take seconds
  // on a large card and this is the input path (rendering-is-notification-
  // driven). loop() runs it after the notice is on the glass.
  warningPending_ = true;
  showNotice("COULD NOT SET IT",
             "The wallpaper was not saved and your sleep screen is unchanged. The card may be full, or the file may be "
             "damaged.",
             "OK", wallpapersui::ActionDismiss);
}

void WallpapersActivity::render(RenderLock&&) {
  const uint32_t tPaint = millis();
  clampPage();
  // Before anything toybox: the preview draws no chrome and takes no theme.
  if (view_ == View::Preview) {
    renderPreview();
    LOG_INF("WALL", "render preview took %ums", millis() - tPaint);
    return;
  }
  renderer.clearScreen();
  // Faces per view, not per app. The grid is a menu and wants the Jersey cut it
  // shares with the shelf; the offer, the progress and the notices are
  // SENTENCES, and at the 20px UI cut a sentence runs off the panel and is cut
  // with an ellipsis. Trivia carries the same split for the same reason.
  const bool prose = view_ == View::Offer || view_ == View::Fetching || view_ == View::Notice || view_ == View::Add ||
                     view_ == View::Sheet || view_ == View::Confirm || view_ == View::Phone || view_ == View::Live;
  // View::Add rebinds the SMALL slot to the bold reading cut so the address has
  // a cut of its own: see readingAddressFaces. Without it the headline, the
  // address, the prose and the footer all land on serif 14 and the one line the
  // reader has to type is indistinguishable from the paragraph under it.
  //
  // The UNPAIRED Live screen goes one rung further for the same reason, and
  // only while it is unpaired: its content is a six-digit code somebody reads
  // down a telephone, so pairingCodeFaces puts the 82px capital in the small
  // slot. Once a phone is attached the screen is three facts and three buttons
  // with no code on it at all, and it takes the same face set the offer and the
  // sheet use -- a huge cut bound for a screen that has nothing to set in it is
  // a cut every unstyled string can fall into.
  // liveShowingCode(), not "unpaired": ADD puts a six-digit code on a
  // reader that IS paired, and it is the same code screen. Read through the one
  // predicate the screen itself draws from (wallpapersui::liveShowsCode), so
  // the face and the arrangement cannot come apart -- the version that came
  // apart would draw a number somebody is reading down a telephone at 20px.
  //
  // The confirm is never the code screen: it is three facts and two buttons.
  const bool liveCode = view_ == View::Live && liveRevokeIndex_ < 0 && liveShowingCode();
  fui::GfxRendererTarget target = toybox::makeTarget(
      renderer, liveCode ? toybox::pairingCodeFaces()
                         : (view_ == View::Add ? toybox::readingAddressFaces()
                                               : (prose ? toybox::readingChromeFaces() : toybox::proseMenuFaces())));
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, device, noInput, interactions_);
  toybox::Screen surface(frame);

  if (view_ == View::Add) {
    wallpapersui::AddModel model;
    model.url = addUrl_.c_str();
    model.altUrl = addAltUrl_.c_str();
    model.added = addArrived_;
    if (!addArrivedName_.empty()) model.arrived = addArrivedName_.c_str();
    // The count, for the window between a file landing and its commit -- and
    // for a commit that failed, where the notice says the rest. Once the
    // picture is on the sleep screen the screen IS the picture and this line is
    // not drawn at all. No plural: fmtwidth cannot bound a "%s" that switches
    // word, and a count beside a fixed noun says the same thing.
    if (addArrived_ > 0 && addArrivedName_.empty()) {
      char line[96];
      std::snprintf(line, sizeof(line), "Added: %d. Send another, or press Back to see them.", addArrived_);
      addStatus_ = line;
      model.status = addStatus_.c_str();
    }
    const wallpapersui::AddRects rects = wallpapersui::buildAdd(surface, model);
    // addQrUrl_, NOT addUrl_ (app/wallqr): the code carries the numeric address,
    // which depends on no responder; the name is the half a human reads.
    if (rects.qr.width > 0) {
      QrUtils::drawQrCode(renderer, Rect{rects.qr.x, rects.qr.y, rects.qr.width, rects.qr.height}, addQrUrl_);
    }
    // The picture, already decoded at exactly this side (addPictureSide is what
    // both the rect and the decode are built from). Blitted and nothing more:
    // no file is opened on this task.
    if (rects.thumb.width > 0) {
      drawThumbInto(addArrivedThumb_, rects.thumb);
      // The hairline goes round the INK, not round the box. A 480x800 wallpaper
      // fitted into a square leaves gutters, and a frame on the box presents
      // those gutters as part of the picture -- which is what somebody checking
      // their own photo reads as "it got cropped".
      if (addArrivedThumb_.ok) {
        renderer.drawRect(static_cast<int16_t>(rects.thumb.x + addArrivedThumb_.ox),
                          static_cast<int16_t>(rects.thumb.y + addArrivedThumb_.oy), addArrivedThumb_.w,
                          addArrivedThumb_.h, 1, true);
      }
    }
  } else if (view_ == View::Phone) {
    wallpapersui::PhoneModel model;
    // A MEMBER, for the reason every string on the Live screens is one:
    // render() runs on the other FreeRTOS task and a std::string composed here
    // is freed before the screen tree reads it.
    model.liveState = livePhoneState_.c_str();
    wallpapersui::buildPhone(surface, model);
  } else if (view_ == View::Live && liveRevokeIndex_ >= 0) {
    // The confirm. Everything on it is a MEMBER, for the reason the list is:
    // render() runs on the other FreeRTOS task, and a string composed here is
    // freed before the screen tree reads it.
    wallpapersui::RevokeModel model;
    model.who = liveSenders_[liveRevokeIndex_].who.c_str();
    model.since = liveSenders_[liveRevokeIndex_].since.empty() ? nullptr : liveSenders_[liveRevokeIndex_].since.c_str();
    wallpapersui::buildLiveRevoke(surface, model);
  } else if (view_ == View::Live) {
    wallpapersui::LiveModel model;
    model.configured = liveConfigured();
    model.on = liveRunning_;
    model.joining = liveJoining_;
    // Every one of these is a MEMBER settled on the loop task, never a string
    // assembled inside this paint: render() runs on the other FreeRTOS task
    // with no lock across it, so a temporary built here is a dangling pointer
    // by the time the screen tree reads it, and a line built inside a paint is
    // a line no test can walk.
    //
    // EMPTY MEANS EMPTY. This used to fall back to kLiveCode -- "482 160", the
    // screenshot harness's stub -- with a comment claiming the fallback was
    // reached only under a build flag. Nothing checked any flag: an empty
    // liveCode_ is the ordinary state of this screen from the moment it opens
    // until the service answers, and the whole state of one that cannot reach
    // the service at all. So a real reader put a plausible six-digit number on
    // the glass, and the QR below encoded it, and the first person to scan it
    // was told by the website that the code did not work. buildLive draws its
    // own placeholder now and returns no square.
    model.code = liveCode_.c_str();
    model.url = wallpapersui::kLiveAddress;
    // Set once, for both halves: buildLive picks the paired or the unpaired
    // stack and each has its own line for this.
    model.status = liveStatus_.empty() ? nullptr : liveStatus_.c_str();
    model.nextCheck = liveNextCheck_.c_str();
    model.cadence = liveScheduleNote_.c_str();
    // The list, as /api/senders last answered it. Pointers into MEMBERS, never
    // into anything built here: the names are the service's and this paint runs
    // on the other task.
    //
    // Still never a placeholder. A screen that lists "Abuela" because a mock
    // did is a screen that lies on the first device it reaches, and an empty
    // list here means the reader really has no senders -- which the screen says
    // in words rather than leaving a gap under a heading.
    model.senderCount = liveSenderCount_;
    for (int i = 0; i < liveSenderCount_ && i < wallpapersui::LiveModel::kMaxSenders; ++i) {
      model.senders[i].who = liveSenders_[i].who.c_str();
      model.senders[i].since = liveSenders_[i].since.empty() ? nullptr : liveSenders_[i].since.c_str();
    }
    const fui::Rect qr = wallpapersui::buildLive(surface, model);
    // The QR carries the LINK, the panel carries the ADDRESS: the same split
    // buildAdd makes, and for the same reason -- a phone that will not scan
    // still has something a person can type, and a QR that encoded only the
    // host would land them on a page with the code still to enter.
    //
    // BUILT by wallpapersui::liveLink from the same constant the line above
    // prints, never typed beside it: a link holding its own copy of the address
    // goes on naming last month's host the moment the page moves, and nothing
    // on either screen would show it (derived-facts-written-as-literals).
    //
    // ONE SOURCE, READ ONCE. The link is derived here from the SAME
    // `model.code` the digits above are drawn from, rather than from a second
    // member built beside it -- because two members are two things that can
    // disagree, and this screen's whole defect was a code and a square naming
    // different numbers. There is now no arrangement of state in which the
    // panel can print one code and encode another: an empty code draws no
    // digits, buildLive returns no square, and nothing here runs.
    //
    // liveLink() takes either spelling, so the grouped "482 160" a person
    // reads aloud and the link a phone opens come from one string.
    //
    // No fallback either. A square encoding a code nothing minted is worse
    // than no square, because it is the one element on this screen a person
    // cannot read before trusting it.
    if (qr.width > 0 && qr.height > 0 && model.code[0] != '\0') {
      QrUtils::drawQrCode(renderer, Rect{qr.x, qr.y, qr.width, qr.height}, wallpapersui::liveLink(model.code));
    }
  } else if (view_ == View::Sheet) {
    wallpapersui::SheetModel model;
    model.name = sheetName_.c_str();
    // Settled in loop() by openSheet, not derived here. render() runs on the
    // OTHER FreeRTOS task and ActivityManager holds no lock across it, so a
    // names_[i] read in this function races scanLibrary()'s clear-and-realloc
    // in deleteWallpaper -- a read of a freed std::string. A bool the loop task
    // owns cannot be freed under the render task.
    model.isActive = sheetIsActive_;
    wallpapersui::buildSheet(surface, model);
  } else if (view_ == View::Confirm) {
    wallpapersui::ConfirmModel model;
    model.name = sheetName_.c_str();
    model.consequence = sheetDetail_.c_str();
    wallpapersui::buildConfirm(surface, model);
  } else if (view_ == View::Fetching) {
    wallpapersui::FetchingModel model;
    model.done = fetchDone_;
    model.total = fetchTotal_;
    model.cancelling = fetchCancel_;
    model.phase = fetchPhase_;
    wallpapersui::buildFetching(surface, model);
  } else if (view_ == View::Notice) {
    wallpapersui::NoticeModel model;
    model.headline = noticeHead_.c_str();
    model.body = noticeBody_.c_str();
    model.actionLabel = noticeAction_;
    model.action = noticeActionId_;
    wallpapersui::buildNotice(surface, model);
  } else if (view_ == View::Offer) {
    // BEFORE: the set is not here. Never an empty grid -- a screen showing
    // nothing reads as a crash, confirmed twice by cold testers.
    wallpapersui::OfferModel model;
    model.count = static_cast<int>(wallpapers::kBuiltInCount);
    model.bytes = wallpapers::builtInPackBytes();
    model.alreadyHave = builtInsPresent();
    model.warning = warning_.empty() ? nullptr : warning_.c_str();
    wallpapersui::buildOffer(surface, model);
  } else {
    const wallpapersui::GridGeom geom = wallpapersui::gridGeom(device);
    const int pages = pageCount();
    // "1 / 6", not "PAGE 1 / 6", and "21 SAVED" is gone. The band now carries a
    // third thing -- the chip -- and headerTitleWidth subtracts every one of
    // them from the title's room: at the display cut the long forms cut
    // "WALLPAPERS" to "WALLPAPE...", which the render showed and no assertion
    // would have. The page dots under the grid say the same thing again, and
    // the count moved to the strip, which has the room for a sentence.
    rightLabel_.clear();
    if (pages > 1 && !choosing_) {
      // 32, not 16: host-tests/fmtwidth sizes a buffer by what the FORMAT can
      // print, not by what this app's page counts happen to reach. Two ints are
      // 11 characters each, so "%d / %d" needs 26 -- and a buffer sized by the
      // value rather than the format is how a truncation ships the day
      // something upstream of it changes.
      char label[32];
      snprintf(label, sizeof(label), "%d / %d", page_ + 1, pages);
      rightLabel_ = label;
    }
    wallpapersui::GridChromeModel model;
    model.rightLabel = rightLabel_.empty() ? nullptr : rightLabel_.c_str();
    model.warning = warning_.empty() ? nullptr : warning_.c_str();
    // A live SET counts as active. Without this the grid draws "Tap one to set
    // your sleep screen." beside five marked wallpapers that already are it.
    model.hasActive = !chosen_.empty();
    // liveRunning_, the same bool drawLiveTile puts the marker on. The strip
    // and the marker are two readings of one fact, and reading it twice from
    // two places is how they came to disagree in the first place.
    model.liveOn = liveRunning_;
    model.choosing = choosing_;
    // Rebuilt from SETTINGS and the card every paint rather than cached at
    // selection time: the reach half is only knowable from the live settings,
    // and a cached sentence is how #354's caveat came to be suppressed for a
    // whole app session. The pointer is a literal out of WallpapersCore except
    // for the one line that carries a count, which is built into note_ -- a
    // member, so it outlives the paint.
    model.note = currentSleepNote();
    wallpapersui::buildGridChrome(surface, model);

    // The chrome is a screen tree; the grid is the app's own surface, drawn
    // after it into the body the same way chess draws its board.
    ensureThumbsForPage();
    drawGrid(geom);
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Wallpapers");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  // The offer screen draws its truchet band live, so the paint cost is worth a
  // number rather than an assumption about a 240MHz part.
  // The slot count, every paint. reportOverflow only speaks once the table is
  // already full, which is the moment a control that draws normally has stopped
  // being tappable with nothing on screen to say so; the number beside it is
  // what says how close a screen was getting before that happened.
  LOG_INF("WALL", "render view=%d took %ums, %d/%d slots", static_cast<int>(view_), millis() - tPaint,
          static_cast<int>(interactions_.count()), static_cast<int>(toybox::kMaxInteractions));
  renderer.displayBuffer();
  painted_ = true;
}

uint32_t WallpapersActivity::surfaceMeaning() const {
  // activeIndex_ is NOT in here. See wallpapersui::gridMeaning: the selection
  // does not remap a single cell, so gating taps on it made the picker deaf for
  // a whole refresh after every tap -- Mario's "touches get lost".
  //
  // choosing_ IS, because it changes what a cell DOES. The cost is real and
  // named: the first tile tap after the chip is refused until the new frame is
  // on the glass, which on this panel is 0.3-2s. That is the correct answer --
  // the tap was aimed at the screen before the mode changed -- but it is a tap
  // a person will feel, so it is written down here and in the pull request
  // rather than discovered on hardware.
  return wallpapersui::gridMeaning(page_, static_cast<int>(view_), static_cast<int>(names_.size()), specialTiles(),
                                   choosing_);
}
