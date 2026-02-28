#include "BookHighlightsAppActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* BOOK_HIGHLIGHTS_DIR = "/book-highlights";
constexpr size_t MAX_HIGHLIGHT_CHARS = 2048;
constexpr size_t MAX_METADATA_CHARS = 256;
constexpr size_t MAX_HISTORY_ITEMS = 24;
constexpr uint32_t WATCHDOG_RESET_BYTES = 4096;

struct CsvRecord {
  std::string highlight;
  std::string bookTitle;
  std::string bookAuthor;
  bool highlightTruncated = false;
  bool titleTruncated = false;
  bool authorTruncated = false;
};

void trim(std::string& s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])) != 0) {
    start++;
  }

  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
    end--;
  }

  if (start == 0 && end == s.size()) {
    return;
  }
  s = s.substr(start, end - start);
}

std::string normalizeToken(const std::string& value) {
  std::string normalized;
  normalized.reserve(value.size());
  for (char ch : value) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) != 0) {
      normalized.push_back(static_cast<char>(std::tolower(uch)));
    }
  }
  return normalized;
}

bool hasCsvExtension(const char* name) {
  if (!name) {
    return false;
  }
  const char* dot = std::strrchr(name, '.');
  if (!dot) {
    return false;
  }

  const char* ext = dot + 1;
  if (*ext == '\0') {
    return false;
  }

  const char csv[] = "csv";
  for (size_t i = 0; i < sizeof(csv) - 1; i++) {
    if (ext[i] == '\0') {
      return false;
    }
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[i])));
    if (lower != csv[i]) {
      return false;
    }
  }
  return ext[3] == '\0';
}

std::string fileNameFromPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash + 1 >= path.size()) {
    return path;
  }
  return path.substr(slash + 1);
}

bool isLikelyReadwiseHeader(const CsvRecord& row) {
  return normalizeToken(row.highlight) == "highlight" && normalizeToken(row.bookTitle) == "booktitle" &&
         normalizeToken(row.bookAuthor) == "bookauthor";
}

void maybeKickWatchdog(uint32_t& bytesSinceYield) {
  if (bytesSinceYield < WATCHDOG_RESET_BYTES) {
    return;
  }
  bytesSinceYield = 0;
  esp_task_wdt_reset();
  yield();
}

bool readNextCsvRecord(FsFile& file, CsvRecord& out, uint32_t& bytesSinceYield) {
  out.highlight.clear();
  out.bookTitle.clear();
  out.bookAuthor.clear();
  out.highlightTruncated = false;
  out.titleTruncated = false;
  out.authorTruncated = false;

  int fieldIndex = 0;
  bool inQuotes = false;
  bool quotePending = false;
  bool recordStarted = false;
  bool currentFieldHasData = false;

  const auto appendToField = [&](const char ch) {
    if (fieldIndex == 0) {
      if (out.highlight.size() < MAX_HIGHLIGHT_CHARS) {
        out.highlight.push_back(ch);
      } else {
        out.highlightTruncated = true;
      }
      return;
    }
    if (fieldIndex == 1) {
      if (out.bookTitle.size() < MAX_METADATA_CHARS) {
        out.bookTitle.push_back(ch);
      } else {
        out.titleTruncated = true;
      }
      return;
    }
    if (fieldIndex == 2) {
      if (out.bookAuthor.size() < MAX_METADATA_CHARS) {
        out.bookAuthor.push_back(ch);
      } else {
        out.authorTruncated = true;
      }
    }
  };

  while (file.available()) {
    const int byteValue = file.read();
    if (byteValue < 0) {
      break;
    }

    bytesSinceYield++;
    maybeKickWatchdog(bytesSinceYield);

    const char ch = static_cast<char>(byteValue);

    if (inQuotes) {
      if (quotePending) {
        if (ch == '"') {
          // Escaped quote ("") inside a quoted field.
          appendToField('"');
          currentFieldHasData = true;
          quotePending = false;
          recordStarted = true;
          continue;
        }

        // Previous quote closed the field, so process this byte as regular CSV syntax.
        inQuotes = false;
        quotePending = false;
      } else {
        if (ch == '"') {
          quotePending = true;
          recordStarted = true;
          continue;
        }

        appendToField(ch);
        currentFieldHasData = true;
        recordStarted = true;
        continue;
      }
    }

    if (ch == '"') {
      if (!currentFieldHasData) {
        inQuotes = true;
      } else {
        appendToField(ch);
      }
      currentFieldHasData = true;
      recordStarted = true;
      continue;
    }

    if (ch == ',') {
      fieldIndex++;
      currentFieldHasData = false;
      recordStarted = true;
      continue;
    }

    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      if (!recordStarted && !currentFieldHasData && fieldIndex == 0) {
        continue;
      }
      break;
    }

    appendToField(ch);
    currentFieldHasData = true;
    recordStarted = true;
  }

  if (!recordStarted && !currentFieldHasData && fieldIndex == 0) {
    return false;
  }

  if (out.highlight.size() >= 3 && static_cast<uint8_t>(out.highlight[0]) == 0xEF &&
      static_cast<uint8_t>(out.highlight[1]) == 0xBB && static_cast<uint8_t>(out.highlight[2]) == 0xBF) {
    out.highlight.erase(0, 3);
  }

  trim(out.highlight);
  trim(out.bookTitle);
  trim(out.bookAuthor);

  return true;
}
}  // namespace

