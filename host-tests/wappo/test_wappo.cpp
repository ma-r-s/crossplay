#include <cassert>
#include <cstdio>

#include "WappoCore.h"
#include "WappoLevels.h"
#include "WappoProgress.h"

namespace {

// A level with nothing on it: perimeter walls only, no pits, no exit. Tests
// build the exact situation they need on it instead of leaning on a level's
// layout, which tools_local/wappo/gen_levels.py is free to change.
void blankBoard(wappo::Game& g, int player, int m0, int m1 = -1) {
  wappo::initGame(g, 0);
  for (int c = 0; c < wappo::kCellCount; ++c) {
    g.cells[c] = {c < 6, c >= 30, c % 6 == 0, c % 6 == 5, 0};
  }
  g.playerPos = static_cast<int8_t>(player);
  g.monsters[0] = {static_cast<int8_t>(m0), m0 >= 0, false, -1};
  g.monsters[1] = {static_cast<int8_t>(m1), m1 >= 0, false, -1};
}

void wallBetween(wappo::Game& g, int a, int b) {
  if (b - a == 6) {
    g.cells[a].wallDown = g.cells[b].wallUp = true;
  } else {
    g.cells[a].wallRight = g.cells[b].wallLeft = true;
  }
}

void exitAt(wappo::Game& g, int c) {
  g.cells[c].special = 2;
  if (c >= 30) g.cells[c].wallDown = false;
}

}  // namespace

