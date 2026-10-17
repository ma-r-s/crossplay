#pragma once

// A note on the sleep screen (Settings > Sleep screen > Note).
//
// Three people asked for it by 2026-09-28: through the report box, on GitHub
// (#231), and in a whole pull request (#198) that built a second to-do app to
// get it. It is drawn LIVE when the device goes to sleep, from the file, in
// the Notes look, rather than rendered to a picture when chosen: a picture
// would be a stale copy the moment the note was ticked from a phone, and
// /sleep.bmp already belongs to the person's own image and to Live.
//
// The choice lives in /.crosspoint/notes-asleep.txt (notes::AsleepChoice):
// the note's name and the sleep mode it replaced, so "stop showing" puts back
// what was there. The Notes menu writes it and switches the setting in one tap.

#include <string>

#include "NotesCore.h"

class GfxRenderer;

namespace notes {

constexpr const char* kAsleepFile = "/.crosspoint/notes-asleep.txt";

bool readAsleep(AsleepChoice& out);
bool writeAsleep(const AsleepChoice& choice);
void clearAsleep();

// Draws the chosen note into the renderer's buffer and returns true; the
// caller puts it on the panel. False, having drawn nothing worth keeping,
// when no note is chosen or the note is gone, so the caller can fall back to
// the default sleep screen instead of showing an empty page.
bool drawAsleep(GfxRenderer& renderer);

}  // namespace notes
