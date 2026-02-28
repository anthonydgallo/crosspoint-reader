#include "TextEditorAppActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "MappedInputManager.h"
#include "activities/util/KeyboardFactory.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StringUtils.h"

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void TextEditorAppActivity::onEnter() {
  Activity::onEnter();

  state = State::FILE_BROWSER;
  browsePath = manifest.path;

  // Ensure the app directory exists
  Storage.ensureDirectoryExists(browsePath.c_str());

  loadFiles();
  selectorIndex = 0;
  requestUpdate();
}

void TextEditorAppActivity::onExit() {
  Activity::onExit();
  files.clear();
  wrappedLines.clear();
  lineStartOffsets.clear();
  undoStack.clear();
  redoStack.clear();
  text.clear();
  savedText.clear();
}

// ---------------------------------------------------------------------------
// File browser
// ---------------------------------------------------------------------------

void TextEditorAppActivity::loadFiles() {
  files.clear();

  auto root = Storage.open(browsePath.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  root.rewindDirectory();

  char name[256];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));

    // Skip hidden files
    if (name[0] == '.') {
      file.close();
      continue;
    }

    if (!file.isDirectory()) {
      std::string filename(name);
      if (StringUtils::checkFileExtension(filename, ".txt")) {
        files.emplace_back(filename);
      }
    }
    file.close();
  }
  root.close();

  // Sort alphabetically
  std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
    // Case-insensitive compare
    std::string la = a, lb = b;
    std::transform(la.begin(), la.end(), la.begin(), ::tolower);
    std::transform(lb.begin(), lb.end(), lb.begin(), ::tolower);
    return la < lb;
  });
}

void TextEditorAppActivity::openFile(const std::string& filename) {
  std::string filePath = browsePath + "/" + filename;

  char buffer[MAX_FILE_SIZE];
  size_t bytesRead = Storage.readFileToBuffer(filePath.c_str(), buffer, sizeof(buffer));

  std::string content;
  if (bytesRead > 0) {
    content.assign(buffer, bytesRead);
    // Strip null terminators that readFileToBuffer may add
    while (!content.empty() && content.back() == '\0') {
      content.pop_back();
    }
  }

  enterEditor(filePath, content);
}

void TextEditorAppActivity::createNewFile() {
  // Use keyboard to get filename, then enter editor
  startActivityForResult(
      std::unique_ptr<Activity>(createKeyboard(
          renderer, mappedInput, "File Name", "", 10,
          60,     // max filename length
          false,  // not password
          nullptr, nullptr)),
      [this](const ActivityResult& res) {
        if (res.isCancelled) {
          requestUpdate();
          return;
        }

        auto* keyboardResult = std::get_if<KeyboardResult>(&res.data);
        if (!keyboardResult || keyboardResult->text.empty()) {
          requestUpdate();
          return;
        }

        const std::string& filename = keyboardResult->text;

        // Sanitize and add .txt extension if needed
        std::string sanitized = StringUtils::sanitizeFilename(filename);
        if (sanitized.empty()) {
          requestUpdate();
          return;
        }
        if (!StringUtils::checkFileExtension(sanitized, ".txt")) {
          sanitized += ".txt";
        }

        std::string filePath = browsePath + "/" + sanitized;
        enterEditor(filePath, "");
      });
}

// ---------------------------------------------------------------------------
// Editor core
// ---------------------------------------------------------------------------

void TextEditorAppActivity::enterEditor(const std::string& filePath, const std::string& content) {
  currentFilePath = filePath;

  // Extract filename for display
  auto pos = filePath.rfind('/');
  currentFileName = (pos != std::string::npos) ? filePath.substr(pos + 1) : filePath;

  text = content;
  savedText = content;
  cursorPos = static_cast<int>(text.size());
  scrollLine = 0;

  undoStack.clear();
  redoStack.clear();
  // Save initial state
  pushUndo();

  state = State::EDITING;
  rewrapText();
  ensureCursorVisible();
  requestUpdate();
}

void TextEditorAppActivity::saveFile() {
  // Ensure parent directory exists
  Storage.ensureDirectoryExists(browsePath.c_str());

  String content(text.c_str());
  if (Storage.writeFile(currentFilePath.c_str(), content)) {
    LOG_DBG("TXTEDIT", "Saved: %s (%d bytes)", currentFilePath.c_str(), static_cast<int>(text.size()));
    savedText = text;
  } else {
    LOG_ERR("TXTEDIT", "Failed to save: %s", currentFilePath.c_str());
  }
}

