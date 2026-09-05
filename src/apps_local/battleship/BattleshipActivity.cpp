#include "BattleshipActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxSeed.h"
#include "../ui/ToyboxTheme.h"

namespace {

// Your own fleet, drawn small under the water you are shooting at. Fixed rather
// than proportional: it is a readout, and the grid you tap gets everything left
// over.
constexpr int kFleetCell = 16;
// Marks inside a cell, as fractions that hold at any cell size.
constexpr int kMissRing = 3;

constexpr char kSavePath[] = "/.crosspoint/bship.sav";
constexpr char kStatsPath[] = "/.crosspoint/bship.cfg";
constexpr int kSaveVersion = 1;

char hexDigit(const int value) { return static_cast<char>(value < 10 ? '0' + value : 'A' + value - 10); }

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

}  // namespace

std::unique_ptr<Activity> BattleshipActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<BattleshipActivity>(renderer, mappedInput);
}

// --- lifecycle --------------------------------------------------------------

void BattleshipActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  // Mixed from the clock, so two games in a row are not the same game. The
  // fleet you are given is the first thing you see, and a deterministic one
  // would be noticed by the second session.
  seed = toybox::seed();
  loadStats();
  hasSavedGame = loadGame();
  goToMenu();
}

void BattleshipActivity::onExit() {
  // Runs on sleep and on an app switch as well as on the way out, which is why
  // this is the door that matters: you press nothing to fall asleep.
  saveGame();
  Activity::onExit();
}

// --- geometry ---------------------------------------------------------------

BattleshipActivity::GridGeometry BattleshipActivity::targetGrid() const {
  // The band your own fleet and the roster will take, reserved before the big
  // grid is sized: a layout that reflows once the game starts is worse than one
  // with a little air in it.
  const int fleetBand = kFleetCell * bship::kSize + toybox::kGutter * 2;
  const int room = bodySlot.height - fleetBand;

  GridGeometry grid;
  const int byWidth = (bodySlot.width - 2 * toybox::kBoardFrame) / bship::kSize;
  const int byHeight = (room - 2 * toybox::kBoardFrame) / bship::kSize;
  grid.cell = std::clamp(std::min(byWidth, byHeight), 8, 44);
  grid.originX = bodySlot.x + (bodySlot.width - grid.cell * bship::kSize) / 2;
  // Anchored under the chrome rather than centred in the slack: a block with
  // equal air above and below reads as unresolved.
  grid.originY = bodySlot.y + toybox::kBoardFrame;
  return grid;
}

BattleshipActivity::GridGeometry BattleshipActivity::fleetGrid() const {
  const GridGeometry target = targetGrid();
  GridGeometry grid;
  grid.cell = kFleetCell;
  grid.originX = bodySlot.x + toybox::kFrame;
  grid.originY = target.originY + target.cell * bship::kSize + toybox::kBoardFrame + toybox::kGutter;
  return grid;
}

BattleshipActivity::GridGeometry BattleshipActivity::placeGrid() const {
  GridGeometry grid;
  const int byWidth = (bodySlot.width - 2 * toybox::kBoardFrame) / bship::kSize;
  const int byHeight = (bodySlot.height - 2 * toybox::kBoardFrame) / bship::kSize;
  grid.cell = std::clamp(std::min(byWidth, byHeight), 8, 44);
  grid.originX = bodySlot.x + (bodySlot.width - grid.cell * bship::kSize) / 2;
  grid.originY = bodySlot.y + toybox::kBoardFrame;
  return grid;
}

int BattleshipActivity::cellAt(const GridGeometry& grid, const int x, const int y) {
  if (grid.cell <= 0) return -1;
  const int col = (x - grid.originX) / grid.cell;
  const int row = (y - grid.originY) / grid.cell;
  if (x < grid.originX || y < grid.originY) return -1;
  if (col < 0 || col >= bship::kSize || row < 0 || row >= bship::kSize) return -1;
  return bship::cellOf(row, col);
}

Rect BattleshipActivity::cellRect(const GridGeometry& grid, const int cell) {
  return Rect{grid.originX + bship::colOf(cell) * grid.cell, grid.originY + bship::rowOf(cell) * grid.cell, grid.cell,
              grid.cell};
}

int BattleshipActivity::statusPillY() const {
  return renderer.getScreenHeight() - toybox::kMargin - toybox::kPillHeight;
}

int BattleshipActivity::boardTop() const { return bodySlot.y; }

// --- drawing ----------------------------------------------------------------

void BattleshipActivity::drawGridLattice(const GridGeometry& grid, const bool heavyFrame) const {
  const int side = grid.cell * bship::kSize;
  // Hairlines, because the lattice is a surface the marks sit on rather than
  // something to be noticed. The frame around it is what carries the weight.
  for (int i = 1; i < bship::kSize; ++i) {
    renderer.fillRect(grid.originX + i * grid.cell, grid.originY, toybox::kHairline, side, true);
    renderer.fillRect(grid.originX, grid.originY + i * grid.cell, side, toybox::kHairline, true);
  }
  if (heavyFrame) {
    const int weight = toybox::kFrame;
    renderer.drawRect(grid.originX - weight, grid.originY - weight, side + 2 * weight, side + 2 * weight, weight, true);
    // The same brackets the chess board wears, so two games read as one device.
    toybox::cornerMarks(renderer,
                        Rect{grid.originX - toybox::kBoardFrame, grid.originY - toybox::kBoardFrame,
                             side + 2 * toybox::kBoardFrame, side + 2 * toybox::kBoardFrame},
                        toybox::kGutter * 2, toybox::kFrame);
    return;
  }
  renderer.drawRect(grid.originX - toybox::kHairline, grid.originY - toybox::kHairline, side + 2 * toybox::kHairline,
                    side + 2 * toybox::kHairline, toybox::kHairline, true);
}

