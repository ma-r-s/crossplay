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
// tightest at twelve letters and the draw needs four, and every later category
// has at least twelve letters left however the earlier ones went.
//
// One word is also why the names are bare surnames. A title costs a word and
// buys nothing: THORNE reads as a person, and MISS THORNE does not fit a 220px
// button on the accusation sheet.

const SuspectEntry kSuspects[kSuspectCount] = {
    {"ABARA", Handed::Left, Eyes::Green, Hair::Black, 68},     {"BLANCHE", Handed::Right, Eyes::Blue, Hair::Blond, 60},
    {"CRANE", Handed::Left, Eyes::Hazel, Hair::Grey, 74},      {"DUVAL", Handed::Right, Eyes::Green, Hair::Brown, 66},
    {"ELDRIDGE", Handed::Left, Eyes::Brown, Hair::Red, 71},    {"FENWICK", Handed::Left, Eyes::Blue, Hair::Blond, 65},
    {"GLASS", Handed::Right, Eyes::Hazel, Hair::Black, 69},    {"HALLAM", Handed::Right, Eyes::Green, Hair::Grey, 69},
    {"ILIC", Handed::Left, Eyes::Hazel, Hair::Brown, 71},      {"JARDINE", Handed::Right, Eyes::Brown, Hair::Blond, 63},
    {"KESTREL", Handed::Left, Eyes::Blue, Hair::Black, 76},    {"LOCKE", Handed::Right, Eyes::Green, Hair::Brown, 67},
    {"MERROW", Handed::Right, Eyes::Blue, Hair::Grey, 74},     {"NADIR", Handed::Left, Eyes::Brown, Hair::Black, 72},
    {"OKONKWO", Handed::Left, Eyes::Brown, Hair::Black, 66},   {"PRYCE", Handed::Left, Eyes::Hazel, Hair::Red, 62},
    {"QUILL", Handed::Right, Eyes::Hazel, Hair::Grey, 70},     {"ROOKWOOD", Handed::Left, Eyes::Green, Hair::Grey, 63},
    {"STRAND", Handed::Right, Eyes::Hazel, Hair::Blond, 75},   {"THORNE", Handed::Right, Eyes::Green, Hair::Red, 64},
    {"URQUHART", Handed::Right, Eyes::Brown, Hair::Brown, 73}, {"VALE", Handed::Left, Eyes::Brown, Hair::Brown, 61},
    {"WREN", Handed::Right, Eyes::Blue, Hair::Red, 67},        {"YUEN", Handed::Left, Eyes::Blue, Hair::Black, 70},
};

// A trait is one short noun phrase and it has to be unique across the table,
// because a clue that names a trait is naming exactly one thing.
const WeaponEntry kWeapons[kWeaponCount] = {
    {"ANVIL", "the anvil", "a scorched face", true},
    {"BRICK", "the brick", "mortar still on it", true},
    {"CLEAVER", "the cleaver", "a taped grip", true},
    {"DAGGER", "the dagger", "a jewelled hilt", false},
    {"EPEE", "the epee", "a bent blade", false},
    {"FLAIL", "the flail", "a rusted chain", true},
    {"GARROTTE", "the garrotte", "a loop of piano wire", false},
    {"HAMMER", "the hammer", "a split handle", true},
    {"ICEPICK", "the icepick", "a cork on the point", false},
    {"KNIFE", "the knife", "a chipped blade", false},
    {"LANTERN", "the lantern", "a cracked glass", true},
    {"MALLET", "the mallet", "a cracked head", true},
    {"NOOSE", "the noose", "thirteen turns", false},
    {"OAR", "the oar", "barnacles on it", true},
    {"POKER", "the poker", "a bent tip", true},
    {"RAZOR", "the razor", "a pearl handle", false},
    {"SHOVEL", "the shovel", "wet earth on it", true},
    {"TROWEL", "the trowel", "a dusting of lime", false},
    {"VASE", "the vase", "a hairline crack", true},
    {"WRENCH", "the wrench", "flecks of red paint", true},
};

const PlaceEntry kPlaces[kPlaceCount] = {
    {"ATTIC", "in the attic", "a broken skylight", true},
    {"BOATHOUSE", "in the boathouse", "a coil of wet rope", true},
    {"CELLAR", "in the cellar", "a spilled bottle", true},
    {"DOCKYARD", "in the dockyard", "a snapped chain", false},
    {"FOUNDRY", "in the foundry", "a cooling ingot", true},
    {"GREENHOUSE", "in the greenhouse", "a cracked pane", true},
    {"HAYLOFT", "in the hayloft", "a fallen ladder", true},
    {"JETTY", "on the jetty", "salt on the boards", false},
    {"KITCHEN", "in the kitchen", "a burning pan", true},
    {"LIGHTHOUSE", "in the lighthouse", "peeling paint", true},
    {"MORGUE", "in the morgue", "an open drawer", true},
    {"ORCHARD", "in the orchard", "windfall apples", false},
    {"PIER", "on the pier", "a torn ticket", false},
    {"QUARRY", "in the quarry", "fresh blasting powder", false},
    {"STABLE", "in the stable", "a loose shoe", true},
    {"TOWER", "in the tower", "a stopped pendulum", true},
};

const MotiveEntry kMotives[kMotiveCount] = {
    {"AMBITION", "ambition"}, {"BLACKMAIL", "blackmail"}, {"CONTEMPT", "contempt"}, {"DEBT", "debt"},
    {"ENVY", "envy"},         {"FEAR", "fear"},           {"GREED", "greed"},       {"HATRED", "hatred"},
    {"JEALOUSY", "jealousy"}, {"LOVE", "love"},           {"PRIDE", "pride"},       {"REVENGE", "revenge"},
    {"SPITE", "spite"},       {"VANITY", "vanity"},
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

int letterIndex(const char* name) {
  const char c = name[0];
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a';
  return 0;
}

}  // namespace

bool drawCast(const uint32_t seed, const Shape shape, uint8_t cast[kMaxCats][kMaxItems]) {
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
      const int j = i + static_cast<int>(rng.below(static_cast<uint32_t>(poolCount - i)));
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

  return attrs;
}

}  // namespace murdle
