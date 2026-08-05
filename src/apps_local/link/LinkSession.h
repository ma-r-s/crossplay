#pragma once

// A two-player match over a local radio, with no server, no router, no lobby
// and nothing for the user to configure. Tap MULTIPLAYER, and the next thing
// you see is a board.
//
// Freestanding: no Arduino, no renderer, no storage. Time arrives as an
// argument rather than being read from millis(), so host-tests/link/ can play
// thousands of matches deterministically.
//
// ---------------------------------------------------------------------------
// Three decisions carry the whole design.
//
// 1. THE PAYLOAD IS THE WHOLE SHARED STATE, NOT THE MOVE.
//    A chess position is ~40 bytes and a connect-four grid is 11, so the entire
//    game fits in one packet with room to spare. Once you send whole states,
//    the two devices cannot disagree: there is no move log to replay, no
//    ordering to get right, and a lost packet is a stale frame that the next
//    one corrects rather than a desync. Nintendo could not do this at 60fps.
//    Board games are small enough that we get it for free.
//
//    "Shared" is the game's call. Battleship keeps each player's ships local
//    and shares only the hit grid; the session moves an opaque blob and does
//    not care.
//
// 2. TURNS STRICTLY ALTERNATE, AND THE HOST GOES FIRST.
//    Only the side whose turn it is produces a new state, so a single sequence
//    number is enough and two writers can never race. Host is elected by lower
//    address, which both sides compute identically with no negotiation, which
//    is why the user never sees the word "host".
//
// 3. A MATCH LIVES ONLY WHILE BOTH APPS ARE OPEN.
//    Leaving the app or locking the device ends it, and the other side is told
//    immediately by a Bye rather than waiting out the silence timeout. There is
//    no resume, no reconnect, and nothing is written to the card.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>

#include "LinkProtocol.h"
#include "LinkTransport.h"

namespace linkplay {

// Discovery broadcasts this often. Searching is bounded by the user's patience,
// so it is deliberately chatty: the radio cost only exists while the SEARCHING
// screen is up.
constexpr uint32_t kHelloIntervalMs = 400;
constexpr uint32_t kJoinIntervalMs = 200;
// While matched, a side with nothing to say re-acks. That is the liveness
// signal, and it is what the peer timeout is measured against.
constexpr uint32_t kHeartbeatIntervalMs = 1000;
// An unacknowledged move is resent this often. The panel takes ~500ms to
// repaint, so a retry that lands inside that window is invisible.
constexpr uint32_t kRetryIntervalMs = 400;
// Silence this long means gone: flat battery, out of range, or a crash. Long
// enough to survive a few missed windows in a room full of WiFi, short enough
// to still read as immediate.
constexpr uint32_t kPeerTimeoutMs = 10000;
// A discovery candidate we have stopped hearing is dropped this fast, so a
// device that pairs with somebody else does not pin us to it.
constexpr uint32_t kCandidateForgetMs = 3000;

class Session {
 public:
  enum class State : uint8_t {
    Idle,       // begin() has not been called, or the session was reset
    Searching,  // broadcasting and listening; nothing found yet
    Playing,    // matched, exchanging states
    Ended,      // over; endReason() says why
  };

  enum class EndReason : uint8_t {
    None,
    PeerLeft,   // they closed the app or locked the device (we got a Bye)
    PeerLost,   // they went silent: battery, range, or a crash
    LocalLeft,  // we left
  };

  // `localName` is what the other device shows while searching and after a
  // disconnect. Copied, truncated to kMaxNameBytes; nullptr is allowed.
  void begin(Transport& transport, uint16_t gameId, const char* localName, uint32_t nowMs);

  // Tells the peer we are going, then ends locally. Safe to call in any state.
  void leave();

  // Drops everything without telling anyone. For teardown paths where the radio
  // is already gone; prefer leave().
  void reset();

  // Pump. Drains the receive queue, then does the time-based sending. Call it
  // every loop pass; it is cheap when there is nothing to do.
  void tick(uint32_t nowMs);

  State state() const { return state_; }
  EndReason endReason() const { return endReason_; }
  // Which side moves first, decided by a coin toss both devices can compute.
  // Never shown to the user; the word "host" does not appear on any screen.
  bool isHost() const { return isHost_; }
  const char* peerName() const { return peerName_; }
  const Address& peerAddress() const { return peer_; }

  // True when this device may submit(). Set at match time from isHost(), then
  // flipped by submit() and by adopting a peer state.
  bool myTurn() const { return state_ == State::Playing && myTurn_; }

  // Hands over the new shared state after a local move. Fails (returns false,
  // changes nothing) if it is not our turn, we are not playing, or the payload
  // does not fit. Retransmits until acknowledged.
  //
  // A game whose opening position is not derivable on both sides (a random
  // deal, say) would need a way to send state without passing the turn. Nothing
  // we play needs it, so it does not exist.
  bool submit(const uint8_t* payload, uint8_t length);

