#include "MurdleCast.h"

#include <cstring>

namespace murdle {

// Names are a title and a surname because that is what reads as a suspect at a
// glance without anybody having to be told who they are. Attributes are spread
// deliberately rather than randomly: an attribute nobody shares makes no clue,
// and one everybody shares makes no clue either, so the columns are mixed so
// that any four drawn suspects almost always yield several usable masks.
const SuspectEntry kSuspects[kSuspectCount] = {
    {"DOCTOR ASHE", Handed::Left, Eyes::Green, Hair::Black, 68},
    {"CAPTAIN MERROW", Handed::Right, Eyes::Blue, Hair::Grey, 74},
    {"SISTER VALE", Handed::Left, Eyes::Brown, Hair::Brown, 63},
    {"PROFESSOR QUILL", Handed::Right, Eyes::Hazel, Hair::Grey, 70},
    {"MADAME OKONKWO", Handed::Left, Eyes::Brown, Hair::Black, 66},
    {"THE VICAR", Handed::Right, Eyes::Blue, Hair::Blond, 71},
    {"JUDGE HALLAM", Handed::Right, Eyes::Green, Hair::Grey, 69},
    {"NURSE PRYCE", Handed::Left, Eyes::Hazel, Hair::Red, 62},
    {"COLONEL BRAY", Handed::Right, Eyes::Brown, Hair::Brown, 73},
    {"LADY FENWICK", Handed::Left, Eyes::Blue, Hair::Blond, 65},
    {"THE LIBRARIAN", Handed::Right, Eyes::Hazel, Hair::Brown, 67},
    {"INSPECTOR NADIR", Handed::Left, Eyes::Brown, Hair::Black, 72},
    {"MISS THORNE", Handed::Right, Eyes::Green, Hair::Red, 64},
    {"BARON KESTREL", Handed::Left, Eyes::Blue, Hair::Black, 76},
    {"THE GARDENER", Handed::Right, Eyes::Brown, Hair::Grey, 70},
    {"DEAN OYELARAN", Handed::Left, Eyes::Hazel, Hair::Black, 68},
    {"CHEF DUVAL", Handed::Right, Eyes::Green, Hair::Brown, 66},
    {"THE ARCHIVIST", Handed::Left, Eyes::Blue, Hair::Grey, 61},
    {"MAJOR STRAND", Handed::Right, Eyes::Hazel, Hair::Blond, 75},
    {"DAME ROOKWOOD", Handed::Left, Eyes::Green, Hair::Grey, 63},
    {"THE APOTHECARY", Handed::Right, Eyes::Brown, Hair::Red, 69},
    {"SERGEANT ILIC", Handed::Left, Eyes::Hazel, Hair::Brown, 71},
    {"WIDOW BLANCHE", Handed::Right, Eyes::Blue, Hair::Blond, 60},
    {"THE CURATOR", Handed::Left, Eyes::Green, Hair::Black, 77},
};

// A trait is one noun phrase and it has to be unique across the table, because
// a clue that names a trait is naming exactly one thing.
const WeaponEntry kWeapons[kWeaponCount] = {
    {"BONE SAW", "the bone saw", "a chipped blade", true},
    {"CROWBAR", "the crowbar", "flecks of red paint", true},
    {"LETTER OPENER", "the letter opener", "a monogrammed handle", false},
    {"FIRE POKER", "the fire poker", "a bent tip", true},
    {"CANDLESTICK", "the candlestick", "cold wax on it", true},
    {"GARROTTE WIRE", "the garrotte wire", "a loop of piano wire", false},
    {"POISONED GIN", "the poisoned gin", "a broken seal", false},
    {"LEAD PIPE", "the lead pipe", "a threaded end", true},
    {"ICE PICK", "the ice pick", "a cork on the point", false},
    {"SHOVEL", "the shovel", "wet earth on it", true},
    {"TYPEWRITER", "the typewriter", "a jammed letter E", true},
    {"DUELLING PISTOL", "the duelling pistol", "one shot fired", false},
    {"ROLLING PIN", "the rolling pin", "a dusting of flour", true},
    {"CLEAVER", "the cleaver", "a taped grip", true},
    {"BOAT OAR", "the boat oar", "barnacles on it", true},
    {"MARBLE BUST", "the marble bust", "a missing nose", true},
};

const PlaceEntry kPlaces[kPlaceCount] = {
    {"LIGHTHOUSE", "in the lighthouse", "peeling paint", true},
    {"BOATHOUSE", "in the boathouse", "a coil of wet rope", true},
    {"GREENHOUSE", "in the greenhouse", "a cracked pane", true},
    {"WINE CELLAR", "in the wine cellar", "a spilled bottle", true},
    {"CLOCK TOWER", "in the clock tower", "a stopped pendulum", true},
    {"ORCHARD", "in the orchard", "windfall apples", false},
    {"LIBRARY", "in the library", "a ladder left out", true},
    {"TRAIN CARRIAGE", "in the train carriage", "a punched ticket", true},
    {"CHAPEL", "in the chapel", "a guttered candle", true},
    {"BILLIARD ROOM", "in the billiard room", "torn green baize", true},
    {"PIER", "on the pier", "salt on the boards", false},
    {"KITCHEN", "in the kitchen", "a burning pan", true},
    {"ATTIC", "in the attic", "a broken skylight", true},
    {"BOILER ROOM", "in the boiler room", "a hissing valve", true},
    {"ROSE GARDEN", "in the rose garden", "trampled roses", false},
    {"MORGUE", "in the morgue", "an open drawer", true},
};

const MotiveEntry kMotives[kMotiveCount] = {
    {"GREED", "greed"},
    {"REVENGE", "revenge"},
    {"JEALOUSY", "jealousy"},
    {"EXPOSURE", "a fear of exposure"},
    {"AN OLD DEBT", "an old debt"},
    {"INHERITANCE", "an inheritance"},
    {"A STOLEN IDEA", "a stolen idea"},
    {"LOVE", "love"},
    {"SPITE", "spite"},
    {"A SECRET", "a buried secret"},
    {"BLACKMAIL", "blackmail"},
    {"PROMOTION", "a promotion"},
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

void drawCast(const uint32_t seed, const Shape shape, uint8_t cast[kMaxCats][kMaxItems]) {
  // Salted away from the seed generate() uses, so that the draw and the
  // solution are not two views of the same random walk.
  Rng rng(seed ^ 0x5BF03635u);
  std::memset(cast, 0, sizeof(uint8_t) * kMaxCats * kMaxItems);

  for (int c = 0; c < shape.cats; ++c) {
    const int size = castSize(c);
    // Partial Fisher-Yates over an index list: draws `items` distinct entries
    // without a rejection loop and without a seen-set.
    uint8_t bag[32];
    const int n = size < 32 ? size : 32;
    for (int i = 0; i < n; ++i) bag[i] = static_cast<uint8_t>(i);
    for (int i = 0; i < shape.items; ++i) {
      const int j = i + static_cast<int>(rng.below(static_cast<uint32_t>(n - i)));
      const uint8_t swap = bag[i];
      bag[i] = bag[j];
      bag[j] = swap;
      cast[c][i] = bag[i];
    }
  }
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
