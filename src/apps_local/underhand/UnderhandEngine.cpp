#include "UnderhandEngine.h"

#include <cstring>

namespace underhand {

namespace {

constexpr Counts kStartingHand = {0, 2, 2, 2, 2, 0};
constexpr int kForesightCards = 3;

int total(const Counts& c) {
  int sum = 0;
  for (int16_t n : c) sum += n;
  return sum;
}

int smaller(int a, int b) { return a < b ? a : b; }

bool isPunishment(uint8_t id) { return id == ids::kGreed || id == ids::kPoliceRaid || id == ids::kDesperate; }

// The three rolls' chances with this hand, in percent: 35 at the threshold
// and 15 more for each one past it, capped at certain.
Odds oddsFor(const Counts& held) {
  auto curve = [](int n, int threshold) {
    if (n < threshold) return 0;
    const int percent = 35 + 15 * (n - threshold);
    return percent < 100 ? percent : 100;
  };
  Odds odds;
  odds.greed = curve(total(held), 16);
  odds.police = curve(held[Suspicion], 5);
  odds.desperate = held[Food] == 0 ? 20 : 0;
  return odds;
}

bool alwaysDealt(uint8_t id) {
  for (uint8_t a : ids::kAlwaysDealt) {
    if (a == id) return true;
  }
  return false;
}

const Option& optionOnTable(const Game& game, const Cards& cards, int k) { return cards.card(game.card)->option[k]; }

bool onTable(const Game& game, const Cards& cards, int k) {
  const Card* card = cards.card(game.card);
  return card && k >= 0 && k < card->optionCount;
}

// OptionModel::isSatisfiable: what is held could pay option k.
bool satisfiable(const Game& game, const Cards& cards, int k) {
  const Option& o = optionOnTable(game, cards, k);
  return detail::satisfiability(game.cost[k], o.swap, game.held) < 1 && total(game.held) >= o.randomCost;
}

uint8_t drawTop(Game& game, const Cards& cards, Random& random) {
  if (game.draw.size == 0) detail::reshuffle(game, cards, random);
  return game.draw.size ? game.draw.erase(game.draw.size - 1) : 0;
}

bool anyAffordable(const Game& game, const Cards& cards) {
  const int options = cards.card(game.card)->optionCount;
  for (int k = 0; k < options; ++k) {
    if (affordable(game, cards, k)) return true;
  }
  return false;
}

// After a choice: foresight discards, then the played card, then the next.
// In the tutorial the choice's shuffle-ins went on top after the peek, so the
// cards seen sit under them; the card marked is the card discarded.
void advance(Game& game, const Cards& cards, Random& random) {
  int above = 0;
  if (game.tutorial && game.seenCount > 0) {
    for (int a = 0; a < game.addedCount; ++a) above += game.added[a].copies;
  }
  int removed = 0;
  for (int i = 0; i < game.seenCount; ++i) {
    if (!(game.discardMask & (1 << i))) continue;
    const int at = game.draw.size - 1 - above - (i - removed);
    if (at >= 0) game.discard.push(game.draw.erase(at));
    ++removed;
  }
  game.seenCount = 0;
  game.discardMask = 0;
  if (cards.card(game.card)->recurring) game.discard.push(game.card);
  detail::nextCard(game, cards, random);
}

}  // namespace

namespace detail {

// DeckModel::reshuffle. The remaining draw pile joins the discard pile, which
// is shuffled, and the new draw pile is built by picking cards one at a time
// with chance proportional to weight. Each pick goes to the bottom, so the
// first card picked is the first drawn.
void reshuffle(Game& game, const Cards& cards, Random& random) {
  Pile& pool = game.discard;
  Pile& draw = game.draw;
  const int moved = smaller(draw.size, Pile::kCapacity - pool.size);
  if (moved < draw.size) pool.overflowed = true;
  std::memmove(pool.card + moved, pool.card, pool.size);
  std::memcpy(pool.card, draw.card, moved);
  pool.size = static_cast<uint8_t>(pool.size + moved);
  draw.size = 0;

  const int n = pool.size;
  for (int i = 0; i + 1 < n; ++i) {
    const int j = i + random.below(n - i);
    const uint8_t t = pool.card[i];
    pool.card[i] = pool.card[j];
    pool.card[j] = t;
  }

  int weight = 0;
  for (int i = 0; i < pool.size; ++i) weight += cards.card(pool.card[i])->weight;
  while (pool.size > 0) {
    const int r = random.below(weight);
    int sum = 0;
    for (int i = 0; i < pool.size; ++i) {
      sum += cards.card(pool.card[i])->weight;
      if (sum > r) {
        weight -= cards.card(pool.card[i])->weight;
        draw.insert(0, pool.erase(i));
        break;
      }
    }
  }
  game.reshuffled = true;
}

// DeckController::resolvePunishments: at most one, placed on top of the deck.
void punish(Game& game, Random& random) {
  // 15 * held - 205 and 15 * suspicion - 40 in the original; the same numbers.
  const Odds odds = oddsFor(game.held);
  if (odds.greed > 0 && random.below(100) < odds.greed) {
    game.draw.push(ids::kGreed);
  } else if (odds.police > 0 && random.below(100) < odds.police) {
    game.draw.push(ids::kPoliceRaid);
  } else if (odds.desperate > 0 && random.below(100) < odds.desperate) {
    game.draw.push(ids::kDesperate);
  }
}

// DeckController::createOptionModelFromOption, for each option on the card.
void resolve(Game& game, const Cards& cards, Random& random) {
  const Card& card = *cards.card(game.card);
  for (int k = 0; k < kMaxOptions; ++k) {
    game.cost[k] = Counts{};
    game.gain[k] = Counts{};
    std::memset(game.rolled[k], 0, sizeof(game.rolled[k]));
    if (k >= card.optionCount) continue;
    const Option& o = card.option[k];
    for (int r = 0; r < kResources; ++r) {
      const int16_t held = game.held[r];
      int16_t cost = o.cost[r];
      if (cost == kOnlyIfNone) {
        cost = held ? kOnlyIfNone : 0;
      } else if (cost == kHalf) {
        cost = static_cast<int16_t>((held + 1) / 2);
      }
      game.cost[k][r] = cost;
      game.gain[k][r] = o.gain[r] == kHalf ? static_cast<int16_t>((held + 1) / 2) : o.gain[r];
    }
    const int span = o.rollHigh - o.rollLow + 1;
    for (int j = 0; j < o.rollCount; ++j) {
      int id = o.rollLow + random.below(span);
      for (int again = 0; !o.rollRepeats && again < j; ++again) {
        if (game.rolled[k][again] == id) {
          id = o.rollLow + random.below(span);
          again = -1;
        }
      }
      game.rolled[k][j] = static_cast<uint8_t>(id);
    }
  }

  // DeckController::nextEventCard: an option that ends the run is locked while
  // another option can be paid. Checked in order, each against the others as
  // they stand by then.
  bool open[kMaxOptions] = {};
  for (int k = 0; k < card.optionCount; ++k) open[k] = satisfiable(game, cards, k);
  game.lockedMask = 0;
  for (int k = 0; k < card.optionCount; ++k) {
    if (!card.option[k].lose) continue;
    bool other = false;
    for (int j = 0; j < card.optionCount; ++j) other = other || (j != k && open[j]);
    if (!other) continue;
    game.cost[k].fill(kOnlyIfNone);
    open[k] = false;
    game.lockedMask = static_cast<uint8_t>(game.lockedMask | (1 << k));
  }
}

// DeckController::nextEventCard.
void nextCard(Game& game, const Cards& cards, Random& random) {
  if (game.card && !game.tutorial && !isPunishment(game.card)) punish(game, random);
  game.card = drawTop(game, cards, random);
  game.phase = Phase::Choosing;
  if (!game.card) {
    game.phase = Phase::Lost;
    game.loss = LossReason::NoCards;
    return;
  }
  resolve(game, cards, random);
  ++game.turn;
  if (!anyAffordable(game, cards)) {
    game.phase = Phase::Lost;
    game.loss = LossReason::Stuck;
  }
}

int satisfiability(const Counts& cost, bool swap, Counts offer) {
  int need[kResources];
  for (int r = 0; r < kResources; ++r) need[r] = cost[r];
  if (swap) {
    need[Cultist] += need[Prisoner];
    need[Prisoner] = 0;
    offer[Cultist] = static_cast<int16_t>(offer[Cultist] + offer[Prisoner]);
    offer[Prisoner] = 0;
  }
  for (int r = Money; r < kResources; ++r) need[r] -= offer[r];
  // Relics pay a relic cost first, then any shortfall, in resource order.
  int relics = offer[Relic];
  for (int r = 0; r < kResources && relics > 0; ++r) {
    if (need[r] <= 0) continue;
    const int use = smaller(need[r], relics);
    need[r] -= use;
    relics -= use;
  }
  need[Relic] -= relics;
  int sum = 0;
  for (int r = 0; r < kResources; ++r) {
    if (need[r] > 0) return 1;
    sum += need[r];
  }
  return sum;
}

}  // namespace detail

int Rng::below(int n) {
  if (n <= 1) return 0;
  const uint32_t bound = static_cast<uint32_t>(n);
  // The largest multiple of n that fits; it is 2^32 itself when n divides it.
  const uint64_t limit = (uint64_t{1} << 32) - ((uint64_t{1} << 32) % bound);
  uint64_t r;
  do {
    // xorshift64*
    state_ ^= state_ >> 12;
    state_ ^= state_ << 25;
    state_ ^= state_ >> 27;
    r = static_cast<uint32_t>((state_ * 0x2545F4914F6CDD1DULL) >> 32);
  } while (r >= limit);
  return static_cast<int>(r % bound);
}

int Profile::summonedCount() const {
  int n = 0;
  for (uint8_t bits = summoned; bits; bits &= bits - 1) ++n;
  return n;
}

bool Pile::insert(int at, uint8_t id) {
  if (size >= kCapacity) {
    overflowed = true;
    return false;
  }
  if (at < 0) at = 0;
  if (at > size) at = size;
  std::memmove(card + at + 1, card + at, size - at);
  card[at] = id;
  ++size;
  return true;
}

uint8_t Pile::erase(int at) {
  const uint8_t id = card[at];
  std::memmove(card + at, card + at + 1, size - at - 1);
  --size;
  return id;
}

void start(Game& game, const Cards& cards, Profile& profile, Random& random) {
  game = Game{};
  game.tutorial = !profile.tutorialDone;
  game.held = game.tutorial ? Counts{} : kStartingHand;

  // DeckController::compileDeck, with StartupController::init's tiers.
  const int gods = profile.summonedCount();
  uint8_t coin[kMaxCards];
  uint8_t sure[kMaxCards + 2];
  int coins = 0;
  int sures = 0;
  for (int id = 1; id < 256; ++id) {
    const Card* c = cards.card(id);
    if (!c) continue;
    bool dealt = c->initial;
    for (const ids::Tier& tier : ids::kTiers) {
      if (tier.card == id && gods >= tier.gods) dealt = true;
    }
    if (!dealt) continue;
    if (alwaysDealt(c->id)) {
      sure[sures++] = c->id;
    } else {
      coin[coins++] = c->id;
    }
  }
  if (profile.previous >= 0 && profile.previous < cards.godCount()) {
    for (uint8_t id : cards.god(profile.previous).unlock) sure[sures++] = id;
  }
  profile.previous = -1;

  // DeckModel::init.
  for (int i = 0; i < coins; ++i) {
    if (random.below(2) == 0) game.draw.push(coin[i]);
  }
  for (int i = 0; i < sures; ++i) game.draw.push(sure[i]);
  detail::reshuffle(game, cards, random);
  game.reshuffled = false;

  game.draw.insert(game.draw.size - random.below(ids::kRumorsDepth), ids::kRumors);
  if (game.tutorial) game.draw.push(ids::kTutorial);
  detail::nextCard(game, cards, random);
}

Odds punishmentOdds(const Game& game) {
  if (game.tutorial || isPunishment(game.card)) return Odds{};
  return oddsFor(game.held);
}

bool affordable(const Game& game, const Cards& cards, int k) {
  return game.phase == Phase::Choosing && onTable(game, cards, k) && satisfiable(game, cards, k);
}

bool exact(const Game& game, const Cards& cards, int k, const Counts& offer) {
  if (!affordable(game, cards, k)) return false;
  for (int r = 0; r < kResources; ++r) {
    if (offer[r] < 0 || offer[r] > game.held[r]) return false;
  }
  return detail::satisfiability(game.cost[k], optionOnTable(game, cards, k).swap, offer) == 0;
}

bool suggest(const Game& game, const Cards& cards, int k, Counts& out) {
  if (!affordable(game, cards, k)) return false;
  const Counts& need = game.cost[k];
  const Counts& held = game.held;
  Counts p{};
  int shortfall = 0;
  auto take = [&](int r, int want) {
    const int t = smaller(held[r], want);
    p[r] = static_cast<int16_t>(p[r] + t);
    return want - t;
  };
  if (optionOnTable(game, cards, k).swap) {
    shortfall += take(Cultist, take(Prisoner, need[Cultist] + need[Prisoner]));
  } else {
    shortfall += take(Cultist, need[Cultist]);
    shortfall += take(Prisoner, need[Prisoner]);
  }
  shortfall += take(Money, need[Money]);
  shortfall += take(Food, need[Food]);
  shortfall += take(Suspicion, need[Suspicion]);
  p[Relic] = static_cast<int16_t>(need[Relic] + shortfall);
  out = p;
  return true;
}

bool choose(Game& game, const Cards& cards, int k, const Counts& offer, Random& random) {
  if (!exact(game, cards, k, offer)) return false;
  const Counts& p = offer;
  const Option& o = optionOnTable(game, cards, k);

  // ResourceController::fulfillOption: the payment, then any random loss, a
  // type at a time until one is held.
  for (int r = 0; r < kResources; ++r) game.held[r] = static_cast<int16_t>(game.held[r] - p[r]);
  game.played = game.card;
  game.playedOption = static_cast<int8_t>(k);
  game.paid = p;
  game.lost = Counts{};
  game.addedCount = 0;
  game.reshuffled = false;
  for (int n = 0; n < o.randomCost && total(game.held) > 0; ++n) {
    int r;
    do {
      r = random.below(kResources);
    } while (game.held[r] < 1);
    --game.held[r];
    ++game.lost[r];
  }
  for (int r = 0; r < kResources; ++r) game.held[r] = static_cast<int16_t>(game.held[r] + game.gain[k][r]);
  game.gained = game.gain[k];

  if (o.win >= 0) {
    game.phase = Phase::Won;
    game.god = o.win;
    return true;
  }
  if (o.lose) {
    game.phase = Phase::Lost;
    game.loss = LossReason::Choice;
    return true;
  }

  // DeckController::fulfillOption. With fewer than three cards left the
  // original reshuffles and shows nothing; this shows the reshuffled top.
  if (o.foresight) {
    if (game.draw.size < kForesightCards) detail::reshuffle(game, cards, random);
    game.seenCount = static_cast<uint8_t>(smaller(kForesightCards, game.draw.size));
    for (int i = 0; i < game.seenCount; ++i) game.seen[i] = game.draw.card[game.draw.size - 1 - i];
    game.discardMask = 0;
    game.mayDiscard = o.foresightDiscard;
  }

  // Shuffled-in cards wait in the discard pile for the next reshuffle, except
  // in the tutorial, which deals them straight onto the deck.
  Pile& into = game.tutorial ? game.draw : game.discard;
  for (int a = 0; a < o.addCount; ++a) {
    for (int c = 0; c < o.add[a].copies; ++c) into.push(o.add[a].card);
    game.added[game.addedCount++] = Game::Added{o.add[a].card, o.add[a].copies};
  }
  for (int j = 0; j < o.rollCount; ++j) {
    into.push(game.rolled[k][j]);
    game.added[game.addedCount++] = Game::Added{game.rolled[k][j], 1};
  }

  if (o.foresight) {
    game.phase = Phase::Foresight;
    return true;
  }
  advance(game, cards, random);
  return true;
}

void toggleDiscard(Game& game, int i) {
  if (game.phase != Phase::Foresight || !game.mayDiscard || i < 0 || i >= game.seenCount) return;
  game.discardMask ^= static_cast<uint8_t>(1 << i);
}

void endForesight(Game& game, const Cards& cards, Random& random) {
  if (game.phase != Phase::Foresight) return;
  game.phase = Phase::Choosing;
  advance(game, cards, random);
}

void finish(Profile& profile, const Game& game) {
  profile.previous = -1;
  if (game.phase != Phase::Won || game.god < 0) return;
  profile.summoned |= static_cast<uint8_t>(1 << game.god);
  profile.previous = game.god;
  if (game.tutorial) profile.tutorialDone = true;
}

bool valid(const Game& game, const Cards& cards) {
  if (game.draw.size > Pile::kCapacity || game.discard.size > Pile::kCapacity) return false;
  for (const Pile* pile : {&game.draw, &game.discard}) {
    for (int i = 0; i < pile->size; ++i) {
      if (!cards.card(pile->card[i])) return false;
    }
  }
  for (int16_t n : game.held) {
    if (n < 0) return false;
  }
  switch (game.phase) {
    case Phase::Choosing:
    case Phase::Foresight:
      if (!cards.card(game.card)) return false;
      break;
    case Phase::Won:
      if (game.god < 0 || game.god >= cards.godCount()) return false;
      break;
    case Phase::Lost:
      if (game.loss == LossReason::None || game.loss > LossReason::NoCards) return false;
      break;
    default:
      return false;
  }
  if (game.seenCount > kForesightCards) return false;
  for (int i = 0; i < game.seenCount; ++i) {
    if (!cards.card(game.seen[i])) return false;
  }
  for (const auto& rolls : game.rolled) {
    for (uint8_t id : rolls) {
      if (id && !cards.card(id)) return false;
    }
  }
  return true;
}

bool rulesFit(const Cards& cards, const char** why) {
  const char* problem = nullptr;
  const uint8_t named[] = {ids::kTutorial, ids::kRumors, ids::kGreed, ids::kPoliceRaid, ids::kDesperate};
  for (uint8_t id : named) {
    if (!cards.card(id)) problem = "a card the rules name is missing";
  }
  for (const ids::Tier& tier : ids::kTiers) {
    if (!cards.card(tier.card)) problem = "a quest opener is missing";
  }
  if (cards.godCount() > 8) problem = "more gods than Profile::summoned has bits";
  if (why) *why = problem;
  return problem == nullptr;
}

}  // namespace underhand
