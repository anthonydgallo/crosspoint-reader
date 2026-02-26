#include "MinesweeperAppActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr uint8_t MINESWEEPER_SAVE_VERSION = 2;
constexpr char MINESWEEPER_SAVE_FILE[] = "/.crosspoint/minesweeper.bin";
constexpr uint8_t MINESWEEPER_STATS_VERSION = 1;
constexpr char MINESWEEPER_STATS_FILE[] = "/.crosspoint/minesweeper_stats.bin";
}  // namespace

// ---------------------------------------------------------------------------
// Timer helpers
// ---------------------------------------------------------------------------

unsigned long MinesweeperAppActivity::getElapsedSecs() const {
  if (!boardInitialized) {
    return 0;
  }
  if (gameOver || victory) {
    return frozenElapsedMs / 1000;
  }
  return (millis() - gameStartMs) / 1000;
}

std::string MinesweeperAppActivity::formatTime(const unsigned long secs) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%lu:%02lu", secs / 60, secs % 60);
  return buf;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void MinesweeperAppActivity::onEnter() {
  Activity::onEnter();
  randomSeed(static_cast<unsigned long>(millis()));

  loadStats();

  if (!loadState()) {
    resetGame();
  }

  confirmingExit = false;
  requestUpdate();
}

void MinesweeperAppActivity::onExit() {
  // Save state if game is in progress (not lost, not won)
  if (boardInitialized && !gameOver && !victory) {
    saveState();
  } else {
    clearSavedState();
  }

  Activity::onExit();
}

// ---------------------------------------------------------------------------
// Game logic
// ---------------------------------------------------------------------------

void MinesweeperAppActivity::resetGame() {
  for (int r = 0; r < kRows; r++) {
    for (int c = 0; c < kCols; c++) {
      board[r][c] = Cell{};
    }
  }

  cursorRow = 0;
  cursorCol = 0;
  boardInitialized = false;
  gameOver = false;
  victory = false;
  revealedSafeCount = 0;
  flaggedCount = 0;
  gameStartMs = 0;
  frozenElapsedMs = 0;
  triggerRow = -1;
  triggerCol = -1;
  newBest = false;
}

bool MinesweeperAppActivity::inBounds(const int row, const int col) const {
  return row >= 0 && row < kRows && col >= 0 && col < kCols;
}

void MinesweeperAppActivity::placeMines(const int safeRow, const int safeCol) {
  int placed = 0;
  while (placed < kMines) {
    const int row = static_cast<int>(random(kRows));
    const int col = static_cast<int>(random(kCols));

    if ((row == safeRow && col == safeCol) || board[row][col].mine) {
      continue;
    }

    board[row][col].mine = true;
    placed++;
  }

  computeAdjacencies();
  boardInitialized = true;
  gameStartMs = millis();
}

void MinesweeperAppActivity::computeAdjacencies() {
  for (int row = 0; row < kRows; row++) {
    for (int col = 0; col < kCols; col++) {
      if (board[row][col].mine) {
        board[row][col].adjacent = 0;
        continue;
      }

      uint8_t mines = 0;
      for (int dRow = -1; dRow <= 1; dRow++) {
        for (int dCol = -1; dCol <= 1; dCol++) {
          if (dRow == 0 && dCol == 0) {
            continue;
          }

          const int nRow = row + dRow;
          const int nCol = col + dCol;
          if (inBounds(nRow, nCol) && board[nRow][nCol].mine) {
            mines++;
          }
        }
      }

      board[row][col].adjacent = mines;
    }
  }
}

void MinesweeperAppActivity::moveCursor(const int dRow, const int dCol) {
  cursorRow = (cursorRow + dRow + kRows) % kRows;
  cursorCol = (cursorCol + dCol + kCols) % kCols;
  requestUpdate();
}

void MinesweeperAppActivity::revealFloodFill(const int startRow, const int startCol) {
  int queueRows[kRows * kCols];
  int queueCols[kRows * kCols];
  bool queued[kRows][kCols] = {{false}};

  int head = 0;
  int tail = 0;

  queueRows[tail] = startRow;
  queueCols[tail] = startCol;
  queued[startRow][startCol] = true;
  tail++;

  while (head < tail) {
    const int row = queueRows[head];
    const int col = queueCols[head];
    head++;

    Cell& cell = board[row][col];
    if (cell.revealed || cell.flagged || cell.mine) {
      continue;
    }

    cell.revealed = true;
    revealedSafeCount++;

    if (cell.adjacent > 0) {
      continue;
    }

    for (int dRow = -1; dRow <= 1; dRow++) {
      for (int dCol = -1; dCol <= 1; dCol++) {
        if (dRow == 0 && dCol == 0) {
          continue;
        }

        const int nRow = row + dRow;
        const int nCol = col + dCol;

        if (!inBounds(nRow, nCol)) {
          continue;
        }

        if (queued[nRow][nCol]) {
          continue;
        }

        const Cell& next = board[nRow][nCol];
        if (next.mine || next.revealed || next.flagged) {
          continue;
        }

        queueRows[tail] = nRow;
        queueCols[tail] = nCol;
        queued[nRow][nCol] = true;
        tail++;
      }
    }
  }
}

