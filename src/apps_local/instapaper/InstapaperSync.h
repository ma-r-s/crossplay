#pragma once

// The device half of the read-later bridge (docs/apps/instapaper-plan.md).
//
// Everything network lives here so the Activity only sequences screens. The
// protocol is the bridge's device API: QR pairing (start / poll / on-device
// confirm), then one JSON POST carrying this reader's whole index and its
// archive queue, a polled job, and one download per article whose text moved.
//
// The POST is the index itself, and that is the design rather than a
// shortcut. Instapaper's own `have` parameter takes id:hash:progress:timestamp
// and answers with only what changed -- so what this device knows IS the
// delta input, and there is no ack, no offset, and no second copy of the truth
// anywhere in the system to fall out of step with the account.
//
// Downloads are checked against the manifest's LENGTH. The summary also
// carries a `sha` and the device uses it to decide WHETHER to download, never
// to verify what arrived; TLS covers the wire. Do not read that field as
// proof something was checked.
//
// Transport, TLS roots and the heap floor are shared with any other bridge in
// this firmware: see src/apps_local/bridge/BridgeHttp.h.

#include <cstdint>
#include <string>
#include <vector>

#include "InstapaperIndex.h"
#include "InstapaperLibrary.h"

namespace instapaper {

constexpr char kBridgeHost[] = "read.crossplay.ma-r-s.com";

// What one finished sync did. Every field here ends up in a sentence on the
// verdict screen; nothing is carried that nobody says out loud.
struct Delivery {
  Article article;
  // The byte count the download is checked against. It rides beside the
  // article rather than inside it because it describes THIS delivery, not the
  // article: nothing on the card should remember it, and an index column for
  // it would be a persisted copy of a fact that expires immediately.
  uint32_t bytes = 0;
};

struct SyncSummary {
  std::vector<Delivery> articles;  // new or changed metadata
  std::vector<int64_t> deleteIds;  // no longer in the unread folder
  std::vector<int64_t> archived;   // intents the bridge confirmed
  int failed = 0;                  // articles Instapaper could not prepare
  std::string firstFailure;        // and the reason for the first of them
  int withheld = 0;                // ready, but not this cycle: more next sync
};

// The deliveries as mergeSummary wants them.
std::vector<Article> deliveredArticles(const SyncSummary& summary);

// The byte count for one delivered article, or 0 when it was not delivered.
uint32_t deliveredBytes(const SyncSummary& summary, int64_t id);

class Sync {
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
  // pending code, a deviceToken revokes a registration the confirm screen
  // declined. Failures are ignored; the code's TTL is the backstop.
  void pairAbandon(const std::string& pollToken, const std::string& deviceToken);

  // Post the index and the archive queue. `now` is the device's clock in
  // epoch seconds, or 0 when it is not set -- an unset clock must not stamp
  // reading progress, because a wrong timestamp either loses the reader's
  // position or pins the account's where nothing can move it.
  bool syncStart(const BridgeState& state, const std::vector<Article>& have, const std::vector<int64_t>& archive,
                 std::string& jobId, std::string& message);

  // True after a request was refused for the token itself: the stored pairing
  // is dead and must be cleared, or every later sync repeats the same refusal
  // forever.
  bool unpaired = false;

  // "running" | "done" | "error" | "" (transport failure).
  std::string syncStatus(const BridgeState& state, const std::string& jobId, SyncSummary& out, std::string& message);

  // `expectedBytes` comes from the delivery and is the ONLY proof this device
  // has that a file arrived whole, so a download with no size is refused
  // rather than written.
  bool downloadToPart(const BridgeState& state, const Article& article, uint32_t expectedBytes,
                      const std::string& partPath, bool* cancel, std::string& message);

  static std::string pairUrl(const std::string& code);
};

}  // namespace instapaper