  // Sends a short note that does NOT touch the turn. Either side may call this
  // at any point in a match, and the newest note wins. Retransmitted until
  // acknowledged, so it is as reliable as a move.
  //
  // This exists because a rematch is a conversation ("shall we?" / "yes") that
  // happens exactly when the game has ended and there is no turn left to carry
  // it. Without it a game has to fake the exchange through the board, which is
  // how "one player started a new game and the other sat waiting" happened.
  bool announce(const uint8_t* payload, uint8_t length);

  // True when a note from the peer is waiting.
  bool hasAnnouncement() const { return incomingSayPending_; }

  // Copies the waiting note out and clears it, exactly once per note.
  bool takeAnnouncement(uint8_t* out, uint8_t capacity, uint8_t& length);

  // True when a peer state is waiting to be applied.
  bool hasIncoming() const { return incomingPending_; }

  // Copies the waiting peer state out and clears it. Returns false if nothing
  // is waiting or `capacity` is too small (in which case the state stays
  // pending rather than being lost).
  bool takeIncoming(uint8_t* out, uint8_t capacity, uint8_t& length);

 private:
  void handlePacket(const Address& from, const PacketView& packet);
  void matchWith(const Address& peer, uint8_t peerToss, const uint8_t* name, uint8_t nameLength);
  void end(EndReason reason);
  bool transmit(const Address& to, PacketType type, uint16_t sequence, const uint8_t* payload, uint8_t payloadLength);
  void sendHello();
  void sendJoin();
  void sendState();
  void sendSay();
  void sendSayAck();
  void sendAck();
  void setPeerName(const uint8_t* name, uint8_t length);

  Transport* transport_ = nullptr;
  uint16_t gameId_ = 0;
  State state_ = State::Idle;
  EndReason endReason_ = EndReason::None;
  bool isHost_ = false;
  bool myTurn_ = false;
  // Set once the peer has sent something only a matched device sends (an Ack or
  // a State). Until then they may still be finishing their own handshake, and a
  // Hello from them means "not matched yet" rather than "left".
  bool peerConfirmed_ = false;

  // This device's coin toss, fixed for one trip through the SEARCHING screen and
  // rolled again on the next. Lower toss moves first, ties broken by address.
  //
  // Without it, "lower address moves first" over two fixed MACs meant the same
  // device played White in every game of chess ever played on the pair. The DS
  // never had that problem because its host was whoever tapped first, and a coin
  // toss is the same answer with nothing for the user to do.
  uint8_t toss_ = 0;

  Address peer_ = {};
  // While searching: the lowest-addressed device we can currently hear. One
  // slot rather than a table -- we only ever want the lowest, and everyone
  // picking the lowest is what makes three devices resolve without a cycle.
  Address candidate_ = {};
  bool hasCandidate_ = false;
  uint32_t candidateHeardMs_ = 0;

  char localName_[kMaxNameBytes + 1] = {};
  char peerName_[kMaxNameBytes + 1] = {};

  // The single shared move counter. Only the side whose turn it is increments
  // it, so it cannot race.
  uint16_t sequence_ = 0;

  uint8_t outgoing_[kMaxPayloadBytes] = {};
  uint8_t outgoingLength_ = 0;
  uint16_t outgoingSequence_ = 0;
  bool outgoingPending_ = false;  // sent, not yet acknowledged

  uint8_t incoming_[kMaxPayloadBytes] = {};
  uint8_t incomingLength_ = 0;
  bool incomingPending_ = false;

  // Notes run on their own sequence, entirely beside the move counter: either
  // side may raise one at any moment, so the two cannot share a number.
  uint8_t outgoingSay_[kMaxSayBytes] = {};
  uint8_t outgoingSayLength_ = 0;
  uint16_t outgoingSaySequence_ = 0;
  bool outgoingSayPending_ = false;
  uint32_t lastSayMs_ = 0;
  uint8_t incomingSay_[kMaxSayBytes] = {};
  uint8_t incomingSayLength_ = 0;
  bool incomingSayPending_ = false;
  uint16_t peerSaySequence_ = 0;

  uint32_t nowMs_ = 0;
  uint32_t lastHelloMs_ = 0;
  uint32_t lastJoinMs_ = 0;
  // Anything we put on the air. This is the heartbeat's clock: a packet of any
  // kind is proof of life to the peer, so sending one defers the next Ack.
  uint32_t lastSentMs_ = 0;
  // Our unacknowledged state specifically. The retry has to have its own clock:
  // keyed off lastSentMs_ it was starved by our own replies, because acking an
  // inbound packet counts as "sent recently" and pushed the resend out. Every
  // tick that acks something would defer the one packet that makes progress.
  uint32_t lastStateMs_ = 0;
  uint32_t lastHeardMs_ = 0;
};

}  // namespace linkplay
