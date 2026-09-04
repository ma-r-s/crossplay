// Navigation, and the machine that composes a move out of taps.
//
// The property worth having is the last one: driving the draft machine to
// Ask::Ready always produces a move the rules accept. The board and the
// rulebook are two judges of what a legal move is, and this is the test that
// stops them drifting.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ToyBattleCore.h"
#include "ToyBattleFlow.h"

using namespace toybattle;

static constexpr int kPG = static_cast<int>(TerrainId::ProvingGround);
static int checks = 0;

static void check(bool ok, const char* what) {
  ++checks;
  if (!ok) {
    printf("FAIL: %s\n", what);
    abort();
  }
}

static uint32_t rngState = 0xBEEF01u;
static uint32_t rnd() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}

// --- the shell -------------------------------------------------------------

static void testNavigation() {
  const Screen all[] = {Screen::Menu,  Screen::Setup, Screen::MapPick, Screen::Lobby,
                        Screen::HowTo, Screen::Board, Screen::Brief,   Screen::Result};
  check(sizeof(all) / sizeof(all[0]) == kScreenCount, "every screen is in the list under test");

  int exits = 0;
  for (Screen s : all) {
    if (leavesApp(s)) ++exits;

    // Back always reaches the top, and quickly.
    Screen at = s;
    int hops = 0;
    while (at != Screen::Menu && hops < kScreenCount + 1) {
      at = back(at);
      ++hops;
    }
    check(at == Screen::Menu, "every screen reaches the menu by pressing Back");
    // Two screens hang off another rather than off the menu: the briefing off
    // the board, and the map list off setup. Everything else is one press from
    // the top, and nothing is ever three.
    const bool nested = s == Screen::Brief || s == Screen::MapPick;
    check(hops <= (nested ? 2 : 1), "and the shell stays shallow");
  }
  check(exits == 1, "exactly one screen leaves the app");

  // No pair can Back into each other, which is the shape that traps a player.
  for (Screen s : all) {
    if (s == Screen::Menu) continue;
    check(back(back(s)) != s, "no two screens Back into each other");
  }
}

// --- driving the board ------------------------------------------------------

// Answers whatever is being asked, at random from the legal answers, until the
// draft is ready. Returns false if it ever gets stuck, which would be a machine
// that can ask a question with no answer.
static Ask lastAsk = Ask::Ready;
static const char* askName(Ask a) {
  switch (a) {
    case Ask::Troop:
      return "Troop";
    case Ask::Slot:
      return "Slot";
    case Ask::JumboVictim:
      return "JumboVictim";
    case Ask::DrawOffer:
      return "DrawOffer";
    case Ask::StealOffer:
      return "StealOffer";
    case Ask::ChainOffer:
      return "ChainOffer";
    case Ask::RecallFrom:
      return "RecallFrom";
    case Ask::ShoveFrom:
      return "ShoveFrom";
    case Ask::ShoveTo:
      return "ShoveTo";
    case Ask::ExhumeKind:
      return "ExhumeKind";
    case Ask::BaseOffer:
      return "BaseOffer";
    case Ask::Ready:
      return "Ready";
  }
  return "?";
}

