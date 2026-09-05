// Walks a real game to a mid-round state with a meaty hand, prints the save.
#include <cstdio>
#include <cstring>

#include "SeaSaltCards.h"
#include "SeaSaltCore.h"
using namespace seasalt;
static uint32_t r = 20260810u;
static uint32_t rnd() {
  r ^= r << 13;
  r ^= r >> 17;
  r ^= r << 5;
  return r;
}
int main(int argc, char** argv) {
  const bool wantTake = argc > 1 && argv[1][0] == 't';
  const bool wantCall = argc > 1 && argv[1][0] == 'c';
  for (int attempt = 0; attempt < 4000; ++attempt) {
    Game g;
    g.newGame(rnd() | 1u, 0);
    g.score[0] = 12;
    g.score[1] = 7;
    g.round = 2;
    int guard = 0;
    while (++guard < 500) {
      if (g.currentPhase() != Phase::Playing) break;
      // stop condition: human's Play step with a rich, varied hand
      const Step wantStep = wantTake ? Step::Take : Step::Play;
      if (g.turn == 0 && g.currentStep() == wantStep) {
        int kinds = 0;
        for (int k = 0; k < kKindCount; ++k)
          if (g.countIn(handOf(0), (Kind)k) > 0) ++kinds;
        const bool rich = !wantCall || g.cardPoints(0) >= kMinToEndRound;
        if (g.handSize(0) >= 8 && kinds >= 5 && rich && g.pileSize(0) > 0 && g.pileSize(1) > 0) {
          char hex[2 * sizeof(Game) + 1];
          const uint8_t* b = (const uint8_t*)&g;
          for (size_t i = 0; i < sizeof(Game); ++i) snprintf(hex + 2 * i, 3, "%02x", b[i]);
          printf("1 0 %s\n", hex);
          fprintf(stderr, "hand=%d points=%d tables=%d/%d piles=%d/%d deck=%d\n", g.handSize(0), g.cardPoints(0),
                  g.tableSize(0), g.tableSize(1), g.pileSize(0), g.pileSize(1), g.deckRemaining());
          return 0;
        }
      }
      switch (g.currentStep()) {
        case Step::Take: {
          bool d = g.deckRemaining() > 0;
          int p[2], n = 0;
          for (int i = 0; i < 2; ++i)
            if (g.pileTop(i) != kNoCard) p[n++] = i;
          if (d && (n == 0 || rnd() % 2))
            g.takeFromDeck();
          else if (n)
            g.takeFromPile(p[rnd() % n]);
          else
            g.endTurn();
          break;
        }
        case Step::ChooseKeep:
          g.keepDrawn(rnd() % 2);
          break;
        case Step::ChoosePile:
          g.discardTo(rnd() % 2);
          break;
        case Step::CrabPile: {
          int p[2], n = 0;
          for (int i = 0; i < 2; ++i)
            if (g.pileSize(i) > 0) p[n++] = i;
          g.chooseCrabPile(p[rnd() % n]);
          break;
        }
        case Step::CrabPick: {
          uint8_t in[58];
          int n = 0;
          for (int c = 0; c < 58; ++c)
            if (g.place[c] == (uint8_t)pileAt(g.crabPile)) in[n++] = c;
          g.takeCrabCard(in[rnd() % n]);
          break;
        }
        case Step::Play: {
          if (rnd() % 4 == 0) {
            for (int k = 0; k < 3; ++k) {
              if (g.playablePairs(g.turn, (Kind)k) > 0) {
                uint8_t a = kNoCard, b2 = kNoCard;
                for (int c = 0; c < 58; ++c) {
                  if (g.place[c] != (uint8_t)handOf(g.turn)) continue;
                  if ((int)kindOf(c) != k) continue;
                  if (a == kNoCard)
                    a = c;
                  else {
                    b2 = c;
                    break;
                  }
                }
                if (b2 != kNoCard) {
                  g.playDuo(a, b2);
                  goto done;
                }
              }
            }
          }
          g.endTurn();
        done:
          break;
        }
      }
    }
  }
  fprintf(stderr, "no state found\n");
  return 1;
}
