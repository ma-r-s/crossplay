#include "MurdleCore.h"

#include <algorithm>
#include <cstring>

namespace murdle {

namespace {

constexpr int kSuspect = static_cast<int>(Cat::Suspect);
constexpr int kLocation = static_cast<int>(Cat::Location);

// How many fresh solutions to try before giving up on a tier. Never reached in
// the test sweep; it exists so a future tier that is accidentally impossible
// fails in a bounded, reportable way rather than hanging the device.
constexpr int kAttempts = 64;

// A propagation guard, not a policy. Each round has to change at least one cell
// to run again, and there are at most 96 cells, so a run past this many rounds
// would mean the loop itself is broken.
constexpr int kMaxRounds = 128;

int factorial(const int n) {
  int f = 1;
  for (int i = 2; i <= n; ++i) f *= i;
  return f;
}

uint8_t fullMask(const int items) { return static_cast<uint8_t>((1u << items) - 1u); }

void buildPerms(const int items, uint8_t perms[kMaxPerms][kMaxItems]) {
  uint8_t cur[kMaxItems];
  for (int i = 0; i < items; ++i) cur[i] = static_cast<uint8_t>(i);
  const int count = factorial(items);
  for (int p = 0; p < count; ++p) {
    for (int i = 0; i < items; ++i) perms[p][i] = cur[i];
    std::next_permutation(cur, cur + items);
  }
}

// The row holding item `item` of category `cat`, against a bare assignment.
int rowIn(const uint8_t assign[kMaxCats][kMaxItems], const int cat, const int item, const int items) {
  for (int r = 0; r < items; ++r) {
    if (assign[cat][r] == item) return r;
  }
  return 0;  // unreachable: every assignment is a permutation
}

// The predicate itself, against a bare assignment so that the enumerator does
// not have to build a Puzzle per candidate.
bool holds(const Clue& clue, const uint8_t assign[kMaxCats][kMaxItems], const int items, const int murderRow) {
  const int row = clue.anchor == Anchor::Murderer ? murderRow : rowIn(assign, clue.anchorCat, clue.anchorItem, items);
  const bool satisfied = (clue.targetMask & static_cast<uint8_t>(1u << assign[clue.targetCat][row])) != 0;
  if (clue.speaker == kNobodySpeaks) return satisfied;
  // Spoken: true from everyone but the murderer, and false from the murderer.
  // This is the whole Impossible tier, and it is why the murderer's identity
  // and the assignment have to come out together.
  const bool truthful = rowIn(assign, kSuspect, clue.speaker, items) != murderRow;
  return satisfied == truthful;
}

// A clue resolved against a hypothesis: "cell (cat,item) x targetCat is
// confined to mask". Four bytes, so the whole working set fits comfortably in
// the stack budget deduce() is allowed.
struct Constraint {
  uint8_t cat;
  uint8_t item;
  uint8_t targetCat;
  uint8_t mask;
};

}  // namespace

// ---------------------------------------------------------------------------
// Rng

uint32_t Rng::below(const uint32_t bound) {
  if (bound <= 1) return 0;
  // Reject the short tail so every value below `bound` is equally likely. The
  // modulo shortcut favours the low values, which here would make one suspect
  // likelier than another to be the murderer.
  const uint32_t threshold = (~bound + 1u) % bound;  // == 2^32 mod bound
  uint32_t v;
  do {
    v = next();
  } while (v < threshold);
  return v % bound;
}

// ---------------------------------------------------------------------------
// Shape

Shape shapeOf(const Tier tier) {
  switch (tier) {
    case Tier::Elementary:
      return Shape{3, 3};
    case Tier::Nosy:
      return Shape{3, 4};
    case Tier::HardBoiled:
      return Shape{4, 4};
    case Tier::Impossible:
      return Shape{4, 4};
  }
  return Shape{3, 3};
}

int candidateCount(const Shape shape) {
  int n = shape.items;
  const int perms = factorial(shape.items);
  for (int c = 1; c < shape.cats; ++c) n *= perms;
  return n;
}

int Puzzle::rowOf(const int cat, const int item) const { return rowIn(assign, cat, item, shape.items); }

bool clueHolds(const Clue& clue, const Puzzle& puzzle) {
  return holds(clue, puzzle.assign, puzzle.shape.items, puzzle.murderRow);
}

uint32_t fingerprint(const Puzzle& puzzle) {
  uint32_t h = 2166136261u;
  const auto feed = [&h](const uint8_t byte) {
    h ^= byte;
    h *= 16777619u;
  };
  feed(static_cast<uint8_t>(puzzle.tier));
  feed(puzzle.shape.cats);
  feed(puzzle.shape.items);
  feed(puzzle.murderRow);
  feed(puzzle.clueCount);
  for (int c = 0; c < puzzle.shape.cats; ++c) {
    for (int i = 0; i < puzzle.shape.items; ++i) {
      feed(puzzle.assign[c][i]);
      feed(puzzle.cast[c][i]);
    }
  }
  for (int i = 0; i < puzzle.clueCount; ++i) {
    const Clue& clue = puzzle.clues[i];
    feed(static_cast<uint8_t>(clue.anchor));
    feed(clue.anchorCat);
    feed(clue.anchorItem);
    feed(clue.targetCat);
    feed(clue.targetMask);
    feed(clue.speaker);
    feed(clue.voice);
    feed(clue.attr);
  }
  return h;
}

// ---------------------------------------------------------------------------
// Grid

int Grid::blockIndex(const int catA, const int catB) {
  // catA < catB. Rows of a strictly upper triangle, flattened.
  return catA * (2 * kMaxCats - catA - 1) / 2 + (catB - catA - 1);
}

void Grid::reset(const Shape shape) {
  shape_ = shape;
  std::memset(cells_, 0, sizeof(cells_));
}

Mark Grid::get(const int catA, const int itemA, const int catB, const int itemB) const {
  if (catA < catB) return static_cast<Mark>(cells_[blockIndex(catA, catB)][itemA][itemB]);
  return static_cast<Mark>(cells_[blockIndex(catB, catA)][itemB][itemA]);
}

int Grid::put(const int catA, const int itemA, const int catB, const int itemB, const Mark mark) {
  uint8_t& cell =
      catA < catB ? cells_[blockIndex(catA, catB)][itemA][itemB] : cells_[blockIndex(catB, catA)][itemB][itemA];
  const uint8_t want = static_cast<uint8_t>(mark);
  if (cell == want) return 0;
  if (cell != static_cast<uint8_t>(Mark::Unknown)) return -1;
  cell = want;
  return 1;
}

bool Grid::set(const int catA, const int itemA, const int catB, const int itemB, const Mark mark) {
  return put(catA, itemA, catB, itemB, mark) >= 0;
}

void Grid::clear(const int catA, const int itemA, const int catB, const int itemB) {
  if (catA < catB) {
    cells_[blockIndex(catA, catB)][itemA][itemB] = static_cast<uint8_t>(Mark::Unknown);
  } else {
    cells_[blockIndex(catB, catA)][itemB][itemA] = static_cast<uint8_t>(Mark::Unknown);
  }
}

// ---------------------------------------------------------------------------
// Marks -- see the note in the header. Assertions in, consequences derived.

int Marks::blockIndex(const int catA, const int catB) {
  return catA * (2 * kMaxCats - catA - 1) / 2 + (catB - catA - 1);
}

uint8_t& Marks::at(const int catA, const int itemA, const int catB, const int itemB) {
  if (catA < catB) return entered_[blockIndex(catA, catB)][itemA][itemB];
  return entered_[blockIndex(catB, catA)][itemB][itemA];
}

const uint8_t& Marks::at(const int catA, const int itemA, const int catB, const int itemB) const {
  if (catA < catB) return entered_[blockIndex(catA, catB)][itemA][itemB];
  return entered_[blockIndex(catB, catA)][itemB][itemA];
}

void Marks::reset(const Shape shape) {
  shape_ = shape;
  std::memset(entered_, 0, sizeof(entered_));
}

Mark Marks::entered(const int catA, const int itemA, const int catB, const int itemB) const {
  return static_cast<Mark>(at(catA, itemA, catB, itemB));
}

void Marks::enter(const int catA, const int itemA, const int catB, const int itemB, const Mark mark) {
  at(catA, itemA, catB, itemB) = static_cast<uint8_t>(mark);
}

Mark Marks::shown(const int catA, const int itemA, const int catB, const int itemB) const {
  const Mark mine = entered(catA, itemA, catB, itemB);
  if (mine != Mark::Unknown) return mine;
  // Nothing entered here, so ask whether one of their ticks rules it out. One
  // suspect holds one weapon, so a tick anywhere else in this row or column
  // crosses this cell -- and that is the only inference this makes. Reaching
  // across blocks would be the deduction, which is the game.
  for (int i = 0; i < shape_.items; ++i) {
    if (i != itemB && entered(catA, itemA, catB, i) == Mark::Yes) return Mark::No;
    if (i != itemA && entered(catA, i, catB, itemB) == Mark::Yes) return Mark::No;
  }
  return Mark::Unknown;
}

bool Marks::derived(const int catA, const int itemA, const int catB, const int itemB) const {
  return entered(catA, itemA, catB, itemB) == Mark::Unknown && shown(catA, itemA, catB, itemB) != Mark::Unknown;
}

TapResult Marks::tap(const int catA, const int itemA, const int catB, const int itemB) {
  TapResult out;
  switch (shown(catA, itemA, catB, itemB)) {
    case Mark::Unknown:
      enter(catA, itemA, catB, itemB, Mark::No);
      out.changed = true;
      return out;
    case Mark::No:
      // A tick here would contradict a tick already standing in this row or
      // column, and the only way to write it would be to take that tick away.
      // So it is not written: the ticks in the way are reported and the board
      // is left exactly as the player left it. Moving a tick is still one extra
      // tap -- clear it, then mark here -- and that tap is the player's.
      for (int i = 0; i < shape_.items; ++i) {
        if (i != itemB && entered(catA, itemA, catB, i) == Mark::Yes) out.sameRow = static_cast<uint8_t>(i);
        if (i != itemA && entered(catA, i, catB, itemB) == Mark::Yes) out.sameCol = static_cast<uint8_t>(i);
      }
      if (out.blocked()) return out;
      enter(catA, itemA, catB, itemB, Mark::Yes);
      out.changed = true;
      return out;
    case Mark::Yes:
      // Back to blank -- or back to crossed, if another tick still rules this
      // cell out. Either way it is one write.
      enter(catA, itemA, catB, itemB, Mark::Unknown);
      out.changed = true;
      return out;
  }
  return out;
}

bool Marks::complete() const {
  for (int a = 0; a < shape_.cats; ++a) {
    for (int b = a + 1; b < shape_.cats; ++b) {
      for (int ia = 0; ia < shape_.items; ++ia) {
        for (int ib = 0; ib < shape_.items; ++ib) {
          if (shown(a, ia, b, ib) == Mark::Unknown) return false;
        }
      }
    }
  }
  return true;
}

int Marks::decided() const {
  int n = 0;
  for (int a = 0; a < shape_.cats; ++a) {
    for (int b = a + 1; b < shape_.cats; ++b) {
      for (int ia = 0; ia < shape_.items; ++ia) {
        for (int ib = 0; ib < shape_.items; ++ib) {
          if (shown(a, ia, b, ib) != Mark::Unknown) ++n;
        }
      }
    }
  }
  return n;
}

int Marks::cells() const { return shape_.cats * (shape_.cats - 1) / 2 * shape_.items * shape_.items; }

bool Grid::complete() const {
  for (int a = 0; a < shape_.cats; ++a) {
    for (int b = a + 1; b < shape_.cats; ++b) {
      for (int ia = 0; ia < shape_.items; ++ia) {
        for (int ib = 0; ib < shape_.items; ++ib) {
          if (cells_[blockIndex(a, b)][ia][ib] == static_cast<uint8_t>(Mark::Unknown)) return false;
        }
      }
    }
  }
  return true;
}

int Grid::decided() const {
  int n = 0;
  for (int a = 0; a < shape_.cats; ++a) {
    for (int b = a + 1; b < shape_.cats; ++b) {
      for (int ia = 0; ia < shape_.items; ++ia) {
        for (int ib = 0; ib < shape_.items; ++ib) {
          if (cells_[blockIndex(a, b)][ia][ib] != static_cast<uint8_t>(Mark::Unknown)) ++n;
        }
      }
    }
  }
  return n;
}

int Grid::cells() const { return shape_.cats * (shape_.cats - 1) / 2 * shape_.items * shape_.items; }

// ---------------------------------------------------------------------------
// Propagation

namespace {

// Applies constraints, block uniqueness and cross-block transitivity to a
// fixpoint. Nothing here is beyond what a person does with a pencil, which is
// the point: a solver reasoning in some stronger system would certify puzzles
// nobody can actually solve.
// `watchItem` is a place; when it acquires an owner, `watchRound` records which
// round that happened in. Threaded through rather than recomputed afterwards so
// that the round numbers come from the same loop that did the work.
int propagate(Grid& grid, const Constraint* cons, const int conCount, const Shape shape, const int watchItem = -1,
              int* watchRound = nullptr, const int watchCat = static_cast<int>(Cat::Location)) {
  const int items = shape.items;
  const int cats = shape.cats;
  int rounds = 0;

  bool changed = true;
  while (changed) {
    changed = false;
    if (++rounds > kMaxRounds) return kContradiction;

    // 1. The clues, as eliminations.
    for (int c = 0; c < conCount; ++c) {
      const Constraint& con = cons[c];
      for (int t = 0; t < items; ++t) {
        if (con.mask & static_cast<uint8_t>(1u << t)) continue;
        if (grid.get(con.cat, con.item, con.targetCat, t) == Mark::No) continue;
        if (!grid.set(con.cat, con.item, con.targetCat, t, Mark::No)) return kContradiction;
        changed = true;
      }
    }

    // 2. Within each block: a lone survivor in a row or a column is a Yes, and
    //    a Yes crosses out the rest of its row and column.
    for (int a = 0; a < cats; ++a) {
      for (int b = a + 1; b < cats; ++b) {
        // Rows, then columns. Same rule twice with the indices swapped, so it
        // is written twice rather than hidden behind a transpose nobody would
        // be able to read.
        for (int ia = 0; ia < items; ++ia) {
          int open = 0;
          int lastOpen = -1;
          int yes = -1;
          for (int ib = 0; ib < items; ++ib) {
            const Mark m = grid.get(a, ia, b, ib);
            if (m == Mark::Yes) yes = ib;
            if (m != Mark::No) {
              ++open;
              lastOpen = ib;
            }
          }
          if (open == 0) return kContradiction;
          const int settled = yes >= 0 ? yes : (open == 1 ? lastOpen : -1);
          if (settled < 0) continue;
          for (int ib = 0; ib < items; ++ib) {
            const Mark want = ib == settled ? Mark::Yes : Mark::No;
            if (grid.get(a, ia, b, ib) == want) continue;
            if (!grid.set(a, ia, b, ib, want)) return kContradiction;
            changed = true;
          }
        }
        for (int ib = 0; ib < items; ++ib) {
          int open = 0;
          int lastOpen = -1;
          int yes = -1;
          for (int ia = 0; ia < items; ++ia) {
            const Mark m = grid.get(a, ia, b, ib);
            if (m == Mark::Yes) yes = ia;
            if (m != Mark::No) {
              ++open;
              lastOpen = ia;
            }
          }
          if (open == 0) return kContradiction;
          const int settled = yes >= 0 ? yes : (open == 1 ? lastOpen : -1);
          if (settled < 0) continue;
          for (int ia = 0; ia < items; ++ia) {
            const Mark want = ia == settled ? Mark::Yes : Mark::No;
            if (grid.get(a, ia, b, ib) == want) continue;
            if (!grid.set(a, ia, b, ib, want)) return kContradiction;
            changed = true;
          }
        }
      }
    }

    // 3. Across blocks: two items known to be the same person agree about
    //    everybody else. This is the step the tutorial calls the key to the
    //    whole book, and it is the only rule here that leaves its own block.
    for (int a = 0; a < cats; ++a) {
      for (int b = 0; b < cats; ++b) {
        if (a == b) continue;
        for (int ia = 0; ia < items; ++ia) {
          for (int ib = 0; ib < items; ++ib) {
            if (grid.get(a, ia, b, ib) != Mark::Yes) continue;
            for (int c = 0; c < cats; ++c) {
              if (c == a || c == b) continue;
              for (int ic = 0; ic < items; ++ic) {
                const Mark ma = grid.get(a, ia, c, ic);
                const Mark mb = grid.get(b, ib, c, ic);
                if (ma == mb) continue;
                if (ma != Mark::Unknown && mb != Mark::Unknown) return kContradiction;
                if (ma == Mark::Unknown) {
                  if (!grid.set(a, ia, c, ic, mb)) return kContradiction;
                } else {
                  if (!grid.set(b, ib, c, ic, ma)) return kContradiction;
                }
                changed = true;
              }
            }
          }
        }
      }
    }

    if (watchRound != nullptr && watchItem >= 0 && *watchRound < 0) {
      for (int su = 0; su < items; ++su) {
        if (grid.get(watchCat, watchItem, kSuspect, su) == Mark::Yes) *watchRound = rounds;
      }
    }
  }
  return rounds;
}

// Resolve a clue into a constraint. `hypothesis` is the row supposed to hold
// the murderer, used only by murder-anchored clues. `negate` inverts the mask,
// which is exactly what a lie is.
Constraint resolve(const Clue& clue, const int items, const int hypothesis, const bool negate) {
  Constraint con{};
  if (clue.anchor == Anchor::Murderer) {
    con.cat = static_cast<uint8_t>(kSuspect);
    con.item = static_cast<uint8_t>(hypothesis);
  } else {
    con.cat = clue.anchorCat;
    con.item = clue.anchorItem;
  }
  con.targetCat = clue.targetCat;
  con.mask = negate ? static_cast<uint8_t>(fullMask(items) & ~clue.targetMask) : clue.targetMask;
  return con;
}

// Does this filled grid say what the puzzle's own solution says, cell for cell?
//
// This closes the last property that was only ever checked by sampling. The
// other three -- one solution, no clue removable, reachable by pencil rules
// alone -- are enforced inside generate() on every case the device ever makes,
// and a case that fails any of them is thrown away rather than shipped. This
// one was an ARGUMENT instead of a check: propagation only applies sound rules
// and the solution is already known to be unique, so a complete grid had to be
// the right grid. True, but it rests on propagate() being correct, and the only
// thing testing that was 400 seeds a tier on a laptop. A rule that wrote one
// wrong Yes would reach a player as a puzzle whose answer is not the answer.
//
// Ninety-six comparisons at the largest shape, once per generated case, off the
// render path. That is a very cheap way to stop trusting an argument.
bool solves(const Puzzle& puzzle, const Grid& grid) {
  const int cats = puzzle.shape.cats;
  const int items = puzzle.shape.items;
  for (int a = 0; a < cats; ++a) {
    for (int b = a + 1; b < cats; ++b) {
      for (int r = 0; r < items; ++r) {
        const int ia = puzzle.assign[a][r];
        const int ib = puzzle.assign[b][r];
        // The true pairing must be marked Yes, and every other cell in that
        // row of the block must be marked No.
        if (grid.get(a, ia, b, ib) != Mark::Yes) return false;
        for (int other = 0; other < items; ++other) {
          if (other == ib) continue;
          if (grid.get(a, ia, b, other) != Mark::No) return false;
        }
      }
    }
  }
  return true;
}

}  // namespace

int deduce(const Puzzle& puzzle, Grid& grid, int* revealRound) {
  const Shape shape = puzzle.shape;
  const int items = shape.items;
  grid.reset(shape);
  if (revealRound != nullptr) *revealRound = -1;

  // The place the crime-scene clue names. The murderer is known the moment it
  // has an owner.
  int scene = -1;
  int sceneCat = kLocation;
  for (int i = 0; i < puzzle.clueCount; ++i) {
    if (puzzle.clues[i].anchor != Anchor::Murderer) continue;
    sceneCat = puzzle.clues[i].targetCat;
    for (int b = 0; b < items; ++b) {
      if (puzzle.clues[i].targetMask & static_cast<uint8_t>(1u << b)) scene = b;
    }
  }

  bool anySpoken = false;
  for (int i = 0; i < puzzle.clueCount; ++i) {
    if (puzzle.clues[i].speaker != kNobodySpeaks) anySpoken = true;
  }

  // Phase one: everything that is unconditionally true and not about the
  // murderer. On every tier but Impossible this is the whole puzzle, because
  // the murder clue names the crime scene and says nothing about who was where.
  // Value-initialised: with no unspoken clues conCount stays 0 and propagate()
  // reads none of it, but GCC cannot see that and -Werror stops the build.
  Constraint cons[kMaxClues] = {};
  int conCount = 0;
  for (int i = 0; i < puzzle.clueCount; ++i) {
    const Clue& clue = puzzle.clues[i];
    if (clue.anchor == Anchor::Murderer || clue.speaker != kNobodySpeaks) continue;
    cons[conCount++] = resolve(clue, items, 0, false);
  }

  const int rounds = propagate(grid, cons, conCount, shape, scene, revealRound, sceneCat);
  if (rounds == kContradiction) return kContradiction;

  if (!anySpoken) return grid.complete() && solves(puzzle, grid) ? rounds : kUnfair;

  // Phase two: one level of case split on who the murderer is. Bounded at
  // `items` hypotheses and never nested, because this is the move the puzzle
  // asks for ("suppose it was her, then her statement is a lie") and not a
  // search. A hypothesis that neither contradicts nor completes cannot be ruled
  // out by a player either, so it fails the whole case rather than being
  // quietly skipped.
  Grid keeper;
  int aliveCount = 0;
  int aliveRounds = 0;
  for (int m = 0; m < items; ++m) {
    Grid trial = grid;
    conCount = 0;
    for (int i = 0; i < puzzle.clueCount; ++i) {
      const Clue& clue = puzzle.clues[i];
      if (clue.speaker != kNobodySpeaks) {
        const bool lying = clue.speaker == m;
        cons[conCount++] = resolve(clue, items, m, lying);
      } else if (clue.anchor == Anchor::Murderer) {
        cons[conCount++] = resolve(clue, items, m, false);
      }
    }
    const int r = propagate(trial, cons, conCount, shape);
    if (r == kContradiction) continue;
    if (!trial.complete()) return kUnfair;
    if (m == puzzle.murderRow && !solves(puzzle, trial)) return kUnfair;
    ++aliveCount;
    if (aliveCount > 1) return kUnfair;
    aliveRounds = rounds + r;
    keeper = trial;
  }
  if (aliveCount != 1) return kUnfair;
  grid = keeper;
  return aliveRounds + 1;
}

// ---------------------------------------------------------------------------
// Counting

int countSolutions(const Puzzle& puzzle, const int limit, Scratch& scratch) {
  const int cats = puzzle.shape.cats;
  const int items = puzzle.shape.items;
  const int permCount = factorial(items);
  buildPerms(items, scratch.perms);

  uint8_t assign[kMaxCats][kMaxItems] = {};
  for (int r = 0; r < items; ++r) assign[kSuspect][r] = static_cast<uint8_t>(r);

  int digit[kMaxCats] = {};
  int found = 0;

  while (true) {
    for (int d = 0; d + 1 < cats; ++d) {
      const uint8_t* perm = scratch.perms[digit[d]];
      for (int r = 0; r < items; ++r) assign[d + 1][r] = perm[r];
    }
    for (int m = 0; m < items; ++m) {
      bool ok = true;
      for (int i = 0; i < puzzle.clueCount; ++i) {
        if (!holds(puzzle.clues[i], assign, items, m)) {
          ok = false;
          break;
        }
      }
      if (ok && ++found >= limit) return found;
    }

    int d = 0;
    for (; d + 1 < cats; ++d) {
      if (++digit[d] < permCount) break;
      digit[d] = 0;
    }
    if (d + 1 >= cats) break;
  }
  return found;
}

// ---------------------------------------------------------------------------
// Generation

namespace {

void addToPool(Scratch& scratch, const Clue& clue) {
  if (scratch.poolCount >= kMaxPool) return;
  scratch.pool[scratch.poolCount++] = clue;
}

// Every clue that is true under this solution and whose shape the tier permits.
// `sceneCat`/`sceneItem` name the crime scene, and exist here for one refusal.
//
// A clue anchored on the scene and pointing at the suspects is a clue about the
// MURDERER, because whoever was at the scene did it. "Either ANNA or OSCAR was
// on the dock" beside "the body was found in the place with wet footprints"
// hands over a one-in-two guess at the only answer the case exists to protect,
// on line two, before any deduction at all. Two play-testers hit exactly that
// and both named it their single worst finding; one of them reported having a
// 50% shot at the headline answer with zero work done.
//
// So the scene's occupant may only be narrowed one suspect at a time. A mask
// missing a single item ("the suspect on the dock was not ANNA") still leaves
// three candidates and is ordinary grid work; a two-bit mask is a coin flip on
// the accusation and is refused. This costs the pool very little -- it is one
// anchor out of the whole case -- and it is the difference between the murderer
// being deduced and being offered.
void buildPool(const Puzzle& p, const AttrMasks& attrs, const Tier tier, const int sceneCat, const int sceneItem,
               Scratch& scratch) {
  const int cats = p.shape.cats;
  const int items = p.shape.items;
  const uint8_t full = fullMask(items);
  // How many bits a mask must have to be allowed to describe the scene's
  // occupant. Everything else in the case is unaffected.
  const int sceneFloor = items - 1;
  // A bare positive naming a suspect is what makes a tier easy, so only
  // Elementary gets those. But a positive between two things that are NOT
  // suspects -- "the suspect in the cave carried the pan" -- is a bridge, and
  // every tier needs them: a play-tester got a case whose weapon grid and place
  // grid never touched, solved them separately, and rightly called it two
  // puzzles rather than one. They also break up clue sets that are otherwise
  // all negations, which grind.
  const bool allowBarePositive = tier == Tier::Elementary;
  const bool allowPair = tier == Tier::HardBoiled || tier == Tier::Impossible;
  // Every tier. They used to be banned on Elementary, which meant that tier
  // printed four attributes a suspect and referenced them in zero of 1,610
  // clues: a table the player builds, guards, and is never paid for. An
  // attribute clue is one extra lookup, not a difficulty spike.
  const bool allowAttribute = true;

  scratch.poolCount = 0;
  for (int a = 0; a < cats; ++a) {
    for (int x = 0; x < items; ++x) {
      const int row = p.rowOf(a, x);
      for (int t = 0; t < cats; ++t) {
        if (t == a) continue;
        const int trueItem = p.assign[t][row];

        Clue clue{};
        clue.anchor = Anchor::Item;
        clue.anchorCat = static_cast<uint8_t>(a);
        clue.anchorItem = static_cast<uint8_t>(x);
        clue.targetCat = static_cast<uint8_t>(t);
        clue.speaker = kNobodySpeaks;
        clue.attr = kNoAttr;

        // Anything said here is said about the murderer; see the note above.
        // The leak is symmetric and the first version only plugged one side.
        // "Either ANNA or OSCAR was on the dock" points from the scene at the
        // suspects; "QUINN was in the garden" points from a suspect at the
        // scene and names the murderer outright. Both narrow who was standing
        // where the body is, which is the whole question. Only a mask that
        // CONTAINS the scene does it -- "QUINN was not in the garden" rules a
        // suspect out and is ordinary grid work.
        const uint8_t sceneBit = static_cast<uint8_t>(1u << sceneItem);
        const bool fromScene = a == sceneCat && x == sceneItem && t == kSuspect;
        const bool atScene = a == kSuspect && t == sceneCat;

        // True when this mask would leave fewer than `sceneFloor` candidates
        // for the murderer. A negative is always fine either way: it removes
        // one candidate, which is what a logic grid is made of.
        const auto narrowsMurderer = [&](const uint8_t mask, const int held) {
          if (fromScene) return held < sceneFloor;
          if (atScene && (mask & sceneBit) != 0) return held < sceneFloor;
          return false;
        };

        const bool bridge = a != kSuspect && t != kSuspect;
        if (allowBarePositive || bridge) {
          const uint8_t mask = static_cast<uint8_t>(1u << trueItem);
          if (!narrowsMurderer(mask, 1)) {
            clue.targetMask = mask;
            addToPool(scratch, clue);
          }
        }
        // "Not with y", for every y it is genuinely not with. Available on every
        // tier: a negative is the backbone of a logic grid, and one of these
        // aimed at the scene still leaves three suspects standing.
        for (int y = 0; y < items; ++y) {
          if (y == trueItem) continue;
          clue.targetMask = static_cast<uint8_t>(full & ~(1u << y));
          addToPool(scratch, clue);
        }
        if (allowPair) {
          for (int y = 0; y < items; ++y) {
            if (y == trueItem) continue;
            const uint8_t mask = static_cast<uint8_t>((1u << trueItem) | (1u << y));
            if (narrowsMurderer(mask, 2)) continue;
            clue.targetMask = mask;
            addToPool(scratch, clue);
          }
        }
        // Attribute clues only ever describe suspects, and only make sense
        // anchored somewhere else ("whoever was at the marina was left-handed").
        if (allowAttribute && t == kSuspect && a != kSuspect) {
          for (int i = 0; i < attrs.count; ++i) {
            const uint8_t mask = static_cast<uint8_t>(attrs.mask[i] & full);
            if (mask == 0 || mask == full) continue;
            if ((mask & static_cast<uint8_t>(1u << trueItem)) == 0) continue;
            // An attribute held by exactly one drawn suspect names that suspect,
            // so on a tier that bans bare positives it has to be banned too --
            // otherwise "the one with the oar had green eyes" is a free
            // identification in a costume, which is how one case handed over its
            // murderer in three consecutive clues.
            int held = 0;
            for (int b = 0; b < items; ++b) {
              if (mask & static_cast<uint8_t>(1u << b)) ++held;
            }
            if (!allowBarePositive && held < 2) continue;
            // And an attribute is no exception to the scene rule. "Whoever was
            // on the dock was left-handed" narrows the murderer exactly as hard
            // as "either ANNA or OSCAR was on the dock" does, and reads more
            // innocently while doing it.
            if (narrowsMurderer(mask, held)) continue;
            clue.targetMask = mask;
            clue.attr = attrs.tag[i];
            addToPool(scratch, clue);
          }
          clue.attr = kNoAttr;
        }
      }
    }
  }
}

void shufflePool(Scratch& scratch, Rng& rng) {
  for (int i = scratch.poolCount - 1; i > 0; --i) {
    const int j = static_cast<int>(rng.below(static_cast<uint32_t>(i + 1)));
    std::swap(scratch.pool[i], scratch.pool[j]);
  }
}

// Move a few attribute clues to the head of the shuffled pool, at most one per
// distinct attribute.
//
// The greedy loop takes clues in pool order until the case has one reading, so
// a clue's chance of appearing is its share of the pool. Attribute clues are
// generated for one target category out of `cats`, while plain negatives are
// generated for every ordered pair -- so at four categories they were 5% and 2%
// of the clues in a finished case, measured over 300 cases a tier. Four
// attributes get printed in the dossier, the player builds a table off them,
// and two tiers then referenced that table about once every other case. Every
// round of play-testing named it.
//
// This is a bias, not a quota. The clues still have to earn their place: the
// greedy loop only takes one if the case is not yet unique, and the prune pass
// afterwards drops any that the rest of the case turned out to imply. A
// promoted clue that is redundant does not survive to be read.
void promoteAttributes(Scratch& scratch, const int want) {
  bool seen[256] = {};
  int front = 0;
  for (int i = 0; i < scratch.poolCount && front < want; ++i) {
    const uint8_t attr = scratch.pool[i].attr;
    if (attr == kNoAttr || seen[attr]) continue;
    seen[attr] = true;
    const Clue promoted = scratch.pool[i];
    for (int j = i; j > front; --j) scratch.pool[j] = scratch.pool[j - 1];
    scratch.pool[front++] = promoted;
  }
}

// One statement per suspect. Everyone but the murderer says something true; the
// murderer says something false.
//
// A statement may be about ANYBODY, not just the speaker. That is the whole
// point of a witness and the first version missed it: every statement was a
// suspect reporting their own weapon or place, so the murderer's lie could only
// ever deny one fact about the liar. Nothing cross-linked, so the tier degraded
// to "one of these four facts is wrong, find which". A lie about somebody else
// poisons *their* row, which is what makes the liar dangerous.
bool addStatements(Puzzle& p, Rng& rng) {
  const int items = p.shape.items;
  for (int s = 0; s < items; ++s) {
    Clue clue{};
    clue.anchor = Anchor::Item;
    clue.speaker = static_cast<uint8_t>(s);
    clue.attr = kNoAttr;
    clue.voice = static_cast<uint8_t>(rng.next());

    // The MURDERER always lies about somebody else, and this is the whole
    // mechanic. A lie about the liar's own place is inert -- the crime-scene
    // clue already fixes where the murderer was, so the false claim contradicts
    // something the player has and never touches another row. Two critics
    // reached the answer in five seconds off exactly that: "the murderer is the
    // one whose alibi is only their own mouth." A lie about a third party has
    // to be believed for several steps and then unwound.
    int subject = s;
    const bool mustPointElsewhere = s == p.murderRow;
    if (items > 1 && (mustPointElsewhere || rng.below(3) != 0)) {
      subject = static_cast<int>(rng.below(static_cast<uint32_t>(items - 1)));
      if (subject >= s) ++subject;
    }

    // Weapon or place only -- "driven by greed" is not a thing a witness
    // reports -- and alternating so the statement layer never confines itself
    // to one column, which is what let a whole case's testimony collapse
    // against a single hard clue.
    const int t = 1 + ((s + static_cast<int>(rng.below(2))) % 2);
    const int trueItem = p.assign[t][subject];
    clue.anchorCat = static_cast<uint8_t>(kSuspect);
    clue.anchorItem = static_cast<uint8_t>(subject);
    clue.targetCat = static_cast<uint8_t>(t);

    if (s == p.murderRow) {
      // A lie: name a value the subject does not have.
      int wrong = static_cast<int>(rng.below(static_cast<uint32_t>(items - 1)));
      if (wrong >= trueItem) ++wrong;
      clue.targetMask = static_cast<uint8_t>(1u << wrong);
    } else {
      clue.targetMask = static_cast<uint8_t>(1u << trueItem);
    }

    // Two statements asserting the same proposition can never straddle the
    // true/false line: exactly one statement is false, so identical twins are
    // both automatically true and clear two suspects for free. A play-tester
    // found that and solved the case with it in two lines.
    for (int e = 0; e < p.clueCount; ++e) {
      const Clue& other = p.clues[e];
      if (other.speaker == kNobodySpeaks) continue;
      if (other.anchorItem == clue.anchorItem && other.targetCat == clue.targetCat &&
          other.targetMask == clue.targetMask) {
        return false;
      }
    }
    // And a statement placing somebody at the crime scene is not a placement,
    // it is "X says: Y did it". Once the scene clue is read it hands over the
    // answer.
    if ((clue.targetMask & static_cast<uint8_t>(1u << p.assign[clue.targetCat][p.murderRow])) != 0 &&
        clue.anchorItem == p.murderRow) {
      return false;
    }

    if (p.clueCount >= kMaxClues) return false;
    p.clues[p.clueCount++] = clue;
  }
  return true;
}

}  // namespace

bool generate(const Tier tier, const uint32_t seed, const uint8_t cast[kMaxCats][kMaxItems], const AttrMasks& attrs,
              Scratch& scratch, Puzzle& out) {
  const Shape shape = shapeOf(tier);
  const int cats = shape.cats;
  const int items = shape.items;
  buildPerms(items, scratch.perms);

  // The best near-miss on the reveal-timing preference, kept so that a tier
  // always produces a case even when nothing scores perfectly.
  Puzzle best{};
  bool haveBest = false;
  int bestReveal = -1;
  int bestRounds = 0;

  for (int attempt = 0; attempt < kAttempts; ++attempt) {
    Rng rng(seed ^ (0x9E3779B9u * static_cast<uint32_t>(attempt + 1)));

    Puzzle p{};
    p.shape = shape;
    p.tier = tier;
    p.seed = seed;
    std::memcpy(p.cast, cast, sizeof(p.cast));

    // A random solution. Suspects are the axis, so category 0 is the identity
    // and every other category is a shuffle of it.
    for (int r = 0; r < items; ++r) p.assign[kSuspect][r] = static_cast<uint8_t>(r);
    for (int c = 1; c < cats; ++c) {
      for (int r = 0; r < items; ++r) p.assign[c][r] = static_cast<uint8_t>(r);
      for (int r = items - 1; r > 0; --r) {
        const int j = static_cast<int>(rng.below(static_cast<uint32_t>(r + 1)));
        std::swap(p.assign[c][r], p.assign[c][j]);
      }
    }
    p.murderRow = static_cast<uint8_t>(rng.below(static_cast<uint32_t>(items)));

    // The murder clue, first and never pruned: it is the only thing in a case
    // that can say who the murderer is, and it does so by naming the crime
    // scene rather than the person.
    Clue murder{};
    murder.anchor = Anchor::Murderer;
    // Half the time the scene is identified by the murder weapon rather than
    // the room. "The body was found beside the weapon with grease on it" cannot
    // be cashed until the weapon column is solved, whereas naming a place is
    // answered by a glance at the fixture list -- which is why moving this clue
    // to the end of the list changed nothing on its own, as two critics said in
    // almost the same words.
    const int sceneCat = rng.below(2) == 0 ? kLocation : static_cast<int>(Cat::Weapon);
    murder.targetCat = static_cast<uint8_t>(sceneCat);
    murder.targetMask = static_cast<uint8_t>(1u << p.assign[sceneCat][p.murderRow]);
    murder.speaker = kNobodySpeaks;
    murder.attr = kNoAttr;
    murder.voice = static_cast<uint8_t>(rng.next());
    p.clues[p.clueCount++] = murder;

    if (tier == Tier::Impossible && !addStatements(p, rng)) continue;
    const int fixedClues = p.clueCount;

    // THE CASE IS BUILT THROUGH THE PENCIL SOLVER, NOT CHECKED AGAINST IT.
    //
    // This is the shape of the whole generator and the second version of it.
    // The first assembled a clue set until exhaustive enumeration said the
    // puzzle had one answer, and only then asked whether a person could reach
    // that answer without guessing -- throwing the entire case away when the
    // answer was no. Rejection sampling. It meant a seed could fail, which is
    // unacceptable for a property the game is supposed to guarantee, and it
    // meant enumerating 55,296 candidates about thirty times per case, which is
    // where essentially all of the generation cost went.
    //
    // Both phases below use `deduce` -- the solver that applies only what a
    // person can apply with a pencil -- and nothing else:
    //
    //   A. ADD until the pencil solver finishes the grid. Guaranteed to get
    //      there, because applying every negative in the pool pins every cell,
    //      so the full pool always closes. A clue that teaches the solver
    //      nothing is dropped on the spot rather than carried.
    //   B. REMOVE any clue the pencil solver can finish without.
    //
    // The invariant "a person can solve this" is established by A and never
    // broken by B, so there is nothing left to reject. Solvability stops being
    // a filter and becomes a property of the construction.
    //
    // Uniqueness comes free and is not computed here. Propagation only ever
    // writes marks that are logical consequences of the clues, so a grid it
    // fills completely is a grid in which every cell was forced -- and a puzzle
    // whose every cell is forced has exactly one solution. The exhaustive
    // enumerator still exists and still proves this, but it belongs in the
    // tests as an oracle that shares no code with the constructor, not in the
    // product where it was the constructor's own opinion of itself.
    Grid probe;
    bool solvable = false;
    {
      buildPool(p, attrs, tier, sceneCat, p.assign[sceneCat][p.murderRow], scratch);
      shufflePool(scratch, rng);
      // Three is one under kMaxPerShape, so the dossier speaks in every case
      // without any one case becoming a dossier quiz. It can no longer starve
      // anything: a bias only reorders the pool, and phase A closes whatever
      // order it is given.
      promoteAttributes(scratch, 3);

      // PHASE A. Add clues until the pencil solver can finish the grid.
      //
      // Two passes. The first refuses any clue whose (anchor, target category)
      // pair has already been spoken about, because minimality does not stop a
      // case saying the same thing twice in different words -- "GRETA did not
      // carry the vase" beside "ROSA did not carry the vase" is two clue slots
      // buying one idea, and play-testers called it out every time. The second
      // pass drops the refusal, so a case that genuinely needs the repeat still
      // gets it rather than failing.
      int spokenOf[kMaxCats][kMaxItems][kMaxCats] = {};
      constexpr int kMaxPerAnchor = 2;
      // A clue whose target is a suspect is *read* from the suspect's side --
      // "NORA was not driven by hate" -- so its subject is the named suspect, not
      // the anchor. Keying only on the anchor let two clues through that named
      // the same person about the same category twice, which is the same stutter
      // in a different costume.
      bool saidAbout[kMaxItems][kMaxCats] = {};
      // And no one shape may dominate. Four clues of the form "whoever was in X
      // was driven by Y or Z" is one rhythm repeated, which play-testers spotted
      // as a template firing rather than as a puzzle.
      int shapeUsed[kMaxCats][kMaxCats] = {};
      constexpr int kMaxPerShape = 3;
      // Nor may one attribute. Three clues ending "had hazel eyes" is the same
      // repetition wearing the dossier's clothes.
      int attrUsed[256] = {};
      constexpr int kMaxPerAttr = 2;
      // And no one dossier COLUMN may speak twice, which the per-tag cap above
      // cannot express. A play-tester got "the one with the iron was shorter
      // than BRUNO" beside "the one on the roof was shorter than BRUNO" in a
      // single case: one tag used twice, at exactly kMaxPerAttr, and it read as
      // one template firing twice. Two different eye colours would be the same
      // complaint one step out. The generator does not know what a column is --
      // see AttrMasks::axis -- only that two masks came from the same one.
      // Nor may one item be ruled out over and over. A case came out with
      // "BRUNO was not in the hall", "OSCAR was not in the hall" and "YARA was
      // not in the hall" as three separate lines, which together say "TOMAS was
      // in the hall" -- a positive in disguise, paid for three times and read as
      // one template stuttering. Every filter above misses it: the anchors all
      // differ so spokenOf sees three different clues, saidAbout only fires when
      // the TARGET is a suspect, and shapeUsed sits at exactly its limit.
      int ruledOut[kMaxCats][kMaxItems] = {};
      constexpr int kMaxRuledOut = 2;

      int axisUsed[8] = {};
      const auto axisOf = [&attrs](const uint8_t tag) -> int {
        if (tag == kNoAttr) return -1;
        for (int i = 0; i < attrs.count; ++i) {
          if (attrs.tag[i] == tag) return attrs.axis[i];
        }
        return -1;
      };

      // How much of the grid the pencil solver can reach with what it has. A
      // clue that does not move this teaches the solver nothing at this point in
      // the case, and carrying it would only give phase B more to undo.
      int reached = -1;
      for (int pass = 0; pass < 2 && !solvable; ++pass) {
        int taken = 0;
        while (!solvable) {
          if (taken >= scratch.poolCount || p.clueCount >= kMaxClues) break;
          Clue clue = scratch.pool[taken++];

          // Which suspect this clue will name, if it names one.
          int names = -1;
          if (clue.attr == kNoAttr && clue.targetCat == static_cast<uint8_t>(Cat::Suspect)) {
            int set = 0;
            int only = 0;
            int missing = 0;
            for (int b = 0; b < items; ++b) {
              if (clue.targetMask & static_cast<uint8_t>(1u << b)) {
                ++set;
                only = b;
              } else {
                missing = b;
              }
            }
            if (set == 1) names = only;
            if (set == items - 1) names = missing;
          }

          // The one item a denial rules out, when it rules out exactly one.
          int denied = -1;
          if (clue.attr == kNoAttr) {
            int missing = 0;
            int clear = 0;
            for (int b = 0; b < items; ++b) {
              if ((clue.targetMask & static_cast<uint8_t>(1u << b)) == 0) {
                ++clear;
                missing = b;
              }
            }
            if (clear == 1) denied = missing;
          }

          const int axis = axisOf(clue.attr);
          if (pass == 0) {
            if (spokenOf[clue.anchorCat][clue.anchorItem][clue.targetCat] >= kMaxPerAnchor) continue;
            if (names >= 0 && saidAbout[names][clue.anchorCat]) continue;
            if (shapeUsed[clue.anchorCat][clue.targetCat] >= kMaxPerShape) continue;
            if (clue.attr != kNoAttr && attrUsed[clue.attr] >= kMaxPerAttr) continue;
            if (axis >= 0 && axisUsed[axis] >= kMaxPerAxis) continue;
            if (denied >= 0 && ruledOut[clue.targetCat][denied] >= kMaxRuledOut) continue;
          }
          clue.voice = static_cast<uint8_t>(rng.next());
          p.clues[p.clueCount] = clue;
          ++p.clueCount;

          const int r = deduce(p, probe);
          if (r >= 0) {
            solvable = true;
            break;
          }
          // Keep it only if the solver got further with it than without. This
          // is what holds the clue count down: the pool is a few hundred true
          // statements and most of them say something the case has already
          // established by the time they come up.
          const int now = probe.decided();
          if (now <= reached) {
            --p.clueCount;
            continue;
          }
          reached = now;

          if (clue.attr != kNoAttr) ++attrUsed[clue.attr];
          if (axis >= 0) ++axisUsed[axis];
          if (denied >= 0) ++ruledOut[clue.targetCat][denied];
          ++spokenOf[clue.anchorCat][clue.anchorItem][clue.targetCat];
          if (names >= 0) saidAbout[names][clue.anchorCat] = true;
          ++shapeUsed[clue.anchorCat][clue.targetCat];
        }
      }
    }
    // Only reachable if the pool ran out or kMaxClues was hit before the grid
    // closed, which the full pool makes impossible on the shapes this game
    // uses. Kept as a bound rather than an assertion because the alternative to
    // a retry here is shipping a case nobody can finish.
    if (!solvable) continue;

    // PHASE B. Remove any clue the pencil solver can finish without.
    //
    // The invariant from phase A holds at every step here, because a clue is
    // only dropped once the solver has been watched finishing without it. So
    // the result is minimal in the sense that matters to a player: take away
    // any line and the case can no longer be *solved*, not merely that its
    // answer stops being unique. Those are different properties and the second
    // one is the weaker guarantee -- a clue can be redundant for pinning the
    // answer and still be the only thing that lets a person get there.
    //
    // Runs back to front, and never touches the murder clue or a statement.
    for (int i = p.clueCount - 1; i >= fixedClues; --i) {
      const Clue saved = p.clues[i];
      for (int j = i; j + 1 < p.clueCount; ++j) p.clues[j] = p.clues[j + 1];
      --p.clueCount;
      if (deduce(p, probe) >= 0) continue;
      for (int j = p.clueCount; j > i; --j) p.clues[j] = p.clues[j - 1];
      p.clues[i] = saved;
      ++p.clueCount;
    }

    // At least one clue has to join two non-suspect categories, or the case is
    // two independent puzzles wearing one grid: solve the weapons from the
    // weapon clues, solve the places from the place clues, and neither half
    // ever informs the other. A play-tester got exactly that and said so.
    if (cats >= 3) {
      bool bridged = false;
      for (int i = 0; i < p.clueCount && !bridged; ++i) {
        const Clue& clue = p.clues[i];
        if (clue.anchor != Anchor::Item || clue.speaker != kNobodySpeaks) continue;
        if (clue.attr != kNoAttr) continue;
        if (clue.anchorCat != kSuspect && clue.targetCat != kSuspect) bridged = true;
      }
      if (!bridged) continue;
    }

    // MOVED ABOVE THE REVEAL GATE: the fallback path below keeps a near-miss
    // case, and reordering after the gate left that copy with its scene clue
    // still in the middle.
    // The crime-scene clue goes last. It was generated first because it is
    // structural, and it was therefore *printed* first -- so a player read "the
    // body was in the kitchen" before doing any thinking, and the case became
    // "solve the place row, then stop caring". Three play-testers said the
    // reveal should be the end of the reading order, not the start of it.
    for (int i = 0; i + 1 < p.clueCount; ++i) {
      if (p.clues[i].anchor != Anchor::Murderer) continue;
      const Clue scene = p.clues[i];
      for (int j = i; j + 1 < p.clueCount; ++j) p.clues[j] = p.clues[j + 1];
      p.clues[p.clueCount - 1] = scene;
      break;
    }

    // Fairness. Uniqueness is not enough: a case that can only be reached by
    // guessing reads as the game cheating, so it is thrown back.
    Grid grid;
    int reveal = -1;
    const int rounds = deduce(p, grid, &reveal);
    if (rounds < 0) continue;

    // And the accusation should be the last thing that resolves. Printing the
    // crime-scene clue last was not enough on its own: a play-tester got a case
    // where the scene's occupant fell out of the first three clues, so five
    // more existed only to fill rows nobody needed. Prefer cases where the
    // murderer's row closes in the final round of deduction, and keep the best
    // near-miss so a tier can never fail to produce anything.
    if (reveal > 0 && reveal < rounds) {
      if (reveal > bestReveal) {
        bestReveal = reveal;
        bestRounds = rounds;
        best = p;
        best.rounds = static_cast<uint8_t>(rounds > 255 ? 255 : rounds);
        haveBest = true;
      }
      continue;
    }

    p.rounds = static_cast<uint8_t>(rounds > 255 ? 255 : rounds);
    out = p;
    return true;
  }
  if (haveBest) {
    (void)bestRounds;
    out = best;
    return true;
  }
  return false;
}

}  // namespace murdle
