#include "SeaSaltBrain.h"

#include "SeaSaltCards.h"

namespace seasalt {

namespace {

constexpr int kQuarter = 4;  // the value scale: 4 units per point

// The mermaid chase. A mermaid's printed value is a colour group, but its real
// value bends sharply upward as the collection grows, because the fourth one
// is the whole game. These are quarter-points added on top of the colour math.
constexpr int kMermaidChase[4] = {2, 6, 14, 400};

int countUnseen(const Observation& obs, const Kind kind) {
  int n = static_cast<int>(kKindSupply[static_cast<int>(kind)]);
  const uint8_t k = static_cast<uint8_t>(kind);
  for (int c = 0; c < kCards; ++c) {
    if (kCardKind[c] != k) continue;
    if (obs.view[c] != static_cast<uint8_t>(View::Unknown)) --n;
  }
  return n;
}

}  // namespace

int marginalValue(const Observation& obs, const uint8_t card) {
  const Kind kind = kindOf(card);
  int quarters = 0;

  switch (kind) {
    case Kind::Crab:
    case Kind::Boat:
    case Kind::Fish: {
      const int held = obs.countMine(kind);
      // An odd count completes a pair: a point plus the effect's card
      // advantage. An even count starts one: worth about half.
      quarters += (held % 2 == 1) ? 6 : 3;
      if (kind == Kind::Boat && obs.countMine(Kind::Lighthouse)) quarters += kQuarter;
      if (kind == Kind::Fish && obs.countMine(Kind::ShoalOfFish)) quarters += kQuarter;
      break;
    }
    case Kind::Swimmer:
      quarters += obs.countMine(Kind::Shark) > obs.countMine(Kind::Swimmer) ? 6 : 3;
      break;
    case Kind::Shark:
      quarters += obs.countMine(Kind::Swimmer) > obs.countMine(Kind::Shark) ? 6 : 3;
      break;
    case Kind::Shell: {
      const int held = obs.countMine(Kind::Shell);
      quarters += (kShellScore[held + 1] - kShellScore[held]) * kQuarter;
      // The first shell scores nothing but opens the ladder.
      if (held == 0) quarters += 2;
      break;
    }
    case Kind::Octopus: {
      const int held = obs.countMine(Kind::Octopus);
      quarters += (kOctopusScore[held + 1] - kOctopusScore[held]) * kQuarter;
      if (held == 0) quarters += 2;
      break;
    }
    case Kind::Penguin: {
      const int held = obs.countMine(Kind::Penguin);
      quarters += (kPenguinScore[held + 1] - kPenguinScore[held]) * kQuarter;
      if (obs.countMine(Kind::PenguinColony)) quarters += 2 * kQuarter;
      break;
    }
    case Kind::Sailor: {
      const int held = obs.countMine(Kind::Sailor);
      quarters += (kSailorScore[held + 1] - kSailorScore[held]) * kQuarter;
      if (obs.countMine(Kind::Captain)) quarters += 3 * kQuarter;
      break;
    }
    case Kind::Mermaid: {
      // Worth the largest colour group it can claim, plus the chase.
      int best = 0;
      for (int i = 0; i < kColourCount; ++i) {
        const int n = obs.countMineColour(static_cast<Colour>(i));
        if (n > best) best = n;
      }
      quarters += best * kQuarter + kMermaidChase[obs.countMine(Kind::Mermaid)];
      break;
    }
    case Kind::Lighthouse:
      quarters += obs.countMine(Kind::Boat) * kQuarter + 2;
      break;
    case Kind::ShoalOfFish:
      quarters += obs.countMine(Kind::Fish) * kQuarter + 2;
      break;
    case Kind::PenguinColony:
      quarters += 2 * obs.countMine(Kind::Penguin) * kQuarter + 2;
      break;
    case Kind::Captain:
      quarters += 3 * obs.countMine(Kind::Sailor) * kQuarter + 2;
      break;
  }

  // The colour tiebreak: a card that feeds my biggest group nudges the bonus
  // and the mermaids. One unit, so it never outranks a real point.
  const int inColour = obs.countMineColour(colourOf(card));
  if (inColour >= 2) quarters += 1;
  if (inColour >= 2 && obs.countMine(Kind::Mermaid) > 0) quarters += 1;

  return quarters;
}

namespace {

// The average marginal value of one fog card, in quarters. What a deck draw is
// worth before you see it, and what one card of their hand is worth to them.
int averageUnseenValue(const Observation& obs) {
  int total = 0;
  int cards = 0;
  for (int k = 0; k < kKindCount; ++k) {
    const int n = countUnseen(obs, static_cast<Kind>(k));
    if (n == 0) continue;
    // Value it as if *I* drew it: close enough for both uses, and honest --
    // valuing it for them would need their hand, which is the fog.
    const uint8_t sample = static_cast<uint8_t>(firstCardOf(static_cast<Kind>(k)));
    total += n * marginalValue(obs, sample);
    cards += n;
  }
  return cards > 0 ? total / cards : 0;
}

int myPileValue(const Observation& obs, const View pile, uint8_t* bestCard) {
  int best = -1;
  uint8_t bestAt = kNoCard;
  uint8_t bestSeq = 0;
  for (int c = 0; c < kCards; ++c) {
    if (obs.view[c] != static_cast<uint8_t>(pile)) continue;
    if (obs.seq[c] < bestSeq) continue;
    bestSeq = obs.seq[c];
    bestAt = static_cast<uint8_t>(c);
  }
  if (bestAt != kNoCard) best = marginalValue(obs, bestAt);
  if (bestCard) *bestCard = bestAt;
  return best;
}

Decision act(const Decision::Act a, const int arg = 0) {
  Decision d;
  d.act = a;
  d.a = static_cast<uint8_t>(arg);
  return d;
}

}  // namespace

int estimateTheirPoints(const Observation& obs) {
  // Their table is public: pairs laid are points banked.
  int points = obs.countView(View::TheirTable, Kind::Crab) / 2 + obs.countView(View::TheirTable, Kind::Boat) / 2 +
               obs.countView(View::TheirTable, Kind::Fish) / 2;
  const int sw = obs.countView(View::TheirTable, Kind::Swimmer);
  const int sh = obs.countView(View::TheirTable, Kind::Shark);
  points += sw < sh ? sw : sh;

  if (obs.theirRevealed) {
    // Revealed is not fog: count their hand for real. Collectors and
    // multipliers only reach a hand, never a table.
    points += kShellScore[obs.countView(View::TheirHand, Kind::Shell)];
    points += kOctopusScore[obs.countView(View::TheirHand, Kind::Octopus)];
    points += kPenguinScore[obs.countView(View::TheirHand, Kind::Penguin)];
    points += kSailorScore[obs.countView(View::TheirHand, Kind::Sailor)];
    points += obs.countView(View::TheirHand, Kind::Crab) / 2;
    points += obs.countView(View::TheirHand, Kind::Fish) / 2;
    points += obs.countView(View::TheirHand, Kind::Boat) / 2;
    return points;
  }

  // Fog cards at what a drafted hand of that size is actually worth, measured
  // over 400 brain-vs-brain matches (docs/apps/seasalt.md). Averaging the unseen
  // census overshot by 2 points per card, because a hand is not an average:
  // singles score nothing and a drafter holds few singles.
  static constexpr uint8_t kDraftedWorth[13] = {0, 0, 1, 2, 4, 5, 7, 9, 11, 13, 15, 16, 18};
  const int h = obs.theirHandSize;
  points += h < 13 ? kDraftedWorth[h] : 18 + (17 * (h - 12)) / 10;
  return points;
}

Decision decide(const Observation& obs, const Skill skill, uint32_t& rng) {
  switch (static_cast<Step>(obs.step)) {
    case Step::Take: {
      uint8_t topA = kNoCard, topB = kNoCard;
      const int valueA = myPileValue(obs, View::PileA, &topA);
      const int valueB = myPileValue(obs, View::PileB, &topB);
      const int pileBest = valueA > valueB ? valueA : valueB;
      const int pile = valueA > valueB ? 0 : 1;

      if (obs.deckRemaining == 0) {
        if (pileBest < 0) return act(Decision::Act::EndTurn);  // nothing to take
        return act(Decision::Act::TakePile, pile);
      }
      // Drawing two and keeping the better is worth more than the average
      // draw; five quarters of margin approximates that edge. Take a pile only
      // when its top visibly beats it.
      if (pileBest >= averageUnseenValue(obs) + 5) return act(Decision::Act::TakePile, pile);
      return act(Decision::Act::TakeDeck);
    }

    case Step::ChooseKeep: {
      const int v0 = obs.drawn[0] != kNoCard ? marginalValue(obs, obs.drawn[0]) : -1;
      const int v1 = obs.drawn[1] != kNoCard ? marginalValue(obs, obs.drawn[1]) : -1;
      return act(Decision::Act::Keep, v1 > v0 ? 1 : 0);
    }

    case Step::ChoosePile: {
      // The discard becomes takeable, so put it where it covers the top the
      // opponent would most want. Their want is fog; the honest proxy is the
      // card's general value, which is mine.
      const int coverA = myPileValue(obs, View::PileA, nullptr);
      const int coverB = myPileValue(obs, View::PileB, nullptr);
      return act(Decision::Act::DiscardTo, coverA >= coverB ? 0 : 1);
    }

    case Step::CrabPile: {
      // The dig takes any card in the pile, so compare the piles by their best
      // card, not their top.
      int bestA = -1, bestB = -1;
      for (int c = 0; c < kCards; ++c) {
        if (obs.view[c] == static_cast<uint8_t>(View::PileA)) {
          const int v = marginalValue(obs, static_cast<uint8_t>(c));
          if (v > bestA) bestA = v;
        } else if (obs.view[c] == static_cast<uint8_t>(View::PileB)) {
          const int v = marginalValue(obs, static_cast<uint8_t>(c));
          if (v > bestB) bestB = v;
        }
      }
      // A pile with nothing in it never wins the comparison: the core only
      // enters this step when one of them is non-empty.
      return act(Decision::Act::DigPile, bestA >= bestB ? 0 : 1);
    }

    case Step::CrabPick: {
      // Only the pile the dig chose is legal, and the observation says which
      // one that is. Scanning both and letting the core reject the wrong pile
      // would wedge on a value tie whose winner sits in the other pile.
      const View pile = obs.crabPile == 0 ? View::PileA : View::PileB;
      uint8_t bestCard = kNoCard;
      int best = -1;
      for (int c = 0; c < kCards; ++c) {
        if (obs.view[c] != static_cast<uint8_t>(pile)) continue;
        const int value = marginalValue(obs, static_cast<uint8_t>(c));
        if (value > best) {
          best = value;
          bestCard = static_cast<uint8_t>(c);
        }
      }
      return act(Decision::Act::DigCard, bestCard);
    }

    case Step::Play: {
      // Lay every pair whose effect can fire: each one is card advantage and
      // none of them moves the score. Boats first -- the extra turn compounds.
      if (obs.countView(View::MyHand, Kind::Boat) >= 2) {
        Decision d = act(Decision::Act::LayDuo);
        d.kind = Kind::Boat;
        return d;
      }
      if (obs.countView(View::MyHand, Kind::Fish) >= 2 && obs.deckRemaining > 0) {
        Decision d = act(Decision::Act::LayDuo);
        d.kind = Kind::Fish;
        return d;
      }
      const bool pilesHaveCards =
          myPileValue(obs, View::PileA, nullptr) >= 0 || myPileValue(obs, View::PileB, nullptr) >= 0;
      if (obs.countView(View::MyHand, Kind::Crab) >= 2 && pilesHaveCards) {
        Decision d = act(Decision::Act::LayDuo);
        d.kind = Kind::Crab;
        return d;
      }
      // The steal, only when it will actually take something.
      if (obs.countView(View::MyHand, Kind::Swimmer) >= 1 && obs.countView(View::MyHand, Kind::Shark) >= 1 &&
          obs.theirHandSize > 0 && !obs.theirRevealed) {
        Decision d = act(Decision::Act::LayDuo);
        d.kind = Kind::Swimmer;
        return d;
      }

      // The round-ending decision. Only offered while the phase allows it.
      if (static_cast<Phase>(obs.phase) == Phase::Playing) {
        const int mine = obs.myPoints();
        if (mine >= kMinToEndRound) {
          if (skill == Skill::Beachcomber) {
            // Stops when clearly worth banking; never bets.
            if (mine >= 10 || obs.deckRemaining <= 6) return act(Decision::Act::Stop);
          } else {
            const int theirs = estimateTheirPoints(obs);
            // The deck running out wipes everybody, so a lead near the end is
            // a lead you must bank now.
            if (obs.deckRemaining <= 4 && mine > theirs) return act(Decision::Act::Stop);
            // Bet only with margin: the bet wins ties, but their final turn
            // will grow their hand by a card or two.
            if (mine >= theirs + 3) return act(Decision::Act::LastChance);
            if (mine >= theirs && mine >= 10) return act(Decision::Act::Stop);
          }
        }
        // Behind with the deck dying: play toward the deck-out, which scores
        // nobody and keeps my banked lead intact. That is a real line in the
        // rulebook's game, not an engine trick.
      }
      (void)rng;
      return act(Decision::Act::EndTurn);
    }
  }
  return act(Decision::Act::EndTurn);
}

}  // namespace seasalt
