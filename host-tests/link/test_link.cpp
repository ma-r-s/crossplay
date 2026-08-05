// Local multiplayer tests. Freestanding: no device, no radio, no PlatformIO.
//
// The point of this file is the FakeLink below. A real radio will not drop a
// packet when you ask it to, so "it works itself out all the edge cases" can
// only be a claim until there is a link that misbehaves on demand. This one
// drops, delays, duplicates and reorders under a seeded PRNG, and the soak at
// the bottom plays thousands of matches through it.
//
// The invariant every game test asserts is the same one: both devices end up
// holding the identical move history, and it strictly alternates. A protocol
// bug that lets the two boards diverge cannot survive that.

#include <cstdio>
#include <cstring>
#include <vector>

#include "../../src/apps_local/link/LinkSession.h"
#include "FakeLink.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  checksRun++;
  if (condition) return;
  checksFailed++;
  std::printf("FAIL test_link.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

using namespace linkplay;

using namespace linktest;

// ---------------------------------------------------------------------------
// A minimal game: the shared state is the whole move history, so any divergence
// between the two devices is visible by comparing bytes.
// ---------------------------------------------------------------------------

constexpr uint16_t kGameId = 0x4321;
constexpr uint8_t kHistoryCap = 64;

struct Player {
  Player(Medium& medium, const uint8_t last, const char* name)
      : transport(medium, addressOf(last)), displayName(name) {}

  FakeTransport transport;
  Session session;
  const char* displayName;
  uint8_t history[kHistoryCap] = {};
  uint8_t historyLength = 0;
  int movesMade = 0;
  bool sawIncoming = false;
  // submit() inside pump() cannot fail: its guard rules out every case the
  // function rejects. Asserting it per move inflated the check count by 4000
  // without testing anything, so it is recorded and asserted once instead.
  bool submitRefused = false;

  void begin(const uint32_t nowMs) { session.begin(transport, kGameId, displayName, nowMs); }

  // One loop pass: pump the session, apply anything that arrived, then move if
  // it is our turn. This is the shape the Activity will have.
  void pump(const uint32_t nowMs, const int moveLimit, const uint8_t myMark) {
    session.tick(nowMs);
    uint8_t length = 0;
    if (session.takeIncoming(history, kHistoryCap, length)) {
      historyLength = length;
      sawIncoming = true;
    }
    if (session.myTurn() && movesMade < moveLimit && historyLength < kHistoryCap) {
      history[historyLength++] = myMark;
      if (!session.submit(history, historyLength)) submitRefused = true;
      movesMade++;
    }
  }
};

// Pumps the sessions only, without the game logic on top. Tests that want to
// inspect what is waiting to be applied need this: Player::pump() consumes an
// incoming state as part of playing, so it cannot be used to observe one.
void runSessions(Medium& medium, std::vector<Player*>& players, const uint32_t durationMs) {
  constexpr uint32_t kStepMs = 10;
  const uint32_t until = medium.nowMs + durationMs;
  while (medium.nowMs < until) {
    for (Player* player : players) player->session.tick(medium.nowMs);
    medium.collect();
    medium.nowMs += kStepMs;
  }
}

// Steps every player through `durationMs` of loop passes.
void run(Medium& medium, std::vector<Player*>& players, const uint32_t durationMs, const int moveLimit = 0) {
  constexpr uint32_t kStepMs = 10;
  const uint32_t until = medium.nowMs + durationMs;
  while (medium.nowMs < until) {
    for (size_t i = 0; i < players.size(); ++i) players[i]->pump(medium.nowMs, moveLimit, static_cast<uint8_t>(i + 1));
    medium.collect();
    medium.nowMs += kStepMs;
  }
}

// ---------------------------------------------------------------------------

void testPacketRoundTrip() {
  uint8_t buffer[kMaxPacketBytes];
  const uint8_t payload[4] = {1, 2, 3, 4};
  const size_t length = encode(buffer, sizeof(buffer), PacketType::State, 0x1234, 7, 0xA5, payload, 4);
  CHECK(length == kHeaderBytes + 4);

  PacketView view;
  CHECK(decode(buffer, length, view));
  CHECK(view.type == PacketType::State);
  CHECK(view.gameId == 0x1234);
  CHECK(view.sequence == 7);
  CHECK(view.payloadLength == 4);
  CHECK(view.payload != nullptr && memcmp(view.payload, payload, 4) == 0);

  // An empty payload is legal and is what an Ack is.
  const size_t ackLength = encode(buffer, sizeof(buffer), PacketType::Ack, 1, 2, 0, nullptr, 0);
  CHECK(ackLength == kHeaderBytes);
  CHECK(decode(buffer, ackLength, view));
  CHECK(view.payload == nullptr && view.payloadLength == 0);

  // The largest packet we will ever send still fits under ESP-NOW v1's limit.
  uint8_t big[kMaxPayloadBytes] = {};
  CHECK(encode(buffer, sizeof(buffer), PacketType::State, 1, 1, 0, big, kMaxPayloadBytes) == kMaxPacketBytes);
  CHECK(kMaxPacketBytes <= 250);
}

void testPacketRejectsRubbish() {
  uint8_t buffer[kMaxPacketBytes];
  const size_t length = encode(buffer, sizeof(buffer), PacketType::State, 9, 1, 0, nullptr, 0);
  PacketView view;

  CHECK(decode(buffer, length, view));  // baseline: this one is good

  uint8_t corrupted[kMaxPacketBytes];
  memcpy(corrupted, buffer, length);
  corrupted[0] = 'Z';
  CHECK(!decode(corrupted, length, view));  // wrong magic

  memcpy(corrupted, buffer, length);
  corrupted[4] = kProtocolVersion + 1;
  CHECK(!decode(corrupted, length, view));  // wrong version

  memcpy(corrupted, buffer, length);
  corrupted[5] = 99;
  CHECK(!decode(corrupted, length, view));  // unknown type

  memcpy(corrupted, buffer, length);
  corrupted[10] = 8;  // claims a payload that is not there
  CHECK(!decode(corrupted, length, view));

  CHECK(!decode(buffer, kHeaderBytes - 1, view));  // truncated
  CHECK(!decode(nullptr, length, view));

  // Encoding refuses what it cannot represent rather than truncating.
  uint8_t small[8];
  CHECK(encode(small, sizeof(small), PacketType::Ack, 1, 1, 0, nullptr, 0) == 0);
}

void testTwoDevicesMatch() {
  Medium medium;
  Player low(medium, 0x10, "LOW");
  Player high(medium, 0x20, "HIGH");
  std::vector<Player*> players = {&low, &high};

  low.begin(medium.nowMs);
  high.begin(medium.nowMs);
  run(medium, players, 3000);

  CHECK(low.session.state() == Session::State::Playing);
  CHECK(high.session.state() == Session::State::Playing);
  CHECK(low.session.peerAddress() == addressOf(0x20));
  CHECK(high.session.peerAddress() == addressOf(0x10));

  // Exactly one side moves first, computed independently and identically. Which
  // one is a coin toss, so it is deliberately not asserted here -- see
  // testWhoMovesFirstIsNotAlwaysTheSameDevice.
  CHECK(low.session.isHost() != high.session.isHost());

  // Names crossed over, so the UI can say who left.
  CHECK(strcmp(low.session.peerName(), "HIGH") == 0);
  CHECK(strcmp(high.session.peerName(), "LOW") == 0);
}

void testMatchingIsOrderIndependent() {
  // Same two devices, started in the other order and with a long stagger. A
  // device that has been broadcasting for the better part of a second still
  // pairs cleanly with one that just arrived, and the two never come away
  // disagreeing about who moves first.
  Medium medium;
  Player high(medium, 0x77, "HIGH");
  Player low(medium, 0x11, "LOW");
  std::vector<Player*> players = {&high, &low};

  high.begin(medium.nowMs);
  run(medium, players, 800);
  low.begin(medium.nowMs);
  run(medium, players, 3000);

  CHECK(low.session.state() == Session::State::Playing);
  CHECK(high.session.state() == Session::State::Playing);
  CHECK(low.session.isHost() != high.session.isHost());
  CHECK(low.session.myTurn() != high.session.myTurn());
  CHECK(low.session.isHost() == low.session.myTurn());
}

void testWhoMovesFirstIsNotAlwaysTheSameDevice() {
  // The defect this replaced: "lower address moves first" over two fixed MACs
  // meant one device played White in every game of chess ever played on the
  // pair. The DS's first player was whoever tapped first; a coin toss reaches
  // the same place with nothing for the user to do.
  constexpr int kTrials = 40;
  int lowFirst = 0;
  int highFirst = 0;
  int agreed = 0;
  for (int trial = 0; trial < kTrials; ++trial) {
    Medium medium;
    medium.nowMs = static_cast<uint32_t>(trial) * 137u;  // a different moment each time
    Player low(medium, 0x10, "A");
    Player high(medium, 0x20, "B");
    std::vector<Player*> players = {&low, &high};
    low.begin(medium.nowMs);
    high.begin(medium.nowMs);
    run(medium, players, 2000);

    if (low.session.state() == Session::State::Playing && high.session.state() == Session::State::Playing &&
        low.session.myTurn() != high.session.myTurn())
      agreed++;
    (low.session.myTurn() ? lowFirst : highFirst)++;
  }
  // Whatever the toss lands on, the two devices never disagree about it.
  CHECK(agreed == kTrials);
  // And it really is a toss: both devices move first sometimes. A one-sided
  // result here is the old defect back.
  CHECK(lowFirst > 0);
  CHECK(highFirst > 0);
}

void testSubmittingBeforeTakingDeliveryIsRefused() {
  // The natural-looking loop is "if (myTurn) move; if (hasIncoming) apply;", and
  // it is wrong: adopting a peer state raises both flags in the same breath, so
  // that order moves from a board one move stale. The peer then adopts what we
  // sent, and both devices come to agree on a position where the opponent's move
  // never happened -- and agreement is exactly what the divergence checks in
  // this file look for, so nothing here could have caught it. Only the refusal
  // can.
  Medium medium;
  Player a(medium, 0x30, "A");
  Player b(medium, 0x40, "B");
  std::vector<Player*> players = {&a, &b};
  a.begin(medium.nowMs);
  b.begin(medium.nowMs);
  runSessions(medium, players, 2000);
  CHECK(a.session.state() == Session::State::Playing);
  CHECK(b.session.state() == Session::State::Playing);

  Player& first = a.session.myTurn() ? a : b;
  Player& second = a.session.myTurn() ? b : a;

  const uint8_t opening[1] = {'X'};
  CHECK(first.session.submit(opening, sizeof(opening)));
  runSessions(medium, players, 200);

  // Their move has landed and it is our turn, both at once.
  CHECK(second.session.hasIncoming());
  CHECK(second.session.myTurn());

  const uint8_t reply[2] = {'X', 'Y'};
  CHECK(!second.session.submit(reply, sizeof(reply)));

  // Take delivery and the same move goes through.
  uint8_t got[4] = {};
  uint8_t length = 0;
  CHECK(second.session.takeIncoming(got, sizeof(got), length));
  CHECK(length == 1);
  CHECK(got[0] == 'X');
  CHECK(second.session.submit(reply, sizeof(reply)));
}

void testHostMovesFirst() {
  Medium medium;
  Player low(medium, 0x10, "A");
  Player high(medium, 0x20, "B");
  std::vector<Player*> players = {&low, &high};
  low.begin(medium.nowMs);
  high.begin(medium.nowMs);
  run(medium, players, 2000);

  CHECK(low.session.myTurn());
  CHECK(!high.session.myTurn());
  // Exactly one side may move, always.
  CHECK(low.session.myTurn() != high.session.myTurn());
}

void testSubmitIsRefusedOutOfTurn() {
  Medium medium;
  Player low(medium, 0x10, "A");
  Player high(medium, 0x20, "B");
  std::vector<Player*> players = {&low, &high};

  const uint8_t payload[2] = {9, 9};
  // Not begun yet.
  CHECK(!low.session.submit(payload, 2));

  low.begin(medium.nowMs);
  high.begin(medium.nowMs);
  // Searching, not playing.
  CHECK(!low.session.submit(payload, 2));

  run(medium, players, 2000);
  CHECK(low.session.myTurn());
  CHECK(!high.session.submit(payload, 2));  // not their turn
  CHECK(low.session.submit(payload, 2));    // ours is
  CHECK(!low.session.submit(payload, 2));   // and now it is not, either

  // Oversize is refused rather than truncated.
  run(medium, players, 1000);
  if (high.session.myTurn()) {
    uint8_t big[kMaxPayloadBytes + 1] = {};
    CHECK(!high.session.submit(big, sizeof(big) > 255 ? 255 : static_cast<uint8_t>(sizeof(big))));
  }
}

// Plays `moveLimit` alternating moves and asserts the two devices agree.
void playAndCompare(const int lossPercent, const int duplicatePercent, const int jitterMs, const int moveLimit,
                    const uint32_t durationMs, const char* label) {
  Medium medium;
  medium.random = 0xC0FFEE;
  medium.lossPercent = lossPercent;
  medium.duplicatePercent = duplicatePercent;
  medium.maxJitterMs = jitterMs;

  Player a(medium, 0x10, "A");
  Player b(medium, 0x20, "B");
  std::vector<Player*> players = {&a, &b};
  a.begin(medium.nowMs);
  b.begin(medium.nowMs);
  run(medium, players, durationMs, moveLimit);

  const bool matched = a.session.state() == Session::State::Playing && b.session.state() == Session::State::Playing;
  if (!matched) std::printf("  (%s: never matched)\n", label);
  CHECK(matched);

  // Both played, and both saw the other play.
  CHECK(!a.submitRefused);
  CHECK(!b.submitRefused);
  CHECK(a.movesMade + b.movesMade >= moveLimit);
  CHECK(a.sawIncoming);
  CHECK(b.sawIncoming);

  // The histories can differ by exactly one move: whoever moved last has it and
  // the other has not received it yet. Anything more is a desync.
  const int difference = static_cast<int>(a.historyLength) - static_cast<int>(b.historyLength);
  const bool closeEnough = difference >= -1 && difference <= 1;
  if (!closeEnough) std::printf("  (%s: %d vs %d moves)\n", label, a.historyLength, b.historyLength);
  CHECK(closeEnough);

  // And the shorter one must be a prefix of the longer: same moves, same order.
  const uint8_t shared = a.historyLength < b.historyLength ? a.historyLength : b.historyLength;
  const bool agree = memcmp(a.history, b.history, shared) == 0;
  if (!agree) std::printf("  (%s: histories diverge)\n", label);
  CHECK(agree);

  // Turns alternated. Player 1 is the host and therefore moved first.
  bool alternating = shared == 0 || a.history[0] == 1;
  for (uint8_t i = 1; i < shared; ++i)
    if (a.history[i] == a.history[i - 1]) alternating = false;
  CHECK(alternating);
}

void testPerfectLink() { playAndCompare(0, 0, 0, 20, 20000, "perfect"); }

void testLossyLink() { playAndCompare(30, 0, 0, 20, 40000, "30% loss"); }

void testHostileLink() {
  // Loss, duplication and reordering together. Nothing here may produce a
  // history the other side disagrees with; it may only make the match slower.
  playAndCompare(25, 20, 40, 16, 60000, "hostile");
}

void testDuplicatesDoNotDoubleApply() {
  // Every packet arrives twice. A duplicated State must not advance the game
  // twice, which is what the sequence check buys.
  playAndCompare(0, 100, 0, 12, 20000, "all duplicated");
}

void testPeerLeavingIsImmediate() {
  Medium medium;
  Player a(medium, 0x10, "A");
  Player b(medium, 0x20, "B");
  std::vector<Player*> players = {&a, &b};
  a.begin(medium.nowMs);
  b.begin(medium.nowMs);
  run(medium, players, 2000);
  CHECK(a.session.state() == Session::State::Playing);

  b.session.leave();
  CHECK(b.session.state() == Session::State::Ended);
  CHECK(b.session.endReason() == Session::EndReason::LocalLeft);

  // The Bye lands well inside one screen refresh, not after the silence
  // timeout. Half a second is already generous.
  std::vector<Player*> remaining = {&a};
  run(medium, remaining, 500);
  CHECK(a.session.state() == Session::State::Ended);
  CHECK(a.session.endReason() == Session::EndReason::PeerLeft);
}

void testSilenceIsDetected() {
  // No Bye: a flat battery, or out of range. The only signal is silence.
  Medium medium;
  Player a(medium, 0x10, "A");
  Player b(medium, 0x20, "B");
  std::vector<Player*> players = {&a, &b};
  a.begin(medium.nowMs);
  b.begin(medium.nowMs);
  run(medium, players, 2000);
  CHECK(a.session.state() == Session::State::Playing);

  std::vector<Player*> alone = {&a};
  run(medium, alone, kPeerTimeoutMs - 1000);
  CHECK(a.session.state() == Session::State::Playing);  // not yet: still patient

  run(medium, alone, 2000);
  CHECK(a.session.state() == Session::State::Ended);
  CHECK(a.session.endReason() == Session::EndReason::PeerLost);
  CHECK(!a.session.myTurn());  // an ended match never offers a move
}

void testOneWayRangeStillConverges() {
  // A can hear B but none of A's Hellos reach B for the first stretch. Without
  // treating a Join as evidence the sender exists, this stalls indefinitely.
  Medium medium;
  Player a(medium, 0x10, "A");
  Player b(medium, 0x20, "B");
  std::vector<Player*> players = {&a, &b};

  medium.hasBlackhole = true;
  medium.blackholeSource = addressOf(0x10);
  a.begin(medium.nowMs);
  b.begin(medium.nowMs);
  run(medium, players, 2000);
  CHECK(a.session.state() == Session::State::Searching);
  CHECK(b.session.state() == Session::State::Searching);

  medium.hasBlackhole = false;
  run(medium, players, 3000);
  CHECK(a.session.state() == Session::State::Playing);
  CHECK(b.session.state() == Session::State::Playing);
}

void testMatchSurvivesALostJoin() {
  // The classic asymmetry: one side matches and stops sending Joins while the
  // other is still searching. The matched side has to answer a stray Join or
  // the searcher waits forever.
  Medium medium;
  medium.random = 0x1234;
  medium.lossPercent = 50;
  Player a(medium, 0x10, "A");
  Player b(medium, 0x20, "B");
  std::vector<Player*> players = {&a, &b};
  a.begin(medium.nowMs);
  b.begin(medium.nowMs);
  run(medium, players, 5000);
  CHECK(a.session.state() == Session::State::Playing);
  CHECK(b.session.state() == Session::State::Playing);
}

void testThirdDeviceIsLeftOut() {
  // Three devices, one game. Exactly two must pair, and the third must be left
  // searching rather than half-joined to either.
  Medium medium;
  Player a(medium, 0x10, "A");
  Player b(medium, 0x20, "B");
  Player c(medium, 0x30, "C");
  std::vector<Player*> players = {&a, &b, &c};
  a.begin(medium.nowMs);
  b.begin(medium.nowMs);
  c.begin(medium.nowMs);
  run(medium, players, 6000);

  int playing = 0;
  for (const Player* player : players)
    if (player->session.state() == Session::State::Playing) playing++;
  CHECK(playing == 2);

  // The pair must agree they are each other's peer. A three-way muddle where
  // one device points at somebody who is not pointing back is the bug here.
  for (const Player* player : players) {
    if (player->session.state() != Session::State::Playing) continue;
    const Address& peer = player->session.peerAddress();
    bool mutual = false;
    for (const Player* other : players) {
      if (!(other->transport.localAddress() == peer)) continue;
      mutual = other->session.state() == Session::State::Playing &&
               other->session.peerAddress() == player->transport.localAddress();
    }
    CHECK(mutual);
  }
}

void testDifferentGamesIgnoreEachOther() {
  // Two devices in chess and one in connect four must not pair across games.
  Medium medium;
  Player a(medium, 0x10, "A");
  Player b(medium, 0x20, "B");
  std::vector<Player*> players = {&a, &b};
  a.session.begin(a.transport, kGameId, "A", medium.nowMs);
  b.session.begin(b.transport, kGameId + 1, "B", medium.nowMs);
  run(medium, players, 5000);

  CHECK(a.session.state() == Session::State::Searching);
  CHECK(b.session.state() == Session::State::Searching);
}

void testLeavingWhileSearchingIsClean() {
  Medium medium;
  Player a(medium, 0x10, "A");
  a.begin(medium.nowMs);
  a.session.leave();
  CHECK(a.session.state() == Session::State::Ended);
  CHECK(a.session.endReason() == Session::EndReason::LocalLeft);

  // leave() on a session that never began does nothing at all.
  Player b(medium, 0x20, "B");
  b.session.leave();
  CHECK(b.session.state() == Session::State::Idle);
}

void testTakeIncomingIsOnceOnly() {
  Medium medium;
  Player a(medium, 0x10, "A");
  Player b(medium, 0x20, "B");
  std::vector<Player*> players = {&a, &b};
  a.begin(medium.nowMs);
  b.begin(medium.nowMs);
  runSessions(medium, players, 2000);

  const uint8_t payload[3] = {7, 8, 9};
  CHECK(a.session.submit(payload, 3));
  runSessions(medium, players, 500);

  uint8_t out[kHistoryCap];
  uint8_t length = 0;
  // A capacity too small leaves the state pending rather than dropping it. A
  // half-delivered board is worse than one that arrives a moment later.
  CHECK(b.session.hasIncoming());
  CHECK(!b.session.takeIncoming(out, 1, length));
  CHECK(b.session.hasIncoming());

  CHECK(b.session.takeIncoming(out, kHistoryCap, length));
  CHECK(length == 3);
  CHECK(memcmp(out, payload, 3) == 0);

  // Taken once and only once, however many retransmits arrive afterwards.
  CHECK(!b.session.hasIncoming());
  CHECK(!b.session.takeIncoming(out, kHistoryCap, length));
  runSessions(medium, players, 1000);
  CHECK(!b.session.hasIncoming());
}

void testStaleStateIsIgnored() {
  // A retransmit of a state we have already answered must not roll our own move
  // back off the board. Everything else in the protocol tolerates a duplicate
  // by being idempotent; this is the one place where adopting an old state
  // would actively undo a move, so it gets its own test rather than being left
  // to the soak.
  Medium medium;
  Player a(medium, 0x10, "A");
  Player b(medium, 0x20, "B");
  std::vector<Player*> players = {&a, &b};
  a.begin(medium.nowMs);
  b.begin(medium.nowMs);
  runSessions(medium, players, 2000);

  uint8_t out[kHistoryCap];
  uint8_t length = 0;
  const uint8_t first[1] = {1};
  CHECK(a.session.submit(first, 1));
  runSessions(medium, players, 300);
  CHECK(b.session.takeIncoming(out, kHistoryCap, length));

  const uint8_t second[2] = {1, 2};
  CHECK(b.session.submit(second, 2));
  runSessions(medium, players, 300);
  CHECK(!b.session.myTurn());

  // Replay a's first state, byte for byte as a late retransmit would look.
  uint8_t packet[kMaxPacketBytes];
  const size_t packetLength = encode(packet, sizeof(packet), PacketType::State, kGameId, 1, 0, first, 1);
  CHECK(packetLength > 0);
  medium.submit(addressOf(0x10), addressOf(0x20), packet, packetLength);
  runSessions(medium, players, 300);

  CHECK(!b.session.hasIncoming());  // nothing to apply
  CHECK(!b.session.myTurn());       // and the turn did not come back
}

// Counts what `who` has put on the air and left in the queue, by type.
int packetsFrom(const Medium& medium, const Address& who, const PacketType type) {
  int count = 0;
  for (const auto& inFlight : medium.queue) {
    if (!(inFlight.from == who)) continue;
    PacketView view;
    if (!decode(inFlight.data, inFlight.length, view)) continue;
    if (view.type == type) count++;
  }
  return count;
}

void testJoinAnswersAreRateLimited() {
  // The window before either side is confirmed is where the storm used to
  // ignite: both devices Playing, neither having heard an Ack from the other
  // yet, and a duplicating link turning one Join into a burst. Answering per
  // copy is what made it geometric. So the answer is capped at the cadence a
  // searching device uses -- one per kJoinIntervalMs, however many arrive.
  Medium medium;
  Player a(medium, 0x10, "A");
  Player b(medium, 0x20, "B");  // never ticks; every packet from B here is injected by hand
  a.begin(medium.nowMs);

  uint8_t join[kMaxPacketBytes];
  const size_t joinLength = encode(join, sizeof(join), PacketType::Join, kGameId, 0, 0, nullptr, 0);
  CHECK(joinLength > 0);

  // One Join is enough to match: it makes B a candidate and is mutual agreement
  // in the same pass. A is now Playing and has heard nothing that proves B is
  // matched back, which is the state under test.
  medium.submit(addressOf(0x20), addressOf(0x10), join, joinLength);
  a.session.tick(medium.nowMs);
  CHECK(a.session.state() == Session::State::Playing);

  medium.nowMs += kJoinIntervalMs;  // past the cadence, so an answer is due
  medium.queue.clear();

  // Eight copies landing together, which is what a duplicating, reordering link
  // delivers after a stretch of backlog.
  for (int copy = 0; copy < 8; ++copy) medium.submit(addressOf(0x20), addressOf(0x10), join, joinLength);
  a.session.tick(medium.nowMs);
  const int firstBurst = packetsFrom(medium, addressOf(0x10), PacketType::Join);
  if (firstBurst != 1) std::printf("  (join rate: %d answers to a burst of 8)\n", firstBurst);
  CHECK(firstBurst == 1);  // answered, and answered once

  // And it is a rate limit rather than a one-shot: once the interval is up, a
  // peer that really is still searching gets answered again.
  medium.nowMs += kJoinIntervalMs;
  medium.collect();
  for (int copy = 0; copy < 8; ++copy) medium.submit(addressOf(0x20), addressOf(0x10), join, joinLength);
  a.session.tick(medium.nowMs);
  CHECK(packetsFrom(medium, addressOf(0x10), PacketType::Join) == 2);
}

void testRetransmitIsNotStarvedByReplies() {
  // The retry needs its own clock. Keyed off "when did we last send anything",
  // every packet we answer pushes our own retransmit further out -- and
  // answering is not optional, because every State is acked, retransmits
  // included. So a peer whose acks are being lost resends its state forever, we
  // ack every copy, and on a duplicating link the copies arrive faster than the
  // 400ms retry. Our own move then never goes out again: the board on the other
  // device stays as it was, with nobody holding the turn.
  Medium medium;
  Player a(medium, 0x10, "A");
  Player b(medium, 0x20, "B");
  std::vector<Player*> players = {&a, &b};
  a.begin(medium.nowMs);
  b.begin(medium.nowMs);
  runSessions(medium, players, 2000);
  CHECK(a.session.state() == Session::State::Playing);

  const uint8_t payload[1] = {1};
  CHECK(a.session.submit(payload, 1));  // unacknowledged from here on

  // A stale State from the peer on every single pass. It is never adopted -- the
  // sequence is one we have already used -- but it is acked every time, and that
  // is what keeps refreshing the clock.
  uint8_t stale[kMaxPacketBytes];
  const size_t staleLength = encode(stale, sizeof(stale), PacketType::State, kGameId, 1, 0, payload, 1);
  CHECK(staleLength > 0);

  // Only A runs. B is deaf for the duration, so everything A put on the air is
  // still sitting in the queue at the end to be counted.
  constexpr uint32_t kStepMs = 10;
  for (int step = 0; step < 200; ++step) {  // 2000ms, five retry intervals
    medium.submit(addressOf(0x20), addressOf(0x10), stale, staleLength);
    a.session.tick(medium.nowMs);
    medium.collect();
    medium.nowMs += kStepMs;
  }

  const int states = packetsFrom(medium, addressOf(0x10), PacketType::State);
  // Five retry intervals fit in the window, so six States is the healthy count
  // and three is already proof the retry is running. Starved, it is exactly one:
  // the original send, and nothing ever again.
  if (states < 3) std::printf("  (retransmit: %d states in 2000ms of answered chatter)\n", states);
  CHECK(states >= 3);
  CHECK(a.session.state() == Session::State::Playing);
}

void testMatchedPeersDoNotEchoJoins() {
  // The storm that read as a deadlock. A matched device answers a Join so that a
  // peer still stuck on SEARCHING gets unstuck -- but a peer that had already
  // matched answered that answer, and the two of them then reflected Joins off
  // each other for the rest of the match. On a link that duplicates, the
  // population doubled every round trip: ~70000 Joins in a 40s soak match, the
  // receive budget pinned, and the state retransmits buried behind seconds of
  // backlog. Both boards sat there, both Playing, neither holding the turn,
  // histories one state apart.
  //
  // A perfect link here, because the bug never needed a bad one -- duplication
  // only decided how fast it ran away.
  Medium medium;
  Player a(medium, 0x10, "A");
  Player b(medium, 0x20, "B");
  std::vector<Player*> players = {&a, &b};
  a.begin(medium.nowMs);
  b.begin(medium.nowMs);
  runSessions(medium, players, 2000);
  CHECK(a.session.state() == Session::State::Playing);
  CHECK(b.session.state() == Session::State::Playing);

  // A late Join from each side, which is what a delayed or duplicated discovery
  // packet looks like when it lands after both devices have matched.
  uint8_t packet[kMaxPacketBytes];
  const size_t length = encode(packet, sizeof(packet), PacketType::Join, kGameId, 0, 0, nullptr, 0);
  CHECK(length > 0);
  const long before = medium.carried;
  medium.submit(addressOf(0x20), addressOf(0x10), packet, length);
  medium.submit(addressOf(0x10), addressOf(0x20), packet, length);
  runSessions(medium, players, 3000);

  // Three seconds of a matched, idle pair is heartbeats and nothing else: one
  // Ack per second each, plus the two injected above. The bound is loose on
  // purpose. It is not pinning the heartbeat rate, it is catching a runaway --
  // an unguarded echo puts over a thousand packets in the air here.
  const long carried = medium.carried - before;
  if (carried > 20) std::printf("  (join echo: %ld packets in 3s of an idle match)\n", carried);
  CHECK(carried <= 20);
  CHECK(a.session.state() == Session::State::Playing);
  CHECK(b.session.state() == Session::State::Playing);

  // The rule underneath, asserted on its own, because the count above cannot see
  // it: a Join from a peer that has already proven it is matched with us gets no
  // answer at all. Rate-limiting the answer instead only makes the reflection
  // slower than the limit. On this harness that happens to stop it dead, because
  // a round trip is 10ms against a 200ms limit and the suppressed reply is what
  // would have kept the loop alive -- but on a link whose round trip is the
  // slower of the two, the same code reflects forever at the limit. The guard
  // has to be the fact that they are matched, not the clock.
  //
  // Clearing the queue is a test-local reset, not a favour to the protocol: it
  // drops what is in flight, which a lossy link may do at any moment anyway.
  medium.queue.clear();
  medium.submit(addressOf(0x20), addressOf(0x10), packet, length);
  for (int step = 0; step < 5; ++step) {
    a.session.tick(medium.nowMs);  // A alone, so anything on the air afterwards is A's
    medium.collect();
    medium.nowMs += 10;
  }
  const int answers = packetsFrom(medium, addressOf(0x10), PacketType::Join);
  if (answers != 0) std::printf("  (join echo: a confirmed peer's Join was answered %d times)\n", answers);
  CHECK(answers == 0);
}

void testPeerRestartingEndsTheMatch() {
  // The wedge that nearly shipped. A device that leaves the app and taps
  // MULTIPLAYER again goes back to broadcasting Hellos. Those used to satisfy
  // "any packet from the peer proves it is alive", so the departed device's own
  // discovery traffic pinned its old opponent in Playing forever -- on a
  // perfect link, with no loss involved at all. The other device would sit on a
  // live board that could never change, which is precisely the thing the whole
  // match-ends-when-you-leave rule exists to prevent.
  Medium medium;
  Player a(medium, 0x10, "A");
  Player b(medium, 0x20, "B");
  std::vector<Player*> players = {&a, &b};
  a.begin(medium.nowMs);
  b.begin(medium.nowMs);
  runSessions(medium, players, 3000);
  CHECK(a.session.state() == Session::State::Playing);
  CHECK(b.session.state() == Session::State::Playing);

  // reset() rather than leave(): no Bye is sent. That is the documented
  // teardown path when the radio has already gone, and on real hardware it is
  // also what a lost Bye looks like.
  b.session.reset();
  b.begin(medium.nowMs);
  runSessions(medium, players, 2000);

  CHECK(a.session.state() == Session::State::Ended);
  CHECK(a.session.endReason() == Session::EndReason::PeerLeft);
  CHECK(!a.session.myTurn());
}

void testLeavingAnEndedMatchKeepsTheReason() {
  // The Activity calls leave() on its way out, which happens after the "they
  // left" screen has been up. Rewriting the reason there would make the screen
  // and the state disagree about what happened.
  Medium medium;
  Player a(medium, 0x10, "A");
  Player b(medium, 0x20, "B");
  std::vector<Player*> players = {&a, &b};
  a.begin(medium.nowMs);
  b.begin(medium.nowMs);
  runSessions(medium, players, 2000);

  b.session.leave();
  std::vector<Player*> remaining = {&a};
  run(medium, remaining, 500);
  CHECK(a.session.endReason() == Session::EndReason::PeerLeft);

  a.session.leave();
  CHECK(a.session.endReason() == Session::EndReason::PeerLeft);
}

void testSoak() {
  // Many matches, each on its own randomly-hostile link. Every one has to reach
  // a state both devices agree on. This is where a rare ordering bug shows up.
  constexpr int kMatches = 200;
  int matched = 0;
  int desyncs = 0;
  int refused = 0;
  int fewestMoves = 1 << 30;
  long busiest = 0;
  uint32_t seed = 0xBEEF;

  for (int i = 0; i < kMatches; ++i) {
    Medium medium;
    medium.random = nextRandom(seed) | 1u;
    medium.lossPercent = static_cast<int>(nextRandom(seed) % 36);
    medium.duplicatePercent = static_cast<int>(nextRandom(seed) % 30);
    medium.maxJitterMs = static_cast<int>(nextRandom(seed) % 60);

    Player a(medium, static_cast<uint8_t>(0x10 + (i % 7)), "A");
    Player b(medium, static_cast<uint8_t>(0x80 + (i % 5)), "B");
    std::vector<Player*> players = {&a, &b};
    a.begin(medium.nowMs);
    b.begin(medium.nowMs);
    run(medium, players, 40000, 10);

    if (a.session.state() != Session::State::Playing) continue;
    if (b.session.state() != Session::State::Playing) continue;
    matched++;

    // Progress, not just agreement. Two devices that agree on an empty board
    // and never move again satisfied every other assertion here, so deleting
    // the retransmit entirely used to pass the soak clean.
    if (medium.carried > busiest) busiest = medium.carried;
    const int played = a.movesMade + b.movesMade;
    if (played < fewestMoves) fewestMoves = played;
    if (a.submitRefused || b.submitRefused) refused++;

    const int difference = static_cast<int>(a.historyLength) - static_cast<int>(b.historyLength);
    const uint8_t shared = a.historyLength < b.historyLength ? a.historyLength : b.historyLength;
    if (difference < -1 || difference > 1 || memcmp(a.history, b.history, shared) != 0) desyncs++;
  }

  // Every match on a link this bad still has ample time to complete; if this
  // number sags, discovery or the retransmit has regressed.
  if (matched != kMatches) std::printf("  (soak: %d of %d matched)\n", matched, kMatches);
  CHECK(matched == kMatches);
  if (desyncs != 0) std::printf("  (soak: %d desynced)\n", desyncs);
  CHECK(desyncs == 0);
  CHECK(refused == 0);
  // Every match must run the move cap out, not merely agree. This was the bound
  // that caught the Join reflection storm: about 8 matches in 200 used to stop
  // early with both sides Playing and NEITHER holding the turn, histories
  // exactly one state apart, always with duplication in play. It looked like the
  // submitter had stopped retransmitting; it had not. Two matched devices were
  // answering each other's Join with a Join, forever, and a duplicating link
  // doubled that population every round trip until the retransmits were buried
  // under a multi-second backlog of them. See the Join case in
  // LinkSession::handlePacket.
  if (fewestMoves < 20) std::printf("  (soak: fewest moves in any match %d of 20)\n", fewestMoves);
  CHECK(fewestMoves >= 20);

  // Chatter, not just correctness. The storm above was invisible to every other
  // assertion here -- the two histories agreed, they had simply stopped growing
  // -- while it put around 140000 packets on the air in a single 40s match. A
  // matched pair at rest costs two heartbeats a second, and the busiest honest
  // match in this soak carries 164 packets, so the ceiling is loose enough not
  // to pin any rate and tight enough that a protocol answering a packet with a
  // packet the peer answers back cannot hide under it.
  constexpr long kTrafficCeiling = 600;  // 40s of match, i.e. 15 packets/second
  if (busiest > kTrafficCeiling) std::printf("  (soak: busiest match carried %ld packets)\n", busiest);
  CHECK(busiest <= kTrafficCeiling);
}

}  // namespace

int main() {
  testPacketRoundTrip();
  testPacketRejectsRubbish();
  testTwoDevicesMatch();
  testMatchingIsOrderIndependent();
  testWhoMovesFirstIsNotAlwaysTheSameDevice();
  testSubmittingBeforeTakingDeliveryIsRefused();
  testHostMovesFirst();
  testSubmitIsRefusedOutOfTurn();
  testPerfectLink();
  testLossyLink();
  testHostileLink();
  testDuplicatesDoNotDoubleApply();
  testPeerLeavingIsImmediate();
  testSilenceIsDetected();
  testOneWayRangeStillConverges();
  testMatchSurvivesALostJoin();
  testThirdDeviceIsLeftOut();
  testDifferentGamesIgnoreEachOther();
  testStaleStateIsIgnored();
  testJoinAnswersAreRateLimited();
  testRetransmitIsNotStarvedByReplies();
  testMatchedPeersDoNotEchoJoins();
  testPeerRestartingEndsTheMatch();
  testLeavingAnEndedMatchKeepsTheReason();
  testLeavingWhileSearchingIsClean();
  testTakeIncomingIsOnceOnly();
  testSoak();

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
