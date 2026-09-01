#pragma once

// Murdle: the puzzle, with nothing else in it.
//
// Freestanding C++17 -- no renderer, no storage, no clock, no Arduino -- so
// host-tests/murdle/ can generate tens of thousands of cases on a laptop and
// assert that every single one of them is a fair puzzle. See docs/apps/murdle.md for
// the design and docs/shelf.md for the three-way split.
//
// THE GAME. Every suspect has a weapon and a location, and at the top tiers a
// motive. Those assignments are a bijection: one suspect per location, one
// weapon per suspect, nobody shares. Exactly one of the suspects is the
// murderer. You solve the whole assignment from a list of clues, and one
// further clue names the crime scene, which is what pins the murderer.
//
// THE ONE PREDICATE. The book's tutorial uses five clues that read like five
// different kinds of sentence ("whoever was in the stadium was right-handed",
// "the suspect with the pencil resented the person at Old Main", "the body was
// found next to peeling paint"). Logically they are all the same thing:
//
//     the row selected by ANCHOR has, in category `targetCat`,
//     a value drawn from `targetMask`.
//
// A direct positive is a mask with one bit set. A negative is one bit clear. An
// attribute clue is the set of suspects carrying that attribute. An either/or
// is two bits. The murder clue is the same predicate with the row selected by
// "whichever row the murderer is in" instead of by a named item.
//
// That collapse is the whole reason this file is short. One predicate means one
// solver, one verifier, one save format, and all the variety lives in the
// sentences (MurdleText) rather than in the logic.

#include <cstdint>

namespace murdle {

// Four categories, four items. The ceiling is not arbitrary: the solver
// enumerates the entire candidate space, which is (items!)^(cats-1) assignments
// times `items` choices of murderer. At 4x4 that is 24^3 * 4 = 55,296, and the
// survivor set is one bit each, 6,912 bytes. Five items would be 120^3 * 5 =
// 8.6 million and the bit set alone would not fit in this device's RAM, so five
// is refused rather than quietly made slow. Three or four of each is what the
// game asks for anyway.
constexpr int kMaxCats = 4;
constexpr int kMaxItems = 4;
constexpr int kMaxClues = 24;

// The categories, in the order they index everything. Suspects are category 0
// and are the axis every assignment is expressed against, which is why
// `assign[0][row] == row` always holds.
enum class Cat : uint8_t {
  Suspect = 0,
  Weapon = 1,
  Location = 2,
  Motive = 3,
};

constexpr int kCatCount = 4;

// The pairwise blocks of the deduction grid, one per unordered pair of
// categories. Three blocks at three categories, six at four.
constexpr int kMaxBlocks = kMaxCats * (kMaxCats - 1) / 2;

// ---------------------------------------------------------------------------
// Difficulty

// The two axes you actually feel: how big the grid is, and how indirect the
// clues are allowed to be. Both move together up this ladder.
enum class Tier : uint8_t {
  Elementary,  // 3 categories of 3, direct positives and negatives
  Nosy,        // 3 categories of 4, attribute clues, no bare positives
  HardBoiled,  // 4 categories of 4, motives in play, either/or clues
  Impossible,  // 4 categories of 4, and the murderer's own statement is a lie
};

constexpr int kTierCount = 4;

struct Shape {
  uint8_t cats = 3;
  uint8_t items = 3;

  int rows() const { return items; }
  int blocks() const { return cats * (cats - 1) / 2; }
};

Shape shapeOf(Tier tier);

// Total assignments times murderer choices, for the tier's shape. Always at or
// below kMaxCandidates.
int candidateCount(Shape shape);

constexpr int kMaxCandidates = 55296;  // 24^3 * 4
constexpr int kMaxPerms = 24;          // 4!
constexpr int kMaxPool = 768;          // candidate clues held while generating

// ---------------------------------------------------------------------------
// The clue

// Which row the predicate is about.
enum class Anchor : uint8_t {
  // The row holding item `anchorItem` of category `anchorCat`.
  Item,
  // Whichever row the murderer is in. Only the murder clue uses this, and it is
  // the only thing in a case that can determine who the murderer is.
  Murderer,
};

// One clue. Six bytes, trivially copyable, no pointers: a Puzzle is memcpy-able
// and could go over the link layer unchanged if this game ever grows a second
// player.
struct Clue {
  Anchor anchor = Anchor::Item;
  uint8_t anchorCat = 0;
  uint8_t anchorItem = 0;
  uint8_t targetCat = 0;
  // Bit i set means "item i of targetCat is still allowed". A single set bit is
  // a positive clue; a single clear bit is a negative one.
  uint8_t targetMask = 0;
  // Which suspect says this out loud, or kNobodySpeaks. A spoken clue is TRUE
  // unless its speaker is the murderer, in which case it is FALSE: that is the
  // entire Impossible tier, and it is what forces the murderer's identity and
  // the assignment to be solved together rather than one after the other.
  uint8_t speaker = 0;
  // A seed for MurdleText's choice of sentence. Carries no logical meaning, and
  // lives here only so that a case regenerated from its seed words itself the
  // same way it did the first time.
  uint8_t voice = 0;
  // Which attribute produced targetMask, or kNoAttr. Recorded rather than
  // inferred: the same mask can arise both as "the left-handed ones" and as a
  // plain either/or, and a sentence that guessed wrong would be describing a
  // different clue from the one the solver checked.
  uint8_t attr = 0xFF;
};

constexpr uint8_t kNobodySpeaks = 0xFF;
constexpr uint8_t kNoAttr = 0xFF;

// ---------------------------------------------------------------------------
// The puzzle

struct Puzzle {
  Shape shape;
  uint32_t seed = 0;
  Tier tier = Tier::Elementary;

