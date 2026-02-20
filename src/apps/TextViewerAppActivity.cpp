#include "TextViewerAppActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void TextViewerAppActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  showingText = false;
  requestUpdate();
}

void TextViewerAppActivity::onExit() { Activity::onExit(); }

void TextViewerAppActivity::loop() {
  if (showingText) {
    // Text viewing mode
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      showingText = false;
      lines.clear();
      scrollLine = 0;
      requestUpdate();
      return;
    }

    // Scroll down
    buttonNavigator.onNext([this] {
      if (scrollLine + linesPerPage < static_cast<int>(lines.size())) {
        scrollLine += linesPerPage;
        if (scrollLine + linesPerPage > static_cast<int>(lines.size())) {
          scrollLine = static_cast<int>(lines.size()) - linesPerPage;
          if (scrollLine < 0) scrollLine = 0;
        }
        requestUpdate();
      }
    });

    // Scroll up
    buttonNavigator.onPrevious([this] {
      if (scrollLine > 0) {
        scrollLine -= linesPerPage;
        if (scrollLine < 0) scrollLine = 0;
        requestUpdate();
      }
    });

    return;
  }

  // List mode
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const int entryCount = static_cast<int>(manifest.entries.size());

  buttonNavigator.onNext([this, entryCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, entryCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, entryCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, entryCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectorIndex < entryCount) {
      loadAndWrapText(selectorIndex);
      selectedEntry = selectorIndex;
      showingText = true;
      scrollLine = 0;
      requestUpdate();
    }
  }
}

void TextViewerAppActivity::loadAndWrapText(int entryIndex) {
  lines.clear();

  const auto& entry = manifest.entries[entryIndex];
  std::string filePath = manifest.path + "/" + entry.file;

  // Read the text file
  char buffer[4096];
  size_t bytesRead = Storage.readFileToBuffer(filePath.c_str(), buffer, sizeof(buffer));
  if (bytesRead == 0) {
    LOG_ERR("TVAPP", "Failed to read: %s", filePath.c_str());
    lines.push_back("Error: Could not load file.");
    return;
  }

  // Calculate available width for text
  auto metrics = UITheme::getInstance().getMetrics();
  const int sidePadding = metrics.contentSidePadding;
  const int maxWidth = renderer.getScreenWidth() - sidePadding * 2;

  wrapText(buffer, UI_10_FONT_ID, maxWidth);

  // Pre-calculate lines per page for scroll navigation
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int pageHeight = renderer.getScreenHeight();
  const int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int availableHeight = pageHeight - contentY - metrics.buttonHintsHeight - metrics.verticalSpacing;
  linesPerPage = availableHeight / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;
}

void TextViewerAppActivity::wrapText(const char* text, int fontId, int maxWidth) {
  const int spaceWidth = renderer.getSpaceWidth(fontId);
  std::string currentLine;
  int currentWidth = 0;

  const char* pos = text;
  while (*pos != '\0') {
    // Handle explicit newlines
    if (*pos == '\n') {
      lines.push_back(currentLine);
      currentLine.clear();
      currentWidth = 0;
      pos++;
      continue;
    }

    // Handle carriage return
    if (*pos == '\r') {
      pos++;
      continue;
    }

    // Find end of word
    const char* wordStart = pos;
    while (*pos != '\0' && *pos != ' ' && *pos != '\n' && *pos != '\r') {
      pos++;
    }

    int wordLen = pos - wordStart;
    if (wordLen == 0) {
      if (*pos == ' ') pos++;
      continue;
    }

    char wordBuf[256];
    int copyLen = (wordLen < 255) ? wordLen : 255;
    memcpy(wordBuf, wordStart, copyLen);
    wordBuf[copyLen] = '\0';

    int wordWidth = renderer.getTextWidth(fontId, wordBuf);

    // Check if word fits on current line
    if (!currentLine.empty() && currentWidth + spaceWidth + wordWidth > maxWidth) {
      lines.push_back(currentLine);
      currentLine = wordBuf;
      currentWidth = wordWidth;
    } else {
      if (!currentLine.empty()) {
        currentLine += " ";
        currentWidth += spaceWidth;
      }
      currentLine += wordBuf;
      currentWidth += wordWidth;
    }

    // Skip space after word
    if (*pos == ' ') pos++;
  }

  if (!currentLine.empty()) {
    lines.push_back(currentLine);
  }
}

void TextViewerAppActivity::render(Activity::RenderLock&&) {
  if (showingText) {
    renderText();
  } else {
    renderList();
  }
}

void TextViewerAppActivity::renderList() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, manifest.name.c_str());

  int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  int contentHeight = pageHeight - contentY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const auto& entries = manifest.entries;
  GUI.drawList(
      renderer, Rect{0, contentY, pageWidth, contentHeight}, static_cast<int>(entries.size()), selectorIndex,
      [&entries](int index) -> std::string { return entries[index].title; }, nullptr, nullptr, nullptr);

  const auto labels = mappedInput.mapLabels("\x11 Back", "View", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void TextViewerAppActivity::renderText() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();
  const int sidePadding = metrics.contentSidePadding;

  const auto& entry = manifest.entries[selectedEntry];
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, entry.title.c_str());

  int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int availableHeight = pageHeight - contentY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  int lpp = availableHeight / lineHeight;
  if (lpp < 1) lpp = 1;
  linesPerPage = lpp;

  // Draw visible lines
  int endLine = scrollLine + lpp;
  if (endLine > static_cast<int>(lines.size())) endLine = static_cast<int>(lines.size());

  for (int i = scrollLine; i < endLine; i++) {
    renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, lines[i].c_str());
    contentY += lineHeight;
  }

  // Page indicator
  int totalPages = (static_cast<int>(lines.size()) + lpp - 1) / lpp;
  if (totalPages < 1) totalPages = 1;
  int currentPage = (scrollLine / lpp) + 1;

  std::string pageStr = std::to_string(currentPage) + "/" + std::to_string(totalPages);
  const auto labels = mappedInput.mapLabels("\x11 Back", pageStr.c_str(), "Pg Up", "Pg Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