int main() {
  printf("Testing WappoCore...\n");
  wappo::Game game{};

  // Test 1: every level loads as its definition says.
  for (int i = 0; i < wappo::kLevelCount; ++i) {
    const wappo::LevelDef& def = wappo::kLevels[i];
    wappo::initGame(game, i);
    assert(game.levelIndex == i && game.turn == 0 && !game.redMonster);
    assert(game.outcome == wappo::Outcome::Playing);
    assert(game.playerPos == def.playerStart);
    assert(game.monsters[0].active == (def.monsterCount >= 1));
    assert(game.monsters[1].active == (def.monsterCount >= 2));
    for (int w = 0; w < def.wallCount; ++w) {
      const int a = def.walls[w].c1, b = def.walls[w].c2;
      if (b - a == 6) assert(game.cells[a].wallDown && game.cells[b].wallUp);
      if (b - a == 1) assert(game.cells[a].wallRight && game.cells[b].wallLeft);
    }
    int exits = 0;
    for (int s = 0; s < def.specialCount; ++s) {
      assert(game.cells[def.specials[s].cell].special == def.specials[s].type);
      exits += def.specials[s].type == 2;
    }
    assert(exits == 1);
    assert(def.par >= 1);
  }

  // Test 2: a monster walled off from the player takes no steps, so the turn
  // comes straight back without a monsters' phase to tap through.
  blankBoard(game, 9, 11);
  wallBetween(game, 10, 11);
  assert(wappo::makePlayerMove(game, wappo::MoveDir::Left));  // 9 -> 8
  assert(game.playerPos == 8 && game.turn == 1);
  assert(game.phase == wappo::TurnPhase::PlayerTurn);
  assert(game.monsters[0].pos == 11);

  // Test 3: undo takes the move back but still costs one; only a restart
  // clears the count.
  assert(game.moves == 1);
  assert(wappo::undoMove(game));
  assert(game.playerPos == 9 && game.turn == 0 && game.moves == 2);
  assert(game.monsters[0].pos == 11);
  assert(wappo::makePlayerMove(game, wappo::MoveDir::Left));
  assert(game.turn == 1 && game.moves == 3);
  wappo::restartLevel(game);
  assert(game.turn == 0 && game.moves == 0);

  // Test 4: Par score computation
  assert(wappo::scoreForLevel(20, 20) == 100);
  assert(wappo::scoreForLevel(20, 10) == 200);
  assert(wappo::scoreForLevel(20, 40) == 50);

  // Test 5: a monster chases sideways first, two steps a turn.
  blankBoard(game, 14, 17);
  assert(wappo::makePlayerMove(game, wappo::MoveDir::Up));  // 14 -> 8
  assert(game.phase == wappo::TurnPhase::MonstersTurn);
  assert(wappo::advanceMonsters(game));
  assert(game.monsters[0].pos == 15);  // 17 -> 16 -> 15, along the row

  // Test 6: two monsters landing on one square merge into the big one. The
  // second is walled in at 8; the first walks 6 -> 7 -> 8 into it.
  blankBoard(game, 4, 6, 8);
  wallBetween(game, 8, 9);
  wallBetween(game, 2, 8);
  assert(wappo::makePlayerMove(game, wappo::MoveDir::Left));  // 4 -> 3
  assert(wappo::advanceMonsters(game));
  assert(game.redMonster && game.monsters[0].active && !game.monsters[1].active);
  assert(game.monsters[0].pos == 8);

  // Test 7: stepping onto the exit still lets the monsters move first.
  blankBoard(game, 32, 27);
  exitAt(game, 33);
  assert(wappo::makePlayerMove(game, wappo::MoveDir::Right));  // onto the exit
  assert(game.phase == wappo::TurnPhase::MonstersTurn);
  assert(game.outcome == wappo::Outcome::Playing);
  assert(wappo::advanceMonsters(game));
  assert(game.outcome == wappo::Outcome::LostCaught);  // 27 -> 33

  // ...and with the monster boxed in, there is no monsters' turn to wait for:
  // the level is won on the move.
  blankBoard(game, 32, 0);
  exitAt(game, 33);
  wallBetween(game, 0, 1);
  wallBetween(game, 0, 6);
  assert(wappo::makePlayerMove(game, wappo::MoveDir::Right));
  assert(game.outcome == wappo::Outcome::Won);

  // Test 8: A trapped monster must not hide the other monster's planned path.
  blankBoard(game, 8, 15, 34);
  game.cells[15].special = 1;
  game.monsters[0].trapped = true;
  game.monsters[0].trappedTurn = 0;
  assert(wappo::makePlayerMove(game, wappo::MoveDir::Down));  // 8 -> 14
  assert(game.monsters[0].trapped);
  assert(game.phase == wappo::TurnPhase::MonstersTurn);
  assert(game.plannedPaths[0].pathLen == 1);
  assert(game.plannedPaths[1].pathLen == 3);
  assert(game.plannedPaths[1].path[1] == 33);
  assert(game.plannedPaths[1].path[2] == 32);
  assert(wappo::advanceMonsters(game));
  assert(game.monsters[0].pos == 15);
  assert(game.monsters[1].pos == 32);

  // Test 9: Progress -- bests, unlocking, completion and points.
  {
    const int par = wappo::kLevels[0].par;
    wappo::Progress p{};
    assert(!wappo::isCleared(p, 0));
    wappo::recordWin(p, 0, par + 5);
    assert(p.best[0] == par + 5 && p.maxUnlocked == 1);
    assert(!wappo::clearedAtPar(p, 0));
    wappo::recordWin(p, 0, par + 10);  // a worse run keeps the best
    assert(p.best[0] == par + 5);
    wappo::recordWin(p, 0, par);
    assert(p.best[0] == par && wappo::clearedAtPar(p, 0));
    assert(wappo::totalPoints(p) == 100);
    assert(!wappo::allCleared(p));

    for (int i = 0; i < wappo::kLevelCount; ++i) wappo::recordWin(p, i, wappo::kLevels[i].par);
    assert(wappo::allCleared(p));
    assert(p.maxUnlocked == wappo::kLevelCount - 1);  // nothing after the last level
    assert(wappo::totalPoints(p) == wappo::kMaxPoints);
    assert(wappo::atParCount(p) == wappo::kLevelCount);

    // Save round trip.
    p.current = 7;
    p.best[3] = 41;
    char buf[wappo::kSaveBytes];
    wappo::formatProgress(p, buf, sizeof(buf));
    wappo::Progress q{};
    assert(wappo::parseProgress(buf, q));
    assert(q.current == 7 && q.maxUnlocked == p.maxUnlocked);
    for (int i = 0; i < wappo::kLevelCount; ++i) assert(q.best[i] == p.best[i]);

    // An old "<current> <maxUnlocked>" save: levels below the unlocked one were
    // cleared, with no move count, so they tick but score nothing yet.
    wappo::Progress old{};
    assert(wappo::parseProgress("4 5\n", old));
    assert(old.current == 4 && old.maxUnlocked == 5);
    for (int i = 0; i < 5; ++i) assert(old.best[i] == wappo::kBestUnknown);
    assert(!wappo::isCleared(old, 5));
    assert(wappo::totalPoints(old) == 0 && wappo::atParCount(old) == 0);
  }

  printf("All WappoCore tests passed successfully!\n");
  return 0;
}