void TextEditorAppActivity::insertChar(char c) {
  if (static_cast<int>(text.size()) >= MAX_FILE_SIZE - 1) return;

  pushUndo();
  redoStack.clear();

  text.insert(text.begin() + cursorPos, c);
  cursorPos++;

  rewrapText();
  ensureCursorVisible();
}

void TextEditorAppActivity::deleteChar() {
  if (cursorPos <= 0) return;

  pushUndo();
  redoStack.clear();

  cursorPos--;
  text.erase(cursorPos, 1);

  rewrapText();
  ensureCursorVisible();
}

void TextEditorAppActivity::pushUndo() {
  // Don't push duplicate states
  if (!undoStack.empty() && undoStack.back().text == text && undoStack.back().cursorPos == cursorPos) {
    return;
  }

  undoStack.push_back({text, cursorPos});

  // Limit history size
  if (static_cast<int>(undoStack.size()) > MAX_UNDO_HISTORY) {
    undoStack.erase(undoStack.begin());
  }
}

void TextEditorAppActivity::undo() {
  if (undoStack.size() <= 1) return;  // Keep at least the initial state

  // Save current state to redo stack
  redoStack.push_back({text, cursorPos});

  // Pop and restore
  undoStack.pop_back();
  const auto& snapshot = undoStack.back();
  text = snapshot.text;
  cursorPos = snapshot.cursorPos;

  // Clamp cursor
  if (cursorPos > static_cast<int>(text.size())) cursorPos = static_cast<int>(text.size());

  rewrapText();
  ensureCursorVisible();
}

void TextEditorAppActivity::redo() {
  if (redoStack.empty()) return;

  const auto& snapshot = redoStack.back();

  // Save current state to undo stack
  undoStack.push_back({text, cursorPos});

  text = snapshot.text;
  cursorPos = snapshot.cursorPos;
  redoStack.pop_back();

  // Clamp cursor
  if (cursorPos > static_cast<int>(text.size())) cursorPos = static_cast<int>(text.size());

  rewrapText();
  ensureCursorVisible();
}

bool TextEditorAppActivity::hasUnsavedChanges() const { return text != savedText; }

// ---------------------------------------------------------------------------
// Text wrapping
// ---------------------------------------------------------------------------

void TextEditorAppActivity::rewrapText() {
  wrappedLines.clear();
  lineStartOffsets.clear();

  auto metrics = UITheme::getInstance().getMetrics();
  const int sidePadding = metrics.contentSidePadding;
  const int maxWidth = renderer.getScreenWidth() - sidePadding * 2;
  const int spaceWidth = renderer.getSpaceWidth(UI_10_FONT_ID);

  int textOffset = 0;
  const int textLen = static_cast<int>(text.size());

  // Process text character by character, wrapping on word boundaries or newlines
  while (textOffset <= textLen) {
    lineStartOffsets.push_back(textOffset);

    if (textOffset == textLen) {
      wrappedLines.emplace_back("");
      break;
    }

    // Handle newline
    if (text[textOffset] == '\n') {
      wrappedLines.emplace_back("");
      textOffset++;
      continue;
    }

    // Find how much text fits on this line
    std::string currentLine;
    int currentWidth = 0;
    int lineEndOffset = textOffset;

    while (lineEndOffset < textLen && text[lineEndOffset] != '\n') {
      // Try to add next word
      int wordStart = lineEndOffset;

      // Handle spaces
      if (text[lineEndOffset] == ' ') {
        if (!currentLine.empty()) {
          int testWidth = currentWidth + spaceWidth;
          if (testWidth > maxWidth) break;
          currentLine += ' ';
          currentWidth = testWidth;
        }
        lineEndOffset++;
        continue;
      }

      // Find end of word
      int wordEnd = lineEndOffset;
      while (wordEnd < textLen && text[wordEnd] != ' ' && text[wordEnd] != '\n') {
        wordEnd++;
      }

      // Measure word
      std::string word = text.substr(lineEndOffset, wordEnd - lineEndOffset);
      int wordWidth = renderer.getTextWidth(UI_10_FONT_ID, word.c_str());

      if (currentLine.empty()) {
        // First word on line - must include it even if too wide
        currentLine = word;
        currentWidth = wordWidth;
        lineEndOffset = wordEnd;
      } else {
        int testWidth = currentWidth + spaceWidth + wordWidth;
        if (testWidth > maxWidth) {
          // Word doesn't fit, break here
          break;
        }
        currentLine += ' ';
        currentLine += word;
        currentWidth = testWidth;
        lineEndOffset = wordEnd;
      }
    }

    wrappedLines.push_back(currentLine);
    textOffset = lineEndOffset;

    // Skip the newline character if that's what stopped us
    if (textOffset < textLen && text[textOffset] == '\n') {
      textOffset++;
    }
  }

  // Ensure at least one line exists
  if (wrappedLines.empty()) {
    wrappedLines.emplace_back("");
    lineStartOffsets.push_back(0);
  }

  // Calculate lines per page
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int pageHeight = renderer.getScreenHeight();
  const int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int availableHeight = pageHeight - contentY - metrics.buttonHintsHeight - metrics.verticalSpacing;
  linesPerPage = availableHeight / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;
}

