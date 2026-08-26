#pragma once

// The device half of the sync bridge (docs/apps/study-sync-bridge-plan.md).
//
// Everything network lives here so StudyActivity only sequences screens. The
// protocol is the bridge's device API: QR pairing (start / poll / on-device
// confirm), then sync as one binary POST (this card's own revlog.dat tails
// and cards.dat, in the exact shapes it already writes), a polled job, and
// hash-manifested deck downloads. Acks are byte offsets into revlog.dat;
// the file itself is never truncated.
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

// /study/.bridge: the pairing token and per-deck-directory ack offsets.
struct BridgeState {
  bool paired = false;
  std::string token;
  char deckDirs[kMaxSyncDecks][32] = {};
  uint32_t ackOffsets[kMaxSyncDecks] = {};
  int deckCount = 0;

  uint32_t ackFor(const char* dir) const;
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

  bool syncStart(const BridgeState& state, const std::vector<DeckPayload>& decks, std::string& jobId,
                 std::vector<std::pair<std::string, uint32_t>>& acks, std::string& message);
  // True after syncStart was refused for the token itself (revoked or
  // unknown): the stored pairing is dead and must be cleared, or every
  // later SYNC repeats the same refusal forever.
  bool unpaired = false;
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
