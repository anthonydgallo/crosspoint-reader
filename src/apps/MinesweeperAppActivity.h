#pragma once

#include <cstdint>
#include <functional>

#include "AppManifest.h"
#include "activities/Activity.h"

class MinesweeperAppActivity final : public Activity {
  static constexpr int kRows = 8;
  static constexpr int kCols = 8;
  static constexpr int kMines = 10;
  static constexpr unsigned long kFlagHoldMs = 500;

  struct Cell {
    bool mine = false;
    bool revealed = false;
    bool flagged = false;
    uint8_t adjacent = 0;
  };

  Cell board[kRows][kCols];

  int cursorRow = 0;
  int cursorCol = 0;
  bool boardInitialized = false;
  bool gameOver = false;
  bool victory = false;
  int revealedSafeCount = 0;
  int flaggedCount = 0;
  bool confirmingExit = false;

  const AppManifest manifest;
  const std::function<void()> onGoHome;

  void resetGame();
  void placeMines(int safeRow, int safeCol);
  void computeAdjacencies();
  bool inBounds(int row, int col) const;
  void moveCursor(int dRow, int dCol);
  void revealCell(int row, int col);
  void revealFloodFill(int startRow, int startCol);
  void toggleFlag(int row, int col);
  void checkWin();

  // State persistence
  bool saveState() const;
  bool loadState();
  void clearSavedState();

 public:
  explicit MinesweeperAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                  const AppManifest& manifest, const std::function<void()>& onGoHome)
      : Activity("Minesweeper", renderer, mappedInput), manifest(manifest), onGoHome(onGoHome) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
};