  // assign[cat][row] is the item of `cat` in that row. Row r is suspect r, so
  // assign[0][r] == r by construction.
  uint8_t assign[kMaxCats][kMaxItems] = {};

  // Which cast entry each item is drawn from: cast[cat][item] indexes into the
  // fixture tables in MurdleCast.h. The logic never reads this; the sentences
  // and the screens do.
  uint8_t cast[kMaxCats][kMaxItems] = {};

  uint8_t murderRow = 0;

  Clue clues[kMaxClues] = {};
  uint8_t clueCount = 0;

  // How many rounds of propagation the reference solver needed. A measured
  // difficulty rather than a guessed one; see deduce().
  uint8_t rounds = 0;

  int itemOf(int cat, int row) const { return assign[cat][row]; }
  // The row holding item `item` of category `cat`. Linear over at most four.
  int rowOf(int cat, int item) const;
};

// True when the clue holds against this assignment and this murderer, taking
// the speaker's honesty into account.
bool clueHolds(const Clue& clue, const Puzzle& puzzle);

// A stable hash of everything a player could see. The save file stores this
// beside the seed, so that a case regenerated by a *changed* generator is
// detected and dropped rather than restored on top of somebody's marks.
uint32_t fingerprint(const Puzzle& puzzle);

// ---------------------------------------------------------------------------
// The grid

enum class Mark : uint8_t {
  Unknown = 0,
  No = 1,
  Yes = 2,
};

// The SOLVER's grid: one cell per pair of items from two different categories.
//
// The player's board is `Marks`, below, and keeping them apart is the point.
// They want opposite things from a write: the solver must never overrule itself,
// because a rule that overwrites a decided cell is a rule that has gone wrong,
// while the player must be able to change their mind about any cell -- by
// clearing it, which is a move the solver has no use for at all.
// One class serving both carried a comment admitting it -- "put() refuses to
// overwrite a decided cell, which is right for the solver and wrong here" -- and
// that sentence was the shape of four separate bugs.
//
// What they still share is the CELL layout, which matters: a solver reasoning in
// a different representation from the one the player marks is a solver that can
// certify a puzzle the player cannot fill in.
class Grid {
 public:
  void reset(Shape shape);

  // Order-independent: get(Weapon, 2, Suspect, 1) and get(Suspect, 1, Weapon, 2)
  // are the same cell. The screens ask in whichever order the axes are drawn,
  // and making the caller normalise would be one more thing to get wrong.
  Mark get(int catA, int itemA, int catB, int itemB) const;

  // Returns false if this contradicts what is already there. Setting a cell to
  // what it already holds is not a contradiction.
  bool set(int catA, int itemA, int catB, int itemB, Mark mark);

  // Back to Unknown, unconditionally. set() refuses to write over a decided
  // cell because the propagation solver must never quietly overrule itself; a
  // player cycling a cell through its three states means exactly that, so they
  // get their own door rather than a flag on set() that the solver could reach.
  void clear(int catA, int itemA, int catB, int itemB);

  // Every cell of every block decided.
  bool complete() const;

  // How many cells are decided, and how many there are. The only honest measure
  // of progress this game has: clues ticked says what you have read, this says
  // what you have worked out.
  int decided() const;
  int cells() const;

  Shape shape() const { return shape_; }

 private:
  static int blockIndex(int catA, int catB);
  // -1 contradiction, 0 already held that value, 1 changed. The propagation
  // loop needs to know whether anything moved, which a bool cannot say.
  int put(int catA, int itemA, int catB, int itemB, Mark mark);

