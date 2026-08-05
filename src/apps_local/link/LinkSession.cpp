#include "LinkSession.h"

#include <cstring>

namespace linkplay {
namespace {

// A tick drains at most this many packets. The device transport queues four, so
// this is not a throughput limit; it is a backstop against a transport that
// hands back packets forever and pins the main loop.
constexpr int kMaxPacketsPerTick = 16;

// Bye is sent unreliably, so it goes out a few times. It is only an optimisation
// -- the peer's silence timeout covers the case where every copy is lost -- but
// it is the difference between "Mario left" appearing at once and ten seconds
// later.
constexpr int kByeRepeats = 3;

size_t nameLength(const char* name) {
  if (name == nullptr) return 0;
  const size_t length = strlen(name);
  return length < kMaxNameBytes ? length : kMaxNameBytes;
}

// The coin toss that decides who moves first. Mixed from the clock so it comes
// out differently every time somebody taps MULTIPLAYER, and from the address so
// two devices tapping in the same millisecond still disagree. Neither input is
// random, but the pair is arbitrary enough for a coin toss, which is all this
// has to be -- and taking no seed keeps a device-only concept out of begin().
uint8_t coinToss(const uint32_t nowMs, const Address& local) {
  uint32_t hash = nowMs * 2654435761u;                                      // Knuth's multiplicative
  for (const uint8_t byte : local.bytes) hash = (hash ^ byte) * 16777619u;  // FNV-1a's prime
  return static_cast<uint8_t>(hash ^ (hash >> 8) ^ (hash >> 16) ^ (hash >> 24));
}

}  // namespace

void Session::begin(Transport& transport, const uint16_t gameId, const char* localName, const uint32_t nowMs) {
  reset();
  transport_ = &transport;
  gameId_ = gameId;
  nowMs_ = nowMs;
  toss_ = coinToss(nowMs, transport.localAddress());

  const size_t length = nameLength(localName);
  if (length > 0) memcpy(localName_, localName, length);
  localName_[length] = '\0';

  state_ = State::Searching;
  lastHeardMs_ = nowMs;
  lastSentMs_ = nowMs;
  lastStateMs_ = nowMs;
  lastJoinMs_ = nowMs;
  // Announce at once rather than waiting out the first interval: the user is
  // looking at a SEARCHING screen and every ms of it is dead time.
  sendHello();
}

void Session::reset() {
  transport_ = nullptr;
  gameId_ = 0;
  state_ = State::Idle;
  endReason_ = EndReason::None;
  isHost_ = false;
  myTurn_ = false;
  peerConfirmed_ = false;
  toss_ = 0;
  peer_ = Address{};
  candidate_ = Address{};
  hasCandidate_ = false;
  candidateHeardMs_ = 0;
  localName_[0] = '\0';
  peerName_[0] = '\0';
  sequence_ = 0;
  outgoingLength_ = 0;
  outgoingSequence_ = 0;
  outgoingPending_ = false;
  incomingLength_ = 0;
  incomingPending_ = false;
  outgoingSayLength_ = 0;
  outgoingSaySequence_ = 0;
  outgoingSayPending_ = false;
  lastSayMs_ = 0;
  incomingSayLength_ = 0;
  incomingSayPending_ = false;
  peerSaySequence_ = 0;
  nowMs_ = 0;
  lastHelloMs_ = 0;
  lastJoinMs_ = 0;
  lastSentMs_ = 0;
  lastStateMs_ = 0;
  lastHeardMs_ = 0;
}

void Session::leave() {
  if (state_ == State::Idle) return;
  // Already over. Leaving again must not rewrite why: the Activity calls this
  // on its way out, which is exactly after the "they left" screen has been up,
  // and that screen's reason is the one worth keeping.
  if (state_ == State::Ended) return;
  if (state_ == State::Playing) {
    const uint8_t reason = static_cast<uint8_t>(ByeReason::LeftApp);
    for (int i = 0; i < kByeRepeats; ++i) transmit(peer_, PacketType::Bye, sequence_, &reason, 1);
  }
  end(EndReason::LocalLeft);
}

void Session::end(const EndReason reason) {
  state_ = State::Ended;
  endReason_ = reason;
  myTurn_ = false;
  outgoingPending_ = false;
  hasCandidate_ = false;
}

void Session::tick(const uint32_t nowMs) {
  nowMs_ = nowMs;
  if (transport_ == nullptr) return;
  if (state_ != State::Searching && state_ != State::Playing) return;

  Address from;
  uint8_t buffer[kMaxPacketBytes];
  size_t length = 0;
  for (int i = 0; i < kMaxPacketsPerTick; ++i) {
    if (!transport_->receive(from, buffer, sizeof(buffer), length)) break;
    PacketView packet;
    if (!decode(buffer, length, packet)) continue;
    if (packet.gameId != gameId_) continue;
    if (from == transport_->localAddress()) continue;  // our own broadcast, reflected
    handlePacket(from, packet);
    if (state_ != State::Searching && state_ != State::Playing) return;
  }

  if (state_ == State::Searching) {
    if (hasCandidate_ && nowMs_ - candidateHeardMs_ >= kCandidateForgetMs) hasCandidate_ = false;
    if (nowMs_ - lastHelloMs_ >= kHelloIntervalMs) sendHello();
    if (hasCandidate_ && nowMs_ - lastJoinMs_ >= kJoinIntervalMs) sendJoin();
    return;
  }

  if (nowMs_ - lastHeardMs_ >= kPeerTimeoutMs) {
    end(EndReason::PeerLost);
    return;
  }
  if (outgoingSayPending_ && nowMs_ - lastSayMs_ >= kRetryIntervalMs) sendSay();

  if (outgoingPending_ && nowMs_ - lastStateMs_ >= kRetryIntervalMs) {
    sendState();
  } else if (nowMs_ - lastSentMs_ >= kHeartbeatIntervalMs) {
    sendAck();
  }
}

void Session::handlePacket(const Address& from, const PacketView& packet) {
  if (state_ == State::Searching) {
    // Both a Hello and a Join are proof the sender is here and wants this game,
    // so both make it a candidate. Taking the Join as evidence matters: with
    // asymmetric loss we may never hear their Hello, and without this the two
    // devices stall until one gets through.
    if (packet.type == PacketType::Hello || packet.type == PacketType::Join) {
      // Lowest address wins, and everyone applying that same rule is what stops
      // three devices from forming a pairing cycle.
      if (!hasCandidate_ || from < candidate_) {
        candidate_ = from;
        hasCandidate_ = true;
        candidateHeardMs_ = nowMs_;
        // Hello and Join both carry the sender's name. Keep it now: a match
        // completed by an Ack carries no name, and "waiting for" with a blank
        // where the opponent should be is a visible defect.
        setPeerName(packet.payload, packet.payloadLength);
      } else if (from == candidate_) {
        candidateHeardMs_ = nowMs_;
      }
    }
    // A Join from the device we picked is mutual agreement: they picked us too.
    if (packet.type == PacketType::Join && hasCandidate_ && from == candidate_) {
      matchWith(from, packet.toss, packet.payload, packet.payloadLength);
      return;
    }

    // An Ack or a State is only ever unicast at a peer, so hearing one means
    // the sender has already matched with us. We are unmatched, so accepting is
    // always right and always the fastest route to both sides agreeing --
    // whoever they are, and whether or not we had picked them yet.
    //
    // The first version replied with a Bye when the sender was not our
    // candidate, to heal a one-sided match faster. That was wrong: under loss
    // the peer's Join is exactly what goes missing, so the common case is a
    // partner who matched first while we had no candidate yet, and the Bye tore
    // down the match we were trying to make.
    if (packet.type == PacketType::Ack || packet.type == PacketType::State || packet.type == PacketType::Say ||
        packet.type == PacketType::SayAck) {
      matchWith(from, packet.toss, nullptr, 0);
    }
    return;
  }

  if (from != peer_) return;  // matched: everyone else is somebody else's game
  lastHeardMs_ = nowMs_;

  switch (packet.type) {
    case PacketType::Join:
      // They are still searching, which means our Join never landed. Answer it,
      // or they will sit on the SEARCHING screen while we think we are playing.
      //
      // Both guards are load-bearing, and the first version had neither.
      //
      // A confirmed peer has sent us an Ack or a State, so it is matched with us
      // and its Join can only be our own answer coming back at us. Answering
      // that is a loop with no exit: two matched devices reflect Joins off each
      // other for the rest of the match, and on a link that duplicates, one Join
      // becomes two, two become four. Measured on the soak, that was ~70000
      // Joins from each side in a 40s match: the receive budget pinned, and the
      // state retransmits buried under seconds of backlog. It looked exactly
      // like a protocol deadlock. (If they really did restart they are
      // broadcasting Hellos too, and the Hello case below ends the match, which
      // is the answer they need rather than another Join.)
      //
      // The rate limit covers the window before either side is confirmed, which
      // is where that fire used to start: a duplicated burst arriving at once
      // must still cost one reply, not one per copy. We never put Joins on the
      // air faster than a searching device does.
      if (!peerConfirmed_ && nowMs_ - lastJoinMs_ >= kJoinIntervalMs) sendJoin();
      break;

    case PacketType::State:
      // Strict alternation means a gap is impossible, but accepting one costs
      // nothing and is safe precisely because this is the whole state, not a
      // delta: a newer state is always complete on its own.
      if (packet.sequence > sequence_) {
        incomingLength_ = packet.payloadLength;
        if (packet.payloadLength > 0) memcpy(incoming_, packet.payload, packet.payloadLength);
        incomingPending_ = true;
        sequence_ = packet.sequence;
        myTurn_ = true;
        outgoingPending_ = false;  // they moved, so ours plainly arrived
      }
      // Ack every copy, including retransmits: an unacknowledged retransmit is
      // how a sender gets stuck resending forever.
      peerConfirmed_ = true;
      sendAck();
      break;

    case PacketType::Ack:
      // Only a matched device sends these, so this is the proof that the match
      // is real on both sides -- which is what makes a later Hello meaningful.
      peerConfirmed_ = true;
      if (outgoingPending_ && packet.sequence >= outgoingSequence_) outgoingPending_ = false;
      break;

    case PacketType::Say:
      // Its own sequence, beside the move counter: either side may raise a note
      // at any moment, so the two cannot share a number. Newest wins.
      if (packet.sequence > peerSaySequence_) {
        incomingSayLength_ = packet.payloadLength;
        if (packet.payloadLength > 0) memcpy(incomingSay_, packet.payload, packet.payloadLength);
        incomingSayPending_ = true;
        peerSaySequence_ = packet.sequence;
      }
      // Acked every copy, retransmits included, for the same reason a State is:
      // an unacknowledged retransmit is how a sender gets stuck resending.
      peerConfirmed_ = true;
      sendSayAck();
      break;

    case PacketType::SayAck:
      peerConfirmed_ = true;
      if (outgoingSayPending_ && packet.sequence >= outgoingSaySequence_) outgoingSayPending_ = false;
      break;

    case PacketType::Bye:
      end(EndReason::PeerLeft);
      break;

    case PacketType::Hello:
      // A Hello is broadcast only by a device that is NOT in a match, so one
      // from a peer we have already exchanged real traffic with is proof they
      // have left and gone back to looking -- the opposite of a heartbeat.
      //
      // This was the worst bug in the first version. The generic "any packet
      // from the peer refreshes liveness" above fired first, so a peer that
      // left and re-entered pinned us in Playing forever, on a perfect link,
      // with its own discovery traffic.
      //
      // The confirmation guard matters just as much. Without it this ends
      // matches during their own handshake: there is a window where we are
      // Playing and they are still Searching, and their Hellos are still going
      // out. The first fix for the wedge above had exactly that regression and
      // broke matching under loss entirely.
      if (peerConfirmed_) end(EndReason::PeerLeft);
      break;
  }
}

void Session::matchWith(const Address& peer, const uint8_t peerToss, const uint8_t* name, const uint8_t nameBytes) {
  peer_ = peer;
  setPeerName(name, nameBytes);
  // Both sides compute this from the same two tosses and reach the same answer,
  // so there is nothing to negotiate and nothing to show the user. The toss
  // rides in every packet precisely so this works whichever one sealed the
  // match, and the address only breaks a tie -- if it decided outright, the
  // lower-addressed device would move first in every game forever.
  //
  // Note this is a different question from which device we pair with, above,
  // where lowest address must win so that three devices cannot form a cycle.
  isHost_ = toss_ != peerToss ? toss_ < peerToss : transport_->localAddress() < peer_;
  myTurn_ = isHost_;
  peerConfirmed_ = false;
  sequence_ = 0;
  outgoingLength_ = 0;
  outgoingSequence_ = 0;
  outgoingPending_ = false;
  incomingLength_ = 0;
  incomingPending_ = false;
  outgoingSayLength_ = 0;
  outgoingSaySequence_ = 0;
  outgoingSayPending_ = false;
  incomingSayLength_ = 0;
  incomingSayPending_ = false;
  peerSaySequence_ = 0;
  hasCandidate_ = false;
  state_ = State::Playing;
  lastHeardMs_ = nowMs_;
  sendAck();
}

void Session::setPeerName(const uint8_t* name, const uint8_t length) {
  // No name supplied means "keep what we already learned", not "blank it".
  if (name == nullptr || length == 0) return;
  const uint8_t clamped = length < kMaxNameBytes ? length : static_cast<uint8_t>(kMaxNameBytes);
  memcpy(peerName_, name, clamped);
  peerName_[clamped] = '\0';
}

bool Session::submit(const uint8_t* payload, const uint8_t length) {
  if (state_ != State::Playing || !myTurn_) return false;
  // Refuse while the opponent's move is still sitting undrained. Adopting a peer
  // state raises incomingPending_ and myTurn_ in the same breath, so a game that
  // checks the turn before taking delivery -- which reads perfectly naturally --
  // would build on a board one move stale. The peer then adopts what we send,
  // and both devices come to agree on a position in which their move never
  // happened. Agreement is precisely what a divergence check looks for, so no
  // assertion in the suite could have caught it; only refusing here can.
  if (incomingPending_) return false;
  if (length > kMaxPayloadBytes) return false;
  if (length > 0 && payload == nullptr) return false;

  if (length > 0) memcpy(outgoing_, payload, length);
  outgoingLength_ = length;
  sequence_ = static_cast<uint16_t>(sequence_ + 1);
  outgoingSequence_ = sequence_;
  outgoingPending_ = true;
  myTurn_ = false;
  sendState();
  return true;
}

bool Session::announce(const uint8_t* payload, const uint8_t length) {
  // No turn check, and that is the whole point: a note is what you send when
  // there is no turn to be had. Only the match has to be live.
  if (state_ != State::Playing) return false;
  if (length == 0 || length > kMaxSayBytes) return false;
  if (payload == nullptr) return false;

  memcpy(outgoingSay_, payload, length);
  outgoingSayLength_ = length;
  outgoingSaySequence_ = static_cast<uint16_t>(outgoingSaySequence_ + 1);
  outgoingSayPending_ = true;
  sendSay();
  return true;
}

bool Session::takeAnnouncement(uint8_t* out, const uint8_t capacity, uint8_t& length) {
  if (!incomingSayPending_) return false;
  if (out == nullptr || capacity < incomingSayLength_) return false;
  if (incomingSayLength_ > 0) memcpy(out, incomingSay_, incomingSayLength_);
  length = incomingSayLength_;
  incomingSayPending_ = false;
  return true;
}

bool Session::takeIncoming(uint8_t* out, const uint8_t capacity, uint8_t& length) {
  if (!incomingPending_) return false;
  if (out == nullptr || capacity < incomingLength_) return false;
  if (incomingLength_ > 0) memcpy(out, incoming_, incomingLength_);
  length = incomingLength_;
  incomingPending_ = false;
  return true;
}

bool Session::transmit(const Address& to, const PacketType type, const uint16_t sequence, const uint8_t* payload,
                       const uint8_t payloadLength) {
  if (transport_ == nullptr) return false;
  uint8_t buffer[kMaxPacketBytes];
  const size_t length = encode(buffer, sizeof(buffer), type, gameId_, sequence, toss_, payload, payloadLength);
  if (length == 0) return false;
  lastSentMs_ = nowMs_;
  return transport_->send(to, buffer, length);
}

void Session::sendHello() {
  lastHelloMs_ = nowMs_;
  const uint8_t length = static_cast<uint8_t>(strlen(localName_));
  transmit(kBroadcast, PacketType::Hello, 0, reinterpret_cast<const uint8_t*>(localName_), length);
}

void Session::sendJoin() {
  lastJoinMs_ = nowMs_;
  const Address& to = state_ == State::Playing ? peer_ : candidate_;
  const uint8_t length = static_cast<uint8_t>(strlen(localName_));
  transmit(to, PacketType::Join, 0, reinterpret_cast<const uint8_t*>(localName_), length);
}

void Session::sendState() {
  lastStateMs_ = nowMs_;
  transmit(peer_, PacketType::State, outgoingSequence_, outgoing_, outgoingLength_);
}

void Session::sendSay() {
  lastSayMs_ = nowMs_;
  transmit(peer_, PacketType::Say, outgoingSaySequence_, outgoingSay_, outgoingSayLength_);
}

void Session::sendSayAck() { transmit(peer_, PacketType::SayAck, peerSaySequence_, nullptr, 0); }

void Session::sendAck() { transmit(peer_, PacketType::Ack, sequence_, nullptr, 0); }

}  // namespace linkplay
