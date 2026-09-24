#pragma once

// The words the screens show for a Game: what an option costs and gives, why
// one cannot be taken, what the last choice did. Freestanding and ASCII only,
// since the Jersey cuts carry nothing else.

#include <cstddef>

#include "UnderhandEngine.h"

namespace underhand::view {

// "MONEY", "CULTIST" for one and "CULTISTS" for more.
const char* resourceName(int resource, int count);

enum class OptionState : uint8_t {
  Open,    // can be paid
  Short,   // cannot be paid from what is held
  Locked,  // ends the run, and another option is open
};
OptionState optionState(const Game& game, const Cards& cards, int k);

// What an option takes or gives, as marks to draw rather than words.
struct Token {
  enum Kind : uint8_t {
    Count,   // `amount` of `resource`
    Either,  // `amount` cultists or prisoners, in any split
    Random,  // `amount` resources of any kind, chosen at random
  };
  Kind kind = Count;
  uint8_t resource = 0;
  int16_t amount = 0;
};
struct Tokens {
  uint8_t count = 0;
  Token token[kResources + 2];
};
Tokens giveTokens(const Game& game, const Cards& cards, int k);
Tokens getTokens(const Game& game, int k);
// A plain count per resource, for what was paid, gained or lost.
Tokens tokensOf(const Counts& counts);

// Every exact payment for option k that what is held can make, best first:
// relics paying for suspicion last (they keep the suspicion), then fewest
// relics, then prisoners before cultists. The first is suggest()'s. Returns
// how many there are, writing at most `max` of them; no option of the real
// cards has more than kMostPayments (a test holds that).
constexpr int kMostPayments = 64;
int payments(const Game& game, const Cards& cards, int k, Counts* out, int max);

// The ways worth offering a player: every exact payment except those that
// leave suspicion unspent which is held and asked for, paying a relic in its
// place. Keeping suspicion is never better (it invites a raid and counts
// toward Greed), so those ways are not offered at all. Same order as
// payments(), so the first is suggest()'s. Returns how many there are,
// writing at most `max`.
int choices(const Game& game, const Cards& cards, int k, Counts* out, int max);

// Whether option k asks for suspicion that is not held and does nothing else,
// and paying it would not lower Greed's chance either: relics would pay and
// change nothing. Such an option is never taken with one tap.
bool buysNothing(const Game& game, const Cards& cards, int k);

// Why option k cannot be taken, in capitals: "SHORT: 2 CULTISTS, 1 FOOD",
// "ONLY WITH NO CULTISTS", "LOCKED: ANOTHER CHOICE IS OPEN". Empty when it can.
// With `relics`, a shortfall that relics would partly cover says how much
// ("SHORT: 1 MONEY, 1 FOOD (A RELIC COVERS 1)").
void whyNot(const Game& game, const Cards& cards, int k, char* out, size_t size, bool relics = true);

// Cards added to the deck, alike titles counted together: the six "Reading
// the Necronomicon" cards are six ids with one name.
struct Adds {
  int count = 0;
  const char* title[kMaxAdds + kMaxRoll] = {};
  int copies[kMaxAdds + kMaxRoll] = {};
};
// What option k of the card on the table adds, its random cards as they were
// rolled when the card was drawn.
Adds optionAdds(const Game& game, const Cards& cards, int k);
// What the last choice added.
Adds lastAdds(const Game& game, const Cards& cards);

// What the last choice did to the deck, as one sentence: '3 x "Reading the
// Necronomicon" join the deck at the next shuffle.', 'The deck was reshuffled
// and now holds "Harvest".', '"Tips and Tricks" goes on top of the deck.'
// Empty when it did nothing to the deck.
void deckSentence(const Game& game, const Cards& cards, char* out, size_t size);

// What else an option does, in a few words: 'Adds "Aeromancy"', 'Adds 3 x
// "Reading the Necronomicon"', "See the next 3 cards", "Summons Rhybaax",
// "Ends the run". Empty when it only trades resources.
void effectLine(const Game& game, const Cards& cards, int k, char* out, size_t size);

// The chance, in percent, of each punishment striking before the next draw
// if the hand stays as it is. Each is rolled only when the ones before it
// missed (Greed, then a raid, then hunger), so a certain Greed leaves the
// other two at nothing. Rounded to the nearest percent.
Odds chances(const Game& game);

// A card's words as this screen shows them. Five tutorial lines describe the
// phone's controls (drag from your hand, the middle of the option box, the
// 'Insert' keyword, "this symbol"); these say the same for a tap and this
// layout. Every other line is the card data's own.
const char* flavorText(const Cards& cards, const Card& card);
const char* optionText(const Cards& cards, const Card& card, int k);

}  // namespace underhand::view
