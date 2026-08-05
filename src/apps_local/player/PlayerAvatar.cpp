#include "PlayerAvatar.h"

#include "PlayerAvatarArt.h"

namespace player {
namespace {

namespace fui = freeink::ui;

// One row per word, in the same order as the word lists in PlayerName.cpp.
//
// PART takes ONE token and spells both halves from it: the word, and the two
// bitmaps that draw it. That is deliberate and it was earned. Written as two
// hand-kept columns -- {"SPIKY", &icon_hair_spiky_48, ...} -- a row can name one
// word and point at another's drawing, and there is no build error, no visible
// defect, and no test that can see it: a deliberate swap of two hair pointers
// survived a check written specifically to catch it, because the check compared
// the word column against the vocabulary and the word column was still right.
//
// With one token there is no second column to be wrong. Reordering rows still
// breaks the pairing against PlayerName.cpp's list, and the test below catches
// that, because the word moves with its artwork. The only remaining way to
// mismatch is to lie in the manifest (`hair_SPIKY = hair_curly`), which is a
// data error in the asset pipeline rather than something C++ can prevent.
//
// This is why tools_local/avatar.txt shouts its aliases.
#define PART(GROUP, WORD) \
  { #WORD, &icon_##GROUP##_##WORD##_48, &icon_##GROUP##_##WORD##_120 }

struct Part {
  const char* word;
  const freeink::Icon* row;
  const freeink::Icon* portrait;
};

constexpr Part kHair[] = {
    PART(hair, BALD), PART(hair, TUFTY),  PART(hair, SLICK), PART(hair, WAVY), PART(hair, CURLY),
    PART(hair, SPIKY), PART(hair, PUNK),  PART(hair, MOP),   PART(hair, BUN),  PART(hair, BRAIDS),
    PART(hair, PONY), PART(hair, BOB),    PART(hair, LONG),  PART(hair, AFRO),
};

constexpr Part kEyes[] = {
    PART(eyes, GRIM),  PART(eyes, SAD),   PART(eyes, SLY),   PART(eyes, SQUINT), PART(eyes, SLEEPY),
    PART(eyes, BLINK), PART(eyes, GLAD),  PART(eyes, WINK),  PART(eyes, BEADY),  PART(eyes, WIDE),
    PART(eyes, CROSS), PART(eyes, BUSHY), PART(eyes, SPECS), PART(eyes, SHADES),
};

constexpr Part kMouth[] = {
    PART(mouth, GRIN),  PART(mouth, TEETH),  PART(mouth, SMIRK),  PART(mouth, GLUM),  PART(mouth, POUT),
    PART(mouth, FROWN), PART(mouth, GASP),   PART(mouth, TONGUE), PART(mouth, FANGS), PART(mouth, SCRUFF),
    PART(mouth, TASH),  PART(mouth, GOATEE), PART(mouth, MUTTON), PART(mouth, BEARD),
};

#undef PART

constexpr const Part* kParts[kSlotCount] = {kHair, kEyes, kMouth};

static_assert(sizeof(kHair) / sizeof(kHair[0]) == kWordCount[SlotHair], "hair art and hair words disagree");
static_assert(sizeof(kEyes) / sizeof(kEyes[0]) == kWordCount[SlotEyes], "eyes art and eyes words disagree");
static_assert(sizeof(kMouth) / sizeof(kMouth[0]) == kWordCount[SlotMouth], "mouth art and mouth words disagree");

}  // namespace

int16_t avatarPixels(const AvatarSize size) { return size == AvatarSize::Portrait ? 120 : 48; }

const char* artWord(const int slot, const uint8_t index) {
  if (slot < 0 || slot >= kSlotCount || index >= kWordCount[slot]) return nullptr;
  return kParts[slot][index].word;
}

Avatar avatarFor(const Name& name, const AvatarSize size) {
  const bool portrait = size == AvatarSize::Portrait;
  Avatar avatar;
  avatar.layer[0] = portrait ? &icon_base_120 : &icon_base_48;
  for (int slot = 0; slot < kSlotCount; ++slot) {
    const uint8_t index = name.word[slot];
    // Out of range covers kUnknownWord and anything from a build with longer
    // lists. Both draw nothing, which is the plain head -- the same thing BALD
    // draws, and the right answer for "I cannot read this name".
    if (index >= kWordCount[slot]) continue;
    const Part& part = kParts[slot][index];
    avatar.layer[slot + 1] = portrait ? part.portrait : part.row;
  }
  return avatar;
}

Avatar avatarFor(const char* name, const AvatarSize size) { return avatarFor(parse(name), size); }

void drawAvatar(fui::DrawTarget& target, const fui::Rect& where, const char* name, const AvatarSize size,
                const fui::Color ink) {
  const Avatar avatar = avatarFor(name, size);
  for (int i = 0; i < Avatar::kLayerCount; ++i) {
    if (avatar.layer[i] == nullptr) continue;
    // Contain, and every layer is the same square, so a rect that is not square
    // letterboxes the whole face identically instead of sliding the mouth off
    // the chin. That is the property that lets callers pass any rect.
    target.bitmap(where, fui::bitmapFromIcon(*avatar.layer[i]), fui::BitmapMode::Contain, fui::Paint::solid(ink));
  }
}

}  // namespace player
