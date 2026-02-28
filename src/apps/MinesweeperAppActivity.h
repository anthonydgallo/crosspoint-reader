#pragma once

#include <cstdint>
#include <string>

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

  struct Stats {
    uint32_t gamesWon = 0;
    uint32_t gamesLost = 0;
    uint32_t bestTimeSecs = 0;  // 0 = no record yet
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

  // Timer
  unsigned long gameStartMs = 0;
  unsigned long frozenElapsedMs = 0;

  // Which mine was clicked (for highlighting)
  int triggerRow = -1;
  int triggerCol = -1;

  // Whether the winning time was a new best
  bool newBest = false;

  // Persistent stats
  Stats stats;

  const AppManifest manifest;

  void resetGame();
  void placeMines(int safeRow, int safeCol);
  void computeAdjacencies();
  bool inBounds(int row, int col) const;
  void moveCursor(int dRow, int dCol);
  void revealCell(int row, int col);
  void revealFloodFill(int startRow, int startCol);
  void toggleFlag(int row, int col);
  void checkWin();

  // Timer helpers
  unsigned long getElapsedSecs() const;
  static std::string formatTime(unsigned long secs);

  // State persistence
  bool saveState() const;
  bool loadState();
  void clearSavedState();

  // Stats persistence
  void saveStats() const;
  void loadStats();

 public:
  explicit MinesweeperAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                  const AppManifest& manifest)
      : Activity("Minesweeper", renderer, mappedInput), manifest(manifest) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