void BattleshipActivity::drawTargetGrid(const bool reveal) const {
  const GridGeometry grid = targetGrid();
  drawGridLattice(grid, true);

  const bship::Side& theirs = game.side[opponentSide()];
  for (int cell = 0; cell < bship::kCells; ++cell) {
    const Rect box = cellRect(grid, cell);
    const bool shot = bship::shotAt(theirs, cell);
    // Their fleet is in memory the whole time. It is only ever consulted for a
    // cell that has already been fired at -- where a hit is public knowledge --
    // or once the game is over and there is nothing left to keep.
    if (!shot) {
      if (reveal && bship::shipAt(theirs.fleet, cell) >= 0) {
        renderer.fillRectDither(box.x + 2, box.y + 2, box.width - 4, box.height - 4, LightGray);
      }
      continue;
    }
    const int ship = bship::shipAt(theirs.fleet, cell);
    if (ship < 0) {
      // A miss is a ring: a dot alone would read as a small hit, and on 1 bit
      // there is no lighter ink to say "nothing here" with.
      const int outer = std::max(10, box.width / 3);
      const int inner = std::max(4, outer - 2 * kMissRing);
      renderer.fillRoundedRect(box.x + (box.width - outer) / 2, box.y + (box.height - outer) / 2, outer, outer,
                               outer / 2, Black);
      renderer.fillRoundedRect(box.x + (box.width - inner) / 2, box.y + (box.height - inner) / 2, inner, inner,
                               inner / 2, White);
      continue;
    }
    if (bship::sunk(theirs, ship)) {
      // A sunk ship goes solid, so its cells merge into one silhouette and you
      // can see the shape of what you killed. It happens five times a game and
      // never changes back, which is exactly when this device can afford black.
      renderer.fillRect(box.x, box.y, box.width, box.height, true);
      continue;
    }
    const int peg = std::max(12, box.width / 2);
    renderer.fillRoundedRect(box.x + (box.width - peg) / 2, box.y + (box.height - peg) / 2, peg, peg, 3, Black);
  }

  // The aim, marked in the corners rather than in the middle: the middle is
  // where the shot marks are, and a cursor that covers them would hide the one
  // thing you are looking at.
  if (aimCell >= 0 && !gameOver()) {
    const Rect box = cellRect(grid, aimCell);
    toybox::cornerMarks(renderer, box, std::max(10, grid.cell / 3), toybox::kFrame);
  }
}

void BattleshipActivity::drawFleetGrid() const {
  const GridGeometry grid = fleetGrid();
  const bship::Side& mine = game.side[mySide];
  for (int cell = 0; cell < bship::kCells; ++cell) {
    const Rect box = cellRect(grid, cell);
    const int ship = bship::shipAt(mine.fleet, cell);
    if (ship >= 0) renderer.fillRectDither(box.x, box.y, box.width, box.height, LightGray);
    if (!bship::shotAt(mine, cell)) continue;
    if (ship >= 0) {
      renderer.fillRect(box.x + 1, box.y + 1, box.width - 2, box.height - 2, true);
      continue;
    }
    const int dot = std::max(4, box.width / 4);
    renderer.fillRoundedRect(box.x + (box.width - dot) / 2, box.y + (box.height - dot) / 2, dot, dot, dot / 2, Black);
  }
  drawGridLattice(grid, false);
}

void BattleshipActivity::drawPlaceGrid() const {
  const GridGeometry grid = placeGrid();
  drawGridLattice(grid, true);

  for (int i = 0; i < bship::kShipCount; ++i) {
    const bship::Ship& ship = myFleet.ships[i];
    const int length = bship::kShipLength[i];
    const Rect bow = cellRect(grid, ship.bow);
    const Rect hull{bow.x, bow.y, ship.horizontal != 0 ? grid.cell * length : grid.cell,
                    ship.horizontal != 0 ? grid.cell : grid.cell * length};
    // One shape per ship rather than per cell: a five-cell run has to read as
    // one object you can pick up, which is what the whole screen is for.
    renderer.fillRectDither(hull.x + 2, hull.y + 2, hull.width - 4, hull.height - 4, LightGray);
    const bool chosen = i == selectedShip;
    // A held ship outlines at kFrame against a hairline for the rest. Equal
    // weights compete and neither wins, which is the rule the board border and
    // the selection frame already follow.
    renderer.drawRect(hull.x + 2, hull.y + 2, hull.width - 4, hull.height - 4,
                      chosen ? toybox::kFrame : toybox::kHairline, true);
    if (chosen) {
      toybox::cornerMarks(renderer, hull, std::max(10, grid.cell / 3), toybox::kFrame);
    }
  }
}

Rect BattleshipActivity::placeRosterRect() const {
  const GridGeometry grid = placeGrid();
  const int top = grid.originY + grid.cell * bship::kSize + toybox::kBoardFrame + toybox::kGutter;
  const int bottom = bodySlot.y + bodySlot.height;
  return Rect{bodySlot.x, top, bodySlot.width, bottom - top};
}

