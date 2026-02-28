#include "RandomQuoteAppActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <esp_system.h>

#include <cctype>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void RandomQuoteAppActivity::onEnter() {
  Activity::onEnter();
  loadQuotes();
  pickRandomQuote();
  requestUpdate();
}

void RandomQuoteAppActivity::onExit() { Activity::onExit(); }

void RandomQuoteAppActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const auto nextQuote = [this]() {
    pickRandomQuote();
    requestUpdate();
  };

  buttonNavigator.onNext(nextQuote);
  buttonNavigator.onPrevious(nextQuote);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    nextQuote();
  }
}

void RandomQuoteAppActivity::loadQuotes() {
  quotes.clear();

  for (const auto& entry : manifest.entries) {
    loadQuotesFromEntry(entry);
  }

  if (quotes.empty()) {
    quotes.push_back({"", "No quotes configured."});
  }
}

void RandomQuoteAppActivity::loadQuotesFromEntry(const AppManifest::Entry& entry) {
  const std::string filePath = manifest.path + "/" + entry.file;
  FsFile file;
  if (!Storage.openFileForRead("RQAPP", filePath, file)) {
    LOG_ERR("RQAPP", "Failed to open quote file: %s", filePath.c_str());
    return;
  }

  std::string line;
  while (file.available()) {
    const char ch = static_cast<char>(file.read());
    if (ch == '\r') {
      continue;
    }
    if (ch != '\n') {
      line.push_back(ch);
      continue;
    }

    trim(line);
    if (!line.empty() && line[0] != '#') {
      const auto splitPos = line.find('|');
      if (splitPos != std::string::npos) {
        std::string reference = line.substr(0, splitPos);
        std::string quoteText = line.substr(splitPos + 1);
        trim(reference);
        trim(quoteText);
        if (!quoteText.empty()) {
          quotes.push_back({reference, quoteText});
        }
      } else {
        quotes.push_back({entry.title, line});
      }
    }
    line.clear();
  }

  trim(line);
  if (!line.empty() && line[0] != '#') {
    const auto splitPos = line.find('|');
    if (splitPos != std::string::npos) {
      std::string reference = line.substr(0, splitPos);
      std::string quoteText = line.substr(splitPos + 1);
      trim(reference);
      trim(quoteText);
      if (!quoteText.empty()) {
        quotes.push_back({reference, quoteText});
      }
    } else {
      quotes.push_back({entry.title, line});
    }
  }

  file.close();
}

void RandomQuoteAppActivity::pickRandomQuote() {
  const int count = static_cast<int>(quotes.size());
  if (count < 1) {
    selectedIndex = -1;
    wrappedLines.clear();
    wrappedLines.push_back("No quotes available.");
    return;
  }

  int nextIndex = 0;
  if (count > 1) {
    nextIndex = static_cast<int>(esp_random() % count);
    if (nextIndex == selectedIndex) {
      nextIndex = (nextIndex + 1 + static_cast<int>(esp_random() % (count - 1))) % count;
    }
  }

  selectedIndex = nextIndex;
  wrapQuote(quotes[selectedIndex]);
}

void RandomQuoteAppActivity::wrapQuote(const Quote& quote) {
  wrappedLines.clear();
  auto metrics = UITheme::getInstance().getMetrics();
  const int maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  wrapText(quote.text.c_str(), UI_12_FONT_ID, maxWidth);
  if (wrappedLines.empty()) {
    wrappedLines.push_back("(empty quote)");
  }
}

void RandomQuoteAppActivity::wrapText(const char* text, int fontId, int maxWidth) {
  const int spaceWidth = renderer.getSpaceWidth(fontId);
  std::string currentLine;
  int currentWidth = 0;

  const char* pos = text;
  while (*pos != '\0') {
    if (*pos == '\n') {
      wrappedLines.push_back(currentLine);
      currentLine.clear();
      currentWidth = 0;
      pos++;
      continue;
    }

    const char* wordStart = pos;
    while (*pos != '\0' && *pos != ' ' && *pos != '\n') {
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
      wrappedLines.push_back(currentLine);
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
    wrappedLines.push_back(currentLine);
  }
}

void RandomQuoteAppActivity::trim(std::string& s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])) != 0) {
    start++;
  }

  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
    end--;
  }

  s = s.substr(start, end - start);
}

void RandomQuoteAppActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = metrics.contentSidePadding;

  const int quoteLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int refLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const bool hasReference = selectedIndex >= 0 && !quotes[selectedIndex].reference.empty();

  const int referencePad = hasReference ? (refLineHeight + metrics.verticalSpacing) : 0;
  const int quoteBlockHeight = static_cast<int>(wrappedLines.size()) * quoteLineHeight;
  const int availableHeight = pageHeight - referencePad;
  int quoteY = (availableHeight - quoteBlockHeight) / 2;
  if (quoteY < metrics.topPadding) {
    quoteY = metrics.topPadding;
  }

  for (const auto& line : wrappedLines) {
    int x = (pageWidth - renderer.getTextWidth(UI_12_FONT_ID, line.c_str())) / 2;
    if (x < sidePadding) {
      x = sidePadding;
    }
    renderer.drawText(UI_12_FONT_ID, x, quoteY, line.c_str());
    quoteY += quoteLineHeight;
  }

  if (hasReference) {
    const auto& reference = quotes[selectedIndex].reference;
    int refX = (pageWidth - renderer.getTextWidth(UI_10_FONT_ID, reference.c_str())) / 2;
    if (refX < sidePadding) {
      refX = sidePadding;
    }
    const int refY = pageHeight - refLineHeight - metrics.topPadding;
    renderer.drawText(UI_10_FONT_ID, refX, refY, reference.c_str());
  }

  renderer.displayBuffer();
}
