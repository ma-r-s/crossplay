#pragma once

// Everything a game needs to become multiplayer, and nothing else.
//
// This is the layer the whole exercise is about. LinkSession is a good radio
// protocol and a bad thing to hand a game author: it offers a raw address, the
// word "host", six protocol timing constants and the entire wire format, and it
// leaves the searching screen, the disconnect dialog, the sleep suppression and
// the receive/submit ordering as five separate things you can get wrong. The DS
// did not work that way. Every DS game connected identically because no game was
// allowed to write that part, and this file is the local equivalent.
//
// What a game writes:
//
//   struct Board { ... };                  // trivially copyable, <= 192 bytes
//   linkplay::Play<Board> play_;
//
//   play_.start(linkplay::GameId::Chess, "MARIO");
//
//   switch (play_.update(millis())) {
//     case Phase::Searching:  break;                        // chrome draws it
//     case Phase::TheirTurn:  if (play_.takeOpponent(board_)) redraw(); break;
//     case Phase::YourTurn:   if (moved) play_.play(board_); break;
//     case Phase::Over:       break;                        // chrome draws it
//     case Phase::Off:        break;
//   }
//
// That is the whole API. No packets, no addresses, no channel, no timeouts, no
// host, no lobby, and nothing for the player to decide either: they tap
// MULTIPLAYER and the next thing they see is a board.
//
// ---------------------------------------------------------------------------
// What this layer makes impossible, rather than merely documented.
//
// - You cannot move on a stale board. Phase is TheirTurn until you have taken
//   delivery of their move, so YourTurn structurally implies "nothing pending".
//   Forgetting takeOpponent() leaves you visibly stuck on their turn instead of
//   quietly desyncing, and it says so in the log.
// - You cannot mis-size the buffer. The state is a type, not a pointer and a
//   capacity, so a mismatch is a compile error.
// - You cannot forget to suppress sleep. wantsAwake() is true for a whole match,
//   and LinkActivity returns it from preventAutoSleep(). Waiting for an opponent
//   to think looks exactly like inactivity to the sleep timer.
// - You cannot leave the radio on. stop() is idempotent and the destructor calls
//   it.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "LinkRadio.h"
#include "LinkSession.h"

namespace linkplay {

// The registry. Two builds only see each other when these agree, so **bump the
// value whenever the shared state's layout changes**: mismatched builds then
// simply fail to find each other, which is the honest failure. Leaving it alone
// means an old device and a new one match happily and then misread each other's
// states. Same discipline as the cache format version (CLAUDE.md golden rule 10).
enum class GameId : uint16_t {
  Chess = 0x0101,
  ConnectFour = 0x0201,
  Battleship = 0x0301,
  Jaipur = 0x0402,  // 0x0401 had no record of the last move in its state
  Checkers = 0x0601,
  Yahtzee = 0x0801,
  Knucklebones = 0x0501,
  SeaSalt = 0x0901,
  // 0x0901 was Toy Battle's on its own branch and Sea Salt's here; both landed
  // while the other was unmerged, and each was correct alone. Toy Battle moves
  // because Sea Salt reached xteink first and this id is a wire protocol: an id
  // that has been on the deploy branch has to be assumed to be on a device.
  // Nothing had shipped with 0x0902, so this costs nobody a match.
  ToyBattle = 0x0902,
  // Reserved for host tests, which need an id no real game will ever use.
  Test = 0xFF01,
};

// Every id above must be distinct, and until 2026-08-11 nothing checked it.
// Sea Salt and Toy Battle were built on separate branches and both took
// 0x0901. Each branch's full suite passed, because neither branch contained
// the other game -- the collision existed only in the merge, and the failure
// it would have shipped is two devices running DIFFERENT games agreeing they
// are in the same match. That is the one thing this id exists to prevent.
//
// ADD NEW IDS HERE TOO. A list you have to remember to update is a weak
// guard, but it turns a silent protocol bug into a compile error, and the
// alternative is nothing.
constexpr GameId kAllGameIds[] = {
    GameId::Chess,   GameId::ConnectFour,  GameId::Battleship, GameId::Jaipur,    GameId::Checkers,
    GameId::Yahtzee, GameId::Knucklebones, GameId::SeaSalt,    GameId::ToyBattle, GameId::Test,
};

constexpr bool gameIdsAreDistinct() {
  constexpr int n = static_cast<int>(sizeof(kAllGameIds) / sizeof(kAllGameIds[0]));
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      if (kAllGameIds[i] == kAllGameIds[j]) return false;
    }
  }
  return true;
}

static_assert(gameIdsAreDistinct(), "two games share a GameId: they would match each other over the link");

// A game's shared state travels as raw bytes between two identical builds on
// identical hardware, so no byte order or padding question arises. That is only
// true because a match is always X4 Pro to X4 Pro (or simulator to simulator);
// it is the reason GameId carries a layout version.
class PlayBase {
 public:
  enum class Phase : uint8_t {
    Off,        // not started
    Searching,  // looking for someone; the shared chrome is drawing
    TheirTurn,  // waiting on them, or they have moved and you have not applied it
    YourTurn,   // your move, and their last one is already on your board
    Over,       // ending() says why; the shared chrome is drawing
  };