void BattleshipActivity::drawPlaceRoster() const {
  const Rect box = placeRosterRect();
  const int rowHeight = box.height / bship::kShipCount;
  if (rowHeight < 12) return;

  for (int i = 0; i < bship::kShipCount; ++i) {
    const int y = box.y + i * rowHeight;
    const bool chosen = i == selectedShip;
    // Dither and an outline, not an inverted bar. This row moves every time you
    // tap a ship, and the one rule the whole design follows is that the black
    // you can afford is inversely proportional to how often it changes. The
    // sunk-ship bars in the other roster invert because they land five times a
    // game and never change back.
    if (chosen) {
      renderer.fillRectDither(box.x, y, box.width, rowHeight - 2, LightGray);
      renderer.drawRect(box.x, y, box.width, rowHeight - 2, toybox::kRule, true);
    }
    toybox::drawCapsCentered(renderer, toybox::kTileFontId, box.x + toybox::kGutter / 2, y, rowHeight - 2,
                             bship::shipName(i), true);

    // The hull, at the length it really is. A row that only named the ship
    // would make you count squares on the grid to know which one you were
    // holding.
    const int pip = std::min(14, rowHeight - 8);
    const int gap = 3;
    const int length = bship::kShipLength[i];
    int x = box.x + box.width - (pip + gap) * length;
    for (int j = 0; j < length; ++j) {
      renderer.fillRectDither(x, y + (rowHeight - 2 - pip) / 2, pip, pip, LightGray);
      renderer.drawRect(x, y + (rowHeight - 2 - pip) / 2, pip, pip, chosen ? toybox::kRule : toybox::kHairline, true);
      x += pip + gap;
    }
  }
}

int BattleshipActivity::placeRosterRowAt(const int x, const int y) const {
  const Rect box = placeRosterRect();
  const int rowHeight = box.height / bship::kShipCount;
  if (rowHeight < 12) return -1;
  if (x < box.x || x >= box.x + box.width) return -1;
  const int row = (y - box.y) / rowHeight;
  if (y < box.y || row < 0 || row >= bship::kShipCount) return -1;
  return row;
}

void BattleshipActivity::drawRoster(const Rect& box, const bool own) const {
  const bship::Side& side = game.side[own ? mySide : opponentSide()];
  const int rows = bship::kShipCount + 1;
  const int rowHeight = box.height / rows;
  toybox::drawCapsCentered(renderer, toybox::kTileFontId, box.x, box.y, rowHeight, own ? "YOUR FLEET" : "THEIR FLEET",
                           true);
  for (int i = 0; i < bship::kShipCount; ++i) {
    const int y = box.y + (i + 1) * rowHeight;
    const bool down = bship::sunk(side, i);
    // Sunk goes solid and the name is knocked out of it. The bar is the news:
    // it lands once per ship and never changes back.
    if (down) renderer.fillRect(box.x, y, box.width, rowHeight - 2, true);
    toybox::drawCapsCentered(renderer, toybox::kTileFontId, box.x + toybox::kGutter / 2, y, rowHeight - 2,
                             bship::shipName(i), !down);
  }
}

void BattleshipActivity::drawMiniGrid(const Rect& slot) const {
  // Your shots at them, from the game you would be going back to. Different
  // every time you play and identical on nobody else's device, which is the
  // test this fork applies to anything decorative.
  const int margin = toybox::kGutter * 4;
  // The legend under it belongs to the block, so it is reserved before the grid
  // is sized and centred with it rather than hung off the bottom.
  const int legend = toybox::kRowHeight / 2;
  const int cell =
      std::clamp(std::min((slot.width - margin) / bship::kSize, (slot.height - margin - legend) / bship::kSize), 6, 40);
  const int side = cell * bship::kSize;
  if (side <= 0) return;
  const int originX = slot.x + (slot.width - side) / 2;
  // Centred in the slot both ways, and centred whether or not a CONTINUE row is
  // under it. The block used to hang from the top, so with no game to continue
  // the slot grew and left a third of a page of nothing beneath the board.
  const int block = side + toybox::kGutter * 2 + legend;
  const int originY = slot.y + (slot.height - block) / 2 + toybox::kGutter;

  toybox::cornerMarks(renderer,
                      Rect{originX - toybox::kGutter, originY - toybox::kGutter, side + toybox::kGutter * 2,
                           side + toybox::kGutter * 2},
                      toybox::kGutter * 2, toybox::kFrame);

  const bship::Side& theirs = game.side[opponentSide()];
  for (int cell_ = 0; cell_ < bship::kCells; ++cell_) {
    const int x = originX + bship::colOf(cell_) * cell;
    const int y = originY + bship::rowOf(cell_) * cell;
    if (!bship::shotAt(theirs, cell_)) continue;
    const int ship = bship::shipAt(theirs.fleet, cell_);
    if (ship >= 0) {
      renderer.fillRect(x + 1, y + 1, cell - 2, cell - 2, true);
      continue;
    }
    const int dot = std::max(3, cell / 4);
    renderer.fillRoundedRect(x + (cell - dot) / 2, y + (cell - dot) / 2, dot, dot, dot / 2, Black);
  }
  for (int i = 1; i < bship::kSize; ++i) {
    renderer.fillRect(originX + i * cell, originY, toybox::kHairline, side, true);
    renderer.fillRect(originX, originY + i * cell, side, toybox::kHairline, true);
  }
  renderer.drawRect(originX - 1, originY - 1, side + 2, side + 2, toybox::kHairline, true);

  // How the game stands, in the marks the board itself uses. A ring is a shot
  // and a solid block is a ship gone down, which is what they mean two inches
  // above, so the legend needs no key and the row below needs no receipt.
  char shots[8];
  char sunk[8];
  std::snprintf(shots, sizeof(shots), "%d", bship::shotsTaken(theirs));
  std::snprintf(sunk, sizeof(sunk), "%d", bship::sunkCount(theirs));

  const int mark = 14;
  const int gap = toybox::kGutter / 2;
  const int span = toybox::kGutter * 2;
  const int shotsWidth = renderer.getTextWidth(toybox::kTileFontId, shots);
  const int sunkWidth = renderer.getTextWidth(toybox::kTileFontId, sunk);
  const int total = mark + gap + shotsWidth + span + mark + gap + sunkWidth;
  int x = slot.x + (slot.width - total) / 2;
  const int y = originY + side + toybox::kGutter * 2;

  renderer.fillRoundedRect(x, y, mark, mark, mark / 2, Black);
  renderer.fillRoundedRect(x + kMissRing, y + kMissRing, mark - 2 * kMissRing, mark - 2 * kMissRing,
                           (mark - 2 * kMissRing) / 2, White);
  x += mark + gap;
  toybox::drawCapsCentered(renderer, toybox::kTileFontId, x, y, mark, shots, true);
  x += shotsWidth + span;

  renderer.fillRect(x, y, mark, mark, true);
  x += mark + gap;
  toybox::drawCapsCentered(renderer, toybox::kTileFontId, x, y, mark, sunk, true);
}

