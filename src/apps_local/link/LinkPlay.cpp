#include "LinkPlay.h"

#include <cstring>

// Same hook as LinkRadio.h: the host tests build this file with no SDK, so the
// log macros have to disappear rather than fail to link.
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
#include <Logging.h>
#else
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#endif

namespace linkplay {
namespace {

// Long enough that a game taking delivery on its next loop pass never trips it,
// short enough to still be in the log while the player is looking at the screen
// wondering why the board stopped.
constexpr uint32_t kUndrainedComplaintMs = 3000;

PlayBase::Ending toEnding(const Session::EndReason reason) {
  switch (reason) {
    case Session::EndReason::PeerLeft:
      return PlayBase::Ending::OpponentLeft;
    case Session::EndReason::PeerLost:
      return PlayBase::Ending::OpponentLost;
    case Session::EndReason::LocalLeft:
      return PlayBase::Ending::YouLeft;
    case Session::EndReason::None:
      break;
  }
  return PlayBase::Ending::None;
}

}  // namespace

Transport& PlayBase::link() {
  // Two statements, not a conditional expression: cppcheck reads the ternary
  // over two lvalue types as a temporary and flags a dangling reference.
  if (testLink_ != nullptr) return *testLink_;
  return radio_;
}

bool PlayBase::start(const GameId gameId, const char* yourName) {
  stop();
  if (testLink_ == nullptr && !radio_.begin()) {
    // The one failure a game has to surface. Everything else this layer handles.
    LOG_ERR("LINK", "radio would not start");
    return false;
  }

  gameId_ = static_cast<uint16_t>(gameId);
  const size_t length = yourName == nullptr ? 0 : strnlen(yourName, kMaxNameBytes);
  if (length > 0) {
    memcpy(name_, yourName, length);
    name_[length] = '\0';
  } else {
    // No name is the layer's problem, not the game's. Two devices showing the
    // same "PLAYER" would make "MARIO LEFT" useless, so one is derived from the
    // radio's own address -- stable per device, different between any two.
    const Address& self = link().localAddress();
    static const char kDigits[] = "0123456789ABCDEF";
    const char stamp[4] = {kDigits[(self.bytes[4] >> 4) & 0x0F], kDigits[self.bytes[4] & 0x0F],
                           kDigits[(self.bytes[5] >> 4) & 0x0F], kDigits[self.bytes[5] & 0x0F]};
    memcpy(name_, "PLAYER ", 7);
    memcpy(name_ + 7, stamp, sizeof(stamp));
    name_[11] = '\0';
  }

  started_ = true;
  searchPending_ = true;
  ending_ = Ending::None;
  matchStartPending_ = false;
  undrainedSinceMs_ = 0;
  warnedUndrained_ = false;
  phase_ = Phase::Searching;
  LOG_INF("LINK", "searching for game %04X as '%s'", gameId_, name_);
  return true;
}

void PlayBase::stop() {
  if (!started_) return;
  // Tells the opponent at once rather than leaving them to time out. Harmless
  // while still searching, where there is nobody to tell.
  session_.leave();
  session_.reset();
  if (testLink_ == nullptr) radio_.end();
  started_ = false;
  searchPending_ = false;
  // Only claim we left if nothing else had already ended it. stop() also runs
  // from the destructor, long after the player has read why the match ended,
  // and overwriting the reason there would rewrite what they were just shown.
  if (ending_ == Ending::None) ending_ = Ending::YouLeft;
  phase_ = Phase::Off;
  LOG_INF("LINK", "stopped");
}

PlayBase::Phase PlayBase::update(const uint32_t nowMs) {
  if (!started_) return phase_;

  if (searchPending_) {
    searchPending_ = false;
    session_.begin(link(), gameId_, name_, nowMs);
  }

  const bool wasPlaying = session_.state() == Session::State::Playing;
  session_.tick(nowMs);

  if (!wasPlaying && session_.state() == Session::State::Playing) {
    matchStartPending_ = true;
    undrainedSinceMs_ = 0;
    warnedUndrained_ = false;
    LOG_INF("LINK", "matched with '%s', %s move first", session_.peerName(), session_.isHost() ? "we" : "they");
  }

  switch (session_.state()) {
    case Session::State::Idle:
      phase_ = Phase::Off;
      break;
    case Session::State::Searching:
      phase_ = Phase::Searching;
      break;
    case Session::State::Playing:
      // The load-bearing line. YourTurn structurally implies their last move is
      // already on your board, so a game cannot compute one from a stale
      // position -- which used to be possible, cost nothing to write, and
      // produced two devices agreeing on a game where a move never happened.
      phase_ = session_.myTurn() && !session_.hasIncoming() ? Phase::YourTurn : Phase::TheirTurn;
      break;
    case Session::State::Ended:
      if (phase_ != Phase::Over) LOG_INF("LINK", "match over (%d)", static_cast<int>(session_.endReason()));
      phase_ = Phase::Over;
      ending_ = toEnding(session_.endReason());
      break;
  }

  // A game that never takes delivery parks here rather than desyncing, which is
  // the right failure but an invisible one. Say so once.
  if (session_.hasIncoming()) {
    if (undrainedSinceMs_ == 0) undrainedSinceMs_ = nowMs;
    if (!warnedUndrained_ && nowMs - undrainedSinceMs_ >= kUndrainedComplaintMs) {
      warnedUndrained_ = true;
      LOG_ERR("LINK", "their move has been waiting %ums, is takeOpponent() being called?",
              static_cast<unsigned>(nowMs - undrainedSinceMs_));
    }
  }

  return phase_;
}

bool PlayBase::takeMatchStart() {
  if (!matchStartPending_) return false;
  matchStartPending_ = false;
  return true;
}

bool PlayBase::say(const uint8_t note) { return session_.announce(&note, 1); }

bool PlayBase::heard(uint8_t& note) {
  uint8_t length = 0;
  return session_.takeAnnouncement(&note, 1, length) && length == 1;
}

bool PlayBase::playRaw(const uint8_t* state, const uint8_t length) {
  // Deliberately redundant, and it cannot be made to fail a test: Session's own
  // guards are a strict superset of this one over every way phase_ can be set
  // (Off/Searching/Over are not Playing, TheirTurn is either !myTurn_ or an
  // undrained state, and the flip below leaves myTurn_ false). It stays so this
  // layer's contract is readable without reading the one underneath it.
  if (phase_ != Phase::YourTurn) return false;
  if (!session_.submit(state, length)) return false;
  // Flip now rather than at the next update(), so a game that draws straight
  // after moving cannot show its own turn for one frame.
  phase_ = Phase::TheirTurn;
  return true;
}

bool PlayBase::takeRaw(uint8_t* out, const uint8_t capacity, uint8_t& length) {
  if (!session_.takeIncoming(out, capacity, length)) return false;
  undrainedSinceMs_ = 0;
  warnedUndrained_ = false;
  // Their move is on the board now, so the turn can become ours in this same
  // loop pass rather than the next one.
  if (session_.state() == Session::State::Playing && session_.myTurn()) phase_ = Phase::YourTurn;
  return true;
}

}  // namespace linkplay
