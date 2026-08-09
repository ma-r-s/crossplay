#pragma once

// Where a game gets its randomness, and the one place it can be pinned.
//
// Every game that deals, shuffles or rolls used to write
// `millis() * 2654435761u + 1u` for itself. That is fine on the device and
// fatal for the site: every capture recipe in `site/recipes/` replays a game by
// repeating the same taps, and a game that deals differently on every run
// cannot be photographed twice. A shot taken today and a shot taken tomorrow
// disagree, and there is no way to tell a layout regression from a different
// deal.
//
// In the simulator, `CROSSPLAY_SEED` pins it. On hardware the environment does
// not exist and this is exactly what each game was already doing.
//
// It is a function rather than a constant because the value has to differ
// between two games started in the same session -- pinning it to a literal
// would make every PLAY AGAIN deal the same hand.

#include <cstdint>

namespace toybox {

// A seed for a game starting now. Mixed, so two games started milliseconds
// apart do not begin with adjacent states.
uint32_t seed();

}  // namespace toybox