// --- models and paints ------------------------------------------------------

bshipui::StartModel BattleshipActivity::startModel() const {
  bshipui::StartModel model;
  model.hasSavedGame = hasSavedGame;
  model.played = played;
  model.won = won;
  model.streak = streak;
  model.selected = startIndex;
  return model;
}

bshipui::PlaceModel BattleshipActivity::placeModel() const {
  bshipui::PlaceModel model;
  model.status = report;
  // Once the fleet is handed over there is nothing left to press, and the
  // buttons dim rather than vanish: a control that disappears takes its space
  // with it and the grid jumps.
  model.canEdit = !fleetCommitted;
  return model;
}

const char* BattleshipActivity::themWord() {
  // Naming them matters: you learn who you are playing on the searching screen
  // and would otherwise spend the whole match being told only that somebody
  // else had shot at you.
  //
  // Their first word, though. Every use of this is inside a sentence -- "%s
  // SANK YOUR %s", "WAITING FOR %s" -- and a full name is three words now, so
  // the sentence would be the part that got cut. See player::shortName.
  player::shortName(inMatch() ? opponentName() : nullptr, them, sizeof(them));
  return them[0] != '\0' ? them : "THEY";
}

const char* BattleshipActivity::capsuleLabel() {
  if (gameOver()) {
    std::snprintf(capsule, sizeof(capsule), "PLAY AGAIN");
    return capsule;
  }
  if (!myTurn()) {
    std::snprintf(capsule, sizeof(capsule), inMatch() ? "THEIR MOVE" : "INCOMING");
    return capsule;
  }
  if (aimCell < 0) {
    std::snprintf(capsule, sizeof(capsule), "TAP A TARGET");
    return capsule;
  }
  char where[4] = {};
  bship::cellName(aimCell, where);
  std::snprintf(capsule, sizeof(capsule), "FIRE AT %s", where);
  return capsule;
}

bshipui::BoardModel BattleshipActivity::boardModel() {
  bshipui::BoardModel model;
  model.report = report;
  model.status = capsuleLabel();
  model.canFire = canAct() && aimCell >= 0 && !gameOver();
  model.gameOver = gameOver();
  // Only in a match; the computer has no face. Same source as themWord(), so
  // the capsule and the portrait beside it can never name two different people.
  model.theirName = inMatch() ? opponentName() : nullptr;
  return model;
}

void BattleshipActivity::drawStartMenu() {
  namespace fui = freeink::ui;
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);
  const fui::Rect slot = bshipui::buildStartMenu(screen, startModel());
  drawMiniGrid(Rect{slot.x, slot.y, slot.width, slot.height});
  interactionsReady = true;
  toybox::reportOverflow(interactions, "Battleship start");
}

void BattleshipActivity::drawPlaceScreen() {
  namespace fui = freeink::ui;
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);
  const fui::Rect slot = bshipui::buildPlaceChrome(screen, placeModel());
  bodySlot = Rect{slot.x, slot.y, slot.width, slot.height};
  drawPlaceGrid();
  drawPlaceRoster();
  interactionsReady = true;
  toybox::reportOverflow(interactions, "Battleship placement");
}

void BattleshipActivity::drawBoardScreen() {
  namespace fui = freeink::ui;
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);
  const fui::Rect slot = bshipui::buildBoardChrome(screen, boardModel());
  bodySlot = Rect{slot.x, slot.y, slot.width, slot.height};

  drawTargetGrid(gameOver());
  drawFleetGrid();

  const GridGeometry fleet = fleetGrid();
  const int rosterX = fleet.originX + fleet.cell * bship::kSize + toybox::kGutter * 2;
  drawRoster(Rect{rosterX, fleet.originY, bodySlot.x + bodySlot.width - rosterX, fleet.cell * bship::kSize}, false);

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Battleship board");
}

void BattleshipActivity::gameRender() {
  switch (view) {
    case View::Menu:
      drawStartMenu();
      break;
    case View::Place:
      drawPlaceScreen();
      break;
    case View::Board:
      drawBoardScreen();
      break;
  }
  renderer.displayBuffer();
}

// --- flow -------------------------------------------------------------------

void BattleshipActivity::goToMenu() {
  view = View::Menu;
  startIndex = 0;
  requestUpdate();
}

void BattleshipActivity::beginPlacement() {
  bship::reset(game);
  mySide = 0;
  fleetCommitted = false;
  selectedShip = -1;
  aimCell = -1;
  computerThinking = false;
  bship::randomFleet(myFleet, seed);
  std::snprintf(report, sizeof(report), "TAP A SHIP TO MOVE IT");
  view = View::Place;
  requestUpdate();
}

