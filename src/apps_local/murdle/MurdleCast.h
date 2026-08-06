#pragma once

// Who and what a case is made of.
//
// There is no lore here and there is not meant to be. Every field exists
// because a clue can reference it, and a field no clue can reference does not
// get written. A case is a random draw from these tables; nobody recurs, there
// is no detective, and no case knows about any other.
//
// Freestanding, so host-tests/murdle/ can assert that every sentence the text
// layer can build out of these tables is grammatical.

#include <cstdint>

#include "MurdleCore.h"

namespace murdle {

// ---------------------------------------------------------------------------
// Suspects

// Attributes exist so that a clue can point at somebody without naming them:
// "whoever was in the lighthouse was left-handed" is only a clue because of who
// happens to have been drawn. Height is carried as a number rather than a band
// because the only useful thing to say about it is comparative.
enum class Handed : uint8_t { Left, Right };
enum class Eyes : uint8_t { Brown, Blue, Green, Hazel };
enum class Hair : uint8_t { Black, Brown, Blond, Red, Grey };

struct SuspectEntry {
  const char* name;  // "DOCTOR ASHE", drawn on the card and used in sentences
  Handed handed;
  Eyes eyes;
  Hair hair;
  uint8_t inches;  // 60 to 78; only ever compared, never stated as a band
};

// ---------------------------------------------------------------------------
// Fixtures
//
// `name` is the short label the grid and the accusation sheet use. `phrase` is
// the form that goes inside a sentence, with its preposition and article
// already attached, because that is the only way to keep a template from having
// to know about English. `trait` is one short noun phrase, and it is what lets
// a clue be indirect without anybody writing prose: the murder clue names the
// crime scene by its trait, and a weapon can be referred to by its own.

struct WeaponEntry {
  const char* name;    // "BONE SAW"
  const char* phrase;  // "the bone saw"      -> "carried the bone saw"
  const char* trait;   // "a chipped blade"   -> "whoever carried the thing with a chipped blade"
  bool heavy;
};

struct PlaceEntry {
  const char* name;    // "LIGHTHOUSE"
  const char* phrase;  // "in the lighthouse" -> "was in the lighthouse"
  const char* trait;   // "peeling paint"     -> "the body was found next to peeling paint"
  bool indoors;
};

struct MotiveEntry {
  const char* name;    // "GREED"
  const char* phrase;  // "greed"             -> "was driven by greed"
};

// ---------------------------------------------------------------------------
// The tables

constexpr int kSuspectCount = 24;
constexpr int kWeaponCount = 16;
constexpr int kPlaceCount = 16;
constexpr int kMotiveCount = 12;

extern const SuspectEntry kSuspects[kSuspectCount];
extern const WeaponEntry kWeapons[kWeaponCount];
extern const PlaceEntry kPlaces[kPlaceCount];
extern const MotiveEntry kMotives[kMotiveCount];

int castSize(int cat);

// ---------------------------------------------------------------------------
// Drawing a cast

// Fills `cast` with `shape.items` distinct entries per live category.
// Deterministic in `seed`, and salted away from the seed generate() uses so
// that the draw and the solution are not correlated.
void drawCast(uint32_t seed, Shape shape, uint8_t cast[kMaxCats][kMaxItems]);

// Every way this particular draw of suspects can be described without naming
// anybody. One mask per attribute value that some but not all of them share,
// plus the tallest and the shortest when those are unambiguous.
//
// This is the seam described in MurdleCore.h: the logic never learns what an
// attribute is, it just gets masks.
AttrMasks attrMasksFor(const uint8_t cast[kMaxCats][kMaxItems], Shape shape);

// What produced a mask, packed into AttrMasks::tag. MurdleText reads it back to
// choose the wording; nothing else looks at it.
constexpr uint8_t kAttrNone = 0xFF;
constexpr uint8_t kAttrTallest = 0xF0;
constexpr uint8_t kAttrShortest = 0xF1;

// tag = kind * 16 + value, for the three attributes that have values.
enum class AttrKind : uint8_t { Handed = 0, Eyes = 1, Hair = 2 };

constexpr uint8_t attrTag(const AttrKind kind, const uint8_t value) {
  return static_cast<uint8_t>(static_cast<uint8_t>(kind) * 16u + value);
}

}  // namespace murdle
