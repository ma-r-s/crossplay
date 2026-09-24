// Writes an Underhand save at a chosen moment, so the simulator opens on it.
// Screenshots of a state nobody can reach by tapping from a fresh run (a
// card with four ways to pay, a danger, foresight) start here.
//
//   c++ -std=c++17 -Isrc/apps_local/underhand -Ilib/JsonParser tools_local/underhand/seed.cpp \
//     src/apps_local/underhand/Underhand{Cards,Engine,Save,View}.cpp lib/JsonParser/StreamingJsonParser.cpp \
//     -o /tmp/underhand-seed
//   /tmp/underhand-seed fs_agent/.crosspoint/underhand.sav card=12 held=1,3,2,4,0,2 turn=9
//   /tmp/underhand-seed - list held=1,3,2,4,2,2      # every card: its options, states and ways to pay
//
// Keys: card=<id> held=<relic,money,cultist,food,prisoner,suspicion> turn=<n>
// summoned=<god bit mask> tutorial foresight=<id,id,id> seed=<n> (the deck's shuffle)
// profile (write only the profile: no run in progress). The deck is a real
// dealt deck; only the card on the table and the hand are replaced.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "UnderhandCards.h"
#include "UnderhandEngine.h"
#include "UnderhandOriginalData.h"
#include "UnderhandSave.h"
#include "UnderhandView.h"

using namespace underhand;

namespace {

int list(const Cards& cards, Save& s, Rng& rng) {
  for (int i = 0; i < cards.count(); ++i) {
    const Card& c = cards.at(i);
    Game g = s.game;
    g.card = c.id;
    detail::resolve(g, cards, rng);
    std::printf("%3d %s\n", c.id, cards.text(c.title));
    for (int k = 0; k < c.optionCount; ++k) {
      Counts ways[32];
      const int n = view::payments(g, cards, k, ways, 32);
      char why[112] = {};
      if (view::optionState(g, cards, k) != view::OptionState::Open) view::whyNot(g, cards, k, why, sizeof(why));
      std::printf("    %d ways=%-2d %s%s%s\n", k, n, cards.text(c.option[k].text), why[0] ? "  -- " : "", why);
    }
  }
  return 0;
}

bool counts(const char* text, int16_t* out, int n) {
  for (int i = 0; i < n; ++i) {
    char* end = nullptr;
    out[i] = static_cast<int16_t>(std::strtol(text, &end, 10));
    if (end == text) return false;
    text = *end == ',' ? end + 1 : end;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <save|-> [list] [key=value...]\n", argv[0]);
    return 2;
  }
  auto cards = std::make_unique<Cards>();
  const char* why = nullptr;
  if (!CardsReader::read(*cards, CardsReader::File::Gods, kGodsJson, sizeof(kGodsJson) - 1, &why) ||
      !CardsReader::read(*cards, CardsReader::File::Cards, kCardsJson, sizeof(kCardsJson) - 1, &why)) {
    std::fprintf(stderr, "cards: %s\n", why);
    return 1;
  }
  Save s;
  s.profile.tutorialDone = true;
  uint64_t seed = 97;
  int card = -1;
  bool haveHeld = false;
  bool listing = false;
  bool profileOnly = false;
  bool tutorial = false;
  Counts held{};
  int16_t seen[3] = {};
  int seenCount = 0;
  int turn = 0;
  for (int a = 2; a < argc; ++a) {
    const char* arg = argv[a];
    if (!std::strcmp(arg, "list")) {
      listing = true;
    } else if (!std::strcmp(arg, "profile")) {
      profileOnly = true;
    } else if (!std::strcmp(arg, "tutorial")) {
      tutorial = true;
    } else if (!std::strncmp(arg, "card=", 5)) {
      card = std::atoi(arg + 5);
    } else if (!std::strncmp(arg, "turn=", 5)) {
      turn = std::atoi(arg + 5);
    } else if (!std::strncmp(arg, "seed=", 5)) {
      seed = std::strtoull(arg + 5, nullptr, 10);
    } else if (!std::strncmp(arg, "summoned=", 9)) {
      s.profile.summoned = static_cast<uint8_t>(std::atoi(arg + 9));
    } else if (!std::strncmp(arg, "held=", 5) && counts(arg + 5, held.data(), kResources)) {
      haveHeld = true;
    } else if (!std::strncmp(arg, "foresight=", 10) && counts(arg + 10, seen, 3)) {
      seenCount = 3;
    } else {
      std::fprintf(stderr, "not understood: %s\n", arg);
      return 2;
    }
  }
  if (tutorial) s.profile.tutorialDone = false;
  Rng rng(seed);
  start(s.game, *cards, s.profile, rng);
  Game& g = s.game;
  if (haveHeld) g.held = held;
  if (listing) return list(*cards, s, rng);
  if (card >= 0) {
    if (!cards->card(card)) {
      std::fprintf(stderr, "no card %d\n", card);
      return 1;
    }
    g.card = static_cast<uint8_t>(card);
    detail::resolve(g, *cards, rng);
  }
  if (turn > 0) g.turn = static_cast<uint16_t>(turn);
  if (seenCount) {
    g.phase = Phase::Foresight;
    g.seenCount = 3;
    g.mayDiscard = true;
    for (int i = 0; i < 3; ++i) g.seen[i] = static_cast<uint8_t>(seen[i]);
  }
  s.inRun = !profileOnly;
  s.rng = rng.state();
  if (s.inRun && !valid(g, *cards)) {
    std::fprintf(stderr, "the state is not one the game accepts\n");
    return 1;
  }
  uint8_t bytes[kSaveBytes];
  encode(s, bytes);
  FILE* f = std::fopen(argv[1], "wb");
  if (!f || std::fwrite(bytes, 1, sizeof(bytes), f) != sizeof(bytes)) {
    std::fprintf(stderr, "could not write %s\n", argv[1]);
    return 1;
  }
  std::fclose(f);
  std::printf("%s: %s, card %d, turn %d\n", argv[1], s.inRun ? "in a run" : "profile only", g.card, g.turn);
  return 0;
}