void BattleshipActivity::startNewGame() {
  Storage.remove(kSavePath);
  hasSavedGame = false;
  beginPlacement();
}

void BattleshipActivity::activateStartRow(const bshipui::StartRow row) {
  switch (row) {
    case bshipui::StartRow::Continue:
      // Through the one door, or the board opens with the narration line blank
      // -- which is the defect the screenshots caught on a fresh game and which
      // came straight back on the one door that is not a fresh game.
      enterBoardView();
      break;
    case bshipui::StartRow::NewGame:
      startNewGame();
      return;
    case bshipui::StartRow::PlayNearby:
      // An action, taken now and remembered nowhere. Leaving multiplayer puts
      // the single-player game back exactly as it was.
      enterLink(linkplay::GameId::Battleship);
      break;
    default:
      break;
  }
  requestUpdate();
}

void BattleshipActivity::routeStartMenu() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // See src/apps_local/Shelf.h: no app names its own destination.
    shelf::leave(renderer, mappedInput);
    return;
  }

  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  if (!input.touchReleased || !interactionsReady) return;

  const fui::ActionEvent event = interactions.route(input);
  if (event.action == bshipui::ActionStartRow) {
    startIndex = event.value;
    activateStartRow(bshipui::startRowAt(startModel(), event.value));
  }
}

// --- setting up -------------------------------------------------------------

void BattleshipActivity::shuffleFleet() {
  bship::randomFleet(myFleet, seed);
  selectedShip = -1;
  std::snprintf(report, sizeof(report), "TAP A SHIP TO MOVE IT");
  requestUpdate();
}

void BattleshipActivity::selectShip(const int shipIndex) {
  selectedShip = shipIndex;
  std::snprintf(report, sizeof(report), "TAP A CELL, OR THE SHIP TO TURN");
  requestUpdate();
}

void BattleshipActivity::rotateSelected() {
  if (selectedShip < 0) return;
  const bship::Ship current = myFleet.ships[selectedShip];
  bship::Ship candidate = current;
  candidate.horizontal = static_cast<uint8_t>(current.horizontal != 0 ? 0 : 1);
  if (bship::canPlace(myFleet, selectedShip, candidate)) {
    myFleet.ships[selectedShip] = candidate;
    requestUpdate();
    return;
  }
  // Turning at the edge would run the ship off the board. Walk the bow back
  // along the new axis rather than refusing, because a ship that will not turn
  // near a wall is the fiddliest part of every version of this game.
  const int length = bship::kShipLength[selectedShip];
  for (int back = 1; back < length; ++back) {
    bship::Ship shifted = candidate;
    const int row = bship::rowOf(current.bow) - (candidate.horizontal != 0 ? 0 : back);
    const int col = bship::colOf(current.bow) - (candidate.horizontal != 0 ? back : 0);
    if (row < 0 || col < 0) break;
    shifted.bow = static_cast<uint8_t>(bship::cellOf(row, col));
    if (!bship::canPlace(myFleet, selectedShip, shifted)) continue;
    myFleet.ships[selectedShip] = shifted;
    requestUpdate();
    return;
  }
  std::snprintf(report, sizeof(report), "IT WILL NOT TURN THERE");
  requestUpdate();
}

void BattleshipActivity::moveSelected(const int cell) {
  if (selectedShip < 0) return;
  bship::Ship candidate = myFleet.ships[selectedShip];
  candidate.bow = static_cast<uint8_t>(cell);
  if (!bship::canPlace(myFleet, selectedShip, candidate)) {
    std::snprintf(report, sizeof(report), "IT WILL NOT FIT THERE");
    requestUpdate();
    return;
  }
  myFleet.ships[selectedShip] = candidate;
  std::snprintf(report, sizeof(report), "TAP A CELL, OR IT AGAIN TO TURN");
  requestUpdate();
}

void BattleshipActivity::handlePlaceTap(const int cell) {
  const int ship = bship::shipAt(myFleet, cell);
  if (ship >= 0) {
    if (ship == selectedShip) {
      rotateSelected();
      return;
    }
    selectShip(ship);
    return;
  }
  if (selectedShip < 0) {
    std::snprintf(report, sizeof(report), "TAP A SHIP TO MOVE IT");
    requestUpdate();
    return;
  }
  // The tapped cell is where the bow goes; the ship runs right or down from it,
  // which is the direction its outline already shows.
  moveSelected(cell);
}

void BattleshipActivity::enterBoardView() {
  if (view == View::Board) return;
  view = View::Board;
  aimCell = -1;
  // Never inherit the line the previous screen was using. In a match this said
  // WAITING FOR MARIO for as long as it took somebody to fire, which is exactly
  // the state that had just ended.
  if (inMatch()) {
    std::snprintf(report, sizeof(report), "BOTH FLEETS ARE SET");
    return;
  }
  std::snprintf(report, sizeof(report), "YOUR MOVE");
}

