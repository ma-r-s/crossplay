#pragma once

// The device half of the sync bridge (docs/apps/study-sync-bridge-plan.md).
//
// Everything network lives here so StudyActivity only sequences screens. The
// protocol is the bridge's device API: QR pairing (start / poll / on-device
// confirm), then sync as one binary POST (this card's own revlog.dat tails
// and cards.dat, in the exact shapes it already writes), a polled job, and
// manifested deck downloads. Acks are byte offsets into revlog.dat; the file
// itself is never truncated.
//
// A download is checked against the manifest's LENGTH, not its sha256: the
// hash arrives and is parsed, and nothing on the device reads it. TLS covers
// the wire, so what this leaves open is a file that lands the right length
// and the wrong bytes on the card. Do not read the sha256 field as proof
// that something verified it.
//
// Two transports, split on FREEINK_NET_WOLFSSL:
//  - Device: freeink::SecureHttpClient with certificate verification against
//    a root BUNDLE (StudySyncRoots.h; SD override /study/.bridge-roots.pem),
//    because the bridge carries the account token and Cloudflare rotates
//    issuing CAs. setInsecure() is not used here, deliberately.
//  - Simulator: curl via popen. The sim's SecureHttpClient stub truncates a
//    binary body at the first NUL (String conversion), and the sync POST's
//    length prefix guarantees NULs, so the stub cannot carry it. In the sim
//    the bridge URL is overridable via STUDY_BRIDGE_URL so tests run against
//    a local bridge with a throwaway account, never a real one.

#include <cstdint>
#include <string>
#include <vector>

namespace study {

constexpr char kBridgeHost[] = "sync.ma-r-s.com";
constexpr int kMaxSyncDecks = 8;
// How many decks the picker will list. Eight is what a card can hold, but the
// list has to reach the one you want before you can choose it, and a
// collection of forty subdecks is ordinary. Each row is a name and a count,
// so the whole list is kilobytes on a board with 8MB of PSRAM.
constexpr size_t kMaxOfferedDecks = 200;

// /study/.bridge: the pairing token and per-deck-directory ack offsets.
struct BridgeState {
  bool paired = false;
  std::string token;
  // 48, not 32: the service slugifies deck names up to 40 characters, so a
  // shorter buffer silently truncated real Anki deck names ("AnkiDroid
  // Japanese Core 2000 Step 01") and then never matched their slug again.
  char deckDirs[kMaxSyncDecks][48] = {};
  uint32_t ackOffsets[kMaxSyncDecks] = {};
  // A checksum of the bytes the offset claims were already sent, i.e. of
  // [0, ack). An offset alone cannot tell a log that grew from one that was
  // replaced, rolled back to an older copy, or half-updated by a failed sync;
  // the hash of the region it covers can. Zero means "not recorded yet".
  uint64_t ackHashes[kMaxSyncDecks] = {};
  // The buildId last downloaded per deck dir: the server reuses a build
  // when nothing changed, and a matching id means every file on the card
  // is already exactly the build the manifest describes.
  char lastBuilds[kMaxSyncDecks][20] = {};
  // Seconds since epoch of the last completed sync; drawn under the door.
  int64_t lastSyncAt = 0;
  int deckCount = 0;
  // Whether this reader has ever answered "which decks?"; without it a
  // re-pair would silently inherit whatever the account last chose.
  bool choseDecks = false;

  const char* buildFor(const char* dir) const;
  void setBuild(const char* dir, const char* buildId);

  uint32_t ackFor(const char* dir) const;
  uint64_t ackHashFor(const char* dir) const;
  void setAckHash(const char* dir, uint64_t hash);
  void setAck(const char* dir, uint32_t offset);
};

bool loadBridgeState(BridgeState& out);
bool saveBridgeState(const BridgeState& state);

// One deck's contribution to a sync POST, read from the card by the caller
// (the activity owns the open files and their flush discipline).
struct DeckPayload {
  const char* dirName;     // ack key; the server echoes it back
  std::string revlogTail;  // bytes of revlog.dat from the acked offset
  std::string cards;       // the whole cards.dat
  uint32_t revlogOffset;   // where the tail starts
};

// A deck the server built for this account, from the job's manifest.
struct ManifestFile {
  std::string path;  // relative inside the build ("deck.dat", "fonts/...")
  size_t size;
  std::string sha256;
};
struct DeckManifest {
  std::string slug;
  std::string deckName;  // Anki deck name; matched against local meta names
  std::string buildId;
  std::vector<ManifestFile> files;
};

class StudySync {
 public:
  // Every failure fills `message` with a sentence the screen shows verbatim.
  struct PairStart {
    std::string code;
    std::string pollToken;
  };

  bool pairStart(PairStart& out, std::string& message);
  // 1 delivered (username+token filled), 0 still pending, -1 failed/expired.
  int pairPoll(const std::string& pollToken, std::string& username, std::string& token, std::string& message);
  // Best-effort hygiene on any pairing walk-away: a pollToken kills the
  // pending code on the bridge, a deviceToken revokes a registration the
  // confirm screen declined. Failures are ignored; the TTL is the backstop.
  void pairAbandon(const std::string& pollToken, const std::string& deviceToken);

  // The account's decks, and which of them this account already syncs. A
  // fresh account has chosen nothing, so the device must ask before its
  // first sync can deliver anything at all.
  struct DeckChoice {
    std::string name;
    int cards = 0;
    bool chosen = false;
  };
  // True after listDecks stopped short of the account's full deck list.
  bool decksWithheld = false;
  bool listDecks(const BridgeState& state, std::vector<DeckChoice>& out, std::string& message);
  bool chooseDecks(const BridgeState& state, const std::vector<std::string>& names, std::string& message);

  bool syncStart(const BridgeState& state, const std::vector<DeckPayload>& decks, std::string& jobId,
                 std::vector<std::pair<std::string, uint32_t>>& acks, std::string& message);
  // True after syncStart was refused for the token itself (revoked or
  // unknown): the stored pairing is dead and must be cleared, or every
  // later SYNC repeats the same refusal forever.
  bool unpaired = false;
  // Decks the bridge could not build this cycle. The rest of the sync
  // succeeds around them, so the verdict has to name them or the user is
  // told SYNCED and finds a deck missing.
  std::vector<std::string> failedDecks;
  // "running" | "done" | "error" | "frozen" | "" (transport failure).
  std::string syncStatus(const BridgeState& state, const std::string& jobId, std::vector<DeckManifest>& manifests,
                         std::string& message);

  // Download one build file to partPath (no rename). deck.dat and cards.dat
  // are index-aligned, so the CALLER downloads a deck's whole set to .part
  // names and renames them together -- per-file atomicity would allow a
  // mismatched pair, which shows the wrong answers under the right questions.
  bool downloadToPart(const BridgeState& state, const DeckManifest& deck, const ManifestFile& file,
                      const std::string& partPath, bool* cancel, std::string& message);

  static std::string pairUrl(const std::string& code);
};

}  // namespace study
