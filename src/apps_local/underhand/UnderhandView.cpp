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

// Keeps the ways in all[0, kept) that spend all the suspicion they can,
// writing at most `max` of them to `out` (which may be `all`). Returns how
// many there are.
int keepChoices(const Game& game, int k, const Counts* all, int kept, Counts* out, int max) {
  const int asked = game.cost[k][Suspicion] > 0 ? game.cost[k][Suspicion] : 0;
  const int spendable = asked < game.held[Suspicion] ? asked : game.held[Suspicion];
  int n = 0;
  for (int i = 0; i < kept; ++i) {
    if (all[i][Suspicion] < spendable) continue;
    if (n < max) out[n] = all[i];
    ++n;
  }
  return n;
}

int choicesThrough(const Game& game, const Cards& cards, int k, Counts* out, int max) {
  Counts all[kMostPayments];
  const int found = payments(game, cards, k, all, kMostPayments);
  return keepChoices(game, k, all, found < kMostPayments ? found : kMostPayments, out, max);
}

}  // namespace

int choices(const Game& game, const Cards& cards, int k, Counts* out, int max) {
  // A buffer that holds every payment is filtered in place, which keeps a
  // second 64-way array off the stack; a smaller one goes through one.
  if (max < kMostPayments) return choicesThrough(game, cards, k, out, max);
  const int found = payments(game, cards, k, out, max);
  return keepChoices(game, k, out, found < kMostPayments ? found : kMostPayments, out, max);
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
    if (handOdds(after).greed < before) return false;
  }
  for (int16_t n : game.gain[k]) {
    if (n > 0) return false;
  }
  const Option& o = card->option[k];
  return o.addCount == 0 && o.rollCount == 0 && !o.foresight && o.win < 0 && !o.lose;
}

void whyNot(const Game& game, const Cards& cards, int k, char* out, size_t size, bool relics) {
  Line line(out, size);
  const Card* card = cards.card(game.card);
  if (!card || k < 0 || k >= card->optionCount) return;
  if (game.lockedMask & (1 << k)) {
    line.words("LOCKED: ", "ANOTHER CHOICE IS OPEN");
    return;
  }
  if (affordable(game, cards, k)) return;
  const Option& o = card->option[k];
  const Counts& need = game.cost[k];
  const Counts& held = game.held;
  for (int r = 0; r < kResources; ++r) {
    if (need[r] == kOnlyIfNone) {
      line.words("ONLY WITH NO ", kPlural[r]);
      return;
    }
  }
  int total = 0;
  for (int16_t n : held) total += n;
  if (o.randomCost > total) {
    line.add("%sNEED %d %s", o.randomCost, "RESOURCES");
    return;
  }
  // Asked only for suspicion, holding none: there is none to lose.
  bool onlySuspicion = need[Suspicion] > held[Suspicion] && held[Suspicion] == 0;
  for (int r = 0; r < kResources && onlySuspicion; ++r) {
    if (r != Suspicion && need[r] > held[r]) onlySuspicion = false;
  }
  if (onlySuspicion && !o.swap) {
    line.words("", "NO SUSPICION TO LOSE");
    return;
  }
  // What is short before any relic, so it reads against the bar.
  char list[96];
  Line shortList(list, sizeof(list));
  for (int r = 0; r < kResources; ++r) {
    if (o.swap && r == Prisoner) continue;
    int want = need[r];
    int have = held[r];
    if (o.swap && r == Cultist) {
      want += need[Prisoner];
      have += held[Prisoner];
    }
    if (want > have) {
      const int n = want - have;
      if (o.swap && r == Cultist) {
        shortList.add(n == 1 ? "%s%d %s OR PRISONER" : "%s%d %s OR PRISONERS", n, resourceName(Cultist, n));
      } else {
        shortList.add("%s%d %s", n, resourceName(r, n));
      }
    }
  }
  line.words("SHORT: ", list);
  // Relics pay for any of it: say how much they would, so the list is not
  // read as all of it still wanted.
  int missing = 0;
  for (int r = 0; r < kResources; ++r) {
    if (o.swap && r == Prisoner) continue;
    int want = need[r] > 0 && need[r] != kOnlyIfNone ? need[r] : 0;
    int have = held[r];
    if (o.swap && r == Cultist) {
      want += need[Prisoner] > 0 && need[Prisoner] != kOnlyIfNone ? need[Prisoner] : 0;
      have += held[Prisoner];
    }
    if (r != Relic && want > have) missing += want - have;
  }
  const int spare = held[Relic] - (need[Relic] > 0 ? need[Relic] : 0);
  if (relics && spare > 0 && missing > 0) {
    const int cover = spare < missing ? spare : missing;
    char covered[32];
    std::snprintf(covered, sizeof(covered), cover == 1 ? " (A RELIC COVERS %d)" : " (RELICS COVER %d)", cover);
    line.raw(covered);
  }
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
