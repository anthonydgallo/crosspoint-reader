#include "FlashcardAppActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include <algorithm>
#include <cstring>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr uint8_t FLASHCARD_SAVE_VERSION = 1;
constexpr int MAX_CARD_TEXT_SIZE = 4096;
// Maximum number of new cards introduced per review session
constexpr int NEW_CARDS_PER_SESSION = 20;
}  // namespace

// ---------------------------------------------------------------------------
// Day tracking — uses millis() uptime divided into ~86400000ms "days".
// Since the ESP32 has no RTC, we use a monotonic counter persisted to disk.
// Each boot increments the day counter by 1 so reviews space out across
// power cycles even without a real clock.
// ---------------------------------------------------------------------------

static uint32_t s_bootDay = 0;
static bool s_bootDayInitialized = false;

uint32_t FlashcardAppActivity::currentDay() {
  if (!s_bootDayInitialized) {
    // Try to read persisted day counter
    constexpr char DAY_FILE[] = "/.crosspoint/flashcard_day.bin";
    FsFile file;
    if (Storage.openFileForRead("FC", DAY_FILE, file)) {
      uint32_t saved = 0;
      serialization::readPod(file, saved);
      file.close();
      s_bootDay = saved + 1;  // New boot = new day
    } else {
      s_bootDay = 1;
    }

    // Persist updated day
    Storage.mkdir("/.crosspoint");
    FsFile wf;
    if (Storage.openFileForWrite("FC", DAY_FILE, wf)) {
      serialization::writePod(wf, s_bootDay);
      wf.close();
    }

    s_bootDayInitialized = true;
  }
  return s_bootDay;
}

// ---------------------------------------------------------------------------
// Activity lifecycle
// ---------------------------------------------------------------------------

void FlashcardAppActivity::onEnter() {
  Activity::onEnter();
  today = currentDay();
  screen = Screen::DeckList;
  selectedDeck = 0;
  sessionReviewed = 0;
  requestUpdate();
}

void FlashcardAppActivity::onExit() {
  if (!cards.empty()) {
    saveState();
  }
  Activity::onExit();
}

// ---------------------------------------------------------------------------
// TSV parsing
// ---------------------------------------------------------------------------

bool FlashcardAppActivity::parseTsvCards(const char* data, size_t len) {
  cards.clear();

  const char* pos = data;
  const char* end = data + len;

  while (pos < end) {
    // Skip leading whitespace/blank lines
    while (pos < end && (*pos == '\r' || *pos == '\n')) {
      pos++;
    }
    if (pos >= end) break;

    // Find end of line
    const char* lineEnd = pos;
    while (lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r') {
      lineEnd++;
    }

    // Skip comment lines
    if (*pos == '#') {
      pos = lineEnd;
      continue;
    }

    // Find tab separator
    const char* tab = pos;
    while (tab < lineEnd && *tab != '\t') {
      tab++;
    }

    if (tab < lineEnd && tab > pos) {
      Card card;
      card.front = std::string(pos, tab - pos);
      card.back = std::string(tab + 1, lineEnd - (tab + 1));

      // Trim trailing \r from back if present
      while (!card.back.empty() && card.back.back() == '\r') {
        card.back.pop_back();
      }

      if (!card.front.empty()) {
        cards.push_back(std::move(card));
      }
    }

    pos = lineEnd;
  }

  LOG_DBG("FC", "Parsed %d cards", static_cast<int>(cards.size()));
  return !cards.empty();
}

// ---------------------------------------------------------------------------
// Deck loading
// ---------------------------------------------------------------------------

