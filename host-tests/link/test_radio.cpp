// The radio, tested over real sockets.
//
// test_link.cpp proves the protocol against a link it invented. This file
// proves the transport the simulator actually uses: two Radio instances, each
// binding its own UDP port on localhost, exchanging real datagrams through the
// kernel. Same code path two simulator windows will run, so "two simulators on
// one Mac can play each other" is a tested claim rather than a hopeful one.
//
// The device path (ESP-NOW) is compiled out here and stays hardware-gated.
//
// Note the polling helpers below. Loopback UDP is not synchronous: measured on
// macOS, a datagram sent and immediately read back is not there yet and shows
// up about two milliseconds later. The first version of this file asserted
// instant delivery, and its broadcast assertions "passed" by picking up the
// previous test's unicast. Anything that expects a packet waits for it, and
// anything that expects silence lets the wire settle first.

#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "../../src/apps_local/link/LinkRadio.h"
#include "../../src/apps_local/link/LinkSession.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  checksRun++;
  if (condition) return;
  checksFailed++;
  std::printf("FAIL test_radio.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

using namespace linkplay;

constexpr uint16_t kGameId = 0x0C4E;

// Long enough for anything already sent to have arrived, short enough that the
// suite still runs in a couple of seconds.
void settle() { ::usleep(20000); }

// Waits for one packet rather than demanding it be there already.
bool receiveWithin(Radio& radio, Address& from, uint8_t* buffer, const size_t capacity, size_t& length) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (radio.receive(from, buffer, capacity, length)) return true;
    ::usleep(1000);
  }
  return false;
}

void testTwoRadiosComeUpDistinct() {
  Radio a;
  Radio b;
  CHECK(a.begin());
  CHECK(b.begin());
  CHECK(a.started() && b.started());

  // Distinct addresses, and a total order, which is what host election needs.
  CHECK(a.localAddress() != b.localAddress());
  CHECK((a.localAddress() < b.localAddress()) != (b.localAddress() < a.localAddress()));

  a.end();
  b.end();
  CHECK(!a.started());
  // A stopped radio refuses to send rather than pretending.
  const uint8_t payload[1] = {1};
  CHECK(!a.send(b.localAddress(), payload, 1));
}

void testUnicastReachesOnlyItsTarget() {
  Radio a;
  Radio b;
  Radio c;
  CHECK(a.begin());
  CHECK(b.begin());
  CHECK(c.begin());

  uint8_t buffer[kMaxPacketBytes];
  Address from;
  size_t length = 0;

  const uint8_t message[5] = {'h', 'e', 'l', 'l', 'o'};
  CHECK(a.send(b.localAddress(), message, sizeof(message)));
  CHECK(receiveWithin(b, from, buffer, sizeof(buffer), length));
  CHECK(length == sizeof(message));
  CHECK(memcmp(buffer, message, sizeof(message)) == 0);
  // The sender is taken from the socket, not the payload, so an instance
  // cannot claim to be somebody else.
  CHECK(from == a.localAddress());

  settle();
  CHECK(!c.receive(from, buffer, sizeof(buffer), length));  // never went there
  CHECK(!b.receive(from, buffer, sizeof(buffer), length));  // and only once

  a.end();
  b.end();
  c.end();
}

void testBroadcastReachesEveryoneButTheSender() {
  Radio a;
  Radio b;
  Radio c;
  CHECK(a.begin());
  CHECK(b.begin());
  CHECK(c.begin());

  uint8_t buffer[kMaxPacketBytes];
  Address from;
  size_t length = 0;

  const uint8_t message[3] = {'h', 'i', '!'};
  CHECK(a.send(kBroadcast, message, sizeof(message)));

  CHECK(receiveWithin(b, from, buffer, sizeof(buffer), length));
  CHECK(from == a.localAddress());
  CHECK(length == sizeof(message));
  CHECK(receiveWithin(c, from, buffer, sizeof(buffer), length));
  CHECK(from == a.localAddress());

  // A radio that heard its own broadcast would make a device discover itself,
  // which is the one way discovery can go badly wrong on its own.
  settle();
  CHECK(!a.receive(from, buffer, sizeof(buffer), length));

  a.end();
  b.end();
  c.end();
}

