#pragma once

// The face a name describes.
//
// This is the whole of the avatar system's public surface, and it is small on
// purpose: every caller passes the name string it already has and gets a
// drawing. There is no avatar to store, no avatar to send, and no avatar that
// can drift out of step with the name, because there is no avatar -- there is a
// name and a function.
//
// That is what makes it free over the link. `LinkModel::theirName` is a string
// that already crossed the radio; drawing their face is the same call as
// drawing yours. Nothing was added to LinkProtocol for this.
//
// Freestanding: SDK only, no renderer, no storage. host-tests/ui compiles it
// with nothing else on the include path, which is what lets the screens that
// draw avatars stay testable.

#include "../ui/ToyboxScreen.h"
#include "PlayerName.h"

namespace player {

// Two sizes, and there is no third. Portrait is the settings screen, where the
// face is the subject of the page. Row is everywhere the face sits beside a
// name in a list -- the shelf footer, both link seats. A new place picks
// whichever of these it is closer to rather than generating another size:
// each one costs 25 bitmaps of flash.
enum class AvatarSize : uint8_t { Row, Portrait };

int16_t avatarPixels(AvatarSize size);

// The layers of one face, back to front, any of which may be null. Exposed
// mostly so a test can assert which parts a name resolves to without a target.
struct Avatar {
  static constexpr int kLayerCount = 4;
  const freeink::Icon* layer[kLayerCount] = {};
};

// A name to a face. Unknown words draw nothing, so a name from a build with
// different word lists comes out as the plain head everyone starts from,
// rather than as the wrong person or as a blank.
Avatar avatarFor(const Name& name, AvatarSize size);
Avatar avatarFor(const char* name, AvatarSize size);

// The word this file believes slot `slot`, index `index` is. Exists for the
// test that asserts it equals player::word() for every slot and index: the
// artwork lives in one table and the vocabulary in another, and nothing but
// that test can catch the two drifting out of order.
const char* artWord(int slot, uint8_t index);

// Stacks the layers into `where`. Every layer is the same square, so they stay
// in register whatever `where` is; a non-square rect letterboxes the whole face
// rather than sliding the mouth off the chin.
//
// `ink` because the face has to be knocked out white on the shelf's black
// footer bar and drawn black everywhere else. A 1-bpp mask painted in the
// colour behind it is invisible and nothing warns you -- the multiplayer mark
// went black-on-black and then white-on-white before anyone looked.
void drawAvatar(freeink::ui::DrawTarget& target, const freeink::ui::Rect& where, const char* name, AvatarSize size,
                freeink::ui::Color ink = freeink::ui::Color::Black);

}  // namespace player
