#include "WikipediaLookup.h"

#include <utility>

#include "WikipediaActivity.h"

namespace wikipedia_lookup {

std::unique_ptr<Activity> open(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title) {
  return WikipediaActivity::createForTitle(renderer, mappedInput, std::move(title));
}

}  // namespace wikipedia_lookup
