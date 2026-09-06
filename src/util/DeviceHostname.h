#pragma once

// The device's own mDNS name, unique per unit.
//
// It was the fixed literal "crossplay" in two places
// (CrossPointWebServerActivity and CalibreConnectActivity), so two X4 Pros on
// one network both answered for crossplay.local. That is not a hypothetical
// about some future user with two readers: there are two on this desk, and
// they are told apart by MAC precisely because nothing else distinguished
// them.
//
// It mattered little while the name was only typed. It matters once a QR code
// carries it, because the two failures are not the same size. A stale IP fails
// LOUDLY -- the browser cannot connect and you know at once. A hostname
// collision SUCCEEDS against the wrong device: the page loads, the upload
// lands, the wallpaper appears on the other reader, and nothing on either
// screen says a word.
//
// WHERE IT COMES FROM, and the two places it deliberately does not:
//   * The factory eFuse MAC. Immutable, needs no radio, survives an NVS wipe,
//     and is already visible to anyone on the LAN at layer 2 -- so putting 24
//     bits of it in a broadcast name leaks nothing that was not already there.
//   * NOT DeviceReport's device id. That one is hashed with a per-device NVS
//     secret specifically so it cannot be linked back to a unit; broadcasting
//     it on the LAN would undo exactly what that hashing is for.
//   * NOT PlayerName. The player can reroll it, and an mDNS name has to be
//     stable or a bookmark rots.
//
// Hex rather than words: a wordlist small enough to justify its flash collides
// probabilistically (PlayerName's 2744 names collide about one pair in 2744),
// and "must not collide" is the entire reason this function exists. 24 bits of
// hashed MAC is one name per unit, deterministically.

#include <string>

namespace devicehost {

// "crossplay-a1b2c3". Stable for the life of the unit, computed once.
// Callers append ".local" themselves; nothing here assumes a use.
const char* mdnsName();

}  // namespace devicehost
