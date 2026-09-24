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

// "2 MONEY, 1 FOOD", "3 CULTISTS OR PRISONERS", "5 AT RANDOM",
// "ONLY WITH NO CULTISTS". Empty when the option is free.
void costLine(const Game& game, const Cards& cards, int k, char* out, size_t size);
// "+1 FOOD, +2 MONEY". Empty when it gives nothing.
void gainLine(const Game& game, int k, char* out, size_t size);
// "PAID 2 MONEY. GAINED 1 FOOD." with any random loss after. Empty before the
// first choice of a run.
void lastTurnLine(const Game& game, char* out, size_t size);

// What an option takes or gives, as marks to draw rather than words.
struct Token {
  enum Kind : uint8_t {
    Count,      // `amount` of `resource`
    Either,     // `amount` cultists or prisoners, in any split
    Random,     // `amount` resources of any kind, chosen at random
    OnlyIfNone  // payable only while none of `resource` is held
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
// What will actually leave the hand if option k is taken: the suggested
// payment when it can be paid, the cost as asked when it cannot. `relicStandsIn`
// says a relic covers something not held.
Tokens payTokens(const Game& game, const Cards& cards, int k, bool* relicStandsIn = nullptr);
// A plain count per resource, for what was paid, gained or lost.
Tokens tokensOf(const Counts& counts);

// Every exact payment for option k that what is held can make, best first:
// fewest relics, then prisoners before cultists. The first is suggest()'s.
// Returns how many there are, writing at most `max` of them.
int payments(const Game& game, const Cards& cards, int k, Counts* out, int max);

// Why option k cannot be taken, in capitals: "SHORT: 2 CULTISTS, 1 FOOD",
// "ONLY WITH NO CULTISTS", "LOCKED: ANOTHER CHOICE IS OPEN". Empty when it can.
void whyNot(const Game& game, const Cards& cards, int k, char* out, size_t size);

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

// What the last choice did to the deck, as one sentence: "3 x Reading the
// Necronomicon join the deck at the next shuffle.", "The deck was reshuffled
// and now holds Harvest.", "Tips and Tricks goes on top of the deck." Empty
// when it did nothing to the deck.
void deckSentence(const Game& game, const Cards& cards, char* out, size_t size);

// What else an option does, in a few words: "Adds Aeromancy", "Adds 3 x
// Reading the Necronomicon", "See the next 3 cards", "Summons Rhybaax",
// "Ends the run". Empty when it only trades resources.
void effectLine(const Game& game, const Cards& cards, int k, char* out, size_t size);

// A card's words as this screen shows them. Five tutorial lines describe the
// phone's controls (drag from your hand, the middle of the option box, the
// 'Insert' keyword, "this symbol"); these say the same for a tap and this
// layout. Every other line is the card data's own.
const char* flavorText(const Cards& cards, const Card& card);
const char* optionText(const Cards& cards, const Card& card, int k);

// The danger the original signals: the punishment each count invites before
// the next draw. Empty during the tutorial, which has none.
struct Danger {
  bool food = false;       // none held: Desperate Measures, 20%
  bool suspicion = false;  // 5 or more: Police Raid
  bool total = false;      // 16 or more in all: Greed
};
Danger danger(const Game& game);

}  // namespace underhand::view
