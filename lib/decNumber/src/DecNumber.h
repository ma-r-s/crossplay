#pragma once

// The vendored decNumber's public headers, with this fork's configuration
// applied first. Include THIS, never third_party/decNumber.h directly: the
// header's struct size depends on DECNUMDIGITS, so a translation unit that
// included it without the configuration would disagree with the library about
// how big a decNumber is.

#include "DecNumberConfig.h"

extern "C" {
#include "../third_party/decContext.h"
#include "../third_party/decNumber.h"
}