  enum class Ending : uint8_t {
    None,
    OpponentLeft,  // they closed the app or locked the device
    OpponentLost,  // they went silent: flat battery, out of range, a crash
    YouLeft,
  };

  // Tests hand in their own link so a whole match can be played against one
  // that drops, duplicates and reorders on purpose. Games pass nothing and get
  // the radio.
  explicit PlayBase(Transport* testLink = nullptr) : testLink_(testLink) {}
  ~PlayBase() { stop(); }

  PlayBase(const PlayBase&) = delete;
  PlayBase& operator=(const PlayBase&) = delete;

  // Brings up the radio and starts looking. `yourName` is what the other device
  // shows; it is truncated rather than rejected. Returns false only if the radio
  // will not start, which is the one failure a game has to show the user.
  bool start(GameId gameId, const char* yourName);

  // Tells the opponent we are going, then shuts the radio down. Idempotent, so
  // an Activity can call it from onExit() without tracking whether it started.
  void stop();

  // Once per loop pass, before the game reads anything else. Drains the radio,
  // runs the clocks, and returns the phase to act on.
  Phase update(uint32_t nowMs);

  Phase phase() const { return phase_; }
  Ending ending() const { return ending_; }
  const char* opponentName() const { return session_.peerName(); }

  // True for exactly one update() after a match begins, so a game can set up a
  // fresh board. goesFirst() is valid from that moment on.
  bool takeMatchStart();

  // A one-byte note that does not touch the turn. Either side may send one at
  // any point in a match; the newest wins; it retransmits until it lands.
  //
  // This is how two devices hold a conversation the board cannot carry -- a
  // rematch is asked and answered exactly when the game has ended and there is
  // no turn left. Keep it to intents ("play again", "leaving"); game state
  // belongs in play(), which is what the turn exists to order.
  bool say(uint8_t note);

  // True once per note the opponent sends, with the note in `note`.
  bool heard(uint8_t& note);

  // Which side you are. Decided by a coin toss both devices compute; there is
  // nothing to negotiate and nothing to show. Chess maps this to White.
  bool goesFirst() const { return session_.isHost(); }

  // A live match must keep the device awake: an opponent thinking for five
  // minutes is indistinguishable from an idle device, and the sleep timer would
  // take the second one. LinkActivity wires this to preventAutoSleep().
  bool wantsAwake() const { return phase_ != Phase::Off; }

 protected:
  bool playRaw(const uint8_t* state, uint8_t length);
  bool takeRaw(uint8_t* out, uint8_t capacity, uint8_t& length);

 private:
  Transport& link();

  Radio radio_;
  Transport* testLink_ = nullptr;
  Session session_;
  Phase phase_ = Phase::Off;
  Ending ending_ = Ending::None;
  bool started_ = false;
  // start() takes no clock, so the session is opened on the first update(). It
  // has to be: the coin toss for who moves first is mixed from the time the
  // player tapped MULTIPLAYER, and starting every match at zero would put the
  // same device first every time.
  bool searchPending_ = false;
  uint16_t gameId_ = 0;
  char name_[kMaxNameBytes + 1] = {};
  bool matchStartPending_ = false;
  // When their move arrived and is still sitting undelivered. Used only to say
  // so in the log if a game forgets takeOpponent() and parks on TheirTurn.
  uint32_t undrainedSinceMs_ = 0;
  bool warnedUndrained_ = false;
};

template <typename State>
class Play final : public PlayBase {
  static_assert(std::is_trivially_copyable<State>::value,
                "A shared game state is copied as bytes between two devices, so it must be trivially copyable: no "
                "pointers, no std::string, no virtuals.");
  static_assert(sizeof(State) <= kMaxPayloadBytes, "A shared game state must fit one packet (192 bytes).");

 public:
  using PlayBase::PlayBase;

  // Sends the board after your move. Refused (returns false, changes nothing)
  // unless the phase is YourTurn, which already rules out a stale board.
  bool play(const State& state) {
    return playRaw(reinterpret_cast<const uint8_t*>(&state), static_cast<uint8_t>(sizeof(State)));
  }

  // Copies the opponent's move in, once. Returns false when there is nothing
  // new, so the natural `if (takeOpponent(board)) redraw();` is also correct.
  //
  // Taken into a scratch first, and only handed over on an exact size match.
  // Writing straight into `out` meant a packet from a build with a different
  // state layout part-wrote the caller's live board and then reported "nothing
  // new" -- and adopting straight into the live board is what this file tells
  // people to do. GameId is supposed to make that unreachable; a rule that
  // holds only because a second rule held is not a rule.
  bool takeOpponent(State& out) {
    State scratch;
    uint8_t length = 0;
    if (!takeRaw(reinterpret_cast<uint8_t*>(&scratch), static_cast<uint8_t>(sizeof(State)), length)) return false;
    if (length != sizeof(State)) return false;
    out = scratch;
    return true;
  }
};

}  // namespace linkplay