  Shape shape_;
  // [block][item of the lower-numbered category][item of the higher one]
  uint8_t cells_[kMaxBlocks][kMaxItems][kMaxItems] = {};
};

// ---------------------------------------------------------------------------
// The player's marks

constexpr uint8_t kNoBlocker = 0xFF;

// What one tap did, and what stopped it when it did nothing.
//
// A refusal has to name the ticks in the way or it is indistinguishable from a
// screen that missed the tap -- which is the complaint this game gets anyway on
// a panel that takes a second to answer.
struct TapResult {
  bool changed = false;
  // Ticks in the way, as item indices inside the tapped block. Only set when
  // `changed` is false; there can be one on each axis, and both have to go
  // before this cell can be ticked.
  uint8_t sameRow = kNoBlocker;  // a tick at (catA, itemA) x (catB, sameRow)
  uint8_t sameCol = kNoBlocker;  // a tick at (catA, sameCol) x (catB, itemB)

  bool blocked() const { return sameRow != kNoBlocker || sameCol != kNoBlocker; }
};

// WHAT THE PLAYER SAID, AND NOTHING ELSE. Everything the grid shows on top of
// that is worked out on the way to the screen and never stored.
//
// This is the fourth attempt at the marking logic and the first that is not a
// bookkeeping problem. The first three stored two different kinds of thing in
// the same cells -- what the player entered, and the crosses that follow from
// it -- and then tried to keep them in step. Every defect was a failure of that
// step, and there were four of them: a cleared Yes leaving its crosses behind,
// an overruled Yes leaving its crosses behind, a cross justified by two Yeses
// vanishing when one went, and clearing a Yes taking away crosses a different
// Yes still needed. Two were found by Mario playing, one by printing the grid,
// one by a fuzz test. None by the code being read.
//
// They were all the same bug wearing different clothes, so the fix is not
// another case: a consequence is not a fact to be maintained, it is a question
// to be answered. `shown()` answers it from the assertions each time it is
// asked. There is no second copy, so there is nothing to fall out of step, and
// undo is free -- take away the assertion and its consequences are simply no
// longer derivable. That is also why the commercial logic-grid apps need a
// multi-level undo stack and this does not.
//
// Cost: shown() is O(items) rather than a lookup, so drawing a full 4x4 board
// is about 400 extra comparisons. On a panel that takes a second to refresh
// that is not a number worth optimising against a class of bug.
//
// THE ONE RULE ON TOP OF THAT: A TAP WRITES ITS OWN CELL AND NO OTHER. The
// board withdraws marks it derived -- that is what deriving them is for -- and
// never one the player entered. A tap that could only be honoured by taking a
// tick away does nothing and reports what is in the way instead. Before that
// rule, one tap on a finished grid unseated the two ticks that had crossed the
// tapped cell out, and a third square went blank because those ticks were the
// only thing crossing it: three cells the player never touched, two of them
// answers, with nothing flagged and no way back.
class Marks {
 public:
  void reset(Shape shape);

  // What the player entered here, ignoring anything implied. Unknown for most
  // cells of most grids: this is a sparse record of decisions.
  Mark entered(int catA, int itemA, int catB, int itemB) const;

  // What the board shows: their own mark, or the cross implied by one of their
  // Yes marks, or blank. This is what the screen draws and what a tap reads.
  Mark shown(int catA, int itemA, int catB, int itemB) const;

  // True when what shown() gives back here was worked out from the player's
  // ticks rather than entered by them. The grid draws the two at different
  // weights, so a player can tell which marks are their own -- and it is the
  // reason a tap here can be refused when the same-looking one next to it is
  // not.
  bool derived(int catA, int itemA, int catB, int itemB) const;

  // The only move the player has. Cycles what they SEE: blank -> crossed ->
  // ticked -> blank, writing the tapped cell and nothing else.
  //
  // One suspect cannot hold two weapons, so the ticked step is refused outright
  // when a tick already stands in that row or column: honouring it would mean
  // deleting that tick, and deleting the player's own marks is not something
  // this board does. The move is still available -- clear the tick first, which
  // is one tap on a cell the result names.
  //
  // One consequence, deliberate and cosmetic: a cross the player entered by
  // hand in a row a tick already owns cannot be rubbed out until that tick
  // moves, because the way back to blank runs through the ticked step. The
  // square reads "ruled out" either way, which is what the tick says about it
  // anyway, so nothing is hidden -- it just cannot be tidied away yet.
  TapResult tap(int catA, int itemA, int catB, int itemB);

  // Restoring a save. Writes an assertion directly, no cycling.
  void enter(int catA, int itemA, int catB, int itemB, Mark mark);

  bool complete() const;
  int decided() const;
  int cells() const;

  Shape shape() const { return shape_; }

 private:
  static int blockIndex(int catA, int catB);
  uint8_t& at(int catA, int itemA, int catB, int itemB);
  const uint8_t& at(int catA, int itemA, int catB, int itemB) const;

