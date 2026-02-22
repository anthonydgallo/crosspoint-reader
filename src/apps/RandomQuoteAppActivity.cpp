#include "RandomQuoteAppActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <esp_system.h>

#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void RandomQuoteAppActivity::onEnter() {
  Activity::onEnter();
  selectedEntry = -1;
  pickRandomQuote();
  requestUpdate();
}

void RandomQuoteAppActivity::onExit() { Activity::onExit(); }

void RandomQuoteAppActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const auto showNextQuote = [this]() {
    pickRandomQuote();
    requestUpdate();
  };

  buttonNavigator.onNext(showNextQuote);
  buttonNavigator.onPrevious(showNextQuote);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    showNextQuote();
  }
}

void RandomQuoteAppActivity::pickRandomQuote() {
  const int quoteCount = static_cast<int>(manifest.entries.size());
  if (quoteCount < 1) {
    lines.clear();
    lines.push_back("No quotes configured.");
    quoteReference.clear();
    return;
  }

  int nextIndex = 0;
  if (quoteCount == 1) {
    nextIndex = 0;
  } else {
    nextIndex = static_cast<int>(esp_random() % quoteCount);
    if (nextIndex == selectedEntry) {
      nextIndex = (nextIndex + 1 + static_cast<int>(esp_random() % (quoteCount - 1))) % quoteCount;
    }
  }

  selectedEntry = nextIndex;
  loadAndWrapQuote(nextIndex);
}

void RandomQuoteAppActivity::loadAndWrapQuote(int entryIndex) {
  lines.clear();

  const auto& entry = manifest.entries[entryIndex];
  quoteReference = entry.title;
  const std::string filePath = manifest.path + "/" + entry.file;

  char buffer[1024];
  const size_t bytesRead = Storage.readFileToBuffer(filePath.c_str(), buffer, sizeof(buffer) - 1);
  if (bytesRead == 0) {
    LOG_ERR("RQAPP", "Failed to read quote file: %s", filePath.c_str());
    lines.push_back("Unable to load quote.");
    return;
  }

  buffer[bytesRead] = '\0';

  auto metrics = UITheme::getInstance().getMetrics();
  const int maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  wrapText(buffer, UI_12_FONT_ID, maxWidth);

  if (lines.empty()) {
    lines.push_back("(empty quote)");
  }
}

void RandomQuoteAppActivity::wrapText(const char* text, int fontId, int maxWidth) {
  const int spaceWidth = renderer.getSpaceWidth(fontId);
  std::string currentLine;
  int currentWidth = 0;

  const char* pos = text;
  while (*pos != '\0') {
    if (*pos == '\r') {
      pos++;
      continue;
    }

    if (*pos == '\n') {
      lines.push_back(currentLine);
      currentLine.clear();
      currentWidth = 0;
      pos++;
      continue;
    }

    const char* wordStart = pos;
    while (*pos != '\0' && *pos != ' ' && *pos != '\n' && *pos != '\r') {
      pos++;
    }

    const int wordLen = static_cast<int>(pos - wordStart);
    if (wordLen == 0) {
      if (*pos == ' ') {
        pos++;
      }
      continue;
    }

    char wordBuf[256];
    const int copyLen = (wordLen < 255) ? wordLen : 255;
    memcpy(wordBuf, wordStart, copyLen);
    wordBuf[copyLen] = '\0';

    const int wordWidth = renderer.getTextWidth(fontId, wordBuf);

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

    if (*pos == ' ') {
      pos++;
    }
  }

  if (!currentLine.empty()) {
    lines.push_back(currentLine);
  }
}

void RandomQuoteAppActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = metrics.contentSidePadding;

  const int quoteFont = UI_12_FONT_ID;
  const int quoteLineHeight = renderer.getLineHeight(quoteFont);
  const int refLineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  const int referenceMargin = quoteReference.empty() ? 0 : (refLineHeight + metrics.verticalSpacing);
  const int quoteBlockHeight = static_cast<int>(lines.size()) * quoteLineHeight;
  const int availableHeight = pageHeight - referenceMargin;
  int quoteY = (availableHeight - quoteBlockHeight) / 2;
  if (quoteY < metrics.topPadding) {
    quoteY = metrics.topPadding;
  }

  for (const auto& line : lines) {
    int x = (pageWidth - renderer.getTextWidth(quoteFont, line.c_str())) / 2;
    if (x < sidePadding) {
      x = sidePadding;
    }
    renderer.drawText(quoteFont, x, quoteY, line.c_str());
    quoteY += quoteLineHeight;
  }

  if (!quoteReference.empty()) {
    int refX = (pageWidth - renderer.getTextWidth(UI_10_FONT_ID, quoteReference.c_str())) / 2;
    if (refX < sidePadding) {
      refX = sidePadding;
    }
    const int refY = pageHeight - refLineHeight - metrics.topPadding;
    renderer.drawText(UI_10_FONT_ID, refX, refY, quoteReference.c_str());
  }

  renderer.displayBuffer();
}