void FlashcardAppActivity::loadDeck(int entryIndex) {
  cards.clear();
  states.clear();
  reviewQueue.clear();
  reviewPos = 0;
  sessionReviewed = 0;
  browseIndex = 0;
  browseShowBack = false;

  const auto& entry = manifest.entries[entryIndex];
  deckName = entry.title;
  deckFilePath = manifest.path + "/" + entry.file;

  // Read TSV file
  char buffer[MAX_CARD_TEXT_SIZE];
  size_t bytesRead = Storage.readFileToBuffer(deckFilePath.c_str(), buffer, sizeof(buffer) - 1);
  if (bytesRead == 0) {
    LOG_ERR("FC", "Failed to read deck file: %s", deckFilePath.c_str());
    return;
  }
  buffer[bytesRead] = '\0';

  if (!parseTsvCards(buffer, bytesRead)) {
    LOG_ERR("FC", "No valid cards found in: %s", deckFilePath.c_str());
    return;
  }

  // Initialize states for all cards
  states.resize(cards.size());

  // Try to load saved state
  loadState();

  // If saved state has fewer cards than deck (new cards added), states vector
  // already has defaults for the new entries since we resize before loading.

  today = currentDay();
}

// ---------------------------------------------------------------------------
// Review queue
// ---------------------------------------------------------------------------

void FlashcardAppActivity::buildReviewQueue() {
  reviewQueue.clear();
  reviewPos = 0;

  // First add all due cards (cards with interval > 0 and dueDay <= today)
  for (int i = 0; i < static_cast<int>(cards.size()); i++) {
    const auto& st = states[i];
    if (st.interval > 0 && st.dueDay <= today) {
      reviewQueue.push_back(i);
    }
  }

  // Then add new cards (interval == 0), limited per session
  int newAdded = 0;
  for (int i = 0; i < static_cast<int>(cards.size()); i++) {
    if (newAdded >= NEW_CARDS_PER_SESSION) break;
    const auto& st = states[i];
    if (st.interval == 0) {
      reviewQueue.push_back(i);
      newAdded++;
    }
  }

  LOG_DBG("FC", "Review queue: %d cards (%d due + %d new)", static_cast<int>(reviewQueue.size()),
          static_cast<int>(reviewQueue.size()) - newAdded, newAdded);
}

// ---------------------------------------------------------------------------
// SM-2 rating
// ---------------------------------------------------------------------------

void FlashcardAppActivity::rateCard(int quality) {
  if (reviewPos >= static_cast<int>(reviewQueue.size())) return;

  int idx = reviewQueue[reviewPos];
  auto& st = states[idx];

  // SM-2 algorithm
  // quality: 0=Again, 1=Hard, 2=Good, 3=Easy
  // Map our 0-3 scale to SM-2's 0-5 scale: 0->0, 1->2, 2->3, 3->5
  int q;
  switch (quality) {
    case 0:
      q = 0;
      break;  // Again -> complete failure
    case 1:
      q = 2;
      break;  // Hard -> correct with difficulty
    case 2:
      q = 3;
      break;  // Good -> correct with hesitation
    case 3:
      q = 5;
      break;  // Easy -> perfect
    default:
      q = 3;
      break;
  }

  if (q < 3) {
    // Failed or struggled — reset repetitions
    st.repetitions = 0;
    st.interval = 1;

    if (quality == 0) {
      // Again: re-queue this card at the end of the current session
      reviewQueue.push_back(idx);
    }
  } else {
    // Successful recall
    st.repetitions++;
    if (st.repetitions == 1) {
      st.interval = 1;
    } else if (st.repetitions == 2) {
      st.interval = 6;
    } else {
      st.interval = static_cast<uint16_t>(static_cast<float>(st.interval) * st.easeFactor);
      if (st.interval < 1) st.interval = 1;
      if (st.interval > 365) st.interval = 365;  // Cap at 1 year
    }
  }

  // Update ease factor
  float ef = st.easeFactor + (0.1f - (5.0f - q) * (0.08f + (5.0f - q) * 0.02f));
  if (ef < 1.3f) ef = 1.3f;
  st.easeFactor = ef;

  // Set due date
  st.dueDay = today + st.interval;

  sessionReviewed++;
}

