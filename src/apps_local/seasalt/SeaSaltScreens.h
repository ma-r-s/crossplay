#pragma once

// The Sea Salt & Paper screens, as freestanding builders over models.
//
// No GfxRenderer, no Activity, no storage: a model in, a drawn frame out,
// which is what lets host-tests/ui/ assert what was drawn and what was made
// tappable. The card grid is drawn here but hit-tested arithmetically in the
// activity, through the same geometry functions this header exports -- a
// sixteen-card hand plus the chrome would blow past the interaction buffer,
// and a regular grid is exactly what the buffer is not for.
//
// Display names are not the rulebook's everywhere the rulebook's icon does not
// exist: Lucide has no octopus and no penguin, so those cards are TURTLE and
// GULL on this device -- card face, hint, how-to, round report, everywhere.
// See tools_local/seasalt_cards.txt.

#include "../ui/ToyboxScreen.h"

namespace seasaltui {

namespace fui = freeink::ui;

// Semantic actions. Every input path resolves to one of these; the 200s belong
// to the shared link screens and these stay below them.
enum : fui::ActionId {
  ActionStartRow = 1,
  ActionTabHand = 2,
  ActionTabYours = 3,
  ActionTabTheirs = 4,
  ActionDeck = 5,
  ActionPileA = 6,
  ActionPileB = 7,
  ActionPrimary = 8,  // the wide bottom pill: whatever it currently says
  ActionCall = 9,     // "10 - CALL IT"
  ActionStop = 12,
  ActionLastChance = 13,
  ActionContinue = 14,  // round over -> next round
  ActionPlayAgain = 15,
  ActionAdvance = 16,  // the next page of the tutorial
  ActionBack = 17,     // leave a modal choice without choosing
};

// The front door, Jaipur's shape.
enum class StartRow : uint8_t { Continue, NewGame, PlayNearby, HowToPlay, Count };

struct StartModel {
  bool hasSavedGame = false;
  const char* continueDetail = "";
  int played = 0;
  int won = 0;
  int selected = 0;
};

int startRows(const StartModel& model);
StartRow startRowAt(const StartModel& model, int visibleIndex);
const char* startRowLabel(StartRow row);
fui::Rect buildStartMenu(toybox::Screen& screen, const StartModel& model);

// One card face, everything the tile needs and nothing the seat is not
// entitled to know. `kind` and `colour` are the core's enum values as plain
// ints, so this header does not depend on the core.
struct CardTile {
  uint8_t kind = 0;
  uint8_t colour = 0;
  // What this kind-group currently scores for its holder: both boats show the
  // pair's 1, two turtles show their 3. The census already carries the count,
  // so the corner number answers "what is this worth", not "how many".
  int8_t groupPoints = 0;
  // How many of this kind exist in the deck, printed as X9: the held count
  // is the tiles themselves -- you can see how many crabs you hold.
  uint8_t supply = 0;
  bool selected = false;
};

// What a discard pile shows: its top card and how deep it is. `size == 0`
// draws the empty frame.
struct PileTile {
  uint8_t kind = 0;
  uint8_t colour = 0;
  uint8_t size = 0;
};

struct BoardModel {
  int yourTotal = 0;  // banked, for the header: "12 - 7"
  int theirTotal = 0;
  // The facts strip. `bestColour` is the colour of your largest group, drawn
  // as its mark beside the count.
  int theyHold = 0;
  int theirTableCount = 0;
  int yourTableCount = 0;
  int bestColourCount = 0;
  uint8_t bestColour = 0;
  int deckCount = 0;
  PileTile piles[2];
  // Which of the three views is up, and the cards it shows. One page's worth:
  // the activity pages past kMaxBoardTiles on the side keys.
  int tab = 0;  // 0 hand, 1 yours, 2 theirs
  // When a decision is pending, the first segment says what the grid shows
  // (KEEP / PLACE / DIG) instead of claiming to be the hand. Null = "HAND n".
  const char* handTabLabel = nullptr;
  int handCount = 0;
  int yoursCount = 0;
  int theirsCount = 0;
  static constexpr int kMaxBoardTiles = 16;
  CardTile tiles[kMaxBoardTiles];
  int tileCount = 0;
  int page = 0;
  int pages = 1;
  // The hint box: what the current selection means, or what to do next. The
  // player should never have to remember a card's rule; this line is where the
  // rule appears at the moment it matters.
  const char* hint = "";
  // The bottom pills. Primary is inert when the label is a report rather than
  // an instruction.
  const char* primaryLabel = "";
  bool primaryEnabled = false;
  int callPoints = 0;  // current card points, shown on the call pill
  bool canCall = false;
};

// Draws everything and returns the rect the card grid occupies, which is also
// the rect the exported geometry functions describe.
fui::Rect buildBoard(toybox::Screen& screen, const BoardModel& model);

// The card grid's geometry, shared between drawing and hit-testing so the two
// cannot drift. `count` is the page's tile count.
//
// There are no chooser screens: keeping one of two drawn cards, placing the
// rejected one on a pile and digging with the crabs are all BOARD states.
// The grid shows the cards the current decision is about, at grid size, where
// cards always live -- a card never changes shape or position by being asked
// about. Mario caught the modal versions morphing three times before this
// became the rule.
fui::Rect cardCellRect(const fui::Rect& grid, int index, int count);
int cardIndexAt(const fui::Rect& grid, int count, int16_t x, int16_t y);

// STOP or LAST CHANCE, with what each means, because this is the one decision
// in the game whose consequences are not on the board.
struct CallModel {
  int yourPoints = 0;
};
fui::Rect buildCallChoice(toybox::Screen& screen, const CallModel& model);

// The end of a round: how it ended, what the bet did, both sides' arithmetic,
// and the running totals. Doubles as the end of the match.
struct RoundModel {
  int round = 1;
  // How the round ended. Exactly one is true unless the deck ran out.
  bool youCalled = false;
  bool wasLastChance = false;
  bool betWon = false;   // meaningful only when wasLastChance
  bool deckOut = false;  // nobody scores
  bool mermaidWin = false;
  bool youWonMatch = false;
  int yourCards = 0;
  int yourBonus = 0;
  int yourBanked = 0;  // what this round actually added
  int theirCards = 0;
  int theirBonus = 0;
  int theirBanked = 0;
  int yourTotal = 0;
  int theirTotal = 0;
  bool matchOver = false;
  const char* theirName = nullptr;
  // Over a link, exactly one device deals the next round. The other is told
  // so rather than given a button that would be refused.
  bool waitingOnThem = false;
};
fui::Rect buildRoundOver(toybox::Screen& screen, const RoundModel& model);

// HOW TO PLAY: one beat a page, drawn from the game's own cards.
struct TutorialModel {
  int page = 0;
};
int tutorialPages();
void buildTutorial(toybox::Screen& screen, const TutorialModel& model);

// The display vocabulary, exported because the activity writes hints and
// reports in it. `kind` is the core's Kind as an int.
const char* kindName(int kind);
// The rule spoken when one card of `kind` is selected, and what a selected
// pair means. Every line of these, after the hint box's sentence split, must
// fit the box -- host-tests/ui measures them, because "PLAYING THEM BUYS
// ANOTHER TURN" once ran straight off the panel.
const char* kindHint(int kind);
const char* pairHint(int duoKind);
// The mark for a colour, at the one generated size (24px).
const freeink::Icon& colourMark(int colour);
// The face for a kind, at the two card cuts.
const freeink::Icon& kindIcon48(int kind);
const freeink::Icon& kindIcon40(int kind);
const freeink::Icon& kindIcon24(int kind);

}  // namespace seasaltui