// One answer, chosen at random from the ones the board accepts.
static bool answerOne(const Game& g, Draft& d) {
  {
    const Ask a = pending(g, d);
    lastAsk = a;
    if (a == Ask::Ready) return true;

    // Every answer the question could take, in a shuffled order, and the first
    // one the machine accepts wins. A refused answer is not a stuck machine --
    // it is the board correctly saying "not that one", which is exactly what
    // stops a player painting themselves into a corner. Only a question where
    // *nothing* is accepted is a defect.
    bool moved = false;

    if (a == Ask::Troop) {
      const uint8_t offer = candidateTroops(g, d);
      const int spin = static_cast<int>(rnd() % kTroopKinds);
      for (int i = 0; i < kTroopKinds && !moved; ++i) {
        const int k = (spin + i) % kTroopKinds;
        if (offer & (1u << k)) moved = answerTroop(g, d, static_cast<Troop>(k));
      }
      if (!moved) return false;
      return true;
    }

    if (a == Ask::ExhumeKind) {
      const int spin = static_cast<int>(rnd() % kTroopKinds);
      for (int i = 0; i < kTroopKinds && !moved; ++i) {
        const int k = (spin + i) % kTroopKinds;
        if (g.discarded[g.turn][k]) moved = answerTarget(g, d, k);
      }
      if (!moved) moved = answerOffer(g, d, false);
      if (!moved) return false;
      return true;
    }

    if (a == Ask::Slot || a == Ask::JumboVictim || a == Ask::RecallFrom || a == Ask::ShoveFrom || a == Ask::ShoveTo) {
      const uint64_t mask = candidateSlots(g, d);
      // Declining a targeted effect is tried first half the time, so the "no
      // thanks" path gets exercised as hard as the "yes" path.
      const bool declineFirst = a != Ask::Slot && (rnd() & 1);
      if (declineFirst) moved = answerOffer(g, d, false);
      const int spin = static_cast<int>(rnd() % kMaxSlots);
      for (int i = 0; i < kMaxSlots && !moved; ++i) {
        const int s = (spin + i) % kMaxSlots;
        if (!(mask & (uint64_t{1} << s))) continue;
        moved = a == Ask::Slot ? answerSlot(g, d, s) : answerTarget(g, d, s);
      }
      if (!moved && a != Ask::Slot) moved = answerOffer(g, d, false);
      if (!moved) return false;
      return true;
    }

    // A plain offer: take it or leave it, and one of the two must work.
    const bool wantIt = (rnd() & 1) != 0;
    if (!answerOffer(g, d, wantIt) && !answerOffer(g, d, !wantIt)) return false;
  }
  return true;
}

static bool driveToReady(const Game& g, Draft& d, int* asked) {
  for (int guard = 0; guard < 40; ++guard) {
    if (pending(g, d) == Ask::Ready) return true;
    ++*asked;
    if (!answerOne(g, d)) return false;
  }
  return false;
}

static void testTheBoardCannotComposeAnIllegalMove(int matches, int terrain) {
  long moves = 0, questions = 0;
  int reachedReady = 0;

  for (int m = 0; m < matches; ++m) {
    Game g;
    g.newGame(rnd(), terrain, static_cast<int>(rnd() & 1u));

    int turns = 0;
    while (g.currentPhase() == Phase::Playing && turns++ < 400) {
      if (!g.hasAnyLegalMove(g.turn)) break;

      // Drawing is the other thing a turn can be, and the draft machine only
      // ever composes placements, so mix both in.
      if (g.isLegal(Move::draw()) && (rnd() % 4) == 0) {
        check(g.apply(Move::draw()), "a draw applies");
        ++moves;
        continue;
      }

      Draft d;
      check(d.empty(), "a fresh draft is empty");
      int asked = 0;
      if (!driveToReady(g, d, &asked)) {
        // Only acceptable when the seat genuinely had no placement to make.
        Step scratch[1];
        if (g.legalPlacements(g.turn, scratch, 1) != 0) {
          printf("STUCK on %s: step=%d stepCount=%d slotChosen=%d effectAnswered=%d baseAnswered=%d kind=%d slot=%d\n",
                 askName(lastAsk), d.step, d.move.stepCount, d.slotChosen, d.effectAnswered, d.baseAnswered,
                 d.move.steps[d.step].kind, d.move.steps[d.step].slot);
        }
        check(g.legalPlacements(g.turn, scratch, 1) == 0, "the draft machine got stuck with a placement available");
        check(g.apply(Move::draw()), "so the seat draws instead");
        ++moves;
        continue;
      }
      ++reachedReady;
      questions += asked;

      // THE PROPERTY. Anything the board could have built, the rulebook takes.
      check(g.isLegal(d.move), "a completed draft is a legal move");
      check(g.apply(d.move), "and it applies");
      ++moves;
    }
  }

  check(reachedReady > 0, "the draft machine actually composed moves");
  printf("flow %-14s %d matches, %ld moves, %d drafts, %ld questions asked\n", terrainAt(terrain).name, matches, moves,
         reachedReady, questions);
}

// --- targeted --------------------------------------------------------------

static void put(Game& g, int seat, int base, Troop kind) {
  g.placeSlot[g.placementCount] = static_cast<uint8_t>(base);
  g.placeTile[g.placementCount] = static_cast<uint8_t>((seat << 3) | static_cast<int>(kind));
  ++g.placementCount;
}

static Game bare() {
  Game g;
  g.newGame(1u, kPG, 0);
  for (int k = 0; k < kTroopKinds; ++k) {
    g.rack[0][k] = 0;
    g.rack[1][k] = 0;
  }
  g.placementCount = 0;
  return g;
}

