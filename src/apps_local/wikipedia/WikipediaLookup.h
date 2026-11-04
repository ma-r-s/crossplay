#pragma once

#include <memory>
#include <string>

class Activity;
class GfxRenderer;
class MappedInputManager;

// The reader's way into Wikipedia: the selection bar's Wiki button.
namespace wikipedia_lookup {

// The app, opened on the article whose title matches `title` (prefix,
// redirect and case folded, as the app's own search), on top of the reader.
// Every way out returns to the book. With no pack it offers GET WIKIPEDIA,
// which restarts into the app rather than handing the card to USB under the
// open book.
std::unique_ptr<Activity> open(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title);

}  // namespace wikipedia_lookup
