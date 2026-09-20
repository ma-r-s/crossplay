// The vendored decNumber sources, compiled once with this fork's configuration.
//
// Same shape as lib/miniz: third_party/ holds upstream byte for byte so a
// version bump is a copy, and the configuration lives here rather than in
// platformio.ini, where a build flag would apply to every translation unit in
// the firmware and could not be read next to the reason for it.
//
// There are no warning suppressions here, deliberately. decNumber 3.68 compiles
// clean under -Wall -Wextra -Werror at -Os and -O2 with both the xtensa gcc this
// firmware uses and the host clang the suites use, and a -Wno- that suppresses
// nothing is worse than none: it reports clean forever, including for whatever
// gets added next to it.

#include "DecNumberConfig.h"

// ONE upstream source per translation unit. decNumberLocal.h carries a
// once-only guard that is an #error rather than an include guard -- both
// decContext.c and decNumber.c include it, so compiling them together does not
// merely warn, it fails to build. See deccontext_impl.c for the other half.
#include "../third_party/decNumber.c"