void MinesweeperAppActivity::revealCell(const int row, const int col) {
  if (gameOver || victory) {
    return;
  }

  if (!boardInitialized) {
    placeMines(row, col);
  }

  Cell& cell = board[row][col];
  if (cell.revealed || cell.flagged) {
    return;
  }

  if (cell.mine) {
    cell.revealed = true;
    gameOver = true;
    triggerRow = row;
    triggerCol = col;
    frozenElapsedMs = millis() - gameStartMs;

    // Reveal all mines
    for (int r = 0; r < kRows; r++) {
      for (int c = 0; c < kCols; c++) {
        if (board[r][c].mine) {
          board[r][c].revealed = true;
        }
      }
    }

    // Update stats
    stats.gamesLost++;
    saveStats();

    requestUpdate();
    return;
  }

  revealFloodFill(row, col);
  checkWin();
  requestUpdate();
}

void MinesweeperAppActivity::toggleFlag(const int row, const int col) {
  if (gameOver || victory) {
    return;
  }

  Cell& cell = board[row][col];
  if (cell.revealed) {
    return;
  }

  cell.flagged = !cell.flagged;
  flaggedCount += cell.flagged ? 1 : -1;
  requestUpdate();
}

void MinesweeperAppActivity::checkWin() {
  const int safeCells = kRows * kCols - kMines;
  if (revealedSafeCount != safeCells) {
    return;
  }

  victory = true;
  frozenElapsedMs = millis() - gameStartMs;

  // Auto-flag remaining mines
  for (int row = 0; row < kRows; row++) {
    for (int col = 0; col < kCols; col++) {
      if (board[row][col].mine && !board[row][col].flagged) {
        board[row][col].flagged = true;
      }
    }
  }
  flaggedCount = kMines;

  // Update stats
  stats.gamesWon++;
  const auto timeSecs = static_cast<uint32_t>(frozenElapsedMs / 1000);
  if (stats.bestTimeSecs == 0 || timeSecs < stats.bestTimeSecs) {
    stats.bestTimeSecs = timeSecs;
    newBest = true;
  }
  saveStats();
}

// ---------------------------------------------------------------------------
// Game state persistence
// ---------------------------------------------------------------------------

bool MinesweeperAppActivity::saveState() const {
  Storage.mkdir("/.crosspoint");

  FsFile file;
  if (!Storage.openFileForWrite("MSW", MINESWEEPER_SAVE_FILE, file)) {
    LOG_ERR("MSW", "Failed to open save file for writing");
    return false;
  }

  serialization::writePod(file, MINESWEEPER_SAVE_VERSION);
  serialization::writePod(file, board);
  serialization::writePod(file, cursorRow);
  serialization::writePod(file, cursorCol);
  serialization::writePod(file, boardInitialized);
  serialization::writePod(file, gameOver);
  serialization::writePod(file, victory);
  serialization::writePod(file, revealedSafeCount);
  serialization::writePod(file, flaggedCount);

  // v2: elapsed timer
  const unsigned long elapsed = millis() - gameStartMs;
  serialization::writePod(file, elapsed);

  file.close();
  LOG_DBG("MSW", "Game state saved");
  return true;
}

bool MinesweeperAppActivity::loadState() {
  FsFile file;
  if (!Storage.openFileForRead("MSW", MINESWEEPER_SAVE_FILE, file)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version != MINESWEEPER_SAVE_VERSION) {
    LOG_ERR("MSW", "Unknown save version %u", version);
    file.close();
    return false;
  }

  serialization::readPod(file, board);
  serialization::readPod(file, cursorRow);
  serialization::readPod(file, cursorCol);
  serialization::readPod(file, boardInitialized);
  serialization::readPod(file, gameOver);
  serialization::readPod(file, victory);
  serialization::readPod(file, revealedSafeCount);
  serialization::readPod(file, flaggedCount);

  // v2: elapsed timer - restore so timer continues seamlessly
  unsigned long elapsed = 0;
  serialization::readPod(file, elapsed);
  gameStartMs = millis() - elapsed;

  file.close();

  // If the saved game was lost or won, reset instead
  if (gameOver || victory) {
    LOG_DBG("MSW", "Saved game was finished, resetting");
    resetGame();
    clearSavedState();
    return true;
  }

  LOG_DBG("MSW", "Game state restored");
  return true;
}