void FlashcardAppActivity::advanceReview() {
  reviewPos++;
  ratingCursor = 2;  // Default to "Good"

  if (reviewPos >= static_cast<int>(reviewQueue.size())) {
    screen = Screen::DeckDone;
    saveState();
  } else {
    screen = Screen::ReviewFront;
    // Pre-wrap front text
    wrappedFront.clear();
    auto metrics = UITheme::getInstance().getMetrics();
    int maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
    wrapText(cards[reviewQueue[reviewPos]].front, UI_12_FONT_ID, maxWidth, wrappedFront);
  }
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------

int FlashcardAppActivity::countDue() const {
  int count = 0;
  for (const auto& st : states) {
    if (st.interval > 0 && st.dueDay <= today) count++;
  }
  return count;
}

int FlashcardAppActivity::countNew() const {
  int count = 0;
  for (const auto& st : states) {
    if (st.interval == 0) count++;
  }
  return count;
}

int FlashcardAppActivity::countLearned() const {
  int count = 0;
  for (const auto& st : states) {
    if (st.interval > 0 && st.dueDay > today) count++;
  }
  return count;
}

// ---------------------------------------------------------------------------
// Text wrapping
// ---------------------------------------------------------------------------

void FlashcardAppActivity::wrapText(const std::string& text, int fontId, int maxWidth,
                                    std::vector<std::string>& out) {
  out.clear();
  const int spaceWidth = renderer.getSpaceWidth(fontId);
  std::string currentLine;
  int currentWidth = 0;

  const char* pos = text.c_str();
  while (*pos != '\0') {
    if (*pos == '\r') {
      pos++;
      continue;
    }
    if (*pos == '\n') {
      out.push_back(currentLine);
      currentLine.clear();
      currentWidth = 0;
      pos++;
      continue;
    }

    const char* wordStart = pos;
    while (*pos != '\0' && *pos != ' ' && *pos != '\n' && *pos != '\r' && *pos != '\t') {
      pos++;
    }

    int wordLen = static_cast<int>(pos - wordStart);
    if (wordLen == 0) {
      if (*pos == ' ' || *pos == '\t') pos++;
      continue;
    }

    char wordBuf[256];
    int copyLen = (wordLen < 255) ? wordLen : 255;
    memcpy(wordBuf, wordStart, copyLen);
    wordBuf[copyLen] = '\0';

    int wordWidth = renderer.getTextWidth(fontId, wordBuf);

    if (!currentLine.empty() && currentWidth + spaceWidth + wordWidth > maxWidth) {
      out.push_back(currentLine);
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

    if (*pos == ' ' || *pos == '\t') pos++;
  }

  if (!currentLine.empty()) {
    out.push_back(currentLine);
  }
}

// ---------------------------------------------------------------------------
// State persistence
// ---------------------------------------------------------------------------

std::string FlashcardAppActivity::getSaveFilePath() const {
  // Hash the deck file path to create a unique save file
  uint32_t hash = 5381;
  for (char c : deckFilePath) {
    hash = ((hash << 5) + hash) + static_cast<uint8_t>(c);
  }
  return "/.crosspoint/flashcards_" + std::to_string(hash) + ".bin";
}

bool FlashcardAppActivity::saveState() const {
  if (states.empty()) return false;

  Storage.mkdir("/.crosspoint");

  std::string path = getSaveFilePath();
  FsFile file;
  if (!Storage.openFileForWrite("FC", path.c_str(), file)) {
    LOG_ERR("FC", "Failed to open save file for writing: %s", path.c_str());
    return false;
  }

  serialization::writePod(file, FLASHCARD_SAVE_VERSION);
  uint32_t cardCount = static_cast<uint32_t>(states.size());
  serialization::writePod(file, cardCount);

  for (const auto& st : states) {
    serialization::writePod(file, st.easeFactor);
    serialization::writePod(file, st.interval);
    serialization::writePod(file, st.repetitions);
    serialization::writePod(file, st.dueDay);
  }

  file.close();
  LOG_DBG("FC", "Saved review state for %u cards", cardCount);
  return true;
}

bool FlashcardAppActivity::loadState() {
  std::string path = getSaveFilePath();
  FsFile file;
  if (!Storage.openFileForRead("FC", path.c_str(), file)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version != FLASHCARD_SAVE_VERSION) {
    LOG_ERR("FC", "Unknown save version %u", version);
    file.close();
    return false;
  }

  uint32_t savedCount;
  serialization::readPod(file, savedCount);

  // Load states for as many cards as we have (handles deck resizing)
  uint32_t loadCount = std::min(savedCount, static_cast<uint32_t>(states.size()));
  for (uint32_t i = 0; i < loadCount; i++) {
    serialization::readPod(file, states[i].easeFactor);
    serialization::readPod(file, states[i].interval);
    serialization::readPod(file, states[i].repetitions);
    serialization::readPod(file, states[i].dueDay);
  }

  // Skip remaining saved entries if deck shrank
  for (uint32_t i = loadCount; i < savedCount; i++) {
    CardState dummy;
    serialization::readPod(file, dummy.easeFactor);
    serialization::readPod(file, dummy.interval);
    serialization::readPod(file, dummy.repetitions);
    serialization::readPod(file, dummy.dueDay);
  }

  file.close();
  LOG_DBG("FC", "Loaded review state (%u saved, %u applied)", savedCount, loadCount);
  return true;
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

void FlashcardAppActivity::loop() {
  switch (screen) {
    // --- Deck list ---
    case Screen::DeckList: {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        onGoHome();
        return;
      }

      int deckCount = static_cast<int>(manifest.entries.size());

      deckNav.onNext([&]() {
        selectedDeck = ButtonNavigator::nextIndex(selectedDeck, deckCount);
        requestUpdate();
      });
      deckNav.onPrevious([&]() {
        selectedDeck = ButtonNavigator::previousIndex(selectedDeck, deckCount);
        requestUpdate();
      });

      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        loadDeck(selectedDeck);
        if (cards.empty()) {
          // Stay on deck list if loading failed
          requestUpdate();
        } else {
          screen = Screen::StudyMenu;
          requestUpdate();
        }
      }
      break;
    }

    // --- Study menu ---
    case Screen::StudyMenu: {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        saveState();
        cards.clear();
        states.clear();
        screen = Screen::DeckList;
        requestUpdate();
        return;
      }

      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        // Start review
        buildReviewQueue();
        if (reviewQueue.empty()) {
          screen = Screen::DeckDone;
        } else {
          reviewPos = 0;
          ratingCursor = 2;
          screen = Screen::ReviewFront;
          // Pre-wrap front text
          wrappedFront.clear();
          auto metrics = UITheme::getInstance().getMetrics();
          int maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
          wrapText(cards[reviewQueue[reviewPos]].front, UI_12_FONT_ID, maxWidth, wrappedFront);
        }
        requestUpdate();
        return;
      }

      // Right button -> Browse mode
      if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
          mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        browseIndex = 0;
        browseShowBack = false;
        screen = Screen::Browse;
        wrappedFront.clear();
        wrappedBack.clear();
        auto metrics = UITheme::getInstance().getMetrics();
        int maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
        wrapText(cards[browseIndex].front, UI_12_FONT_ID, maxWidth, wrappedFront);
        wrapText(cards[browseIndex].back, UI_12_FONT_ID, maxWidth, wrappedBack);
        requestUpdate();
      }
      break;
    }

    // --- Review front ---
    case Screen::ReviewFront: {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        saveState();
        screen = Screen::StudyMenu;
        requestUpdate();
        return;
      }

      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
          mappedInput.wasReleased(MappedInputManager::Button::Right) ||
          mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        // Reveal answer
        screen = Screen::ReviewBack;
        wrappedBack.clear();
        auto metrics = UITheme::getInstance().getMetrics();
        int maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
        wrapText(cards[reviewQueue[reviewPos]].back, UI_12_FONT_ID, maxWidth, wrappedBack);
        ratingCursor = 2;  // Default to Good
        requestUpdate();
      }
      break;
    }

    // --- Review back (rating) ---
    case Screen::ReviewBack: {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        // Go back to front without rating
        screen = Screen::ReviewFront;
        requestUpdate();
        return;
      }

      // Navigate rating buttons with Left/Right
      if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
          mappedInput.wasReleased(MappedInputManager::Button::Up)) {
        if (ratingCursor > 0) {
          ratingCursor--;
          requestUpdate();
        }
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
          mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        if (ratingCursor < 3) {
          ratingCursor++;
          requestUpdate();
        }
      }

      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        rateCard(ratingCursor);
        advanceReview();
        requestUpdate();
      }
      break;
    }

    // --- Deck done ---
    case Screen::DeckDone: {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
          mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        screen = Screen::StudyMenu;
        requestUpdate();
      }
      break;
    }

    // --- Browse ---
    case Screen::Browse: {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        screen = Screen::StudyMenu;
        requestUpdate();
        return;
      }

      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        browseShowBack = !browseShowBack;
        requestUpdate();
        return;
      }

      int totalCards = static_cast<int>(cards.size());

      browseNav.onNext([&]() {
        browseIndex = ButtonNavigator::nextIndex(browseIndex, totalCards);
        browseShowBack = false;
        wrappedFront.clear();
        wrappedBack.clear();
        auto metrics = UITheme::getInstance().getMetrics();
        int maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
        wrapText(cards[browseIndex].front, UI_12_FONT_ID, maxWidth, wrappedFront);
        wrapText(cards[browseIndex].back, UI_12_FONT_ID, maxWidth, wrappedBack);
        requestUpdate();
      });
      browseNav.onPrevious([&]() {
        browseIndex = ButtonNavigator::previousIndex(browseIndex, totalCards);
        browseShowBack = false;
        wrappedFront.clear();
        wrappedBack.clear();
        auto metrics = UITheme::getInstance().getMetrics();
        int maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
        wrapText(cards[browseIndex].front, UI_12_FONT_ID, maxWidth, wrappedFront);
        wrapText(cards[browseIndex].back, UI_12_FONT_ID, maxWidth, wrappedBack);
        requestUpdate();
      });
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void FlashcardAppActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto metrics = UITheme::getInstance().getMetrics();
  const int sidePadding = metrics.contentSidePadding;

  switch (screen) {
    // --- Deck list ---
    case Screen::DeckList: {
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, manifest.name.c_str());

      int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
      int deckCount = static_cast<int>(manifest.entries.size());

      if (deckCount == 0) {
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "No decks found.", true);
      } else {
        const int rowHeight = metrics.listRowHeight;

        for (int i = 0; i < deckCount; i++) {
          if (contentY + rowHeight > pageHeight - metrics.buttonHintsHeight) break;

          const bool selected = (i == selectedDeck);
          if (selected) {
            renderer.fillRect(sidePadding - 4, contentY, pageWidth - sidePadding * 2 + 8, rowHeight);
            renderer.drawText(UI_10_FONT_ID, sidePadding, contentY + 6, manifest.entries[i].title.c_str(), false);
          } else {
            renderer.drawText(UI_10_FONT_ID, sidePadding, contentY + 6, manifest.entries[i].title.c_str(), true);
          }

          contentY += rowHeight;
        }
      }

      const auto labels = mappedInput.mapLabels("« Back", "Open", "Up", "Down");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    // --- Study menu ---
    case Screen::StudyMenu: {
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, deckName.c_str());

      int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
      const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
      const int lineHeightLarge = renderer.getLineHeight(UI_12_FONT_ID);

      // Deck stats
      int totalCards = static_cast<int>(cards.size());
      int dueCards = countDue();
      int newCards = countNew();
      int learnedCards = countLearned();
      int toReview = std::min(newCards, NEW_CARDS_PER_SESSION) + dueCards;

      std::string totalLine = "Total cards: " + std::to_string(totalCards);
      renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, totalLine.c_str());
      contentY += lineHeight + 4;

      std::string dueLine = "Due for review: " + std::to_string(dueCards);
      renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, dueLine.c_str());
      contentY += lineHeight + 4;

      std::string newLine = "New cards: " + std::to_string(newCards);
      renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, newLine.c_str());
      contentY += lineHeight + 4;

      std::string learnedLine = "Learned: " + std::to_string(learnedCards);
      renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, learnedLine.c_str());
      contentY += lineHeight + metrics.verticalSpacing * 2;

      // Call to action
      if (toReview > 0) {
        std::string reviewLine = "Press Confirm to study " + std::to_string(toReview) + " cards";
        renderer.drawCenteredText(UI_12_FONT_ID, contentY, reviewLine.c_str(), true, EpdFontFamily::BOLD);
      } else {
        renderer.drawCenteredText(UI_12_FONT_ID, contentY, "All caught up!", true, EpdFontFamily::BOLD);
      }
      contentY += lineHeightLarge + 4;

      renderer.drawCenteredText(UI_10_FONT_ID, contentY, "Press Right/Down to browse all cards", true);

      const auto labels = mappedInput.mapLabels("« Back", "Study", "", "Browse");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    // --- Review front ---
    case Screen::ReviewFront: {
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, deckName.c_str());

      int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

      // Progress indicator
      std::string progress = std::to_string(reviewPos + 1) + " / " + std::to_string(reviewQueue.size());
      renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, progress.c_str());
      contentY += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;

      // Center the front text vertically in the remaining space
      const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
      const int textBlockHeight = static_cast<int>(wrappedFront.size()) * lineHeight;
      const int availableHeight = pageHeight - contentY - metrics.buttonHintsHeight - metrics.verticalSpacing;
      int textY = contentY + (availableHeight - textBlockHeight) / 2;
      if (textY < contentY) textY = contentY;

      for (const auto& line : wrappedFront) {
        int x = (pageWidth - renderer.getTextWidth(UI_12_FONT_ID, line.c_str())) / 2;
        if (x < sidePadding) x = sidePadding;
        renderer.drawText(UI_12_FONT_ID, x, textY, line.c_str(), true, EpdFontFamily::BOLD);
        textY += lineHeight;
      }

      const auto labels = mappedInput.mapLabels("« Back", "Reveal", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    // --- Review back (with rating) ---
    case Screen::ReviewBack: {
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, deckName.c_str());

      int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

      // Front text (smaller, at top)
      const int smallLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
      for (const auto& line : wrappedFront) {
        // Re-wrap at smaller font for the top section
        renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, line.c_str());
        contentY += smallLineHeight;
      }

      // Divider line
      contentY += 4;
      renderer.drawLine(sidePadding, contentY, pageWidth - sidePadding, contentY);
      contentY += 8;

      // Answer text (larger, centered)
      const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
      const int ratingAreaHeight = 70;  // Space for rating buttons at bottom
      const int textBlockHeight = static_cast<int>(wrappedBack.size()) * lineHeight;
      const int availableHeight =
          pageHeight - contentY - metrics.buttonHintsHeight - ratingAreaHeight - metrics.verticalSpacing;
      int textY = contentY + (availableHeight - textBlockHeight) / 2;
      if (textY < contentY) textY = contentY;

      for (const auto& line : wrappedBack) {
        int x = (pageWidth - renderer.getTextWidth(UI_12_FONT_ID, line.c_str())) / 2;
        if (x < sidePadding) x = sidePadding;
        renderer.drawText(UI_12_FONT_ID, x, textY, line.c_str());
        textY += lineHeight;
      }

      // Rating buttons at bottom
      const char* ratingLabels[] = {"Again", "Hard", "Good", "Easy"};
      const int buttonCount = 4;
      const int buttonSpacing = 8;
      const int totalButtonWidth = pageWidth - sidePadding * 2;
      const int buttonWidth = (totalButtonWidth - buttonSpacing * (buttonCount - 1)) / buttonCount;
      const int buttonHeight = 30;
      const int buttonY = pageHeight - metrics.buttonHintsHeight - buttonHeight - metrics.verticalSpacing;

      // Label above buttons
      renderer.drawCenteredText(UI_10_FONT_ID, buttonY - smallLineHeight - 4, "Rate your recall:", true);

      for (int i = 0; i < buttonCount; i++) {
        int bx = sidePadding + i * (buttonWidth + buttonSpacing);

        if (i == ratingCursor) {
          // Selected: filled black with white text
          renderer.fillRect(bx, buttonY, buttonWidth, buttonHeight);
          int textX = bx + (buttonWidth - renderer.getTextWidth(UI_10_FONT_ID, ratingLabels[i])) / 2;
          int textYPos = buttonY + (buttonHeight - smallLineHeight) / 2;
          renderer.drawText(UI_10_FONT_ID, textX, textYPos, ratingLabels[i], false);
        } else {
          // Unselected: outlined
          renderer.drawRect(bx, buttonY, buttonWidth, buttonHeight);
          int textX = bx + (buttonWidth - renderer.getTextWidth(UI_10_FONT_ID, ratingLabels[i])) / 2;
          int textYPos = buttonY + (buttonHeight - smallLineHeight) / 2;
          renderer.drawText(UI_10_FONT_ID, textX, textYPos, ratingLabels[i], true);
        }
      }

      const auto labels = mappedInput.mapLabels("« Flip", "Rate", "Prev", "Next");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    // --- Deck done ---
    case Screen::DeckDone: {
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, deckName.c_str());

      int centerY = pageHeight / 2 - 30;
      renderer.drawCenteredText(UI_12_FONT_ID, centerY, "Session complete!", true, EpdFontFamily::BOLD);
      centerY += renderer.getLineHeight(UI_12_FONT_ID) + 8;

      std::string statsLine = "Cards reviewed: " + std::to_string(sessionReviewed);
      renderer.drawCenteredText(UI_10_FONT_ID, centerY, statsLine.c_str(), true);
      centerY += renderer.getLineHeight(UI_10_FONT_ID) + 4;

      int remaining = countDue() + std::min(countNew(), NEW_CARDS_PER_SESSION);
      if (remaining > 0) {
        std::string moreLine = std::to_string(remaining) + " more cards available";
        renderer.drawCenteredText(UI_10_FONT_ID, centerY, moreLine.c_str(), true);
      } else {
        renderer.drawCenteredText(UI_10_FONT_ID, centerY, "All caught up! Come back later.", true);
      }

      const auto labels = mappedInput.mapLabels("« Back", "OK", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    // --- Browse ---
    case Screen::Browse: {
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Browse");

      int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

      // Card counter
      std::string counter =
          "Card " + std::to_string(browseIndex + 1) + " / " + std::to_string(static_cast<int>(cards.size()));
      renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, counter.c_str());
      contentY += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;

      // Show card state info
      const auto& st = states[browseIndex];
      std::string stateInfo;
      if (st.interval == 0) {
        stateInfo = "Status: New";
      } else if (st.dueDay <= today) {
        stateInfo = "Status: Due (interval " + std::to_string(st.interval) + "d)";
      } else {
        int daysLeft = static_cast<int>(st.dueDay - today);
        stateInfo = "Status: " + std::to_string(daysLeft) + "d until review";
      }
      renderer.drawText(SMALL_FONT_ID, sidePadding, contentY, stateInfo.c_str());
      contentY += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;

      // Divider
      renderer.drawLine(sidePadding, contentY, pageWidth - sidePadding, contentY);
      contentY += 8;

      // Front text
      const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
      renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, "Front:", true, EpdFontFamily::BOLD);
      contentY += renderer.getLineHeight(UI_10_FONT_ID) + 2;

      for (const auto& line : wrappedFront) {
        if (contentY + lineHeight > pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing) break;
        renderer.drawText(UI_12_FONT_ID, sidePadding, contentY, line.c_str());
        contentY += lineHeight;
      }
      contentY += metrics.verticalSpacing;

      // Back text (only if revealed)
      if (browseShowBack) {
        renderer.drawLine(sidePadding, contentY, pageWidth - sidePadding, contentY);
        contentY += 8;

        renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, "Back:", true, EpdFontFamily::BOLD);
        contentY += renderer.getLineHeight(UI_10_FONT_ID) + 2;

        for (const auto& line : wrappedBack) {
          if (contentY + lineHeight > pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing) break;
          renderer.drawText(UI_12_FONT_ID, sidePadding, contentY, line.c_str());
          contentY += lineHeight;
        }
      } else {
        renderer.drawCenteredText(UI_10_FONT_ID, contentY + metrics.verticalSpacing,
                                  "Press Confirm to reveal answer", true);
      }

      const auto labels = mappedInput.mapLabels("« Back", browseShowBack ? "Hide" : "Reveal", "Prev", "Next");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }
  }

  renderer.displayBuffer();
}
