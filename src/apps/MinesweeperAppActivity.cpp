#include "MinesweeperAppActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <string>

#include "components/UITheme.h"
#include "fontIds.h"

void MinesweeperAppActivity::onEnter() {
  Activity::onEnter();
  randomSeed(static_cast<unsigned long>(millis()));
  resetGame();
  requestUpdate();
}

void MinesweeperAppActivity::onExit() { Activity::onExit(); }

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

    for (int r = 0; r < kRows; r++) {
      for (int c = 0; c < kCols; c++) {
        if (board[r][c].mine) {
          board[r][c].revealed = true;
        }
      }
    }

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
  for (int row = 0; row < kRows; row++) {
    for (int col = 0; col < kCols; col++) {
      if (board[row][col].mine && !board[row][col].flagged) {
        board[row][col].flagged = true;
      }
    }
  }
  flaggedCount = kMines;
}

void MinesweeperAppActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
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

void MinesweeperAppActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, manifest.name.c_str());

  int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int sidePadding = metrics.contentSidePadding;

  std::string statLine = "Mines " + std::to_string(kMines) + "  Flags " + std::to_string(flaggedCount);
  renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, statLine.c_str());
  contentY += renderer.getLineHeight(UI_10_FONT_ID) + 2;

  std::string tipLine;
  if (victory) {
    tipLine = "Cleared. Confirm for new game";
  } else if (gameOver) {
    tipLine = "Boom. Confirm to retry";
  } else {
    tipLine = "Tap confirm=open, hold=flag";
  }
  renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, tipLine.c_str());

  const int boardTop = contentY + renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
  const int availableBoardHeight = pageHeight - boardTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int availableBoardWidth = pageWidth - sidePadding * 2;
  const int cellSize = std::max(16, std::min(availableBoardWidth / kCols, availableBoardHeight / kRows));

  const int boardWidth = cellSize * kCols;
  const int boardHeight = cellSize * kRows;
  const int boardX = (pageWidth - boardWidth) / 2;
  const int boardY = boardTop + std::max(0, (availableBoardHeight - boardHeight) / 2);

  for (int row = 0; row < kRows; row++) {
    for (int col = 0; col < kCols; col++) {
      const int x = boardX + col * cellSize;
      const int y = boardY + row * cellSize;

      const Cell& cell = board[row][col];

      if (cell.revealed) {
        renderer.fillRect(x + 1, y + 1, cellSize - 2, cellSize - 2, false);
        renderer.drawRect(x, y, cellSize, cellSize, true);

        if (cell.mine) {
          renderer.drawLine(x + 3, y + 3, x + cellSize - 4, y + cellSize - 4);
          renderer.drawLine(x + cellSize - 4, y + 3, x + 3, y + cellSize - 4);
        } else if (cell.adjacent > 0) {
          char nbuf[2] = {static_cast<char>('0' + cell.adjacent), '\0'};
          const int textX = x + (cellSize - renderer.getTextWidth(UI_10_FONT_ID, nbuf)) / 2;
          const int textY = y + (cellSize - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
          renderer.drawText(UI_10_FONT_ID, textX, textY, nbuf);
        }
      } else {
        renderer.fillRect(x + 1, y + 1, cellSize - 2, cellSize - 2);
        renderer.drawRect(x, y, cellSize, cellSize, false);

        if (cell.flagged) {
          const char* flag = "F";
          const int textX = x + (cellSize - renderer.getTextWidth(UI_10_FONT_ID, flag)) / 2;
          const int textY = y + (cellSize - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
          renderer.drawText(UI_10_FONT_ID, textX, textY, flag, false);
        }
      }

      if (row == cursorRow && col == cursorCol) {
        const bool cursorColor = cell.revealed;
        renderer.drawRect(x + 1, y + 1, cellSize - 2, cellSize - 2, 2, cursorColor);
      }
    }
  }

  const auto labels = mappedInput.mapLabels("\x11 Back", "Open/Flag", "Left", "Right");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, "Up", "Down");

  renderer.displayBuffer();
}