void testOversizeIsRefusedNotTruncated() {
  Radio a;
  Radio b;
  CHECK(a.begin());
  CHECK(b.begin());

  uint8_t buffer[kMaxPacketBytes];
  Address from;
  size_t length = 0;

  uint8_t big[kMaxPacketBytes + 1] = {};
  CHECK(!a.send(b.localAddress(), big, sizeof(big)));
  settle();
  CHECK(!b.receive(from, buffer, sizeof(buffer), length));

  // The largest legal packet still goes through whole.
  CHECK(a.send(b.localAddress(), big, kMaxPacketBytes));
  CHECK(receiveWithin(b, from, buffer, sizeof(buffer), length));
  CHECK(length == kMaxPacketBytes);

  a.end();
  b.end();
}

// Drives two sessions for `steps` loop passes with real time passing, so the
// datagrams actually have somewhere to be delivered in between.
void pump(Session& a, Session& b, uint32_t& nowMs, const int steps) {
  for (int step = 0; step < steps; ++step) {
    nowMs += 10;
    a.tick(nowMs);
    b.tick(nowMs);
    ::usleep(1000);
  }
}

void testAMatchOverRealSockets() {
  // The claim under test: start two instances, and they find each other and
  // play with nothing configured on either side.
  Radio radioA;
  Radio radioB;
  CHECK(radioA.begin());
  CHECK(radioB.begin());

  Session a;
  Session b;
  uint32_t nowMs = 0;
  a.begin(radioA, kGameId, "ALICE", nowMs);
  b.begin(radioB, kGameId, "BOB", nowMs);

  pump(a, b, nowMs, 300);
  CHECK(a.state() == Session::State::Playing);
  CHECK(b.state() == Session::State::Playing);
  CHECK(strcmp(a.peerName(), "BOB") == 0);
  CHECK(strcmp(b.peerName(), "ALICE") == 0);
  CHECK(a.isHost() != b.isHost());
  CHECK(a.myTurn() != b.myTurn());

  uint8_t boardA[8] = {};
  uint8_t boardB[8] = {};
  uint8_t lengthA = 0;
  uint8_t lengthB = 0;
  int moves = 0;

  for (int step = 0; step < 400 && moves < 6; ++step) {
    nowMs += 10;
    a.tick(nowMs);
    b.tick(nowMs);

    uint8_t got = 0;
    if (a.takeIncoming(boardA, sizeof(boardA), got)) lengthA = got;
    if (b.takeIncoming(boardB, sizeof(boardB), got)) lengthB = got;

    if (a.myTurn() && lengthA < sizeof(boardA)) {
      boardA[lengthA++] = 'A';
      CHECK(a.submit(boardA, lengthA));
      moves++;
    } else if (b.myTurn() && lengthB < sizeof(boardB)) {
      boardB[lengthB++] = 'B';
      CHECK(b.submit(boardB, lengthB));
      moves++;
    }
    ::usleep(1000);
  }

  CHECK(moves == 6);
  const uint8_t shared = lengthA < lengthB ? lengthA : lengthB;
  CHECK(shared > 0);
  CHECK(memcmp(boardA, boardB, shared) == 0);
  // Both sides moved, and they alternated.
  bool alternating = true;
  for (uint8_t i = 1; i < shared; ++i)
    if (boardA[i] == boardA[i - 1]) alternating = false;
  CHECK(alternating);

  // And leaving is seen by the other side, well inside one screen refresh.
  b.leave();
  pump(a, b, nowMs, 30);
  CHECK(a.state() == Session::State::Ended);
  CHECK(a.endReason() == Session::EndReason::PeerLeft);

  radioA.end();
  radioB.end();
}

void testGamesDoNotCrossOver() {
  // Two instances in different apps must not pair, even though they share the
  // one channel and hear every one of each other's broadcasts.
  Radio radioA;
  Radio radioB;
  CHECK(radioA.begin());
  CHECK(radioB.begin());

  Session a;
  Session b;
  uint32_t nowMs = 0;
  a.begin(radioA, kGameId, "A", nowMs);
  b.begin(radioB, kGameId + 1, "B", nowMs);
  pump(a, b, nowMs, 200);

  CHECK(a.state() == Session::State::Searching);
  CHECK(b.state() == Session::State::Searching);

  radioA.end();
  radioB.end();
}

}  // namespace

int main() {
  testTwoRadiosComeUpDistinct();
  testUnicastReachesOnlyItsTarget();
  testBroadcastReachesEveryoneButTheSender();
  testOversizeIsRefusedNotTruncated();
  testAMatchOverRealSockets();
  testGamesDoNotCrossOver();

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
