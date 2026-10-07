#include "WappoCore.h"

#include <cstdlib>

namespace wappo {

namespace {

inline int absDiff(int a, int b) {
  int d = a - b;
  return d < 0 ? -d : d;
}

inline int minVal(int a, int b) { return a < b ? a : b; }

inline int maxVal(int a, int b) { return a > b ? a : b; }

}  // namespace

void buildCells(int levelIndex, CellState (&cells)[kCellCount]) {
  for (int y = 0; y < kBoardHeight; ++y) {
    for (int x = 0; x < kBoardWidth; ++x) {
      int idx = y * kBoardWidth + x;
      cells[idx].wallLeft = (x == 0);
      cells[idx].wallRight = (x == kBoardWidth - 1);
      cells[idx].wallUp = (y == 0);
      cells[idx].wallDown = (y == kBoardHeight - 1);
      cells[idx].special = 0;
    }
  }

  const LevelDef& def = kLevels[levelIndex];

  for (int i = 0; i < def.wallCount; ++i) {
    int c1 = def.walls[i].c1;
    int c2 = def.walls[i].c2;
    int diff = absDiff(c1, c2);
    if (diff == 6) {
      cells[minVal(c1, c2)].wallDown = true;
      cells[maxVal(c1, c2)].wallUp = true;
    } else if (diff == 1) {
      cells[minVal(c1, c2)].wallRight = true;
      cells[maxVal(c1, c2)].wallLeft = true;
    }
  }

  for (int i = 0; i < def.specialCount; ++i) {
    int cell = def.specials[i].cell;
    int type = def.specials[i].type;
    cells[cell].special = type;
    if (type == 2) {
      // Exit opens the perimeter wall
      if (cell % 6 == 0)
        cells[cell].wallLeft = false;
      else if (cell % 6 == 5)
        cells[cell].wallRight = false;
      else if (cell / 6 == 0)
        cells[cell].wallUp = false;
      else if (cell / 6 == 5)
        cells[cell].wallDown = false;
    }
  }
}

void initGame(Game& game, int levelIndex) {
  if (levelIndex < 0) levelIndex = 0;
  if (levelIndex >= kLevelCount) levelIndex = kLevelCount - 1;

  game.levelIndex = levelIndex;
  game.historyCount = 0;
  game.turn = 0;
  game.moves = 0;
  game.outcome = Outcome::Playing;
  game.phase = TurnPhase::PlayerTurn;
  game.plannedPaths[0].pathLen = 0;
  game.plannedPaths[1].pathLen = 0;
  game.redMonster = false;
  buildCells(levelIndex, game.cells);

  const LevelDef& def = kLevels[levelIndex];
  game.playerPos = def.playerStart;
  game.monsters[0] = {def.monsterStart[0], def.monsterCount >= 1, false, -1};
  game.monsters[1] = {def.monsterStart[1], def.monsterCount >= 2, false, -1};
}

void restartLevel(Game& game) { initGame(game, game.levelIndex); }

bool canMove(const Game& game, MoveDir dir) {
  if (game.outcome != Outcome::Playing) return false;
  if (dir == MoveDir::Up) return !game.cells[game.playerPos].wallUp;
  if (dir == MoveDir::Down) return !game.cells[game.playerPos].wallDown;
  if (dir == MoveDir::Left) return !game.cells[game.playerPos].wallLeft;
  if (dir == MoveDir::Right) return !game.cells[game.playerPos].wallRight;
  return false;
}

bool isOver(const Game& game) { return game.outcome != Outcome::Playing; }

int scoreForLevel(int par, int moves) {
  if (moves <= 0) return 0;
  return (par * 100) / moves;
}

bool undoMove(Game& game) {
  if (game.historyCount <= 0) return false;
  --game.historyCount;
  const StepRecord& prev = game.history[game.historyCount];
  game.playerPos = prev.playerPos;
  game.monsters[0] = prev.monsters[0];
  game.monsters[1] = prev.monsters[1];
  game.redMonster = prev.redMonster;
  game.turn = prev.turn;
  ++game.moves;  // taking a move back is a move too; only a restart clears the count
  game.outcome = Outcome::Playing;
  game.phase = TurnPhase::PlayerTurn;
  game.plannedPaths[0].pathLen = 0;
  game.plannedPaths[1].pathLen = 0;
  return true;
}

namespace {

// One step of the original chase rule: close the column gap first; if that
// side is walled (or the column already matches), close the row gap instead.
// Returns the monster's own cell when it is blocked.
int chaseStep(const Game& game, int pos) {
  const CellState& cell = game.cells[pos];
  const int mx = pos % 6;
  const int my = pos / 6;
  const int px = game.playerPos % 6;
  const int py = game.playerPos / 6;

  if (mx < px && !cell.wallRight) return pos + 1;
  if (mx > px && !cell.wallLeft) return pos - 1;
  if (my < py && !cell.wallDown) return pos + 6;
  if (my > py && !cell.wallUp) return pos - 6;
  return pos;
}

void computePlannedPaths(Game& game) {
  game.plannedPaths[0].pathLen = 0;
  game.plannedPaths[1].pathLen = 0;

  // Clone current monster positions and states for simulation
  MonsterState simMonsters[2] = {game.monsters[0], game.monsters[1]};
  bool simRed = game.redMonster;

  for (int m = 0; m < 2; ++m) {
    if (simMonsters[m].active) {
      game.plannedPaths[m].path[0] = simMonsters[m].pos;
      game.plannedPaths[m].pathLen = 1;
    }
  }

  const int steps = simRed ? 3 : 2;
  for (int step = 0; step < steps; ++step) {
    for (int m = 0; m < 2; ++m) {
      auto& mon = simMonsters[m];
      if (!mon.active || mon.trapped) continue;

      const int oldPos = mon.pos;
      mon.pos = static_cast<int8_t>(chaseStep(game, mon.pos));

      if (mon.pos != oldPos) {
        if (game.plannedPaths[m].pathLen < 4) {
          game.plannedPaths[m].path[game.plannedPaths[m].pathLen++] = mon.pos;
        }
        if (game.cells[mon.pos].special == 1 && !simRed) {
          mon.trapped = true;
        }
      }
    }

    if (simMonsters[0].active && simMonsters[1].active && simMonsters[0].pos == simMonsters[1].pos) {
      simMonsters[1].active = false;
      simRed = true;
      break;
    }
  }
}

}  // namespace

bool makePlayerMove(Game& game, MoveDir dir) {
  if (game.phase != TurnPhase::PlayerTurn) return false;
  if (!canMove(game, dir)) return false;

  if (game.historyCount < Game::kMaxHistory) {
    game.history[game.historyCount++] = {
        game.playerPos, {game.monsters[0], game.monsters[1]}, game.redMonster, game.turn};
  }

  if (dir == MoveDir::Up) {
    game.playerPos -= 6;
  } else if (dir == MoveDir::Down) {
    game.playerPos += 6;
  } else if (dir == MoveDir::Left) {
    game.playerPos -= 1;
  } else if (dir == MoveDir::Right) {
    game.playerPos += 1;
  }

  ++game.turn;
  ++game.moves;

  // Unfreeze monsters whose 3 trapped turns have expired
  for (auto& m : game.monsters) {
    if (m.trapped && (game.turn - m.trappedTurn > 3)) {
      m.trapped = false;
    }
  }

  // Check Pit fall
  if (game.cells[game.playerPos].special == 1) {
    game.outcome = Outcome::LostPit;
    game.phase = TurnPhase::PlayerTurn;
    return true;
  }

  // Check if player stepped directly into a monster
  for (const auto& m : game.monsters) {
    if (m.active && m.pos == game.playerPos) {
      game.outcome = Outcome::LostCaught;
      game.phase = TurnPhase::PlayerTurn;
      return true;
    }
  }

  // Check if any monster can move
  bool hasActiveMonsters = false;
  for (const auto& m : game.monsters) {
    if (m.active) hasActiveMonsters = true;
  }

  if (!hasActiveMonsters) {
    if (game.cells[game.playerPos].special == 2) {
      game.outcome = Outcome::Won;
    }
    game.phase = TurnPhase::PlayerTurn;
    return true;
  }

  // Plan monster paths for preview
  computePlannedPaths(game);

  int movingMonsters = 0;
  for (int m = 0; m < 2; ++m) {
    if (game.monsters[m].active && game.plannedPaths[m].pathLen > 1) {
      ++movingMonsters;
    }
  }

  game.phase = TurnPhase::MonstersTurn;

  if (movingMonsters == 0) {
    advanceMonsters(game);
    return true;
  }

  return true;
}

bool advanceMonsters(Game& game) {
  if (game.phase != TurnPhase::MonstersTurn) return false;
  if (game.outcome != Outcome::Playing) return false;

  const int steps = game.redMonster ? 3 : 2;
  for (int step = 0; step < steps; ++step) {
    for (auto& m : game.monsters) {
      if (!m.active || m.trapped) continue;

      const int oldPos = m.pos;
      m.pos = static_cast<int8_t>(chaseStep(game, m.pos));

      if (m.pos == game.playerPos) {
        game.outcome = Outcome::LostCaught;
        game.phase = TurnPhase::PlayerTurn;
        return true;
      }
      if (m.pos != oldPos && game.cells[m.pos].special == 1 && !game.redMonster) {
        m.trapped = true;
        m.trappedTurn = game.turn;
      }
    }

    if (game.monsters[0].active && game.monsters[1].active && game.monsters[0].pos == game.monsters[1].pos) {
      game.monsters[1].active = false;
      game.redMonster = true;
      game.monsters[0].trapped = false;
      game.monsters[0].trappedTurn = 0;
      break;
    }
  }

  if (game.cells[game.playerPos].special == 2) {
    game.outcome = Outcome::Won;
  }

  game.phase = TurnPhase::PlayerTurn;
  game.plannedPaths[0].pathLen = 0;
  game.plannedPaths[1].pathLen = 0;
  return true;
}

bool makeMove(Game& game, MoveDir dir) {
  if (game.phase == TurnPhase::PlayerTurn) {
    if (!makePlayerMove(game, dir)) return false;
    if (game.phase == TurnPhase::MonstersTurn) {
      advanceMonsters(game);
    }
    return true;
  }
  return false;
}

}  // namespace wappo