void BookHighlightsAppActivity::onEnter() {
  Activity::onEnter();
  csvFiles.clear();
  history.clear();
  wrappedHighlight.clear();
  historyIndex = -1;
  isLoading = false;
  statusMessage.clear();

  scanCsvFiles();
  if (csvFiles.empty()) {
    if (statusMessage.empty()) {
      statusMessage = "No CSV files found in /book-highlights.";
    }
    requestUpdate();
    return;
  }

  loadNextRandomHighlight();
}

void BookHighlightsAppActivity::onExit() {
  csvFiles.clear();
  history.clear();
  wrappedHighlight.clear();
  Activity::onExit();
}

void BookHighlightsAppActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  if (isLoading) {
    return;
  }

  if (csvFiles.empty()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      scanCsvFiles();
      if (!csvFiles.empty()) {
        loadNextRandomHighlight();
      } else {
        requestUpdate();
      }
    }
    return;
  }

  buttonNavigator.onPreviousRelease([this] { showPreviousHighlight(); });

  const auto nextRandom = [this] { loadNextRandomHighlight(); };
  buttonNavigator.onNextRelease(nextRandom);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    nextRandom();
  }
}

void BookHighlightsAppActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = metrics.contentSidePadding;

  const char* title = manifest.name.empty() ? "Book Highlights" : manifest.name.c_str();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  if (isLoading || historyIndex < 0 || historyIndex >= static_cast<int>(history.size())) {
    const char* message = statusMessage.empty() ? "No highlights available." : statusMessage.c_str();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, message);

    const char* confirmLabel = csvFiles.empty() ? "Rescan" : "Random";
    const auto labels = mappedInput.mapLabels("« Home", confirmLabel, "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const auto& current = history[historyIndex];
  const int highlightLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int metaLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int metaY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - metaLineHeight * 2;

  int y = contentTop;
  for (const auto& line : wrappedHighlight) {
    if (y + highlightLineHeight > metaY - metrics.verticalSpacing) {
      break;
    }
    renderer.drawText(UI_12_FONT_ID, sidePadding, y, line.c_str());
    y += highlightLineHeight;
  }

  std::string position = std::to_string(historyIndex + 1) + "/" + std::to_string(history.size());
  const int positionWidth = renderer.getTextWidth(SMALL_FONT_ID, position.c_str());
  int titleMaxWidth = pageWidth - sidePadding * 2 - positionWidth - 10;
  if (titleMaxWidth < 120) {
    titleMaxWidth = pageWidth - sidePadding * 2;
    position.clear();
  }

  const std::string titleText = current.bookTitle.empty() ? "(Unknown title)" : current.bookTitle;
  std::string authorText = current.bookAuthor.empty() ? "(Unknown author)" : current.bookAuthor;
  if (!current.sourceFile.empty()) {
    authorText += " - ";
    authorText += current.sourceFile;
  }

  const std::string titleLine = renderer.truncatedText(SMALL_FONT_ID, titleText.c_str(), titleMaxWidth);
  const std::string authorLine = renderer.truncatedText(SMALL_FONT_ID, authorText.c_str(), pageWidth - sidePadding * 2);

  renderer.drawText(SMALL_FONT_ID, sidePadding, metaY, titleLine.c_str(), true, EpdFontFamily::BOLD);

  if (!position.empty()) {
    int posX = pageWidth - sidePadding - positionWidth;
    if (posX < sidePadding) {
      posX = sidePadding;
    }
    renderer.drawText(SMALL_FONT_ID, posX, metaY, position.c_str());
  }

  renderer.drawText(SMALL_FONT_ID, sidePadding, metaY + metaLineHeight, authorLine.c_str());

  const char* prevLabel = (historyIndex > 0) ? "Prev" : "";
  const auto labels = mappedInput.mapLabels("« Home", "Random", prevLabel, "Next");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void BookHighlightsAppActivity::scanCsvFiles() {
  csvFiles.clear();
  statusMessage.clear();

  if (!Storage.exists(BOOK_HIGHLIGHTS_DIR)) {
    statusMessage = "Create /book-highlights with CSV files.";
    return;
  }

  auto dir = Storage.open(BOOK_HIGHLIGHTS_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    statusMessage = "Could not open /book-highlights.";
    return;
  }

  dir.rewindDirectory();

  char name[256];
  uint32_t scannedEntries = 0;
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    scannedEntries++;
    if (!entry.isDirectory()) {
      name[0] = '\0';
      entry.getName(name, sizeof(name));
      if (hasCsvExtension(name)) {
        csvFiles.push_back(std::string(BOOK_HIGHLIGHTS_DIR) + "/" + name);
      }
    }
    entry.close();

    if ((scannedEntries & 0x0Fu) == 0u) {
      esp_task_wdt_reset();
      yield();
    }
  }

  dir.close();

  std::sort(csvFiles.begin(), csvFiles.end());
  LOG_DBG("BHAPP", "Found %d CSV file(s) in %s", static_cast<int>(csvFiles.size()), BOOK_HIGHLIGHTS_DIR);

  if (csvFiles.empty()) {
    statusMessage = "No CSV files found in /book-highlights.";
  }
}

