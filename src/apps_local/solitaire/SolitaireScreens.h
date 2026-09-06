#pragma once

// Solitaire's screens, as freestanding builders. See ToyboxScreen.h for why
// screens are written this way: a free function over a plain model, drawing
// into a DrawTarget it was handed, so host-tests/ui/ can run them with a fake
// target and no device.
//
// This is the first landscape app in the fork. The renderer does the rotation
// (`setOrientation(LandscapeCounterClockwise)` swaps logical width and height)
// and `tapToLogical()` already maps touches through the same transform, so
// everything below is plain 800x480 arithmetic with no rotation in it.

#include "../ui/ToyboxScreen.h"
#include "SolitaireCore.h"

namespace solitaireui {

namespace fui = freeink::ui;

// The band height, which this app alone raises: 56 rather than the fork's 76.
// In portrait the header costs a tenth of the screen; in landscape the standard
// band would cost a sixth of the dimension the tableau needs.
//
// Named here because the Activity has to hand the same number to the theme
// token that the builders lay out against, and it used to do that by typing 56
// again in another file. Two literals that must agree and cannot be checked
// against each other is the shape half this app's chrome bugs came in.
inline constexpr int16_t kHeaderBand = 56;

enum : fui::ActionId {
  ActionPile = 1,    // value is a pile index
  ActionButton = 2,  // value is a Button below
};

enum Button : int {
  ButtonUndo = 0,
  ButtonNew = 1,
  ButtonFinish = 2,
};

// Where every card ended up. The builder fills this while drawing, and the
// activity reads it to turn a tap into a card index. That is the whole point:
// hit-testing shares the geometry that drew the pixels rather than recomputing
// it, which is the rule three separate bugs in this project came from breaking.
struct Layout {
  // Top-left of each pile.
  int16_t x[solitaire::kPileCount] = {};
  int16_t y[solitaire::kPileCount] = {};
  // Vertical offset of card `i` within its tableau pile. Non-tableau piles
  // stack in place, so only the top card is reachable and these stay zero.
  int16_t cardOffset[solitaire::kTableauPiles][solitaire::kPileCapacity] = {};
  int16_t cardWidth = 0;
  int16_t cardHeight = 0;
  // Where the selection outline goes, worked out by the builder while it draws.
  // The first version derived it from the pile's origin, which is wrong the
  // moment a pile fans sideways: selecting a card in the waste drew the outline
  // around the leftmost card of the fan while the card you had actually picked
  // up sat outside it. Same class of bug as hit-testing computed twice.
  fui::Rect selection = {};
  bool hasSelection = false;

  // Which card of `pile` a tap at logical `ty` lands on, or -1. Tapping the
  // fanned part of a column selects the card whose visible strip you touched;
  // tapping below the last card still means the last card, because the bottom
  // card is drawn at full height and is the largest target on the screen.
  int cardAt(const solitaire::Game& game, int pile, int ty) const;
};

struct BoardModel {
  const solitaire::Game* game = nullptr;
  // The run currently picked up, or pile -1 for nothing selected.
  int selectedPile = -1;
  int selectedCard = 0;
  // Set for the frame that lands on a win, so the board can say so.
  bool won = false;
};

struct MenuModel {
  bool hasSave = false;
  int savedMoves = 0;
  bool savedDrawThree = false;
  bool drawThree = false;
  int played = 0;
  int wins = 0;
  int streak = 0;
  // The last sixteen games, oldest first: 0 unplayed, 1 lost/abandoned, 2 won.
  uint8_t recent[16] = {};
};

enum : int { MenuResume = 0, MenuNew = 1, MenuDrawMode = 2, WinAgain = 3, WinMenu = 4 };

struct WinModel {
  int moves = 0;
  int wins = 0;
  int streak = 0;
  bool drawThree = false;
};

// The board. Fills `layout` as it draws.
void buildBoard(toybox::Screen& screen, const BoardModel& model, Layout& layout);

// The front door, following the pattern in docs/design-language.md.
void buildMenu(toybox::Screen& screen, const MenuModel& model);

// The payoff. Drawn on its own screen rather than over the board, because the
// board at the moment of winning is four foundations and five empty columns:
// the least interesting it has looked all game.
void buildWin(toybox::Screen& screen, const WinModel& model);

}  // namespace solitaireui