static void testQuestionsWithNoAnswerAreNotAsked() {
  // Jumbo with nobody next to it is not asked who to hit.
  Game g = bare();
  g.rack[0][static_cast<int>(Troop::Jumbo)] = 1;
  Draft d;
  check(answerTroop(g, d, Troop::Jumbo), "pick Jumbo");
  check(pending(g, d) == Ask::Slot, "it wants a slot");
  check(answerSlot(g, d, 0), "put it next to nobody");
  check(pending(g, d) == Ask::Ready, "and it is not asked for a victim it does not have");
  check(g.isLegal(d.move), "the move stands");

  // With an enemy alongside, it is asked.
  Game h = bare();
  h.rack[0][static_cast<int>(Troop::Jumbo)] = 1;
  put(h, 1, 1, Troop::Skully);
  Draft e;
  check(answerTroop(h, e, Troop::Jumbo), "pick Jumbo");
  check(answerSlot(h, e, 0), "place it beside them");
  check(pending(h, e) == Ask::JumboVictim, "now it asks");
  check(candidateSlots(h, e) == (uint64_t{1} << 1), "and offers exactly the one victim");
  check(answerOffer(h, e, false), "declining is allowed");
  check(pending(h, e) == Ask::Ready, "which finishes the move");
  check(h.isLegal(e.move), "and it is legal");
}

static void testCapnChainIsComposed() {
  Game g = bare();
  g.rack[0][static_cast<int>(Troop::Capn)] = 1;
  g.rack[0][static_cast<int>(Troop::Roxy)] = 1;

  Draft d;
  check(answerTroop(g, d, Troop::Capn), "pick the Cap'n");
  check(answerSlot(g, d, 0), "place it");
  check(pending(g, d) == Ask::ChainOffer, "it offers a second placement");
  check(answerOffer(g, d, true), "take it");
  check(pending(g, d) == Ask::Troop, "which asks for another troop");
  check(answerTroop(g, d, Troop::Roxy), "pick it");
  check(pending(g, d) == Ask::Slot, "and where it goes");
  check(answerSlot(g, d, 5), "place it");
  check(pending(g, d) == Ask::Ready, "the turn is complete");
  check(d.move.stepCount == 2, "as two placements in one move");
  check(g.isLegal(d.move), "and the rules accept it");

  // Declining leaves a one-placement move.
  Draft e;
  check(answerTroop(g, e, Troop::Capn) && answerSlot(g, e, 0), "place a Cap'n again");
  check(answerOffer(g, e, false), "decline the extra");
  check(pending(g, e) == Ask::Ready && e.move.stepCount == 1, "one placement, and done");
  check(g.isLegal(e.move), "also legal");
}

static void testOnlyTheQuestionBeingAskedIsAnswerable() {
  Game g = bare();
  g.rack[0][static_cast<int>(Troop::Roxy)] = 1;
  Draft d;
  // A slot answer before a troop is chosen must bounce, or a stray tap on the
  // board would half-build a move.
  check(!answerSlot(g, d, 0), "a slot cannot be chosen before a troop");
  check(!answerOffer(g, d, true), "and there is no offer to take yet");
  check(!answerTarget(g, d, 0), "nor a target");
  check(d.empty(), "so nothing was recorded");

  check(answerTroop(g, d, Troop::Roxy), "pick a troop");
  check(!answerTroop(g, d, Troop::Roxy), "the same question cannot be answered twice");
  check(!answerSlot(g, d, 7), "a slot outside the candidates is refused");
  check(candidateSlots(g, d) != 0, "there are candidates");
  check((candidateSlots(g, d) & (uint64_t{1} << 7)) == 0, "and 7 is not one of them");
}

