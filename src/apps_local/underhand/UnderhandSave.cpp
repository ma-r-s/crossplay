#include "UnderhandSave.h"

#include <cstddef>
#include <cstring>

namespace underhand {

namespace {

constexpr uint8_t kMagic[4] = {'U', 'H', 'N', 'D'};
constexpr uint16_t kVersion = 1;
constexpr uint16_t kGameBytes = sizeof(Game);

// The profile as saved, checked against these cards. Its bool is read as a
// byte: a damaged one copied straight into a bool is neither true nor false.
Profile readProfile(const uint8_t* at, const Cards& cards) {
  Profile p;
  std::memcpy(&p.summoned, at + offsetof(Profile, summoned), sizeof(p.summoned));
  std::memcpy(&p.previous, at + offsetof(Profile, previous), sizeof(p.previous));
  p.tutorialDone = at[offsetof(Profile, tutorialDone)] != 0;
  const int gods = cards.godCount();
  if (p.previous < -1 || p.previous >= gods) p.previous = -1;
  p.summoned = static_cast<uint8_t>(p.summoned & ((1u << gods) - 1));
  return p;
}

}  // namespace

void encode(const Save& save, uint8_t* out) {
  std::memcpy(out, kMagic, 4);
  std::memcpy(out + 4, &kVersion, 2);
  std::memcpy(out + 6, &kGameBytes, 2);
  size_t at = 8;
  std::memcpy(out + at, &save.profile, sizeof(Profile));
  at += sizeof(Profile);
  out[at++] = static_cast<uint8_t>((save.inRun ? 1 : 0) | (save.inRun && save.showOutcome ? 2 : 0));
  std::memcpy(out + at, &save.game, sizeof(Game));
  at += sizeof(Game);
  std::memcpy(out + at, &save.rng, sizeof(uint64_t));
}

bool decode(const uint8_t* data, size_t len, const Cards& cards, Save& out) {
  uint16_t version = 0;
  uint16_t gameBytes = 0;
  if (len < 8 + sizeof(Profile) || std::memcmp(data, kMagic, 4) != 0) return false;
  std::memcpy(&version, data + 4, 2);
  std::memcpy(&gameBytes, data + 6, 2);
  if (version != kVersion) return false;
  if (len != kSaveBytes || gameBytes != kGameBytes) {
    // A build whose Game is laid out differently cannot resume the run, but
    // the gods summoned and the tutorial sit in front of it, where they were.
    Save kept;
    kept.profile = readProfile(data + 8, cards);
    out = kept;
    return true;
  }

  Save s;
  size_t at = 8;
  s.profile = readProfile(data + at, cards);
  at += sizeof(Profile);
  const uint8_t flags = data[at++];
  if (flags > 3) return false;
  s.inRun = (flags & 1) != 0;
  s.showOutcome = (flags & 2) != 0;
  std::memcpy(&s.game, data + at, sizeof(Game));
  at += sizeof(Game);
  std::memcpy(&s.rng, data + at, sizeof(uint64_t));

  if (s.rng == 0) s.rng = 1;
  const bool playing = s.game.phase == Phase::Choosing || s.game.phase == Phase::Foresight;
  if (s.inRun && (!playing || !valid(s.game, cards))) s.inRun = false;
  // An outcome is only ever shown over a card, for a choice that was made.
  if (!s.inRun || s.game.phase != Phase::Choosing || s.game.played == 0) s.showOutcome = false;
  if (!s.inRun) s.game = Game{};
  out = s;
  return true;
}

}  // namespace underhand
