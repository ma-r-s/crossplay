#include "SeaSaltCore.h"

#include "SeaSaltCards.h"

namespace seasalt {

namespace {

uint32_t nextRandom(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

uint32_t streamFor(const uint32_t seed, const uint32_t stream) {
  uint32_t s = seed ^ (0x9E3779B9u * (stream + 1));
  return s ? s : 0x1234567u;  // xorshift dies on zero
}

// The shark's steal has to pick the same card on both devices, and the state is
// identical on both, so any pure function of the state does. This one moves
// with every card that has changed hands, so two steals in one turn cannot pick
// the same index.
uint32_t stealStream(const Game& g) {
  uint32_t s = streamFor(g.seed, 64u + g.seqNext);
  s ^= 0x85EBCA6Bu * (static_cast<uint32_t>(g.deckNext) + 1u);
  s ^= 0xC2B2AE35u * (static_cast<uint32_t>(g.turn) + 1u);
  nextRandom(s);
  return s ? s : 0x9E3779B9u;
}

int otherSeat(const int seat) { return seat ^ 1; }

}  // namespace

// --- card identity ---------------------------------------------------------

Kind kindOf(const uint8_t card) { return static_cast<Kind>(kCardKind[card]); }
Colour colourOf(const uint8_t card) { return static_cast<Colour>(kCardColour[card]); }

// --- set-up ----------------------------------------------------------------

uint8_t Game::cardAt(const int index) const {
  uint8_t order[kCards];
  for (int i = 0; i < kCards; ++i) order[i] = static_cast<uint8_t>(i);
  uint32_t state = streamFor(seed, 0);
  for (int i = kCards - 1; i > 0; --i) {
    const int j = static_cast<int>(nextRandom(state) % static_cast<uint32_t>(i + 1));
    const uint8_t tmp = order[i];
    order[i] = order[j];
    order[j] = tmp;
  }
  return order[index];
}

void Game::deal(const uint32_t roundSeed, const uint8_t starter) {
  seed = roundSeed;
  for (int c = 0; c < kCards; ++c) {
    place[c] = static_cast<uint8_t>(Place::Deck);
    seq[c] = 0;
  }
  seqNext = 1;
  deckNext = 0;

  // No opening hands. The first two cards turn face up as the two piles.
  for (int p = 0; p < kPiles; ++p) {
    const uint8_t card = cardAt(deckNext++);
    place[card] = static_cast<uint8_t>(pileAt(p));
    seq[card] = seqNext++;
  }

  turn = starter;
  roundStarter = starter;
  phase = static_cast<uint8_t>(Phase::Playing);
  step = static_cast<uint8_t>(Step::Take);
  ender = kNoSeat;
  betWasLastChance = 0;
  owedFinalTurns = 0;
  drawn[0] = kNoCard;
  drawn[1] = kNoCard;
  pendingDiscard = kNoCard;
  crabPile = 0;
  revealedMask = 0;
  extraTurns = 0;
}

void Game::newGame(const uint32_t roundSeed, const uint8_t starter) {
  score[0] = 0;
  score[1] = 0;
  round = 1;
  deal(roundSeed, starter);
}

// --- counting --------------------------------------------------------------

int Game::countIn(const Place where, const Kind kind) const {
  int n = 0;
  const uint8_t w = static_cast<uint8_t>(where);
  const uint8_t k = static_cast<uint8_t>(kind);
  for (int c = 0; c < kCards; ++c) {
    if (place[c] == w && kCardKind[c] == k) ++n;
  }
  return n;
}

int Game::countHeld(const int seat, const Kind kind) const {
  return countIn(handOf(seat), kind) + countIn(tableOf(seat), kind);
}

int Game::countHeldColour(const int seat, const Colour colour) const {
  const uint8_t hand = static_cast<uint8_t>(handOf(seat));
  const uint8_t table = static_cast<uint8_t>(tableOf(seat));
  const uint8_t want = static_cast<uint8_t>(colour);
  int n = 0;
  for (int c = 0; c < kCards; ++c) {
    if ((place[c] == hand || place[c] == table) && kCardColour[c] == want) ++n;
  }
  return n;
}

int Game::handSize(const int seat) const {
  const uint8_t w = static_cast<uint8_t>(handOf(seat));
  int n = 0;
  for (int c = 0; c < kCards; ++c) {
    if (place[c] == w) ++n;
  }
  return n;
}

int Game::tableSize(const int seat) const {
  const uint8_t w = static_cast<uint8_t>(tableOf(seat));
  int n = 0;
  for (int c = 0; c < kCards; ++c) {
    if (place[c] == w) ++n;
  }
  return n;
}

int Game::pileSize(const int pile) const {
  const uint8_t w = static_cast<uint8_t>(pileAt(pile));
  int n = 0;
  for (int c = 0; c < kCards; ++c) {
    if (place[c] == w) ++n;
  }
  return n;
}

uint8_t Game::pileTop(const int pile) const {
  const uint8_t w = static_cast<uint8_t>(pileAt(pile));
  uint8_t best = kNoCard;
  uint8_t bestSeq = 0;
  for (int c = 0; c < kCards; ++c) {
    if (place[c] == w && seq[c] >= bestSeq) {
      bestSeq = seq[c];
      best = static_cast<uint8_t>(c);
    }
  }
  return best;
}

// --- scoring ---------------------------------------------------------------

int Game::cardPoints(const int seat) const {
  int points = 0;

  // Duos score whether they were laid down or not; only the effect needed the
  // laying. Crab, boat and fish pair with themselves; swimmer pairs with shark.
  points += countHeld(seat, Kind::Crab) / 2;
  points += countHeld(seat, Kind::Boat) / 2;
  points += countHeld(seat, Kind::Fish) / 2;
  const int swimmers = countHeld(seat, Kind::Swimmer);
  const int sharks = countHeld(seat, Kind::Shark);
  points += swimmers < sharks ? swimmers : sharks;

  const int shells = countHeld(seat, Kind::Shell);
  const int octopuses = countHeld(seat, Kind::Octopus);
  const int penguins = countHeld(seat, Kind::Penguin);
  const int sailors = countHeld(seat, Kind::Sailor);
  points += kShellScore[shells];
  points += kOctopusScore[octopuses];
  points += kPenguinScore[penguins];
  points += kSailorScore[sailors];

  // Multipliers count the cards they name and are never one of them.
  if (countHeld(seat, Kind::Lighthouse)) points += countHeld(seat, Kind::Boat);
  if (countHeld(seat, Kind::ShoalOfFish)) points += countHeld(seat, Kind::Fish);
  if (countHeld(seat, Kind::PenguinColony)) points += 2 * penguins;
  if (countHeld(seat, Kind::Captain)) points += 3 * sailors;

  // Each mermaid takes a colour, and no two may take the same one, so the n
  // mermaids take the n largest colour groups.
  const int mermaids = countHeld(seat, Kind::Mermaid);
  if (mermaids > 0) {
    int perColour[kColourCount];
    for (int i = 0; i < kColourCount; ++i) {
      perColour[i] = countHeldColour(seat, static_cast<Colour>(i));
    }
    for (int taken = 0; taken < mermaids; ++taken) {
      int best = 0;
      int bestAt = -1;
      for (int i = 0; i < kColourCount; ++i) {
        if (perColour[i] > best) {
          best = perColour[i];
          bestAt = i;
        }
      }
      if (bestAt < 0) break;  // no colours left to claim
      points += best;
      perColour[bestAt] = 0;
    }
  }

  return points;
}

int Game::colourBonus(const int seat) const {
  int best = 0;
  for (int i = 0; i < kColourCount; ++i) {
    const int n = countHeldColour(seat, static_cast<Colour>(i));
    if (n > best) best = n;
  }
  return best;
}

bool Game::betWon() const {
  if (ender == kNoSeat) return false;
  const int mine = cardPoints(ender);
  const int theirs = cardPoints(otherSeat(ender));
  return mine >= theirs;  // a tie is the ender's, per the rulebook
}

int Game::roundScore(const int seat) const {
  if (ender == kNoSeat) return 0;  // the deck ran out: nobody scores
  if (!betWasLastChance) return cardPoints(seat);

  const bool won = betWon();
  if (seat == ender) return won ? cardPoints(seat) + colourBonus(seat) : colourBonus(seat);
  return won ? colourBonus(seat) : cardPoints(seat);
}

int Game::matchWinner() const {
  for (int s = 0; s < kSeats; ++s) {
    if (mermaidsHeld(s) == kMermaidsToWin) return s;
  }
  if (currentPhase() != Phase::GameOver) return -1;
  if (score[0] == score[1]) {
    // A tie goes to whoever played last in the final round, which is whoever
    // did not start it.
    return otherSeat(roundStarter);
  }
  return score[0] > score[1] ? 0 : 1;
}

// --- playing ---------------------------------------------------------------

namespace {

void moveToHand(Game& g, const uint8_t card, const int seat) {
  g.place[card] = static_cast<uint8_t>(handOf(seat));
  g.seq[card] = 0;  // no longer in a pile, so it has no pile order
}

// Somebody just took a card. If it was the fourth mermaid the game is over
// right now, mid-turn, before anything else happens.
bool checkMermaidWin(Game& g, const int seat) {
  if (g.mermaidsHeld(seat) < kMermaidsToWin) return false;
  g.phase = static_cast<uint8_t>(Phase::GameOver);
  return true;
}

// Banks the round and decides whether the match is over. Only called once the
// last hand that owes a turn has played it.
void bankRound(Game& g) {
  for (int s = 0; s < kSeats; ++s) {
    g.score[s] = static_cast<uint8_t>(g.score[s] + g.roundScore(s));
  }
  const bool reached = g.score[0] >= kTargetScore || g.score[1] >= kTargetScore;
  g.phase = static_cast<uint8_t>(reached ? Phase::GameOver : Phase::RoundOver);
}

}  // namespace

bool Game::takeFromDeck() {
  if (currentStep() != Step::Take || deckRemaining() < 1) return false;

  drawn[0] = cardAt(deckNext++);
  place[drawn[0]] = static_cast<uint8_t>(Place::Drawn);
  // The rulebook deals two. With one card left there is nothing to reject, so
  // the choice collapses and the card goes straight to hand.
  if (deckRemaining() >= 1) {
    drawn[1] = cardAt(deckNext++);
    place[drawn[1]] = static_cast<uint8_t>(Place::Drawn);
    step = static_cast<uint8_t>(Step::ChooseKeep);
    return true;
  }
  drawn[1] = kNoCard;
  moveToHand(*this, drawn[0], turn);
  drawn[0] = kNoCard;
  if (!checkMermaidWin(*this, turn)) step = static_cast<uint8_t>(Step::Play);
  return true;
}

bool Game::takeFromPile(const int pile) {
  if (currentStep() != Step::Take) return false;
  if (pile < 0 || pile >= kPiles) return false;
  const uint8_t card = pileTop(pile);
  if (card == kNoCard) return false;

  moveToHand(*this, card, turn);
  if (!checkMermaidWin(*this, turn)) step = static_cast<uint8_t>(Step::Play);
  return true;
}

bool Game::keepDrawn(const int which) {
  if (currentStep() != Step::ChooseKeep) return false;
  if (which < 0 || which > 1) return false;
  if (drawn[which] == kNoCard) return false;

  const uint8_t keep = drawn[which];
  const uint8_t reject = drawn[which ^ 1];
  drawn[0] = kNoCard;
  drawn[1] = kNoCard;
  moveToHand(*this, keep, turn);
  pendingDiscard = reject;

  if (checkMermaidWin(*this, turn)) return true;

  // An empty pile takes the discard: the rulebook gives no choice there.
  for (int p = 0; p < kPiles; ++p) {
    if (pileTop(p) == kNoCard) return discardTo(p);
  }
  step = static_cast<uint8_t>(Step::ChoosePile);
  return true;
}

bool Game::discardTo(const int pile) {
  if (pendingDiscard == kNoCard) return false;
  if (currentStep() != Step::ChooseKeep && currentStep() != Step::ChoosePile) return false;
  if (pile < 0 || pile >= kPiles) return false;

  place[pendingDiscard] = static_cast<uint8_t>(pileAt(pile));
  seq[pendingDiscard] = seqNext++;
  pendingDiscard = kNoCard;
  step = static_cast<uint8_t>(Step::Play);
  return true;
}

bool Game::playDuo(const uint8_t a, const uint8_t b) {
  if (currentStep() != Step::Play) return false;
  if (a == b || a >= kCards || b >= kCards) return false;
  const uint8_t mine = static_cast<uint8_t>(handOf(turn));
  if (place[a] != mine || place[b] != mine) return false;

  const Kind ka = kindOf(a);
  const Kind kb = kindOf(b);
  const bool matched = (ka == kb && (ka == Kind::Crab || ka == Kind::Boat || ka == Kind::Fish)) ||
                       (ka == Kind::Swimmer && kb == Kind::Shark) || (ka == Kind::Shark && kb == Kind::Swimmer);
  if (!matched) return false;

  const uint8_t table = static_cast<uint8_t>(tableOf(turn));
  place[a] = table;
  place[b] = table;
  seq[a] = 0;
  seq[b] = 0;

  switch (ka == Kind::Shark ? kb : ka) {
    case Kind::Crab:
      // Only worth a screen if there is something to dig through.
      if (pileSize(0) > 0 || pileSize(1) > 0) step = static_cast<uint8_t>(Step::CrabPile);
      return true;
    case Kind::Boat:
      ++extraTurns;
      return true;
    case Kind::Fish:
      if (deckRemaining() > 0) {
        moveToHand(*this, cardAt(deckNext++), turn);
        checkMermaidWin(*this, turn);
      }
      return true;
    case Kind::Swimmer: {
      // A revealed hand is out of reach: that is the whole protection the final
      // turns of a LAST CHANCE round buy.
      const int victim = otherSeat(turn);
      if (revealedMask & (1u << victim)) return true;
      const int held = handSize(victim);
      if (held == 0) return true;
      uint32_t state = stealStream(*this);
      int wanted = static_cast<int>(nextRandom(state) % static_cast<uint32_t>(held));
      const uint8_t theirs = static_cast<uint8_t>(handOf(victim));
      for (int c = 0; c < kCards; ++c) {
        if (place[c] != theirs) continue;
        if (wanted-- > 0) continue;
        moveToHand(*this, static_cast<uint8_t>(c), turn);
        checkMermaidWin(*this, turn);
        break;
      }
      return true;
    }
    default:
      return true;
  }
}

bool Game::chooseCrabPile(const int pile) {
  if (currentStep() != Step::CrabPile) return false;
  if (pile < 0 || pile >= kPiles || pileSize(pile) == 0) return false;
  crabPile = static_cast<uint8_t>(pile);
  step = static_cast<uint8_t>(Step::CrabPick);
  return true;
}

bool Game::takeCrabCard(const uint8_t card) {
  if (currentStep() != Step::CrabPick || card >= kCards) return false;
  if (place[card] != static_cast<uint8_t>(pileAt(crabPile))) return false;

  moveToHand(*this, card, turn);
  if (!checkMermaidWin(*this, turn)) step = static_cast<uint8_t>(Step::Play);
  return true;
}

bool Game::canTake() const {
  if (deckRemaining() > 0) return true;
  for (int p = 0; p < kPiles; ++p) {
    if (pileTop(p) != kNoCard) return true;
  }
  return false;
}

bool Game::endTurn() {
  // Step::Take is allowed here only when there is genuinely nothing to take,
  // so a player can never skip the draw they owe.
  if (currentStep() == Step::Take && !canTake()) {
    step = static_cast<uint8_t>(Step::Play);
  }
  if (currentStep() != Step::Play) return false;

  // A boat pair buys another whole turn, taken before anybody else moves.
  if (extraTurns > 0) {
    --extraTurns;
    step = static_cast<uint8_t>(Step::Take);
    return true;
  }

  // A bet already on the table outranks the empty deck: the final turns are
  // owed, they can still be taken off a pile, and the bet resolves.
  if (currentPhase() == Phase::LastChance) {
    owedFinalTurns = static_cast<uint8_t>(owedFinalTurns & ~(1u << turn));
    revealedMask = static_cast<uint8_t>(revealedMask | (1u << turn));
    if (owedFinalTurns == 0) {
      bankRound(*this);
      return true;
    }
    turn = static_cast<uint8_t>(otherSeat(turn));
    step = static_cast<uint8_t>(Step::Take);
    return true;
  }

  // "If the deck is empty at the end of a player's turn, the round ends
  // immediately without scoring." `ender` stays unset, and that is what
  // roundScore reads to pay nobody.
  if (deckRemaining() == 0) {
    phase = static_cast<uint8_t>(Phase::RoundOver);
    return true;
  }

  turn = static_cast<uint8_t>(otherSeat(turn));
  step = static_cast<uint8_t>(Step::Take);
  return true;
}

bool Game::endRound(const bool lastChance) {
  if (currentStep() != Step::Play || currentPhase() != Phase::Playing) return false;
  if (!mayEndRound(turn)) return false;

  ender = turn;
  betWasLastChance = lastChance ? 1 : 0;
  revealedMask = static_cast<uint8_t>(revealedMask | (1u << turn));

  if (!lastChance) {
    bankRound(*this);
    return true;
  }

  // Every opponent owes one more full turn, and their hand is safe once taken.
  phase = static_cast<uint8_t>(Phase::LastChance);
  owedFinalTurns = 0;
  for (int s = 0; s < kSeats; ++s) {
    if (s != ender) owedFinalTurns = static_cast<uint8_t>(owedFinalTurns | (1u << s));
  }
  turn = static_cast<uint8_t>(otherSeat(turn));
  step = static_cast<uint8_t>(Step::Take);
  return true;
}

int Game::playablePairs(const int seat, const Kind kind) const {
  const Place hand = handOf(seat);
  switch (kind) {
    case Kind::Crab:
    case Kind::Boat:
    case Kind::Fish:
      return countIn(hand, kind) / 2;
    case Kind::Swimmer:
    case Kind::Shark: {
      const int swimmers = countIn(hand, Kind::Swimmer);
      const int sharks = countIn(hand, Kind::Shark);
      return swimmers < sharks ? swimmers : sharks;
    }
    default:
      return 0;
  }
}

}  // namespace seasalt
