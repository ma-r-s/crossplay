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

bool Grid::setYes(const int catA, const int itemA, const int catB, const int itemB) {
  if (put(catA, itemA, catB, itemB, Mark::Yes) < 0) return false;
  // One suspect cannot hold two weapons, and one weapon cannot be held by two
  // suspects. Bookkeeping, not deduction: this stays inside the block.
  for (int i = 0; i < shape_.items; ++i) {
    if (i != itemB && put(catA, itemA, catB, i, Mark::No) < 0) return false;
    if (i != itemA && put(catA, i, catB, itemB, Mark::No) < 0) return false;
  }
  return true;
}

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

// ---------------------------------------------------------------------------
// Propagation

namespace {

// Applies constraints, block uniqueness and cross-block transitivity to a
// fixpoint. Nothing here is beyond what a person does with a pencil, which is
// the point: a solver reasoning in some stronger system would certify puzzles
// nobody can actually solve.
int propagate(Grid& grid, const Constraint* cons, const int conCount, const Shape shape) {
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

}  // namespace

int deduce(const Puzzle& puzzle, Grid& grid) {
  const Shape shape = puzzle.shape;
  const int items = shape.items;
  grid.reset(shape);

  bool anySpoken = false;
  for (int i = 0; i < puzzle.clueCount; ++i) {
    if (puzzle.clues[i].speaker != kNobodySpeaks) anySpoken = true;
  }

  // Phase one: everything that is unconditionally true and not about the
  // murderer. On every tier but Impossible this is the whole puzzle, because
  // the murder clue names the crime scene and says nothing about who was where.
  Constraint cons[kMaxClues];
  int conCount = 0;
  for (int i = 0; i < puzzle.clueCount; ++i) {
    const Clue& clue = puzzle.clues[i];
    if (clue.anchor == Anchor::Murderer || clue.speaker != kNobodySpeaks) continue;
    cons[conCount++] = resolve(clue, items, 0, false);
  }

  const int rounds = propagate(grid, cons, conCount, shape);
  if (rounds == kContradiction) return kContradiction;

  if (!anySpoken) return grid.complete() ? rounds : kUnfair;

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
void buildPool(const Puzzle& p, const AttrMasks& attrs, const Tier tier, Scratch& scratch) {
  const int cats = p.shape.cats;
  const int items = p.shape.items;
  const uint8_t full = fullMask(items);
  const bool allowBarePositive = tier == Tier::Elementary;
  const bool allowPair = tier == Tier::HardBoiled || tier == Tier::Impossible;
  const bool allowAttribute = tier != Tier::Elementary;

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

        if (allowBarePositive) {
          clue.targetMask = static_cast<uint8_t>(1u << trueItem);
          addToPool(scratch, clue);
        }
        // "Not with y", for every y it is genuinely not with. Available on every
        // tier: a negative is the backbone of a logic grid.
        for (int y = 0; y < items; ++y) {
          if (y == trueItem) continue;
          clue.targetMask = static_cast<uint8_t>(full & ~(1u << y));
          addToPool(scratch, clue);
        }
        if (allowPair) {
          for (int y = 0; y < items; ++y) {
            if (y == trueItem) continue;
            clue.targetMask = static_cast<uint8_t>((1u << trueItem) | (1u << y));
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

// One statement per suspect. Everyone but the murderer says something true;
// the murderer says something false, which is a single-bit claim about where
// they were or what they carried that simply is not so.
bool addStatements(Puzzle& p, Rng& rng) {
  // Weapon and location only, which is why the category is picked from two
  // rather than from all of them: "I was driven by greed" is not a sentence a
  // witness says about themselves.
  const int items = p.shape.items;
  for (int s = 0; s < items; ++s) {
    Clue clue{};
    clue.anchor = Anchor::Item;
    clue.speaker = static_cast<uint8_t>(s);
    clue.voice = static_cast<uint8_t>(rng.next());

    if (s == p.murderRow) {
      // A lie: name a category, and claim a value it does not have.
      const int t = 1 + static_cast<int>(rng.below(2));
      const int trueItem = p.assign[t][s];
      int wrong = static_cast<int>(rng.below(static_cast<uint32_t>(items - 1)));
      if (wrong >= trueItem) ++wrong;
      clue.anchorCat = static_cast<uint8_t>(kSuspect);
      clue.anchorItem = static_cast<uint8_t>(s);
      clue.targetCat = static_cast<uint8_t>(t);
      clue.targetMask = static_cast<uint8_t>(1u << wrong);
    } else {
      // A truth, drawn from the same pool the ordinary clues come from but
      // pinned to this speaker's own row, because a witness talks about
      // themselves.
      const int t = 1 + static_cast<int>(rng.below(2));
      clue.anchorCat = static_cast<uint8_t>(kSuspect);
      clue.anchorItem = static_cast<uint8_t>(s);
      clue.targetCat = static_cast<uint8_t>(t);
      clue.targetMask = static_cast<uint8_t>(1u << p.assign[t][s]);
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
    murder.targetCat = static_cast<uint8_t>(kLocation);
    murder.targetMask = static_cast<uint8_t>(1u << p.assign[kLocation][p.murderRow]);
    murder.speaker = kNobodySpeaks;
    murder.attr = kNoAttr;
    murder.voice = static_cast<uint8_t>(rng.next());
    p.clues[p.clueCount++] = murder;

    if (tier == Tier::Impossible && !addStatements(p, rng)) continue;
    const int fixedClues = p.clueCount;

    buildPool(p, attrs, tier, scratch);
    shufflePool(scratch, rng);

    // Greedy: add clues until the case has exactly one reading.
    int taken = 0;
    while (countSolutions(p, 2, scratch) > 1) {
      if (taken >= scratch.poolCount || p.clueCount >= kMaxClues) break;
      Clue clue = scratch.pool[taken++];
      clue.voice = static_cast<uint8_t>(rng.next());
      p.clues[p.clueCount++] = clue;
    }
    if (countSolutions(p, 2, scratch) != 1) continue;

    // Prune: a clue the case solves without is a clue that should not be in
    // it. Runs back to front, and never touches the murder clue or a statement.
    for (int i = p.clueCount - 1; i >= fixedClues; --i) {
      const Clue saved = p.clues[i];
      for (int j = i; j + 1 < p.clueCount; ++j) p.clues[j] = p.clues[j + 1];
      --p.clueCount;
      if (countSolutions(p, 2, scratch) == 1) continue;
      for (int j = p.clueCount; j > i; --j) p.clues[j] = p.clues[j - 1];
      p.clues[i] = saved;
      ++p.clueCount;
    }

    // Fairness. Uniqueness is not enough: a case that can only be reached by
    // guessing reads as the game cheating, so it is thrown back.
    Grid grid;
    const int rounds = deduce(p, grid);
    if (rounds < 0) continue;

    p.rounds = static_cast<uint8_t>(rounds > 255 ? 255 : rounds);
    out = p;
    return true;
  }
  return false;
}

}  // namespace murdle
