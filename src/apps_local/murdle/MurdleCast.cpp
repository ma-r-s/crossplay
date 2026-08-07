#include "MurdleCast.h"

#include <cstring>

namespace murdle {

// EVERY NAME IS ONE WORD, AND EVERY LETTER MEANS ONE THING.
//
// Those two rules are the same rule. The grid labels its axes with single
// letters, so a letter has to be unambiguous across the whole case: if a
// suspect is ABARA then nothing else in that case may start with A, not a
// weapon, not a place, not a motive. Otherwise the player has to remember which
// axis they are looking at before they can read a label, which is exactly the
// work the letters were supposed to save.
//
// The axes are icons rather than letters, precisely so they spend none of the
// alphabet. See kReservedLetters, which is now empty and says why.
//
// Within each table below the initials are already distinct, so the draw's only
// job is to keep the four categories from colliding with each other. It does
// that by taking them scarcest-first and refusing letters already spoken for;
// see drawCast(). The tables are sized so that cannot fail: motives is the
// tightest at twelve letters and draws first when all of them are free, and
// every later category has at least twelve left however the earlier ones went.
//
// One word is also why suspects are bare first names. A title costs a word and
// buys nothing, and a surname you have never seen is one more thing to decode:
// ANNA and HUGO read instantly, ROOKWOOD and URQUHART do not. Everything in
// these tables is a word a child would know, because a clue you have to parse
// twice is a clue that is doing the wrong kind of work.

const SuspectEntry kSuspects[kSuspectCount] = {
    {"ANNA", Handed::Left, Eyes::Green, Hair::Black, 64},  {"BRUNO", Handed::Right, Eyes::Brown, Hair::Brown, 73},
    {"CARLA", Handed::Left, Eyes::Hazel, Hair::Red, 62},   {"DIEGO", Handed::Right, Eyes::Brown, Hair::Black, 70},
    {"ELENA", Handed::Left, Eyes::Blue, Hair::Blond, 66},  {"FELIX", Handed::Right, Eyes::Green, Hair::Grey, 69},
    {"GRETA", Handed::Left, Eyes::Brown, Hair::Grey, 61},  {"HUGO", Handed::Right, Eyes::Blue, Hair::Brown, 75},
    {"IVAN", Handed::Left, Eyes::Hazel, Hair::Black, 72},  {"JULIA", Handed::Right, Eyes::Green, Hair::Red, 65},
    {"KARL", Handed::Left, Eyes::Blue, Hair::Blond, 76},   {"LUIS", Handed::Right, Eyes::Brown, Hair::Black, 68},
    {"MARIA", Handed::Left, Eyes::Green, Hair::Brown, 63}, {"NORA", Handed::Right, Eyes::Hazel, Hair::Grey, 67},
    {"OSCAR", Handed::Left, Eyes::Brown, Hair::Red, 71},   {"PABLO", Handed::Right, Eyes::Blue, Hair::Black, 74},
    {"QUINN", Handed::Left, Eyes::Hazel, Hair::Blond, 60}, {"ROSA", Handed::Right, Eyes::Green, Hair::Brown, 66},
    {"SOFIA", Handed::Left, Eyes::Blue, Hair::Red, 63},    {"TOMAS", Handed::Right, Eyes::Hazel, Hair::Brown, 77},
    {"UMA", Handed::Left, Eyes::Brown, Hair::Blond, 64},   {"VERA", Handed::Right, Eyes::Green, Hair::Black, 69},
    {"WALTER", Handed::Left, Eyes::Blue, Hair::Grey, 72},  {"YARA", Handed::Right, Eyes::Hazel, Hair::Black, 67},
};

// Everyday objects, and that is the requirement rather than a preference. An
// earlier table reached for EPEE, FLAIL, GARROTTE and TROWEL because they
// carried the letters it needed, and a weapon whose name you have to look up is
// a clue you cannot read.
//
// SEVEN CHARACTERS IS THE CEILING, and it is a layout constraint rather than
// taste. The grid's key lays entries in four columns of about a hundred pixels,
// which is nine or ten glyphs; "J=JEALOUSY" was eleven and ran straight into
// the entry beside it. A test asserts the cap so the next word nobody measures
// cannot do it again.
//
// A trait is one short noun phrase, unique across the table, because a clue
// that names a trait is naming exactly one thing.
const WeaponEntry kWeapons[kWeaponCount] = {
    {"AXE", "the axe", "a bent handle", true},        {"BAT", "the bat", "tape on the grip", true},
    {"CANE", "the cane", "a silver tip", false},      {"DAGGER", "the dagger", "blood on it", false},
    {"FORK", "the fork", "a bent prong", false},      {"GUN", "the gun", "one shot fired", false},
    {"HAMMER", "the hammer", "a split handle", true}, {"IRON", "the iron", "a burn mark", true},
    {"JAR", "the jar", "a cracked lid", false},       {"KNIFE", "the knife", "a chipped edge", false},
    {"LAMP", "the lamp", "a frayed cord", true},      {"MUG", "the mug", "a chip on the rim", false},
    {"NAIL", "the nail", "rust on it", false},        {"OAR", "the oar", "sand on it", true},
    {"PAN", "the pan", "grease on it", true},         {"ROPE", "the rope", "thirteen knots", false},
    {"SPADE", "the spade", "wet mud on it", true},    {"TORCH", "the torch", "a dead battery", false},
    {"VASE", "the vase", "a crack in it", true},      {"WIRE", "the wire", "red paint on it", false},
};

const PlaceEntry kPlaces[kPlaceCount] = {
    {"ATTIC", "in the attic", "a broken window", true},
    {"BARN", "in the barn", "loose straw", true},
    {"CAVE", "in the cave", "cold water", false},
    {"DOCK", "on the dock", "wet footprints", false},
    {"FARM", "on the farm", "muddy boots", false},
    {"GARDEN", "in the garden", "trampled flowers", false},
    {"HALL", "in the hall", "a stopped clock", true},
    {"INN", "at the inn", "a spilled drink", true},
    {"KITCHEN", "in the kitchen", "a burning smell", true},
    {"LAKE", "at the lake", "a torn net", false},
    {"MILL", "at the mill", "flour on the floor", true},
    {"OFFICE", "in the office", "a torn letter", true},
    {"PARK", "in the park", "cut grass", false},
    {"ROOF", "on the roof", "a fallen tile", false},
    {"STUDY", "in the study", "an open drawer", true},
    {"TOWER", "in the tower", "a broken step", true},
};

const MotiveEntry kMotives[kMotiveCount] = {
    {"ANGER", "anger"}, {"DEBT", "debt"},   {"ENVY", "envy"},       {"FEAR", "fear"},
    {"GREED", "greed"}, {"HATE", "hate"},   {"JUSTICE", "justice"}, {"LOVE", "love"},
    {"MERCY", "mercy"}, {"POWER", "power"}, {"REVENGE", "revenge"}, {"SHAME", "shame"},
};

int castSize(const int cat) {
  switch (static_cast<Cat>(cat)) {
    case Cat::Suspect:
      return kSuspectCount;
    case Cat::Weapon:
      return kWeaponCount;
    case Cat::Location:
      return kPlaceCount;
    case Cat::Motive:
      return kMotiveCount;
  }
  return 0;
}

const char* castName(const int cat, const int entry) {
  switch (static_cast<Cat>(cat)) {
    case Cat::Suspect:
      return kSuspects[entry].name;
    case Cat::Weapon:
      return kWeapons[entry].name;
    case Cat::Location:
      return kPlaces[entry].name;
    case Cat::Motive:
      return kMotives[entry].name;
  }
  return "";
}

namespace {

// Motives that mean nearly the same thing must not appear in one case. GREED
// beside MONEY made a play-tester stop and disambiguate mid-solve, which is
// work the puzzle should never ask for; REVENGE beside JUSTICE and ANGER beside
// HATE are the same trap one step further out. Indices into kMotives.
constexpr int kMotiveClash[][2] = {
    {0, 7},  // ANGER  / HATE
    {9, 6},  // REVENGE / JUSTICE
    {4, 8},  // GREED  / MERCY is fine; FEAR / MERCY is the soft pair
};

bool motivesClash(const int a, const int b) {
  for (const auto& pair : kMotiveClash) {
    if ((pair[0] == a && pair[1] == b) || (pair[0] == b && pair[1] == a)) return true;
  }
  return false;
}

int letterIndex(const char* name) {
  const char c = name[0];
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a';
  return 0;
}

}  // namespace

bool drawOnce(uint32_t seed, Shape shape, uint8_t cast[kMaxCats][kMaxItems]);
int dossierUsefulness(const uint8_t cast[kMaxCats][kMaxItems], Shape shape);

bool drawCast(const uint32_t seed, const Shape shape, uint8_t cast[kMaxCats][kMaxItems]) {
  // Redraw while the dossier is decoration. Best-effort and bounded: a case
  // with a dull cast is far better than a case that will not generate, so the
  // last attempt stands whatever it scores.
  uint8_t best[kMaxCats][kMaxItems] = {};
  int bestScore = -1;
  for (int attempt = 0; attempt < 12; ++attempt) {
    if (!drawOnce(seed + static_cast<uint32_t>(attempt) * 2654435761u, shape, cast)) return false;
    const int score = dossierUsefulness(cast, shape);
    if (score > bestScore) {
      bestScore = score;
      std::memcpy(best, cast, sizeof(best));
    }
    if (score >= 3) return true;
  }
  std::memcpy(cast, best, sizeof(best));
  return true;
}

bool drawOnce(const uint32_t seed, const Shape shape, uint8_t cast[kMaxCats][kMaxItems]) {
  // Salted away from the seed generate() uses, so that the draw and the
  // solution are not two views of the same random walk.
  Rng rng(seed ^ 0x5BF03635u);
  std::memset(cast, 0, sizeof(uint8_t) * kMaxCats * kMaxItems);

  // Scarcest first. Motives has twelve letters against sixteen for places and
  // weapons and twenty-four for suspects, so it picks while the alphabet is
  // still empty; letting suspects go first could strand it. The order is fixed
  // rather than sorted at run time because it is a property of the tables, and
  // a test asserts the tables still have this shape.
  constexpr int kOrder[kMaxCats] = {
      static_cast<int>(Cat::Motive),
      static_cast<int>(Cat::Location),
      static_cast<int>(Cat::Weapon),
      static_cast<int>(Cat::Suspect),
  };

  uint32_t usedLetters = kReservedLetters;  // empty, now that the axes are icons
  for (const int cat : kOrder) {
    if (cat >= shape.cats) continue;

    // Everything in this category whose initial is still free. Within a table
    // the initials are already distinct, so this is also the list of usable
    // letters.
    uint8_t pool[32];
    int poolCount = 0;
    const int size = castSize(cat);
    for (int i = 0; i < size && poolCount < static_cast<int>(sizeof(pool)); ++i) {
      const int letter = letterIndex(castName(cat, i));
      if (usedLetters & (1u << letter)) continue;
      pool[poolCount++] = static_cast<uint8_t>(i);
    }
    if (poolCount < shape.items) return false;

    for (int i = 0; i < shape.items; ++i) {
      // Draw, then refuse a motive that clashes with one already taken. Bounded
      // and best-effort: if every remaining candidate clashes the draw stands
      // rather than failing, because a slightly repetitive motive pair is a far
      // smaller problem than a case that will not generate.
      int j = i + static_cast<int>(rng.below(static_cast<uint32_t>(poolCount - i)));
      if (static_cast<Cat>(cat) == Cat::Motive) {
        for (int tries = 0; tries < poolCount - i; ++tries) {
          bool clash = false;
          for (int k = 0; k < i && !clash; ++k) clash = motivesClash(pool[j], cast[cat][k]);
          if (!clash) break;
          j = i + (j - i + 1) % (poolCount - i);
        }
      }
      const uint8_t swap = pool[i];
      pool[i] = pool[j];
      pool[j] = swap;
      cast[cat][i] = pool[i];
      usedLetters |= (1u << letterIndex(castName(cat, pool[i])));
    }
  }
  return true;
}

namespace {

void addMask(AttrMasks& attrs, const uint8_t mask, const uint8_t tag, const uint8_t full) {
  if (mask == 0 || mask == full) return;  // describes nobody, or everybody
  if (attrs.count >= kMaxAttrMasks) return;
  for (int i = 0; i < attrs.count; ++i) {
    if (attrs.mask[i] == mask) return;  // two ways to say the same set
  }
  attrs.mask[attrs.count] = mask;
  attrs.tag[attrs.count] = tag;
  ++attrs.count;
}

}  // namespace

// How many of the four dossier axes could carry a clue at all, for this draw.
//
// An axis is useful when its values split the drawn suspects into a proper
// subset -- all four sharing an eye colour says nothing, and so does an axis
// where every value is unique if no clue ever reaches for it. Two critics
// independently found casts where eyes were 2/2 and heights were within four
// inches, so half the printed dossier could not have discriminated anybody even
// in principle. The draw now prefers casts that can.
int dossierUsefulness(const uint8_t cast[kMaxCats][kMaxItems], const Shape shape) {
  const int items = shape.items;
  const auto at = [&cast](const int i) -> const SuspectEntry& {
    return kSuspects[cast[static_cast<int>(Cat::Suspect)][i]];
  };
  int useful = 0;

  // Handedness, eyes, hair: an axis counts when some but not all share a value.
  for (int axis = 0; axis < 3; ++axis) {
    bool splits = false;
    for (int i = 0; i < items && !splits; ++i) {
      int same = 0;
      for (int j = 0; j < items; ++j) {
        const bool eq = axis == 0   ? at(i).handed == at(j).handed
                        : axis == 1 ? at(i).eyes == at(j).eyes
                                    : at(i).hair == at(j).hair;
        if (eq) ++same;
      }
      if (same > 0 && same < items) splits = true;
    }
    if (splits) ++useful;
  }

  // Height counts when the tallest and shortest are unambiguous and far enough
  // apart to be worth a sentence.
  int tallest = 0;
  int shortest = 0;
  for (int i = 1; i < items; ++i) {
    if (at(i).inches > at(tallest).inches) tallest = i;
    if (at(i).inches < at(shortest).inches) shortest = i;
  }
  int tallTies = 0;
  int shortTies = 0;
  for (int i = 0; i < items; ++i) {
    if (at(i).inches == at(tallest).inches) ++tallTies;
    if (at(i).inches == at(shortest).inches) ++shortTies;
  }
  if (tallTies == 1 && shortTies == 1 && at(tallest).inches - at(shortest).inches >= 4) ++useful;
  return useful;
}

AttrMasks attrMasksFor(const uint8_t cast[kMaxCats][kMaxItems], const Shape shape) {
  AttrMasks attrs;
  const int items = shape.items;
  const uint8_t full = static_cast<uint8_t>((1u << items) - 1u);

  const auto bit = [](const int i) { return static_cast<uint8_t>(1u << i); };
  const auto suspect = [&cast](const int i) -> const SuspectEntry& {
    return kSuspects[cast[static_cast<int>(Cat::Suspect)][i]];
  };

  for (uint8_t v = 0; v < 2; ++v) {
    uint8_t mask = 0;
    for (int i = 0; i < items; ++i) {
      if (static_cast<uint8_t>(suspect(i).handed) == v) mask = static_cast<uint8_t>(mask | bit(i));
    }
    addMask(attrs, mask, attrTag(AttrKind::Handed, v), full);
  }
  for (uint8_t v = 0; v < 4; ++v) {
    uint8_t mask = 0;
    for (int i = 0; i < items; ++i) {
      if (static_cast<uint8_t>(suspect(i).eyes) == v) mask = static_cast<uint8_t>(mask | bit(i));
    }
    addMask(attrs, mask, attrTag(AttrKind::Eyes, v), full);
  }
  for (uint8_t v = 0; v < 5; ++v) {
    uint8_t mask = 0;
    for (int i = 0; i < items; ++i) {
      if (static_cast<uint8_t>(suspect(i).hair) == v) mask = static_cast<uint8_t>(mask | bit(i));
    }
    addMask(attrs, mask, attrTag(AttrKind::Hair, v), full);
  }

  // Tallest and shortest, but only when they are unambiguous. "The tallest of
  // them" describing two people is not a clue, it is a mistake.
  int tallest = 0;
  int shortest = 0;
  int tallTies = 0;
  int shortTies = 0;
  for (int i = 1; i < items; ++i) {
    if (suspect(i).inches > suspect(tallest).inches) tallest = i;
    if (suspect(i).inches < suspect(shortest).inches) shortest = i;
  }
  for (int i = 0; i < items; ++i) {
    if (suspect(i).inches == suspect(tallest).inches) ++tallTies;
    if (suspect(i).inches == suspect(shortest).inches) ++shortTies;
  }
  if (tallTies == 1) addMask(attrs, bit(tallest), kAttrTallest, full);
  if (shortTies == 1) addMask(attrs, bit(shortest), kAttrShortest, full);

  // Comparisons against a named suspect, which is where height earns its place.
  // Added after the superlatives so that a set which is describable both ways
  // keeps the shorter wording: addMask dedupes on the mask, and "was the
  // tallest of them" beats "was taller than BRUNO" for the same set of people.
  // What survives here is the multi-bit middle -- exactly the masks the
  // superlatives cannot express and the hard tiers require.
  //
  // No tie check is needed. The reference suspect is named, and "taller than
  // ANNA" is a well-defined set even when somebody else is ANNA's exact height:
  // it simply excludes them both.
  for (int i = 0; i < items; ++i) {
    uint8_t taller = 0;
    uint8_t shorter = 0;
    for (int j = 0; j < items; ++j) {
      if (j == i) continue;
      if (suspect(j).inches > suspect(i).inches) taller = static_cast<uint8_t>(taller | bit(j));
      if (suspect(j).inches < suspect(i).inches) shorter = static_cast<uint8_t>(shorter | bit(j));
    }
    addMask(attrs, taller, static_cast<uint8_t>(kAttrTallerThan + i), full);
    addMask(attrs, shorter, static_cast<uint8_t>(kAttrShorterThan + i), full);
  }

  return attrs;
}

}  // namespace murdle
