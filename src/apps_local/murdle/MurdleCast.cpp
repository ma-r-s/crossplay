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
// A WEAPON'S DETAIL IS A SUBSTANCE OR A MARK ON THE OBJECT. A PLACE'S IS A
// STATE OF THE ROOM. The two tables are written in different registers on
// purpose, and it is the fix for the most dangerous defect play-testing found.
//
// The old set had four weapons whose detail was a crack or a chip, three whose
// detail was a damaged handle, and pairs across the tables that rhymed: AXE "a
// bent handle" beside TOWER "a broken step", SPADE "wet mud on it" beside FARM
// "muddy boots", CAVE "cold water" beside LAKE "a torn net". Six critics
// reported it independently and one spelled out why it matters more than the
// other prose complaints: the body clue names the crime scene BY its detail, so
// a reader who half-remembers "b... bent, broken" picks the wrong fixture and
// accuses the wrong suspect. That is a wrong answer produced by careful
// reasoning about the wrong thing, and nothing in the game can catch it.
//
// So: no two details anywhere in either table share a theme or a significant
// word, and a test holds that. When adding a fixture, the question is not "is
// this evocative" but "could a tired reader confuse this with any of the other
// thirty-five".
const WeaponEntry kWeapons[kWeaponCount] = {
    {"AXE", "the axe", "sawdust on it", true},       {"BAT", "the bat", "tape on the grip", true},
    {"CANE", "the cane", "a silver tip", false},     {"DAGGER", "the dagger", "blood on it", false},
    {"FORK", "the fork", "a twisted prong", false},  {"GUN", "the gun", "one shot fired", false},
    {"HAMMER", "the hammer", "a wobbly head", true}, {"IRON", "the iron", "a scorch mark", true},
    {"JAR", "the jar", "honey inside", false},       {"KNIFE", "the knife", "a chipped edge", false},
    {"LAMP", "the lamp", "a frayed cord", true},     {"MUG", "the mug", "lipstick on the rim", false},
    {"NAIL", "the nail", "rust on it", false},       {"OAR", "the oar", "sand on it", true},
    {"PAN", "the pan", "grease on it", true},        {"ROPE", "the rope", "thirteen knots", false},
    {"SPADE", "the spade", "dried mud on it", true}, {"TORCH", "the torch", "a dead battery", false},
    {"VASE", "the vase", "gold leaf on it", true},   {"WIRE", "the wire", "red paint on it", false},
};