void MinesweeperAppActivity::clearSavedState() {
  if (Storage.exists(MINESWEEPER_SAVE_FILE)) {
    Storage.remove(MINESWEEPER_SAVE_FILE);
    LOG_DBG("MSW", "Saved state cleared");
  }
}

// ---------------------------------------------------------------------------
// Stats persistence
// ---------------------------------------------------------------------------

void MinesweeperAppActivity::saveStats() const {
  Storage.mkdir("/.crosspoint");

  FsFile file;
  if (!Storage.openFileForWrite("MSW", MINESWEEPER_STATS_FILE, file)) {
    LOG_ERR("MSW", "Failed to open stats file for writing");
    return;
  }

  serialization::writePod(file, MINESWEEPER_STATS_VERSION);
  serialization::writePod(file, stats);
  file.close();
  LOG_DBG("MSW", "Stats saved (W:%lu L:%lu Best:%lu)", (unsigned long)stats.gamesWon,
          (unsigned long)stats.gamesLost, (unsigned long)stats.bestTimeSecs);
}

void MinesweeperAppActivity::loadStats() {
  FsFile file;
  if (!Storage.openFileForRead("MSW", MINESWEEPER_STATS_FILE, file)) {
    stats = Stats{};
    return;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version != MINESWEEPER_STATS_VERSION) {
    LOG_ERR("MSW", "Unknown stats version %u", version);
    file.close();
    stats = Stats{};
    return;
  }

  serialization::readPod(file, stats);
  file.close();
  LOG_DBG("MSW", "Stats loaded (W:%lu L:%lu Best:%lu)", (unsigned long)stats.gamesWon,
          (unsigned long)stats.gamesLost, (unsigned long)stats.bestTimeSecs);
}

// ---------------------------------------------------------------------------
// Input loop
// ---------------------------------------------------------------------------