void BattleshipActivity::commitFleet() {
  if (fleetCommitted) return;
  fleetCommitted = true;
  selectedShip = -1;

  if (inMatch()) {
    // The fleet is decided; handing it over waits for the turn. Both players
    // arrange at once and only the wire is alternating, so this is where the
    // simultaneous phase gets serialized -- invisibly, unless the other player
    // is slower than you.
    std::snprintf(report, sizeof(report), "WAITING FOR %s", themWord());
    sendFleetIfReady();
    requestUpdate();
    return;
  }

  if (!bship::place(game, mySide, myFleet)) {
    LOG_ERR("BSHIP", "the fleet on screen was refused by the rules");
    return;
  }
  // Where your fleet ended up, so a drawing question ("is the small grid
  // showing the same board as the big one?") is answered by the log rather than
  // by counting dithered squares in a screenshot.
  for (int i = 0; i < bship::kShipCount; ++i) {
    const bship::Ship& ship = myFleet.ships[i];
    LOG_INF("BSHIP", "fleet %s at row %d col %d %s", bship::shipName(i), bship::rowOf(ship.bow), bship::colOf(ship.bow),
            ship.horizontal != 0 ? "across" : "down");
  }

  // Single player: the computer sets up in the same instant, because there is
  // nobody to wait for. In a match this is where the other device's fleet is
  // still on its way, and the wait is the screen's job rather than a branch.
  bship::Fleet theirs;
  bship::randomFleet(theirs, seed);
  if (!bship::place(game, opponentSide(), theirs)) {
    LOG_ERR("BSHIP", "the computer could not place its fleet");
    return;
  }
  // The line under the rule is never empty on this screen: an empty band at the
  // top reads as something that failed to draw.
  enterBoardView();
  saveGame();
  requestUpdate();
}

// `view` is not a screen label here, it is the grid arithmetic: placeGrid()
// and targetGrid() share an origin but not a cell size, because the target
// grid reserves the fleet band. Every pixel is a different cell on a different
// board across that switch -- and the switch is fired by the NETWORK, in
// takeOpponentState(), not by anything this player touched.
//
// The rest is the live/dead bit: whether a shot can be taken at all, which the
// opponent's arriving shot and the computer's turn both flip with no tap here.
//
// Absent on purpose: aimCell and selectedShip. Aim-then-fire is two taps on
// the same cell and the aim repaints between them, so hashing aimCell would
// drop the confirming tap -- the primary firing gesture. Arranging a fleet is
// fifteen consecutive taps with a repaint after each. Neither moves which cell
// a pixel is; both are the local, deliberate kind of change, not the uniform
// kind that reinterprets the whole surface under a resting finger.
uint32_t BattleshipActivity::surfaceMeaning() const {
  uint32_t meaning = paintclock::mixMeaning(paintclock::kMeaningSeed, static_cast<uint32_t>(view));
  meaning = paintclock::mixMeaning(meaning, fleetCommitted ? 1u : 0u);
  meaning = paintclock::mixMeaning(meaning, computerThinking ? 1u : 0u);
  return paintclock::mixMeaning(meaning, (canAct() && !gameOver()) ? 1u : 0u);
}

void BattleshipActivity::routePlacement() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (linkRequested()) {
      leaveLink();
      return;
    }
    goToMenu();
    return;
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;

  if (interactionsReady) {
    fui::InputSnapshot input;
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
    const fui::ActionEvent event = interactions.route(input);
    if (event.action == bshipui::ActionShuffle) {
      shuffleFleet();
      return;
    }
    if (event.action == bshipui::ActionReady) {
      commitFleet();
      return;
    }
  }

  // The grid is hit-tested against geometry and never reaches route().
  if (!surfaceRevealed()) return;
  const int cell = cellAt(placeGrid(), tapX, tapY);
  if (cell >= 0) {
    handlePlaceTap(cell);
    return;
  }
  // Picking a ship by name is the same act as picking it up off the grid, so it
  // goes through the same function rather than growing a second one.
  const int row = placeRosterRowAt(tapX, tapY);
  if (row >= 0) selectShip(row);
}

// --- playing ----------------------------------------------------------------

void BattleshipActivity::reportShot(const bool theirs) {
  if (game.lastShot == 0) {
    report[0] = '\0';
    return;
  }
  char where[4] = {};
  bship::cellName(game.lastShot - 1, where);
  const int sank = bship::lastShotSank(game);
  const bool hit = bship::lastShotHit(game);
  if (theirs) {
    if (sank >= 0) {
      std::snprintf(report, sizeof(report), "%s SANK YOUR %s", themWord(), bship::shipName(sank));
      return;
    }
    std::snprintf(report, sizeof(report), hit ? "%s HIT YOU AT %s" : "%s MISSED AT %s", themWord(), where);
    return;
  }
  if (sank >= 0) {
    std::snprintf(report, sizeof(report), "YOU SANK THEIR %s", bship::shipName(sank));
    return;
  }
  std::snprintf(report, sizeof(report), hit ? "%s: HIT" : "%s: MISS", where);
}

void BattleshipActivity::handleTargetTap(const int cell) {
  if (gameOver() || !canAct()) return;
  if (bship::shotAt(game.side[opponentSide()], cell)) {
    std::snprintf(report, sizeof(report), "ALREADY FIRED THERE");
    requestUpdate();
    return;
  }
  if (cell == aimCell) {
    // A second tap on the cell you are aiming at is the same action as the
    // capsule, so both go through one function.
    fireAtAimed();
    return;
  }
  aimCell = cell;
  requestUpdate();
}

void BattleshipActivity::fireAtAimed() {
  // Gated before the board is touched, not after. The link refuses an
  // out-of-turn send, but nothing stops an app from mutating its own state
  // first and only then discovering it cannot send it -- which is the one trap
  // this layer cannot close for you.
  if (gameOver() || !canAct() || aimCell < 0) return;
  if (!bship::fire(game, aimCell)) {
    LOG_ERR("BSHIP", "the rules refused a shot the board offered");
    return;
  }
  reportShot(false);
  aimCell = -1;
  if (inMatch()) {
    if (!link.play(game)) LOG_ERR("BSHIP", "the link refused a shot the board allowed");
    if (gameOver()) finishGame();
    requestUpdate();
    return;
  }
  if (gameOver()) {
    finishGame();
    requestUpdate();
    return;
  }
  // Deferred a pass, so the repaint showing your shot lands before the gunner
  // thinks. It is instant either way, but the order is what makes it read as
  // your shot and then theirs rather than as both at once.
  computerThinking = true;
  requestUpdate();
}

