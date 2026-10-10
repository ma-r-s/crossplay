#pragma once

#include <cstdint>

#include "WappoLevels.h"

namespace wappo {

enum class Outcome : uint8_t {
  Playing,
  Won,
  LostCaught,
  LostPit,
};

enum class MoveDir : int8_t {
  None = -1,
  Up = 0,
  Down = 1,
  Left = 2,
  Right = 3,
};

enum class TurnPhase : uint8_t {
  PlayerTurn,
  MonstersTurn,
};

struct CellState {
  bool wallUp;
  bool wallDown;
  bool wallLeft;
  bool wallRight;
  int8_t special;  // 0 = None, 1 = Pit, 2 = Exit
};

struct MonsterState {
  int8_t pos;
  bool active;
  bool trapped;
  int16_t trappedTurn;
};

struct MonsterPlan {
  int8_t path[4];  // [0] = current, [1] = step 1, [2] = step 2, [3] = step 3
  int8_t pathLen;  // number of positions in path (1 to 4)
};

struct StepRecord {
  int8_t playerPos;
  MonsterState monsters[2];
  bool redMonster;
  int turn;
};

struct Game {
  int levelIndex;  // 0 .. 124
  int8_t playerPos;
  MonsterState monsters[2];
  bool redMonster;
  int turn;   // game time: undo rewinds it, and pit traps count it
  int moves;  // what the level costs: every move and every undo adds one
  Outcome outcome;
  TurnPhase phase;
  MonsterPlan plannedPaths[2];
  CellState cells[kCellCount];

  static constexpr int kMaxHistory = 256;
  StepRecord history[kMaxHistory];
  int historyCount;
};

void buildCells(int levelIndex, CellState (&cells)[kCellCount]);
void initGame(Game& game, int levelIndex);
bool makeMove(Game& game, MoveDir dir);
bool makePlayerMove(Game& game, MoveDir dir);
bool advanceMonsters(Game& game);
bool undoMove(Game& game);
bool canMove(const Game& game, MoveDir dir);
void restartLevel(Game& game);
bool isOver(const Game& game);
int scoreForLevel(int par, int moves);

}  // namespace wappo
