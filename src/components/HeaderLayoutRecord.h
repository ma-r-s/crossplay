#pragma once

// fork-local: where BaseTheme::drawHeader last laid out its band, as the
// FreeInkUI header reported it (freeink::ui::HeaderLayout). An app that draws
// its own words inside the theme's band -- Study's card count and its PHOTO
// pill -- places them on this row rather than finding its own middle of the
// band. It found one before: centred on the whole band, battery strip
// included, while the theme centres its back arrow on the row below the
// strip, so "3 LEFT" sat 12px above its own arrow. A left-aligned word starts
// at `textLeft`, not `title.x`, which is under the arrow in a theme that
// centres its title.
//
// Written and read on the render task, in the same paint: drawHeader, then
// the app's own drawing.
#include <FreeInkUI.h>

namespace HeaderLayoutRecord {
inline freeink::ui::HeaderLayout last{};
}  // namespace HeaderLayoutRecord