void BattleshipActivity::playComputerShot() {
  computerThinking = false;
  const int cell = bship::chooseShot(game.side[mySide], seed);
  if (cell < 0 || !bship::fire(game, cell)) {
    LOG_ERR("BSHIP", "the gunner had nowhere left to shoot");
    return;
  }
  reportShot(true);
  if (gameOver()) finishGame();
  saveGame();
  requestUpdate();
}

void BattleshipActivity::finishGame() {
  ++played;
  const bool mine = bship::winner(game) == mySide;
  if (mine) {
    ++won;
    ++streak;
  } else {
    streak = 0;
  }
  // The result replaces the last shot's narration. Both are worth saying and
  // only one of them is worth saying twice.
  const int loser = mine ? opponentSide() : mySide;
  std::snprintf(report, sizeof(report), mine ? "YOU WIN IN %d SHOTS" : "THEY WIN IN %d SHOTS",
                bship::shotsTaken(game.side[loser]));
  saveStats();
  saveGame();
  hasSavedGame = false;
}

void BattleshipActivity::routeBoard() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (linkRequested()) {
      // Tells them and shuts the radio down; saveGame() refuses during a match
      // on its own, so there is nothing to remember here.
      leaveLink();
      return;
    }
    saveGame();
    hasSavedGame = !gameOver();
    goToMenu();
    return;
  }

  if (computerThinking) {
    playComputerShot();
    return;
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;

  if (interactionsReady) {
    fui::InputSnapshot input;
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
    const fui::ActionEvent event = interactions.route(input);
    if (event.action == bshipui::ActionFire) {
      fireAtAimed();
      return;
    }
    if (event.action == bshipui::ActionPlayAgain) {
      requestNewGame();
      return;
    }
  }

  // The grid is hit-tested against geometry and never reaches route().
  if (!surfaceRevealed()) return;
  const int cell = cellAt(targetGrid(), tapX, tapY);
  if (cell >= 0) handleTargetTap(cell);
}

void BattleshipActivity::gameLoop() {
  switch (view) {
    case View::Menu:
      routeStartMenu();
      return;
    case View::Place:
      routePlacement();
      return;
    case View::Board:
      routeBoard();
      return;
  }
}

// --- the link ---------------------------------------------------------------
//
// Nine methods and no screens. LinkActivity owns the searching screen, the
// rematch conversation, the seat states, the notes, the tick and the sleep
// suppression; what follows is only what battleship can answer.

const char* BattleshipActivity::linkHeadline() const {
  if (linkPhase() == linkplay::PlayBase::Phase::Searching) return "LOOKING FOR A PLAYER";
  if (!gameOver()) return "ANOTHER GAME?";
  return bship::winner(game) == mySide ? "YOU WIN" : "THEY WIN";
}

void BattleshipActivity::onMatchStart(const bool goesFirst) {
  // Side 0 is whoever holds the first turn, because the rules let a side place
  // its fleet only on its own turn. Both devices compute the same answer from
  // the same coin toss, so there is nothing to agree on and nothing to show.
  mySide = goesFirst ? 0 : 1;
  bship::reset(game);
  bship::randomFleet(myFleet, seed);
  fleetCommitted = false;
  selectedShip = -1;
  aimCell = -1;
  computerThinking = false;
  std::snprintf(report, sizeof(report), "TAP A SHIP TO MOVE IT");
  view = View::Place;
}

void BattleshipActivity::sendFleetIfReady() {
  if (!inMatch() || !fleetCommitted) return;
  if (game.side[mySide].placed != 0) return;
  // Not our turn yet: they are still arranging, and the screen is already
  // saying so. Nothing is lost by waiting -- the fleet is safe in myFleet,
  // which is exactly why it lives there rather than in the shared state.
  if (!linkYourTurn()) return;
  if (!bship::place(game, mySide, myFleet)) {
    LOG_ERR("BSHIP", "the rules refused a fleet the screen accepted");
    return;
  }
  if (!link.play(game)) LOG_ERR("BSHIP", "the link refused a fleet on our own turn");
  if (bship::bothPlaced(game)) enterBoardView();
  requestUpdate();
}

bool BattleshipActivity::takeOpponentState() {
  bship::Game incoming;
  if (!link.takeOpponent(incoming)) return false;

  const bool wasPlacing = !bship::bothPlaced(game);
  game = incoming;
  aimCell = -1;
  // Their shot, in their name. A whole state says nothing about how it was
  // reached, which is exactly why lastShot rides along with it.
  if (game.lastShot != 0) reportShot(true);

  if (bship::bothPlaced(game) && wasPlacing) enterBoardView();
  // Ours may have been waiting for the turn their packet has just handed us.
  sendFleetIfReady();
  if (gameOver()) finishGame();
  return true;
}

void BattleshipActivity::onRematch() {
  // Same rule as the first match: side 0 is whoever holds the turn, which the
  // session carries on from the game that just ended.
  onMatchStart(linkYourTurn());
  requestUpdate();
}

void BattleshipActivity::onLinkEnded() {
  // The single-player game goes back exactly as it was: a match never wrote to
  // that file, which is the invariant saveGame() holds rather than each door.
  mySide = 0;
  hasSavedGame = loadGame();
  if (!hasSavedGame) bship::reset(game);
  goToMenu();
}

