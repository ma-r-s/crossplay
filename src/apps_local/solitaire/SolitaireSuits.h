#pragma once

// The suit artwork moved to the shared deck when Hearts became the second game
// to need it. This shim keeps solitaire/ compiling against its old include
// rather than touching every reference in a commit about a different game.
#include "../cards/CardSuits.h"
