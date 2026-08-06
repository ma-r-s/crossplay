#include "MurdleText.h"

#include <cstdio>
#include <cstring>

namespace murdletext {

using murdle::Anchor;
using murdle::Cat;
using murdle::Clue;
using murdle::kMaxCats;
using murdle::kNoAttr;
using murdle::kNobodySpeaks;
using murdle::Puzzle;

namespace {

int popcount(uint8_t mask) {
  int n = 0;
  while (mask) {
    mask &= static_cast<uint8_t>(mask - 1);
    ++n;
  }
  return n;
}

int lowestBit(const uint8_t mask) {
  for (int i = 0; i < 8; ++i) {
    if (mask & static_cast<uint8_t>(1u << i)) return i;
  }
  return 0;
}

int highestBit(const uint8_t mask) {
  int found = 0;
  for (int i = 0; i < 8; ++i) {
    if (mask & static_cast<uint8_t>(1u << i)) found = i;
  }
  return found;
}

// The one item a mask leaves out, when it leaves out exactly one.
int missingBit(const uint8_t mask, const int items) {
  for (int i = 0; i < items; ++i) {
    if ((mask & static_cast<uint8_t>(1u << i)) == 0) return i;
  }
  return 0;
}

const murdle::SuspectEntry& suspectOf(const Puzzle& p, const int item) {
  return murdle::kSuspects[p.cast[static_cast<int>(Cat::Suspect)][item]];
}
const murdle::WeaponEntry& weaponOf(const Puzzle& p, const int item) {
  return murdle::kWeapons[p.cast[static_cast<int>(Cat::Weapon)][item]];
}
const murdle::PlaceEntry& placeOf(const Puzzle& p, const int item) {
  return murdle::kPlaces[p.cast[static_cast<int>(Cat::Location)][item]];
}
const murdle::MotiveEntry& motiveOf(const Puzzle& p, const int item) {
  return murdle::kMotives[p.cast[static_cast<int>(Cat::Motive)][item]];
}

// The form a fixture takes inside a sentence, preposition and article already
// attached. Suspects are their own name; everything else was written with its
// grammar built in, so a template never has to decide between "a" and "an".
const char* phraseOf(const Puzzle& p, const int cat, const int item) {
  switch (static_cast<Cat>(cat)) {
    case Cat::Suspect:
      return suspectOf(p, item).name;
    case Cat::Weapon:
      return weaponOf(p, item).phrase;
    case Cat::Location:
      return placeOf(p, item).phrase;
    case Cat::Motive:
      return motiveOf(p, item).phrase;
  }
  return "";
}

void appendCapitalised(char* out, const int cap, const char* text) {
  const int len = static_cast<int>(std::strlen(out));
  std::snprintf(out + len, static_cast<size_t>(cap - len), "%s", text);
  if (out[len] >= 'a' && out[len] <= 'z') out[len] = static_cast<char>(out[len] - 'a' + 'A');
}

void append(char* out, const int cap, const char* fmt, const char* a, const char* b = nullptr) {
  const int len = static_cast<int>(std::strlen(out));
  if (b) {
    std::snprintf(out + len, static_cast<size_t>(cap - len), fmt, a, b);
  } else {
    std::snprintf(out + len, static_cast<size_t>(cap - len), fmt, a);
  }
}

// ---------------------------------------------------------------------------
// Who the clue is about

void anchorPhrase(const Puzzle& p, const Clue& clue, char* out, const int cap) {
  out[0] = '\0';
  if (clue.anchor == Anchor::Murderer) {
    std::snprintf(out, static_cast<size_t>(cap), "the killer");
    return;
  }
  const int cat = clue.anchorCat;
  const int item = clue.anchorItem;
  switch (static_cast<Cat>(cat)) {
    case Cat::Suspect:
      std::snprintf(out, static_cast<size_t>(cap), "%s", suspectOf(p, item).name);
      return;
    case Cat::Weapon:
      switch (clue.voice % 3) {
        case 0:
          append(out, cap, "whoever carried %s", weaponOf(p, item).phrase);
          return;
        case 1:
          append(out, cap, "the one with %s", weaponOf(p, item).phrase);
          return;
        default:
          // Naming the weapon by its trait instead of its name. Same clue, one
          // more step of looking something up, which is the whole difference
          // between an easy tier and a hard one.
          append(out, cap, "whoever carried something with %s", weaponOf(p, item).trait);
          return;
      }
    case Cat::Location:
      if (clue.voice % 2 == 0) {
        append(out, cap, "whoever was %s", placeOf(p, item).phrase);
      } else {
        append(out, cap, "the one %s", placeOf(p, item).phrase);
      }
      return;
    case Cat::Motive:
      // Both variants have to work for every motive in the table. "Whoever
      // wanted an inheritance" reads well and "whoever wanted jealousy" does
      // not, so that variant is gone: a wording that is right for two thirds of
      // the table is a wording that ships broken sentences a third of the time.
      if (clue.voice % 2 == 0) {
        append(out, cap, "whoever was driven by %s", motiveOf(p, item).phrase);
      } else {
        append(out, cap, "the one driven by %s", motiveOf(p, item).phrase);
      }
      return;
  }
}

// ---------------------------------------------------------------------------
// What is said about them

const char* handedWord(const uint8_t v) { return v == 0 ? "left-handed" : "right-handed"; }

const char* eyeWord(const uint8_t v) {
  static const char* const words[4] = {"brown", "blue", "green", "hazel"};
  return words[v & 3];
}

const char* hairWord(const uint8_t v) {
  static const char* const words[5] = {"black", "brown", "blond", "red", "grey"};
  return words[v < 5 ? v : 0];
}

bool attributePhrase(const uint8_t attr, char* out, const int cap) {
  if (attr == kNoAttr) return false;
  if (attr == murdle::kAttrTallest) {
    std::snprintf(out, static_cast<size_t>(cap), "was the tallest of them");
    return true;
  }
  if (attr == murdle::kAttrShortest) {
    std::snprintf(out, static_cast<size_t>(cap), "was the shortest of them");
    return true;
  }
  const uint8_t kind = static_cast<uint8_t>(attr / 16u);
  const uint8_t value = static_cast<uint8_t>(attr % 16u);
  switch (static_cast<murdle::AttrKind>(kind)) {
    case murdle::AttrKind::Handed:
      std::snprintf(out, static_cast<size_t>(cap), "was %s", handedWord(value));
      return true;
    case murdle::AttrKind::Eyes:
      std::snprintf(out, static_cast<size_t>(cap), "had %s eyes", eyeWord(value));
      return true;
    case murdle::AttrKind::Hair:
      std::snprintf(out, static_cast<size_t>(cap), "had %s hair", hairWord(value));
      return true;
  }
  return false;
}

// Templates per category, in the three shapes a mask can take. Written out
// rather than assembled from parts, because "was not driven by greed" and "did
// not carry the crowbar" do not share a shape and pretending they do is how a
// generated sentence starts reading like a form letter.
struct Forms {
  const char* positive;
  const char* negative;
  const char* either;
};

Forms formsFor(const int cat) {
  switch (static_cast<Cat>(cat)) {
    case Cat::Suspect:
      return Forms{"was %s", "was not %s", "was either %s or %s"};
    case Cat::Weapon:
      return Forms{"carried %s", "did not carry %s", "carried either %s or %s"};
    case Cat::Location:
      return Forms{"was %s", "was not %s", "was either %s or %s"};
    case Cat::Motive:
      return Forms{"was driven by %s", "was not driven by %s", "was driven by %s or by %s"};
  }
  return Forms{"was %s", "was not %s", "was either %s or %s"};
}

void predicatePhrase(const Puzzle& p, const Clue& clue, char* out, const int cap) {
  out[0] = '\0';
  if (attributePhrase(clue.attr, out, cap)) return;

  const int items = p.shape.items;
  const uint8_t mask = clue.targetMask;
  const int set = popcount(mask);
  const Forms forms = formsFor(clue.targetCat);

  if (set == 1) {
    append(out, cap, forms.positive, phraseOf(p, clue.targetCat, lowestBit(mask)));
    return;
  }
  // A mask missing exactly one item reads as a denial, and it reads better that
  // way than as an either/or over the rest -- which at three items is the same
  // set of words and twice the length.
  if (set == items - 1) {
    append(out, cap, forms.negative, phraseOf(p, clue.targetCat, missingBit(mask, items)));
    return;
  }
  append(out, cap, forms.either, phraseOf(p, clue.targetCat, lowestBit(mask)),
         phraseOf(p, clue.targetCat, highestBit(mask)));
}

}  // namespace

// ---------------------------------------------------------------------------

const char* label(const Puzzle& p, const int cat, const int item) {
  switch (static_cast<Cat>(cat)) {
    case Cat::Suspect:
      return suspectOf(p, item).name;
    case Cat::Weapon:
      return weaponOf(p, item).name;
    case Cat::Location:
      return placeOf(p, item).name;
    case Cat::Motive:
      return motiveOf(p, item).name;
  }
  return "";
}

const char* trait(const Puzzle& p, const int cat, const int item) {
  switch (static_cast<Cat>(cat)) {
    case Cat::Weapon:
      return weaponOf(p, item).trait;
    case Cat::Location:
      return placeOf(p, item).trait;
    case Cat::Suspect:
    case Cat::Motive:
      return "";
  }
  return "";
}

const char* categoryName(const int cat) {
  switch (static_cast<Cat>(cat)) {
    case Cat::Suspect:
      return "SUSPECTS";
    case Cat::Weapon:
      return "WEAPONS";
    case Cat::Location:
      return "PLACES";
    case Cat::Motive:
      return "MOTIVES";
  }
  return "";
}

void clueLine(const Puzzle& p, const int clueIndex, char* out, const int cap) {
  out[0] = '\0';
  if (clueIndex < 0 || clueIndex >= p.clueCount || cap < 8) return;
  const Clue& clue = p.clues[clueIndex];

  // The murder clue names the crime scene by something in it rather than by its
  // name, which is what keeps the last step of a case a deduction instead of an
  // announcement.
  if (clue.anchor == Anchor::Murderer) {
    const int place = lowestBit(clue.targetMask);
    std::snprintf(out, static_cast<size_t>(cap), "The body was found next to %s.", placeOf(p, place).trait);
    return;
  }

  // A statement is the suspect's own voice, so it is first person and it is in
  // quotes. Every statement this generator makes is a plain positive claim,
  // true from everyone but the murderer.
  if (clue.speaker != kNobodySpeaks) {
    const int item = lowestBit(clue.targetMask);
    const char* claim = static_cast<Cat>(clue.targetCat) == Cat::Weapon ? "I carried %s" : "I was %s";
    char body[kLineMax];
    body[0] = '\0';
    append(body, kLineMax, claim, phraseOf(p, clue.targetCat, item));
    std::snprintf(out, static_cast<size_t>(cap), "%s says: \"%s.\"", suspectOf(p, clue.speaker).name, body);
    return;
  }

  char who[kLineMax];
  char what[kLineMax];
  anchorPhrase(p, clue, who, kLineMax);
  predicatePhrase(p, clue, what, kLineMax);

  out[0] = '\0';
  appendCapitalised(out, cap, who);
  const int len = static_cast<int>(std::strlen(out));
  std::snprintf(out + len, static_cast<size_t>(cap - len), " %s.", what);
}

void suspectAttributes(const Puzzle& p, const int item, char* out, const int cap) {
  const murdle::SuspectEntry& s = suspectOf(p, item);
  std::snprintf(out, static_cast<size_t>(cap), "%s, %s eyes, %s hair, %d'%d\"",
                s.handed == murdle::Handed::Left ? "left-handed" : "right-handed",
                eyeWord(static_cast<uint8_t>(s.eyes)), hairWord(static_cast<uint8_t>(s.hair)), s.inches / 12,
                s.inches % 12);
}

void accusationLine(const Puzzle& p, const uint8_t picks[kMaxCats], char* out, const int cap) {
  const int cats = p.shape.cats;
  char tail[kLineMax];
  tail[0] = '\0';
  if (cats > static_cast<int>(Cat::Motive)) {
    append(tail, kLineMax, ", driven by %s", motiveOf(p, picks[static_cast<int>(Cat::Motive)]).phrase);
  }
  std::snprintf(out, static_cast<size_t>(cap), "It was %s with %s %s%s.",
                suspectOf(p, picks[static_cast<int>(Cat::Suspect)]).name,
                weaponOf(p, picks[static_cast<int>(Cat::Weapon)]).phrase,
                placeOf(p, picks[static_cast<int>(Cat::Location)]).phrase, tail);
}

}  // namespace murdletext