bool BattleshipActivity::canAct() const {
  if (!inMatch()) return myTurn();
  // Both have to say yes. Asking only whether they AGREE was wrong in the way
  // that only two devices could show: they agree perfectly when it is the
  // opponent's turn, so a tap went through, the board mutated, and the link
  // then refused to send it. That is the one trap this layer cannot close for a
  // game -- gate the input, not the send.
  //
  // The link lagging the board by one pass is normal and means wait: it reports
  // YourTurn only once their state has been taken delivery of, and taking
  // delivery is what moved the board's turn in the first place. The other
  // direction cannot happen unless the two have genuinely drifted, which is the
  // only desync this design can still produce, so it says so.
  if (linkYourTurn() && !myTurn()) {
    LOG_ERR("BSHIP", "the link offers a turn the board refuses: board turn %d, our side %d, %s",
            static_cast<int>(game.turn), static_cast<int>(mySide), bship::bothPlaced(game) ? "playing" : "setting up");
    return false;
  }
  return myTurn() && linkYourTurn();
}

void BattleshipActivity::requestNewGame() {
  if (inMatch()) {
    proposeRematch();
    return;
  }
  startNewGame();
}

// --- persistence ------------------------------------------------------------

void BattleshipActivity::saveGame() const {
  // Never during a match. The board on screen is the shared game and this file
  // is the single-player slot -- the one onLinkEnded() reloads from, which is
  // exactly why nothing may overwrite it here. The rule lives in the callee
  // because no caller has ever wanted the other behaviour.
  if (linkRequested()) return;

  // Nothing to keep before both fleets are down: a half-arranged board is not a
  // game, and restoring one would drop the player into a setup they thought
  // they had left.
  if (!bship::bothPlaced(game)) return;

  // 50 bytes of state as hex, on one line, after the version. Small enough to
  // build in a stack buffer and readable enough to diff when something is off.
  char line[8 + 2 * sizeof(bship::Game)] = {};
  int at = std::snprintf(line, sizeof(line), "%d %d ", kSaveVersion, static_cast<int>(mySide));
  const auto* bytes = reinterpret_cast<const uint8_t*>(&game);
  for (size_t i = 0; i < sizeof(bship::Game) && at + 2 < static_cast<int>(sizeof(line)); ++i) {
    line[at++] = hexDigit(bytes[i] >> 4);
    line[at++] = hexDigit(bytes[i] & 0x0F);
  }
  line[at] = '\0';
  Storage.writeFile(kSavePath, String(line));
}

bool BattleshipActivity::loadGame() {
  if (!Storage.exists(kSavePath)) return false;
  char buffer[8 + 2 * sizeof(bship::Game) + 4] = {};
  if (Storage.readFileToBuffer(kSavePath, buffer, sizeof(buffer)) == 0) return false;

  int version = 0;
  int side = 0;
  int consumed = 0;
  if (std::sscanf(buffer, "%d %d %n", &version, &side, &consumed) < 2) return false;
  if (version != kSaveVersion) {
    LOG_INF("BSHIP", "Ignoring save with unknown version");
    return false;
  }
  const char* hex = buffer + consumed;
  if (std::strlen(hex) < 2 * sizeof(bship::Game)) {
    LOG_ERR("BSHIP", "Corrupt save, starting fresh");
    return false;
  }

  bship::Game restored;
  auto* bytes = reinterpret_cast<uint8_t*>(&restored);
  for (size_t i = 0; i < sizeof(bship::Game); ++i) {
    const int high = hexValue(hex[2 * i]);
    const int low = hexValue(hex[2 * i + 1]);
    if (high < 0 || low < 0) {
      LOG_ERR("BSHIP", "Corrupt save, starting fresh");
      return false;
    }
    bytes[i] = static_cast<uint8_t>((high << 4) | low);
  }
  // A save whose fleets do not obey the rules is a save from a different build,
  // and adopting it would put an illegal board on screen rather than fail.
  for (int s = 0; s < 2; ++s) {
    if (restored.side[s].placed == 0) return false;
    for (int i = 0; i < bship::kShipCount; ++i) {
      if (!bship::canPlace(restored.side[s].fleet, i, restored.side[s].fleet.ships[i])) {
        LOG_ERR("BSHIP", "Save holds an illegal fleet, starting fresh");
        return false;
      }
    }
  }

  game = restored;
  mySide = static_cast<uint8_t>(side & 1);
  fleetCommitted = true;
  aimCell = -1;
  // A game saved while the gunner still owed a shot resumes with it owing one.
  // Back is checked before the deferred shot runs, so pressing it in that one
  // pass used to save a board whose turn belonged to a computer that would
  // never move again: the resumed game refused every tap and there was nothing
  // on screen to say why.
  computerThinking = !bship::over(restored) && (restored.turn & 1) != (side & 1);
  report[0] = '\0';
  LOG_INF("BSHIP", "Resumed a game, %d shots fired", bship::shotsTaken(game.side[opponentSide()]));
  return !bship::over(game);
}

void BattleshipActivity::saveStats() const {
  char line[48];
  std::snprintf(line, sizeof(line), "%d %d %d %d\n", kSaveVersion, played, won, streak);
  Storage.writeFile(kStatsPath, String(line));
}

void BattleshipActivity::loadStats() {
  if (!Storage.exists(kStatsPath)) return;
  char buffer[48] = {};
  if (Storage.readFileToBuffer(kStatsPath, buffer, sizeof(buffer)) == 0) return;
  int version = 0;
  int storedPlayed = 0;
  int storedWon = 0;
  int storedStreak = 0;
  if (std::sscanf(buffer, "%d %d %d %d", &version, &storedPlayed, &storedWon, &storedStreak) < 4) return;
  if (version != kSaveVersion) return;
  played = storedPlayed;
  won = storedWon;
  streak = storedStreak;
}
