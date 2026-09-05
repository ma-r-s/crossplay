#pragma once
#include <string>

// The one cached OPDS cover, and the one list of extensions it can be under.
//
// The detail screen saves a book's cover to a fixed path whose EXTENSION comes
// from the catalog's URL, because the decoder is chosen by extension. The
// download screen then finds that file again by probing. Those are two lists
// that must agree, and when they did not the reader got the PREVIOUS book's
// art: a leftover opds-cover.bmp outranks the .jpg this book would have used.
// So there is one list, and both halves are derived from it rather than from a
// literal repeated at each site.
namespace opdscover {

// Probe order for findCached(), and the full set clearAll() removes. A new
// format added here reaches both.
inline constexpr const char* kExtensions[] = {".bmp", ".jpg", ".jpeg", ".png"};

inline std::string pathFor(const char* extension) { return std::string("/.crosspoint/opds-cover") + extension; }

// Templated on the card so the host can drive both halves against a fake.
// Called on every entry to a book's detail screen, NOT from the fetch: a book
// with no cover link never fetches, and that is exactly the case where a
// leftover from the previous book is still on the card and still findable.
template <typename Card>
void clearAll(Card& card) {
  for (const char* extension : kExtensions) {
    const std::string path = pathFor(extension);
    if (card.exists(path.c_str())) card.remove(path.c_str());
  }
}

// The cached cover, or an empty string. Costs no request: the file is whatever
// the detail screen just fetched.
template <typename Card>
std::string findCached(Card& card) {
  for (const char* extension : kExtensions) {
    const std::string path = pathFor(extension);
    if (card.exists(path.c_str())) return path;
  }
  return std::string();
}

}  // namespace opdscover