void BookHighlightsAppActivity::loadNextRandomHighlight() {
  if (csvFiles.empty()) {
    statusMessage = "No CSV files found in /book-highlights.";
    requestUpdate();
    return;
  }

  isLoading = true;
  statusMessage = "Loading random highlight...";
  requestUpdate(true);

  HighlightRecord selected;
  const bool found = pickRandomHighlight(selected);

  isLoading = false;
  if (!found) {
    statusMessage = "No valid highlights found in CSV files.";
    requestUpdate();
    return;
  }

  statusMessage.clear();
  pushHistory(std::move(selected));
  refreshWrappedHighlight();
  requestUpdate();
}

bool BookHighlightsAppActivity::pickRandomHighlight(HighlightRecord& out) {
  CsvRecord row;
  uint32_t seenRecords = 0;
  uint32_t bytesSinceYield = 0;
  bool hasSelection = false;

  for (const auto& csvPath : csvFiles) {
    FsFile file;
    if (!Storage.openFileForRead("BHAPP", csvPath, file)) {
      LOG_ERR("BHAPP", "Failed to open CSV file: %s", csvPath.c_str());
      continue;
    }

    bool firstRecord = true;
    while (readNextCsvRecord(file, row, bytesSinceYield)) {
      if (firstRecord) {
        firstRecord = false;
        if (isLikelyReadwiseHeader(row)) {
          continue;
        }
      }

      if (row.highlight.empty()) {
        continue;
      }

      seenRecords++;
      if (!hasSelection || (esp_random() % seenRecords) == 0u) {
        out.highlight = row.highlight;
        if (row.highlightTruncated && out.highlight.size() + 3 <= MAX_HIGHLIGHT_CHARS) {
          out.highlight += "...";
        }
        out.bookTitle = row.bookTitle;
        out.bookAuthor = row.bookAuthor;
        out.sourceFile = fileNameFromPath(csvPath);
        hasSelection = true;
      }

      if ((seenRecords & 0x3Fu) == 0u) {
        esp_task_wdt_reset();
        yield();
      }
    }

    file.close();
    esp_task_wdt_reset();
    yield();
  }

  LOG_DBG("BHAPP", "Scanned %u highlight record(s) across %d CSV file(s)", seenRecords, static_cast<int>(csvFiles.size()));
  return hasSelection;
}

void BookHighlightsAppActivity::pushHistory(HighlightRecord&& record) {
  if (historyIndex >= 0 && historyIndex + 1 < static_cast<int>(history.size())) {
    history.erase(history.begin() + historyIndex + 1, history.end());
  }

  history.push_back(std::move(record));
  if (history.size() > MAX_HISTORY_ITEMS) {
    const size_t toDrop = history.size() - MAX_HISTORY_ITEMS;
    history.erase(history.begin(), history.begin() + static_cast<int>(toDrop));
  }

  historyIndex = static_cast<int>(history.size()) - 1;
}

void BookHighlightsAppActivity::refreshWrappedHighlight() {
  wrappedHighlight.clear();
  if (historyIndex < 0 || historyIndex >= static_cast<int>(history.size())) {
    return;
  }

  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = metrics.contentSidePadding;

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int metaLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int metaY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - metaLineHeight * 2;
  const int availableHeight = metaY - metrics.verticalSpacing - contentTop;

  int maxLines = availableHeight / lineHeight;
  if (maxLines < 1) {
    maxLines = 1;
  }

  const std::string fallback = "(empty highlight)";
  const std::string& source = history[historyIndex].highlight.empty() ? fallback : history[historyIndex].highlight;
  wrappedHighlight = renderer.wrappedText(UI_12_FONT_ID, source.c_str(), pageWidth - sidePadding * 2, maxLines);

  if (wrappedHighlight.empty()) {
    wrappedHighlight.push_back(fallback);
  }
}

void BookHighlightsAppActivity::showPreviousHighlight() {
  if (historyIndex <= 0) {
    return;
  }
  historyIndex--;
  refreshWrappedHighlight();
  requestUpdate();
}