int TextEditorAppActivity::getCursorLine() const {
  for (int i = static_cast<int>(lineStartOffsets.size()) - 1; i >= 0; i--) {
    if (cursorPos >= lineStartOffsets[i]) {
      return i;
    }
  }
  return 0;
}

void TextEditorAppActivity::ensureCursorVisible() {
  int cursorLine = getCursorLine();

  if (cursorLine < scrollLine) {
    scrollLine = cursorLine;
  } else if (cursorLine >= scrollLine + linesPerPage) {
    scrollLine = cursorLine - linesPerPage + 1;
  }

  if (scrollLine < 0) scrollLine = 0;
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

void TextEditorAppActivity::loop() {
  switch (state) {
    case State::FILE_BROWSER: {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        onGoHome();
        return;
      }

      // "New File" is the first entry; actual files follow
      const int totalItems = static_cast<int>(files.size()) + 1;

      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        if (selectorIndex == 0) {
          createNewFile();
        } else {
          openFile(files[selectorIndex - 1]);
        }
        return;
      }

      buttonNavigator.onNextRelease([this, totalItems] {
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
        requestUpdate();
      });

      buttonNavigator.onPreviousRelease([this, totalItems] {
        selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
        requestUpdate();
      });

      const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);

      buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
        selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
        requestUpdate();
      });

      buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
        selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
        requestUpdate();
      });
      break;
    }

    case State::EDITING: {
      // Back button = backspace (short press), quit (long press 1s+)
      if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= 1000) {
        if (hasUnsavedChanges()) {
          state = State::CONFIRM_QUIT;
          requestUpdate();
        } else {
          // No changes, go back to file browser
          state = State::FILE_BROWSER;
          loadFiles();
          selectorIndex = 0;
          requestUpdate();
        }
        return;
      }

      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        if (mappedInput.getHeldTime() < 1000) {
          deleteChar();
          requestUpdate();
        }
        return;
      }

      // Confirm = open keyboard to type
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        startActivityForResult(
            std::unique_ptr<Activity>(createKeyboard(
                renderer, mappedInput, "Type Text", "", 10,
                0,      // unlimited
                false,  // not password
                nullptr, nullptr)),
            [this](const ActivityResult& res) {
              if (!res.isCancelled) {
                auto* keyboardResult = std::get_if<KeyboardResult>(&res.data);
                if (keyboardResult) {
                  // Insert each character from the keyboard
                  for (char c : keyboardResult->text) {
                    insertChar(c);
                  }
                }
              }
              requestUpdate();
            });
        return;
      }

      // Up = undo / save combo
      // Short press Up = undo
      if (mappedInput.isPressed(MappedInputManager::Button::Up) && mappedInput.getHeldTime() >= 1000) {
        saveFile();
        requestUpdate();
        return;
      }

      if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
        if (mappedInput.getHeldTime() < 1000) {
          undo();
          requestUpdate();
        }
        return;
      }

      // Down = redo / newline combo
      // Short press Down = redo
      if (mappedInput.isPressed(MappedInputManager::Button::Down) && mappedInput.getHeldTime() >= 1000) {
        insertChar('\n');
        requestUpdate();
        return;
      }

      if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        if (mappedInput.getHeldTime() < 1000) {
          redo();
          requestUpdate();
        }
        return;
      }

      // Left = scroll up
      if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
        if (scrollLine > 0) {
          scrollLine -= linesPerPage;
          if (scrollLine < 0) scrollLine = 0;
          requestUpdate();
        }
        return;
      }

      // Right = scroll down
      if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
        if (scrollLine + linesPerPage < static_cast<int>(wrappedLines.size())) {
          scrollLine += linesPerPage;
          requestUpdate();
        }
        return;
      }

      break;
    }

    case State::CONFIRM_QUIT: {
      // Confirm = save and quit
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        saveFile();
        state = State::FILE_BROWSER;
        loadFiles();
        selectorIndex = 0;
        requestUpdate();
        return;
      }

      // Left = quit without saving
      if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
        state = State::FILE_BROWSER;
        loadFiles();
        selectorIndex = 0;
        requestUpdate();
        return;
      }

      // Back / Right = cancel, go back to editing
      if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
          mappedInput.wasReleased(MappedInputManager::Button::Right)) {
        state = State::EDITING;
        requestUpdate();
        return;
      }

      break;
    }
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void TextEditorAppActivity::render(RenderLock&&) {
  switch (state) {
    case State::FILE_BROWSER:
      renderFileBrowser();
      break;
    case State::EDITING:
      renderEditor();
      break;
    case State::CONFIRM_QUIT:
      renderConfirmQuit();
      break;
  }
}