void MinesweeperAppActivity::loop() {
  // Exit confirmation state
  if (confirmingExit) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      onGoHome();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      confirmingExit = false;
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // If game is in progress, ask for confirmation before exiting
    if (boardInitialized && !gameOver && !victory) {
      confirmingExit = true;
      requestUpdate();
      return;
    }
    onGoHome();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    moveCursor(0, -1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    moveCursor(0, 1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    moveCursor(-1, 0);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    moveCursor(1, 0);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (gameOver || victory) {
      clearSavedState();
      resetGame();
      requestUpdate();
      return;
    }

    if (mappedInput.getHeldTime() >= kFlagHoldMs) {
      toggleFlag(cursorRow, cursorCol);
    } else {
      revealCell(cursorRow, cursorCol);
    }
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void MinesweeperAppActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto metrics = UITheme::getInstance().getMetrics();

  // --- Exit confirmation dialog ---
  if (confirmingExit) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, manifest.name.c_str());

    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 30, "Exit game?", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Your progress will be saved.", true);

    const auto labels = mappedInput.mapLabels("« Cancel", "Exit", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

    renderer.displayBuffer();
    return;
  }

  // --- Header ---
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, manifest.name.c_str());

  int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int sidePadding = metrics.contentSidePadding;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);

  // --- Status line 1: game info ---
  std::string line1;
  if (victory) {
    line1 = "YOU WIN!  Time: " + formatTime(getElapsedSecs());
    if (newBest) {
      line1 += "  NEW BEST!";
    }
  } else if (gameOver) {
    line1 = "GAME OVER  Time: " + formatTime(getElapsedSecs());
  } else {
    line1 = "Mines:" + std::to_string(kMines) + "  Flags:" + std::to_string(flaggedCount) +
            "  Time:" + formatTime(getElapsedSecs());
  }
  const bool line1Bold = gameOver || victory;
  renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, line1.c_str(), true,
                    line1Bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  contentY += lineH + 2;

  // --- Status line 2: tips or stats ---
  std::string line2;
  if (victory || gameOver) {
    line2 = "W:" + std::to_string(stats.gamesWon) + " L:" + std::to_string(stats.gamesLost);
    if (stats.bestTimeSecs > 0) {
      line2 += " Best:" + formatTime(stats.bestTimeSecs);
    }
    line2 += "  Confirm=new game";
  } else {
    line2 = "Tap=open  Hold=flag";
    if (stats.gamesWon + stats.gamesLost > 0) {
      line2 += "  W:" + std::to_string(stats.gamesWon) + " L:" + std::to_string(stats.gamesLost);
    }
  }
  renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, line2.c_str());

  // --- Board layout ---
  const int boardTop = contentY + lineH + metrics.verticalSpacing;
  const int availableBoardHeight = pageHeight - boardTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int availableBoardWidth = pageWidth - sidePadding * 2;
  const int cellSize = std::max(16, std::min(availableBoardWidth / kCols, availableBoardHeight / kRows));

  const int boardWidth = cellSize * kCols;
  const int boardHeight = cellSize * kRows;
  const int boardX = (pageWidth - boardWidth) / 2;
  const int boardY = boardTop + std::max(0, (availableBoardHeight - boardHeight) / 2);

  // --- Draw cells ---
  for (int row = 0; row < kRows; row++) {
    for (int col = 0; col < kCols; col++) {
      const int x = boardX + col * cellSize;
      const int y = boardY + row * cellSize;

      const Cell& cell = board[row][col];

      if (cell.revealed) {
        if (cell.mine) {
          // --- Mine cell rendering ---
          const bool isTriggered = (row == triggerRow && col == triggerCol);
          const int cx = x + cellSize / 2;
          const int cy = y + cellSize / 2;
          const int r = cellSize / 2 - 4;
          const int dotR = std::max(2, cellSize / 10);

          if (isTriggered) {
            // Triggered mine: solid black fill, white symbol
            renderer.fillRect(x + 1, y + 1, cellSize - 2, cellSize - 2, true);
            renderer.drawRect(x, y, cellSize, cellSize, true);

            // Mine spokes in white
            renderer.drawLine(cx - r, cy, cx + r, cy, false);
            renderer.drawLine(cx, cy - r, cx, cy + r, false);
            renderer.drawLine(cx - r + 1, cy - r + 1, cx + r - 1, cy + r - 1, false);
            renderer.drawLine(cx + r - 1, cy - r + 1, cx - r + 1, cy + r - 1, false);
            // Center dot in white
            renderer.fillRect(cx - dotR, cy - dotR, dotR * 2 + 1, dotR * 2 + 1, false);
          } else {
            // Other mines: dark gray fill, black symbol
            renderer.fillRectDither(x + 1, y + 1, cellSize - 2, cellSize - 2, Color::DarkGray);
            renderer.drawRect(x, y, cellSize, cellSize, true);

            // Mine spokes in black
            renderer.drawLine(cx - r, cy, cx + r, cy);
            renderer.drawLine(cx, cy - r, cx, cy + r);
            renderer.drawLine(cx - r + 1, cy - r + 1, cx + r - 1, cy + r - 1);
            renderer.drawLine(cx + r - 1, cy - r + 1, cx - r + 1, cy + r - 1);
            // Center dot
            renderer.fillRect(cx - dotR, cy - dotR, dotR * 2 + 1, dotR * 2 + 1, true);
          }
        } else {
          // --- Safe revealed cell ---
          renderer.fillRect(x + 1, y + 1, cellSize - 2, cellSize - 2, false);
          renderer.drawRect(x, y, cellSize, cellSize, true);

          if (cell.adjacent > 0) {
            char nbuf[2] = {static_cast<char>('0' + cell.adjacent), '\0'};
            const int textX = x + (cellSize - renderer.getTextWidth(UI_10_FONT_ID, nbuf)) / 2;
            const int textY = y + (cellSize - lineH) / 2;
            renderer.drawText(UI_10_FONT_ID, textX, textY, nbuf);
          }
        }
      } else {
        // --- Unrevealed cell ---
        renderer.fillRect(x + 1, y + 1, cellSize - 2, cellSize - 2);
        renderer.drawRect(x, y, cellSize, cellSize, false);

        if (cell.flagged) {
          const char* flag = "F";
          const int textX = x + (cellSize - renderer.getTextWidth(UI_10_FONT_ID, flag)) / 2;
          const int textY = y + (cellSize - lineH) / 2;
          renderer.drawText(UI_10_FONT_ID, textX, textY, flag, false);
        }
      }

      // --- Cursor highlight ---
      if (row == cursorRow && col == cursorCol) {
        const bool cursorColor = cell.revealed;
        renderer.drawRect(x + 1, y + 1, cellSize - 2, cellSize - 2, 2, cursorColor);
      }
    }
  }

  // --- Button hints (context-sensitive) ---
  const char* btn2Label = (gameOver || victory) ? "New Game" : "Open/Flag";
  const auto labels = mappedInput.mapLabels("« Back", btn2Label, "Left", "Right");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, "Up", "Down");

  renderer.displayBuffer();
}
