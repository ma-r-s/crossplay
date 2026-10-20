#pragma once

namespace study {

// Removes the glyph lists older converters wrote as glyphs-*.txt into a deck
// folder. The Library lists every .txt on the card, so each deck showed four of
// them as books; the converter now writes .lst, and the device never read
// either. Touches nothing else in the folder. Returns how many it removed.
int removeLegacyGlyphLists(const char* deckDir);

}  // namespace study