void TextEditorAppActivity::renderFileBrowser() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, manifest.name.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const int totalItems = static_cast<int>(files.size()) + 1;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, totalItems, selectorIndex,
      [this](int index) -> std::string {
        if (index == 0) return "+ New File";
        return files[index - 1];
      },
      nullptr, nullptr, nullptr);

  const auto labels = mappedInput.mapLabels("« Back", tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void TextEditorAppActivity::renderEditor() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();
  const int sidePadding = metrics.contentSidePadding;

  // Header: show filename and modified indicator
  std::string header = currentFileName;
  if (hasUnsavedChanges()) {
    header += " *";
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, header.c_str());

  int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  // Draw visible lines
  int endLine = scrollLine + linesPerPage;
  if (endLine > static_cast<int>(wrappedLines.size())) {
    endLine = static_cast<int>(wrappedLines.size());
  }

  for (int i = scrollLine; i < endLine; i++) {
    renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, wrappedLines[i].c_str());

    // Draw cursor on the correct line
    int cursorLine = getCursorLine();
    if (i == cursorLine && i < static_cast<int>(lineStartOffsets.size())) {
      int lineStart = lineStartOffsets[i];
      int cursorInLine = cursorPos - lineStart;

      // Get text up to cursor position within this line
      std::string beforeCursor;
      if (cursorInLine > 0 && cursorInLine <= static_cast<int>(wrappedLines[i].size())) {
        beforeCursor = wrappedLines[i].substr(0, cursorInLine);
      } else if (cursorInLine > static_cast<int>(wrappedLines[i].size())) {
        beforeCursor = wrappedLines[i];
      }

      int cursorX = sidePadding;
      if (!beforeCursor.empty()) {
        cursorX += renderer.getTextWidth(UI_10_FONT_ID, beforeCursor.c_str());
      }

      // Draw cursor as a vertical line
      renderer.drawLine(cursorX, contentY, cursorX, contentY + lineHeight - 2);
      renderer.drawLine(cursorX + 1, contentY, cursorX + 1, contentY + lineHeight - 2);
    }

    contentY += lineHeight;
  }

  // Show page info and character count
  int totalLines = static_cast<int>(wrappedLines.size());
  int totalPages = (totalLines + linesPerPage - 1) / linesPerPage;
  if (totalPages < 1) totalPages = 1;
  int currentPage = (scrollLine / linesPerPage) + 1;

  std::string pageInfo = std::to_string(currentPage) + "/" + std::to_string(totalPages);

  // Draw status line with character count
  std::string charCount = std::to_string(text.size()) + " chars";
  renderer.drawText(UI_10_FONT_ID, sidePadding, pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - lineHeight,
                    charCount.c_str());

  const auto labels = mappedInput.mapLabels("Bksp / Quit", "Type", "Pg Up", "Pg Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  GUI.drawSideButtonHints(renderer, "Undo/Save", "Redo/Enter");

  renderer.displayBuffer();
}

void TextEditorAppActivity::renderConfirmQuit() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Unsaved Changes");

  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, "You have unsaved changes.", true);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, currentFileName.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 30, "What would you like to do?", true);

  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_SAVE), "Discard", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
