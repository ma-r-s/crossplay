#include "UnderhandView.h"

#include <cstdio>
#include <cstring>

namespace underhand::view {

namespace {

constexpr const char* kSingular[kResources] = {"RELIC", "MONEY", "CULTIST", "FOOD", "PRISONER", "SUSPICION"};
constexpr const char* kPlural[kResources] = {"RELICS", "MONEY", "CULTISTS", "FOOD", "PRISONERS", "SUSPICION"};

// Appends to a fixed buffer, separating items with ", ", and never overruns.
struct Line {
  char* out;
  size_t size;
  size_t used = 0;

  Line(char* o, size_t s) : out(o), size(s) {
    if (size) out[0] = '\0';
  }
  // `fmt` takes the separator, then n, then word.
  void add(const char* fmt, int n, const char* word) {
    if (used + 1 >= size) return;
    advance(std::snprintf(out + used, size - used, fmt, used ? ", " : "", n, word));
  }
  void raw(const char* text) {
    if (used + 1 >= size) return;
    advance(std::snprintf(out + used, size - used, "%s", text));
  }
  void words(const char* prefix, const char* word) {
    if (used + 1 >= size) return;
    advance(std::snprintf(out + used, size - used, "%s%s%s", used ? ", " : "", prefix, word));
  }
  void advance(int wrote) {
    if (wrote > 0) used += static_cast<size_t>(wrote) < size - used ? static_cast<size_t>(wrote) : size - used - 1;
  }
};

}  // namespace

const char* resourceName(int resource, int count) {
  if (resource < 0 || resource >= kResources) return "";
  return count == 1 ? kSingular[resource] : kPlural[resource];
}

OptionState optionState(const Game& game, const Cards& cards, int k) {
  if (game.lockedMask & (1 << k)) return OptionState::Locked;
  return affordable(game, cards, k) ? OptionState::Open : OptionState::Short;
}

Tokens giveTokens(const Game& game, const Cards& cards, int k) {
  Tokens out;
  const Card* card = cards.card(game.card);
  if (!card || k < 0 || k >= card->optionCount || (game.lockedMask & (1 << k))) return out;
  const Option& o = card->option[k];
  const Counts& cost = game.cost[k];
  auto push = [&](Token::Kind kind, int resource, int amount) {
    out.token[out.count++] = Token{kind, static_cast<uint8_t>(resource), static_cast<int16_t>(amount)};
  };
  for (int r = 0; r < kResources; ++r) {
    if (cost[r] <= 0 || cost[r] == kOnlyIfNone) continue;
    if (o.swap && (r == Cultist || r == Prisoner)) continue;
    push(Token::Count, r, cost[r]);
  }
  if (o.swap) {
    int n = 0;
    for (int r : {Cultist, Prisoner}) n += cost[r] > 0 && cost[r] != kOnlyIfNone ? cost[r] : 0;
    if (n > 0) push(Token::Either, Cultist, n);
  }
  if (o.randomCost > 0) push(Token::Random, 0, o.randomCost);
  return out;
}

Tokens tokensOf(const Counts& counts) {
  Tokens out;
  for (int r = 0; r < kResources; ++r) {
    if (counts[r] > 0) out.token[out.count++] = Token{Token::Count, static_cast<uint8_t>(r), counts[r]};
  }
  return out;
}

int payments(const Game& game, const Cards& cards, int k, Counts* out, int max) {
  if (!affordable(game, cards, k)) return 0;
  const Option& o = cards.card(game.card)->option[k];
  const Counts& need = game.cost[k];
  const Counts& held = game.held;
  auto upTo = [](int a, int b) { return a < b ? a : b; };
  // Each named resource pays anything from what it can down to nothing, and
  // relics make up the rest. With a swap, cultists and prisoners split one
  // shared need between them. Exact by construction.
  const int shared = need[Cultist] + need[Prisoner];
  constexpr int kMost = kMostPayments;
  Counts all[kMost];
  int found = 0;
  for (int money = upTo(held[Money], need[Money]); money >= 0; --money) {
    for (int food = upTo(held[Food], need[Food]); food >= 0; --food) {
      for (int suspicion = upTo(held[Suspicion], need[Suspicion]); suspicion >= 0; --suspicion) {
        const int mostPrisoners = upTo(held[Prisoner], o.swap ? shared : need[Prisoner]);
        for (int prisoners = mostPrisoners; prisoners >= 0; --prisoners) {
          const int mostCultists = upTo(held[Cultist], o.swap ? shared - prisoners : need[Cultist]);
          for (int cultists = mostCultists; cultists >= 0; --cultists) {
            const int people =
                o.swap ? shared - prisoners - cultists : (need[Cultist] - cultists) + (need[Prisoner] - prisoners);
            const int relics =
                need[Relic] + (need[Money] - money) + (need[Food] - food) + (need[Suspicion] - suspicion) + people;
            if (relics > held[Relic]) continue;
            if (found < kMost) {
              all[found] = Counts{static_cast<int16_t>(relics),    static_cast<int16_t>(money),
                                  static_cast<int16_t>(cultists),  static_cast<int16_t>(food),
                                  static_cast<int16_t>(prisoners), static_cast<int16_t>(suspicion)};
            }
            ++found;
          }
        }
      }
    }
  }
  // A relic paying for suspicion keeps the suspicion and loses the relic, so
  // those ways go last; then fewest relics first, keeping the walk's order
  // otherwise, which already spends named resources and prisoners before
  // relics and cultists.
  auto later = [&](const Counts& a, const Counts& b) {
    const int aSuspicion = need[Suspicion] - a[Suspicion];
    const int bSuspicion = need[Suspicion] - b[Suspicion];
    if (aSuspicion != bSuspicion) return aSuspicion > bSuspicion;
    return a[Relic] > b[Relic];
  };
  const int kept = found < kMost ? found : kMost;
  for (int i = 1; i < kept; ++i) {
    for (int j = i; j > 0 && later(all[j - 1], all[j]); --j) {
      const Counts t = all[j];
      all[j] = all[j - 1];
      all[j - 1] = t;
    }
  }
  for (int i = 0; i < kept && i < max; ++i) out[i] = all[i];
  return found;
}

namespace {

// Keeps the ways in all[0, kept) worth offering, writing at most `max` of
// them to `out` (which may be `all`), and returns how many there are:
//   - every one spends the suspicion it can, since keeping suspicion is never
//     better;
//   - every one uses the fewest relics any payment needs, since a relic spent
//     where the named resource is held saves nothing (Greed counts both);
//   - except that when all of those would leave no food, the ways that keep
//     one food with the fewest relics in its place are offered as well: food
//     is the one resource whose running out is punished. Not where nothing is
//     rolled after (the tutorial, a punishment card, a choice that wins or
//     loses the run) and not when the choice gives food back.
// The 64 kept by payments() hold every way of the real cards (at most about
// 36), so none of the food ways is ever cut off.
int keepChoices(const Game& game, const Cards& cards, int k, const Counts* all, int kept, Counts* out, int max) {
  const Counts& need = game.cost[k];
  const Counts& held = game.held;
  const Option& option = cards.card(game.card)->option[k];
  const int asked = need[Suspicion] > 0 ? need[Suspicion] : 0;
  const int spendable = asked < held[Suspicion] ? asked : held[Suspicion];
  int fewest = -1;
  for (int i = 0; i < kept; ++i) {
    if (all[i][Suspicion] < spendable) continue;
    if (fewest < 0 || all[i][Relic] < fewest) fewest = all[i][Relic];
  }
  if (fewest < 0) return 0;
  // An empty hand invites Desperate Measures wherever anything is rolled at
  // all, which is neither in the tutorial nor after a punishment card.
  const bool rolled = punishmentOdds(game, Counts{}).desperate > 0 && option.win < 0 && !option.lose;
  auto keepsOne = [&](const Counts& way) { return held[Food] - way[Food] + game.gain[k][Food] == 1; };
  // The food ways: more relics than the fewest, one food left, and of those
  // the fewest relics. Only when no fewest-relic way leaves any food.
  int foodRelics = -1;
  if (rolled && need[Food] > 0) {
    bool leavesFood = false;
    for (int i = 0; i < kept && !leavesFood; ++i) {
      if (all[i][Suspicion] < spendable || all[i][Relic] != fewest) continue;
      leavesFood = held[Food] - all[i][Food] + game.gain[k][Food] >= 1;
    }
    for (int i = 0; i < kept && !leavesFood; ++i) {
      if (all[i][Suspicion] < spendable || all[i][Relic] <= fewest || !keepsOne(all[i])) continue;
      if (foodRelics < 0 || all[i][Relic] < foodRelics) foodRelics = all[i][Relic];
    }
  }
  // The food ways are copied aside before anything is written, since `out`
  // may be `all`; the pass that writes never gets ahead of what it reads.
  constexpr int kMostFoodWays = 8;
  Counts food[kMostFoodWays];
  int foodWays = 0;
  if (foodRelics >= 0) {
    for (int i = 0; i < kept; ++i) {
      if (all[i][Suspicion] < spendable || all[i][Relic] != foodRelics || !keepsOne(all[i])) continue;
      if (foodWays < kMostFoodWays) food[foodWays] = all[i];
      ++foodWays;
    }
  }
  int n = 0;
  for (int i = 0; i < kept; ++i) {
    if (all[i][Suspicion] < spendable || all[i][Relic] != fewest) continue;
    if (n < max) out[n] = all[i];
    ++n;
  }
  for (int i = 0; i < foodWays && i < kMostFoodWays; ++i) {
    if (n < max) out[n] = food[i];
    ++n;
  }
  return n;
}

int choicesThrough(const Game& game, const Cards& cards, int k, Counts* out, int max) {
  Counts all[kMostPayments];
  const int found = payments(game, cards, k, all, kMostPayments);
  return keepChoices(game, cards, k, all, found < kMostPayments ? found : kMostPayments, out, max);
}

}  // namespace

int choices(const Game& game, const Cards& cards, int k, Counts* out, int max) {
  // A buffer that holds every payment is filtered in place, which keeps a
  // second 64-way array off the stack; a smaller one goes through one.
  if (max < kMostPayments) return choicesThrough(game, cards, k, out, max);
  const int found = payments(game, cards, k, out, max);
  return keepChoices(game, cards, k, out, found < kMostPayments ? found : kMostPayments, out, max);
}

Counts common(const Game& game, const Cards& cards, int k) {
  Counts all[kMostPayments];
  const int found = choices(game, cards, k, all, kMostPayments);
  if (found <= 0) return Counts{};
  Counts least = all[0];
  for (int i = 1; i < found && i < kMostPayments; ++i) {
    for (int r = 0; r < kResources; ++r) least[r] = all[i][r] < least[r] ? all[i][r] : least[r];
  }
  return least;
}

int chips(const Game& game, const Cards& cards, int k, Tokens* out, int max) {
  Counts all[kMostPayments];
  const int found = choices(game, cards, k, all, kMostPayments);
  const int n = found < kMostPayments ? found : kMostPayments;
  if (n <= 0) return 0;
  const Counts& cost = game.cost[k];
  const int asked = cost[Relic] > 0 && cost[Relic] != kOnlyIfNone ? cost[Relic] : 0;
  bool standIn = false;
  for (int i = 0; i < n; ++i) standIn = standIn || all[i][Relic] > asked;
  // A cultist-or-prisoner cost does not say which goes.
  const bool either = cards.card(game.card)->option[k].swap && cost[Cultist] + cost[Prisoner] > 0;
  if (n == 1 && !standIn && !either) return 0;
  if (n > max) return n;
  if (n == 1) {
    out[0] = tokensOf(all[0]);
    if (out[0].count == 1 && out[0].token[0].amount == 1) out[0].token[0].kind = Token::Symbol;
    return 1;
  }
  bool differs[kResources] = {};
  for (int r = 0; r < kResources; ++r) {
    for (int i = 1; i < n; ++i) differs[r] = differs[r] || all[i][r] != all[0][r];
  }
  differs[Relic] = differs[Relic] || standIn;
  bool single = true;
  for (int i = 0; i < n; ++i) {
    Counts part{};
    for (int r = 0; r < kResources; ++r) part[r] = differs[r] ? all[i][r] : 0;
    out[i] = tokensOf(part);
    single = single && out[i].count == 1 && out[i].token[0].amount == 1;
  }
  // Where every way is one of something, the count only repeats the cost.
  for (int i = 0; i < n && single; ++i) out[i].token[0].kind = Token::Symbol;
  return n;
}

bool savesLastFood(const Game& game, const Cards& cards, int k) {
  Counts all[kMostPayments];
  const int found = choices(game, cards, k, all, kMostPayments);
  for (int i = 1; i < found && i < kMostPayments; ++i) {
    if (all[i][Relic] > all[0][Relic]) return true;
  }
  return false;
}

bool buysNothing(const Game& game, const Cards& cards, int k) {
  const Card* card = cards.card(game.card);
  if (!card || k < 0 || k >= card->optionCount) return false;
  const int asked = game.cost[k][Suspicion];
  if (asked <= 0 || game.held[Suspicion] > 0) return false;
  // Greed counts relics too, and is rolled on the hand after paying: where
  // the payment lowers its chance, the relics buy that.
  const int before = punishmentOdds(game).greed;
  if (before > 0) {
    Counts pay{};
    if (!suggest(game, cards, k, pay)) return false;
    Counts after = game.held;
    for (int r = 0; r < kResources; ++r) after[r] = static_cast<int16_t>(after[r] - pay[r]);
    if (punishmentOdds(game, after).greed < before) return false;
  }
  for (int16_t n : game.gain[k]) {
    if (n > 0) return false;
  }
  const Option& o = card->option[k];
  return o.addCount == 0 && o.rollCount == 0 && !o.foresight && o.win < 0 && !o.lose;
}

bool canAdd(const Game& game, const Cards& cards, int k, const Counts& offer, int resource) {
  const Card* card = cards.card(game.card);
  if (!card || k < 0 || k >= card->optionCount || resource < 0 || resource >= kResources) return false;
  if (offer[resource] >= game.held[resource]) return false;
  Counts more = offer;
  ++more[resource];
  // Only toward one of the ways offered: the offer must fit inside one of
  // them, so a relic goes in only where the hand needs it or it saves the
  // last food.
  Counts all[kMostPayments];
  const int found = choices(game, cards, k, all, kMostPayments);
  for (int i = 0; i < found && i < kMostPayments; ++i) {
    bool fits = true;
    for (int r = 0; r < kResources && fits; ++r) fits = more[r] <= all[i][r];
    if (fits) return true;
  }
  return false;
}

Why whyNot(const Game& game, const Cards& cards, int k) {
  Why why;
  auto say = [&](const char* words) { std::snprintf(why.words, sizeof(why.words), "%s", words); };
  auto symbol = [&](Token::Kind kind, int resource, int amount) {
    if (why.tokens.count < kResources + 2) {
      why.tokens.token[why.tokens.count++] = Token{kind, static_cast<uint8_t>(resource), static_cast<int16_t>(amount)};
    }
  };
  const Card* card = cards.card(game.card);
  if (!card || k < 0 || k >= card->optionCount) return why;
  if (game.lockedMask & (1 << k)) {
    say("ONLY WHEN NOTHING ELSE IS OPEN");
    return why;
  }
  if (affordable(game, cards, k)) return why;
  // Anything else short shows by itself, the cost beside what the bar holds.
  // A cost of none is not drawn with the cost, so it is said.
  for (int r = 0; r < kResources; ++r) {
    if (game.cost[k][r] == kOnlyIfNone) {
      say("ONLY WITH NO");
      symbol(Token::Symbol, r, 0);
      return why;
    }
  }
  return why;
}

Tokens getTokens(const Game& game, int k) {
  Tokens out;
  if (k < 0 || k >= kMaxOptions) return out;
  for (int r = 0; r < kResources; ++r) {
    if (game.gain[k][r] > 0) out.token[out.count++] = Token{Token::Count, static_cast<uint8_t>(r), game.gain[k][r]};
  }
  return out;
}

namespace {

void addTitle(Adds& adds, const char* title, int copies) {
  for (int i = 0; i < adds.count; ++i) {
    if (std::strcmp(adds.title[i], title) == 0) {
      adds.copies[i] += copies;
      return;
    }
  }
  if (adds.count >= kMaxAdds + kMaxRoll) return;
  adds.title[adds.count] = title;
  adds.copies[adds.count++] = copies;
}

const char* titleOf(const Cards& cards, int id) {
  const Card* c = cards.card(id);
  return c ? cards.text(c->title) : "?";
}

}  // namespace

Adds optionAdds(const Game& game, const Cards& cards, int k) {
  Adds adds;
  const Card* card = cards.card(game.card);
  if (!card || k < 0 || k >= card->optionCount) return adds;
  const Option& o = card->option[k];
  for (int a = 0; a < o.addCount; ++a) addTitle(adds, titleOf(cards, o.add[a].card), o.add[a].copies);
  for (int j = 0; j < o.rollCount; ++j) addTitle(adds, titleOf(cards, game.rolled[k][j]), 1);
  return adds;
}

Adds lastAdds(const Game& game, const Cards& cards) {
  Adds adds;
  for (int a = 0; a < game.addedCount; ++a) addTitle(adds, titleOf(cards, game.added[a].card), game.added[a].copies);
  return adds;
}

// Shuffled-in cards wait in the discard pile for the next reshuffle, unless
// that reshuffle came with this very draw; the tutorial deals them straight
// onto the deck.
void deckSentence(const Game& game, const Cards& cards, char* out, size_t size) {
  if (size) out[0] = '\0';
  const Adds adds = lastAdds(game, cards);
  if (adds.count == 0 && !game.reshuffled) return;
  size_t used = 0;
  auto put = [&](const char* text) {
    if (used + 1 >= size) return;
    const int w = std::snprintf(out + used, size - used, "%s", text);
    if (w > 0) used += static_cast<size_t>(w) < size - used ? static_cast<size_t>(w) : size - used - 1;
  };
  if (game.reshuffled) put(adds.count ? "The deck was reshuffled and now holds " : "The deck was reshuffled");
  int copies = 0;
  char one[96];
  for (int a = 0; a < adds.count; ++a) {
    copies += adds.copies[a];
    const char* joiner = a == 0 ? "" : a + 1 == adds.count ? " and " : ", ";
    if (adds.copies[a] > 1) {
      std::snprintf(one, sizeof(one), "%s%d x \"%s\"", joiner, adds.copies[a], adds.title[a]);
    } else {
      std::snprintf(one, sizeof(one), "%s\"%s\"", joiner, adds.title[a]);
    }
    put(one);
  }
  const bool single = copies == 1;
  if (game.reshuffled) {
    put(".");
  } else if (game.tutorial) {
    put(single ? " goes on top of the deck." : " go on top of the deck.");
  } else {
    put(single ? " joins the deck at the next shuffle." : " join the deck at the next shuffle.");
  }
}

void effectLine(const Game& game, const Cards& cards, int k, char* out, size_t size) {
  if (size) out[0] = '\0';
  const Card* card = cards.card(game.card);
  if (!card || k < 0 || k >= card->optionCount) return;
  const Option& o = card->option[k];
  if (o.win >= 0 && o.win < cards.godCount()) {
    std::snprintf(out, size, "Summons %s", cards.text(cards.god(o.win).name));
    return;
  }
  if (o.lose) {
    std::snprintf(out, size, "Ends the run");
    return;
  }
  size_t used = 0;
  auto sentence = [&](const char* text) {
    if (used + 1 >= size) return;
    const int w = std::snprintf(out + used, size - used, "%s%s", used ? ". " : "", text);
    if (w > 0) used += static_cast<size_t>(w) < size - used ? static_cast<size_t>(w) : size - used - 1;
  };
  // One sentence for every card it adds: 'Adds "A", 2 x "B" and "C"'.
  const Adds adds = optionAdds(game, cards, k);
  if (adds.count > 0) {
    char list[160];
    size_t at = 0;
    for (int i = 0; i < adds.count && at + 1 < sizeof(list); ++i) {
      const char* joiner = i == 0 ? "Adds " : i + 1 == adds.count ? " and " : ", ";
      const int w = adds.copies[i] == 1 ? std::snprintf(list + at, sizeof(list) - at, "%s\"%s\"", joiner, adds.title[i])
                                        : std::snprintf(list + at, sizeof(list) - at, "%s%d x \"%s\"", joiner,
                                                        adds.copies[i], adds.title[i]);
      if (w > 0) at += static_cast<size_t>(w) < sizeof(list) - at ? static_cast<size_t>(w) : sizeof(list) - at - 1;
    }
    sentence(list);
  }
  if (o.foresight) sentence(o.foresightDiscard ? "See the next 3, discard any" : "See the next 3 cards");
}

namespace {

// Matched on the original words as well as the id, so a changed card file
// shows its own words rather than a stale replacement.
struct Reworded {
  int card;
  int option;  // -1 for the flavour line
  const char* original;
  const char* shown;
};
constexpr Reworded kReworded[] = {
    {91, -1, "Rewards and effects are shown at the bottom of the option box",
     "What an option gives is shown at its right, and what else it does below"},
    {92, -1, "Drag these resources from your hand", "Tap an option to pay for it from what you hold"},
    {92, 0, "The middle of the option box shows these requirements", "What an option takes is shown at its left"},
    {93, 0, "This is denoted by the 'Insert' keyword", "The option says which card it adds"},
    {99, 0, "This symbol means a mix of prisoners and cultists can be used",
     "Here prisoners and cultists pay in any mix"},
};

const char* reworded(const Card& card, int option, const char* original) {
  for (const Reworded& r : kReworded) {
    if (r.card == card.id && r.option == option && std::strcmp(r.original, original) == 0) return r.shown;
  }
  return original;
}

}  // namespace

Odds chances(const Game& game) {
  const Odds roll = punishmentOdds(game);
  Odds strike;
  strike.greed = roll.greed;
  strike.police = ((100 - roll.greed) * roll.police + 50) / 100;
  strike.desperate = ((100 - roll.greed) * (100 - roll.police) * roll.desperate + 5000) / 10000;
  return strike;
}

const char* flavorText(const Cards& cards, const Card& card) { return reworded(card, -1, cards.text(card.flavor)); }

const char* optionText(const Cards& cards, const Card& card, int k) {
  if (k < 0 || k >= card.optionCount) return "";
  return reworded(card, k, cards.text(card.option[k].text));
}

}  // namespace underhand::view