// States of a room, never marks on an object -- see the note above kWeapons.
const PlaceEntry kPlaces[kPlaceCount] = {
    {"ATTIC", "in the attic", "a swinging hatch", true},
    {"BARN", "in the barn", "loose straw", true},
    {"CAVE", "in the cave", "an echo", false},
    {"DOCK", "on the dock", "a creaking plank", false},
    {"FARM", "on the farm", "a barking dog", false},
    {"GARDEN", "in the garden", "a buzzing hive", false},
    {"HALL", "in the hall", "a stopped clock", true},
    {"INN", "at the inn", "an unpaid bill", true},
    {"KITCHEN", "in the kitchen", "an empty larder", true},
    {"LAKE", "at the lake", "a rising mist", false},
    {"MILL", "at the mill", "a turning wheel", true},
    {"OFFICE", "in the office", "a locked safe", true},
    {"PARK", "in the park", "a kite in a tree", false},
    {"ROOF", "on the roof", "a pigeon feather", false},
    {"STUDY", "in the study", "an open drawer", true},
    {"TOWER", "in the tower", "a long shadow", true},
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
  // NO TWO SUSPECTS AT THE SAME HEIGHT, treated as part of the score rather
  // than as a hard refusal.
  //
  // "The one at the inn was taller than ANNA" correctly excludes anybody who is
  // ANNA's exact height, because taller means strictly taller. It is also, in
  // the words of the play-tester who hit it, "the single most likely place a
  // real player gets this wrong for a non-logical reason" -- two rows reading
  // 5'7" on a small screen, and "taller" parsed as "not shorter". The rule
  // stays correct and the trap goes away.
  const auto tiedHeights = [&](const uint8_t c[kMaxCats][kMaxItems]) {
    for (int i = 0; i < shape.items; ++i) {
      for (int j = i + 1; j < shape.items; ++j) {
        if (kSuspects[c[static_cast<int>(Cat::Suspect)][i]].inches ==
            kSuspects[c[static_cast<int>(Cat::Suspect)][j]].inches) {
          return true;
        }
      }
    }
    return false;
  };

  for (int attempt = 0; attempt < 12; ++attempt) {
    if (!drawOnce(seed + static_cast<uint32_t>(attempt) * 2654435761u, shape, cast)) return false;
    const int score = dossierUsefulness(cast, shape) - (tiedHeights(cast) ? 2 : 0);
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

// The dossier's columns, as opaque ids the generator can count without knowing
// what any of them mean. Kept here rather than in MurdleCore because that is
// the seam: the logic gets masks and an "these came from the same column"
// relation, and nothing else.
enum Axis : uint8_t { kAxisHanded = 0, kAxisEyes = 1, kAxisHair = 2, kAxisHeight = 3 };

void addMask(AttrMasks& attrs, const uint8_t mask, const uint8_t tag, const uint8_t axis, const uint8_t full) {
  if (mask == 0 || mask == full) return;  // describes nobody, or everybody
  if (attrs.count >= kMaxAttrMasks) return;
  for (int i = 0; i < attrs.count; ++i) {
    if (attrs.mask[i] == mask) return;  // two ways to say the same set
  }
  attrs.mask[attrs.count] = mask;
  attrs.tag[attrs.count] = tag;
  attrs.axis[attrs.count] = axis;
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
    addMask(attrs, mask, attrTag(AttrKind::Handed, v), kAxisHanded, full);
  }
  for (uint8_t v = 0; v < 4; ++v) {
    uint8_t mask = 0;
    for (int i = 0; i < items; ++i) {
      if (static_cast<uint8_t>(suspect(i).eyes) == v) mask = static_cast<uint8_t>(mask | bit(i));
    }
    addMask(attrs, mask, attrTag(AttrKind::Eyes, v), kAxisEyes, full);
  }
  for (uint8_t v = 0; v < 5; ++v) {
    uint8_t mask = 0;
    for (int i = 0; i < items; ++i) {
      if (static_cast<uint8_t>(suspect(i).hair) == v) mask = static_cast<uint8_t>(mask | bit(i));
    }
    addMask(attrs, mask, attrTag(AttrKind::Hair, v), kAxisHair, full);
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
  if (tallTies == 1) addMask(attrs, bit(tallest), kAttrTallest, kAxisHeight, full);
  if (shortTies == 1) addMask(attrs, bit(shortest), kAttrShortest, kAxisHeight, full);

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
  // A COMPARISON AGAINST AN EXTREME IS A NEGATION IN ELEVEN EXTRA WORDS, and is
  // refused here.
  //
  // "The one with the iron was shorter than BRUNO", where BRUNO is the tallest
  // drawn suspect, excludes BRUNO and nobody else -- exactly what "BRUNO did not
  // carry the iron" says, at three times the length and with a detour through
  // the dossier. Two play-testers worked that out independently and one of them
  // got a case containing two of them against the same suspect.
  //
  // Comparatives were added to stop the dossier being decoration; a degenerate
  // one turns it straight back into decoration while looking like it is
  // working. So a mask survives only if it leaves out somebody besides the
  // reference: `held < items - 1`.
  const int useful = items - 1;
  for (int i = 0; i < items; ++i) {
    uint8_t taller = 0;
    uint8_t shorter = 0;
    int tallCount = 0;
    int shortCount = 0;
    for (int j = 0; j < items; ++j) {
      if (j == i) continue;
      if (suspect(j).inches > suspect(i).inches) {
        taller = static_cast<uint8_t>(taller | bit(j));
        ++tallCount;
      }
      if (suspect(j).inches < suspect(i).inches) {
        shorter = static_cast<uint8_t>(shorter | bit(j));
        ++shortCount;
      }
    }
    if (tallCount < useful) addMask(attrs, taller, static_cast<uint8_t>(kAttrTallerThan + i), kAxisHeight, full);
    if (shortCount < useful) addMask(attrs, shorter, static_cast<uint8_t>(kAttrShorterThan + i), kAxisHeight, full);
  }

  return attrs;
}

}  // namespace murdle