  Shape shape_;
  uint8_t entered_[kMaxBlocks][kMaxItems][kMaxItems] = {};
};

// ---------------------------------------------------------------------------
// Solving

// Scratch space for generation: about 4.7KB, which is too much for a task stack
// and not worth leaving in DRAM between cases, so the caller owns it and frees
// it once the case is made. Host tests just declare one.
//
// There is deliberately no survivor bit set here. Counting solutions walks the
// candidate space afresh and gives up on a candidate at its first failing clue,
// which at 4x4 costs far less than the 55,296-bit set it would replace and
// removes a second representation of the same fact.
struct Scratch {
  uint8_t perms[kMaxPerms][kMaxItems];
  Clue pool[kMaxPool];
  int poolCount;
};

// Counts assignments consistent with the puzzle's clues, stopping once `limit`
// have been found. Exhaustive enumeration: at 4x4 that is 55,296 candidates
// against at most 24 clues, all table lookups and no division.
int countSolutions(const Puzzle& puzzle, int limit, Scratch& scratch);

// The reference solver, and the fairness gate. Applies only what a person can
// apply with a pencil:
//
//   1. each clue, as an elimination on its block
//   2. a row or column of a block with one survivor becomes Yes, and a Yes
//      crosses out the rest of its row and column
//   3. transitivity across blocks: if A is with B, then A is with whatever B is
//      with, and A is not with whatever B is not with
//
// and, for tiers with spoken clues, one level of case split on who the murderer
// is, which is exactly the "suppose it was him, then his statement is false"
// move the puzzle is asking for. One level, bounded, never a search.
//
// Returns the number of rounds it took, or kUnfair if it stalled with the grid
// unfinished, or kContradiction. A puzzle that returns kUnfair has a unique
// solution that can only be reached by guessing, and is thrown away: uniqueness
// alone is not fairness, and a puzzle that needs trial and error feels like the
// game cheating.
constexpr int kUnfair = -1;
constexpr int kContradiction = -2;

// `revealRound`, when given, reports the round in which the crime scene got an
// owner -- which is the round the murderer stops being a mystery. Compared
// against the return value it says whether a case peaks at the end or halfway
// through and then coasts, which is a quality the correctness checks cannot
// see and which play-testers named on every case they were given.
int deduce(const Puzzle& puzzle, Grid& grid, int* revealRound = nullptr);

// ---------------------------------------------------------------------------
// Generation

// The seam between the logic and the cast.
//
// An attribute clue ("whoever was at the marina was left-handed") is only a
// clue because of who happens to have been drawn: left-handedness is a positive
// clue if exactly one of these four suspects is left-handed and an elimination
// if two are. So the mask depends on the cast, and the cast is data this file
// knows nothing about. MurdleCast computes these once the four suspects are
// drawn and hands them over as plain masks over suspect items, which keeps the
// logic free of any opinion about what an attribute is.
constexpr int kMaxAttrMasks = 16;

struct AttrMasks {
  uint8_t mask[kMaxAttrMasks] = {};
  // Which attribute produced mask[i]. Opaque here; MurdleText reads it back to
  // word the sentence.
  uint8_t tag[kMaxAttrMasks] = {};
  // Which dossier COLUMN produced mask[i], as an opaque id. The generator never
  // learns that column 3 is height -- it only learns that two masks came from
  // the same place, which is enough to stop a case leaning on one column twice.
  // A play-tester got "the one with the iron was shorter than BRUNO" beside
  // "the one on the roof was shorter than BRUNO"; the per-tag cap could not see
  // that, because both are the same tag used twice at exactly its limit, and a
  // per-tag cap would not have caught two different eye colours either.
  uint8_t axis[kMaxAttrMasks] = {};
  uint8_t count = 0;
};

// How many masks from one dossier column a single case may use.
constexpr int kMaxPerAxis = 1;

// Builds one case. Deterministic in `seed`: the same seed, tier, cast and
// attribute masks give the same case byte for byte, which is what lets the save
// file store six bytes instead of a puzzle.
//
// `cast` is the already-drawn cast (see MurdleCast::draw), copied into the
// puzzle verbatim. It reaches this function rather than being drawn inside it
// because the attribute masks have to be computed from it first.
//
// Returns false only if no fair puzzle was found within the attempt budget,
// which the tests assert never happens on any tier.
bool generate(Tier tier, uint32_t seed, const uint8_t cast[kMaxCats][kMaxItems], const AttrMasks& attrs,
              Scratch& scratch, Puzzle& out);

// xorshift32, the same one Insider deals with. Small, freestanding, and above
// all seedable, which is what lets a test replay an exact case.
class Rng {
 public:
  explicit Rng(const uint32_t seed) : state_(seed ? seed : 0x9E3779B9u) {}

  uint32_t next() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return state_;
  }

  // Unbiased below `bound` by rejection. The modulo shortcut skews low values,
  // which here would quietly make one suspect likelier to be the murderer than
  // another.
  uint32_t below(uint32_t bound);

 private:
  uint32_t state_;
};

}  // namespace murdle