// A refusal has to agree with the thing that refused. If the board can light a
// slot but not explain it, or explains one it would have accepted, the message
// under the title is lying.
static void testEveryRefusalAgreesWithTheRules(int matches) {
  long lit = 0, refused = 0;
  for (int m = 0; m < matches; ++m) {
    Game g;
    g.newGame(rnd(), static_cast<int>(TerrainId::CastleField), static_cast<int>(rnd() & 1u));
    int turns = 0;
    while (g.currentPhase() == Phase::Playing && turns++ < 200) {
      if (!g.hasAnyLegalMove(g.turn)) break;
      Draft d;
      int guard = 0;
      // One answer at a time, checking every slot at every state along the
      // way. Driving straight to Ready only ever samples "pick a troop", where
      // nothing is lit and the check passes without meaning anything.
      while (pending(g, d) != Ask::Ready && guard++ < 12) {
        const uint64_t candidates = candidateSlots(g, d);
        for (int slot = 0; slot < g.board().slotCount(); ++slot) {
          const bool candidate = (candidates & (uint64_t{1} << slot)) != 0;
          check(candidate == (whyNotSlot(g, d, slot) == Refusal::None), "a slot is lit exactly when it has no refusal");
          candidate ? ++lit : ++refused;
        }
        const uint8_t offer = candidateTroops(g, d);
        for (int k = 0; k < kTroopKinds; ++k) {
          check(((offer & (1u << k)) != 0) == (whyNotTroop(g, d, static_cast<Troop>(k)) == Refusal::None),
                "a troop is offered exactly when it has no refusal");
        }
        if (!answerOne(g, d)) break;
      }
      if (pending(g, d) != Ask::Ready) break;
      check(g.apply(d.move), "the drafted move applies");
    }
  }
  check(lit > 0, "the board lit something along the way");
  printf("refusals   %ld slots lit, %ld refused, every one agreeing with the rules\n", lit, refused);
}

// --- continuing -------------------------------------------------------------

static void testASaveSurvivesOnlyIfItIsIntact() {
  // A position with some history in it, so the bytes under test are not mostly
  // zeroes -- a checksum looks strong against a save that is nearly empty.
  Game g;
  g.newGame(0xC0FFEEu, static_cast<int>(TerrainId::CastleField), 0, true);
  for (int i = 0; i < 12 && g.currentPhase() == Phase::Playing; ++i) {
    Draft d;
    int asked = 0;
    if (!driveToReady(g, d, &asked)) break;
    g.apply(d.move);
  }

  Saved saved;
  saved.options.terrain = static_cast<uint8_t>(TerrainId::CastleField);
  saved.options.skill = Skill::General;
  saved.options.specialBases = true;
  saved.options.mode = Mode::Solo;
  saved.game = g;
  saved.seat = 1;

  uint8_t bytes[kSaveBytes];
  const int n = encodeSave(saved, bytes);
  check(n == kSaveBytes, "a save is exactly the size it claims");

  Saved back;
  check(decodeSave(bytes, n, back), "an intact save decodes");
  check(back.seat == saved.seat, "and the seat survives");
  check(back.options.skill == saved.options.skill, "and the difficulty survives");
  check(back.options.specialBases == saved.options.specialBases, "and the special bases setting survives");
  check(back.game.placementCount == g.placementCount, "and the position survives");
  check(memcmp(&back.game, &g, sizeof(Game)) == 0, "byte for byte");

  // The property worth having: NO single-bit corruption is ever accepted. A
  // card that lost power mid-write is the ordinary failure here, and a save
  // that decodes into a plausible wrong position is worse than one that does
  // not decode at all.
  for (int byte = 0; byte < kSaveBytes; ++byte) {
    for (int bit = 0; bit < 8; ++bit) {
      uint8_t corrupt[kSaveBytes];
      memcpy(corrupt, bytes, sizeof(corrupt));
      corrupt[byte] = static_cast<uint8_t>(corrupt[byte] ^ (1u << bit));
      Saved wrong;
      check(!decodeSave(corrupt, kSaveBytes, wrong), "no single flipped bit is ever accepted");
    }
  }

  // Truncation, at every length. A short read is what a half-written file is.
  for (int len = 0; len < kSaveBytes; ++len) {
    Saved wrong;
    check(!decodeSave(bytes, len, wrong), "a truncated save is refused at every length");
  }

  // And a finished game is not offered as one to continue.
  check(isResumable(saved), "a game in progress can be continued");
  Saved link = saved;
  link.options.mode = Mode::Link;
  check(!isResumable(link), "a link game cannot: the other device is not there");
}

int main() {
  testNavigation();
  testQuestionsWithNoAnswerAreNotAsked();
  testCapnChainIsComposed();
  testOnlyTheQuestionBeingAskedIsAnswerable();
  testTheBoardCannotComposeAnIllegalMove(120, kPG);
  testTheBoardCannotComposeAnIllegalMove(120, static_cast<int>(TerrainId::CastleField));
  testEveryRefusalAgreesWithTheRules(40);
  testASaveSurvivesOnlyIfItIsIntact();

  printf("flow       %d checks, 0 failed\n", checks);
  return 0;
}
