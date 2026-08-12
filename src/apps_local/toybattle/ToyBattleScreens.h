#pragma once

// Toy Battle's screens. Free functions over plain models, drawing only through
// FreeInkUI, so host-tests/ui/ can build them against a fake target with no
// renderer and no device.
//
// The board is hand-drawn into the body rect. It has 17 slots and a rack of 8,
// which is 25 against a 24-slot interaction buffer, so nothing on it is
// registered as a control: both are hit-tested arithmetically from the same
// geometry that drew them.

#include "../ui/ToyboxScreen.h"
#include "ToyBattleCore.h"
#include "ToyBattleFlow.h"

namespace tbui {
namespace fui = freeink::ui;

enum : fui::ActionId {
  // A turn is "place a troop" or "draw two", and the second had no way in at
  // all until this existed. The foot of the board is now whichever of these
  // the position actually offers.
  ActionDraw = 2,
  ActionCancel = 3,
  ActionTake = 4,
  ActionSkip = 5,
  ActionBrief = 6,
  ActionBriefNext = 9,
  ActionBriefPrev = 10,
  ActionAgain = 7,
  ActionDone = 8,
};

struct BoardModel {
  toybattle::Game game{};
  toybattle::Draft draft{};
  uint8_t seat = 0;
  bool yourTurn = true;
  // The question being asked, on its own line under the rule.
  const char* prompt = "";
  // Drawing is only offered when it is legal: a full rack or an empty reserve
  // dims it rather than hiding it.
  bool canDraw = false;
};

// --- geometry, shared by the drawing and the hit test -----------------------

// Where slot `slot` sits, in device pixels. Pure on purpose: the drawing, the
// markers and the tap all call this, so a tap lands on the base the player is
// looking at by construction rather than by two sums happening to agree.
fui::Point slotCenter(const fui::DeviceContext& device, const toybattle::Terrain& board, int slot);
int16_t slotRadius();

// The slot under a tap, or -1. Radius-based, because the board is a graph
// rather than a grid and there is no cell to divide into.
int slotAt(const fui::DeviceContext& device, const toybattle::Terrain& board, int x, int y);

// The rack is one tile per TROOP, not one per kind: the limit is eight troops
// and there are eight slots, so a hand of two Skullys is two tiles. Drawing one
// tile per kind and counting duplicates in a corner pip meant drawing two
// troops could add one tile, which is exactly what it looked like -- a bug.
fui::Rect rackTile(const fui::DeviceContext& device, int position);
// The troop in rack position 0..7, or -1 if that slot is empty.
int handKindAt(const toybattle::Game& game, int seat, int position);
// The troop kind under a tap, or -1.
// Takes the draft because the row shows the DISCARD while the Cursed Cemetery
// question is open, and the hit test has to agree with what was drawn.
int rackAt(const fui::DeviceContext& device, const toybattle::Game& game, const toybattle::Draft& draft, int seat,
           int x, int y);

void buildBoard(toybox::Screen& screen, const BoardModel& model);

// What this terrain's special bases do. The only thing that changes between
// maps, so it is the only thing the briefing has to carry.
struct BriefModel {
  const toybattle::Terrain* board = nullptr;
  bool specialBases = true;
  // Page 0 is this map's special bases, page 1 the troop reference. Two pages
  // because one could not hold both: the troop list under the specials got a
  // 30px box with the mark parked beside it rather than the card's own face,
  // and on the four-kind maps it reached within a row of the bottom edge.
  int page = 0;
};
int briefPages();
void buildBrief(toybox::Screen& screen, const BriefModel& model);

// Every card, its real face and what it does, drawn into the box it is given.
// The rules deck shows this same screen: two screens explaining the cards is
// two things to keep in agreement, and they would not stay in agreement.
void troopReference(toybox::Screen& screen, const fui::Rect& box);

// The card face itself -- the numeral over the mark, the rack's own geometry
// scaled. Every screen that draws a card calls this one.
void troopCardFace(toybox::Screen& screen, const fui::Rect& card, toybattle::Troop troop, fui::Paint ground,
                   uint8_t edge);

struct ResultModel {
  toybattle::Game game{};
  uint8_t seat = 0;
};
void buildResult(toybox::Screen& screen, const ResultModel& model);

// The largest cut a header title fits in, walking the cuts down rather than
// letting the component truncate. Map names are data and the deck's page titles
// are prose; both have already overrun the display cut once.
// `reserved` is whatever else shares the band -- a page counter, a medal
// tally. Measuring against the full width is how TWO WAYS TO WIN came out
// as TWO WAYS TO W beside its own 1/6.
fui::TextStyle fittedHeaderTitle(toybox::Screen& screen, const char* title, int16_t reserved = 0);

// The marks, for anything outside this file that has to draw them. The rules
// screens teach the same symbols the board uses, which is the whole point of
// having a design language rather than a set of pictures.
void drawTroopMark(toybox::Screen& screen, fui::Point at, int16_t size, toybattle::Troop kind);
void drawSpecialGlyph(toybox::Screen& screen, fui::Point at, int16_t size, toybattle::Special what, bool white);

// What a troop does, in a few words, and why a tap was refused. Both feed the
// one line under the title.
const char* troopBlurb(toybattle::Troop kind);
const char* refusalBlurb(toybattle::Refusal why);

}  // namespace tbui
