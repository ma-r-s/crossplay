#pragma once

#include "../ui/ToyboxScreen.h"
#include "WappoCore.h"
#include "WappoFlow.h"
#include "WappoProgress.h"

namespace wappoui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionMenuRow = 1,
  ActionHowToNext = 2,
  ActionHowToPrev = 3,
  ActionUndo = 4,
  ActionRestart = 5,
  ActionLevelsNext = 8,
  ActionLevelsPrev = 9,
  ActionLevelPicked = 10,
  ActionBackMenu = 11,
  ActionDone = 12,
  ActionLevels = 14,
  ActionHowTo = 15,
  ActionNextLevel = 16,
  ActionFinish = 17,
  ActionMenu = 18,
};

enum class MenuRow : int {
  Play = 0,
  Levels,
  HowTo,
  Count,
};

struct MenuModel {
  int selected = -1;
  int currentLevel = 0;
  const wappo::Progress* progress = nullptr;
};

struct HowToModel {
  int page = 0;
};

struct LevelsModel {
  int page = 0;
  int currentLevel = 0;
  const wappo::Progress* progress = nullptr;
};

struct EndingModel {
  int points = 0;
  int atPar = 0;
};

struct BoardModel {
  // Borrowed, not copied: a Game is about 5 KB (mostly its undo history), and
  // this model lives on the render task's stack.
  const wappo::Game* game = nullptr;
  int par = 0;
};

struct Layout {
  fui::Rect board{};
  int16_t cellSize = 60;

  bool cellAt(int x, int y, int& col, int& row) const;
  fui::Rect cellRect(int col, int row) const;
};

struct LevelsLayout {
  fui::Rect grid{};
  int16_t cellW = 80;
  int16_t cellH = 50;
  int16_t cols = 4;
  int16_t rows = 5;
  int page = 0;

  int levelAt(int x, int y) const;
};

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildHowTo(toybox::Screen& screen, const HowToModel& model);
void buildLevels(toybox::Screen& screen, const LevelsModel& model, LevelsLayout& layout);
void buildBoard(toybox::Screen& screen, const BoardModel& model, Layout& layout);
void buildEnding(toybox::Screen& screen, const EndingModel& model);

int howToPages();

}  // namespace wappoui
