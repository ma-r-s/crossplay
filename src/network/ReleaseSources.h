#pragma once

// Where the update check asks for the latest release, in the order it asks.
//
// The device used to ask GitHub's releases/latest directly, so the one
// request nearly every device makes was the one nobody could count: a device
// was seen on the board only when it used Get Books or a bridge. The site's
// /api/latest answers with GitHub's JSON verbatim, and because its host is
// ours the request carries the three device headers (DeviceReport.h) and the
// site posts the events the bridges post (docs/workflow/events.md). GitHub
// itself comes second: when the site does not answer, or answers nothing
// with a tag_name in it, the check goes on exactly as it did before, so no
// device loses OTA to the site, its certificate or its rate limit. Old
// firmware keeps asking GitHub until it updates once.
//
// A pure header, so host-tests/devreport can pin the two facts that matter:
// the first source is one the device reports to, the second is not.

namespace release_sources {

constexpr const char* const kUrls[] = {
    "https://crossplay.ma-r-s.com/api/latest",
    "https://api.github.com/repos/ma-r-s/crossplay/releases/latest",
};
constexpr int kCount = static_cast<int>(sizeof(kUrls) / sizeof(kUrls[0]));

}  // namespace release_sources
