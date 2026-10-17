#pragma once

// The simulator ignores lib/hal (platformio.sim.ini: lib_ignore = hal) and its
// own package has no haptics header yet. Upstream's is already inert under
// CROSSPOINT_EMULATED, so use it as it is rather than keep a copy that drifts.
#include "../lib/hal/HalHaptics.h"
