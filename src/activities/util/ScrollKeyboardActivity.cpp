#include "ScrollKeyboardActivity.h"

#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Character sets optimized for common text entry (WiFi passwords, URLs, filenames)
const char* const ScrollKeyboardActivity::charSets[NUM_CHAR_SETS] = {
    "abcdefghijklmnopqrstuvwxyz ",  // lowercase + space (27 chars)
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ ",  // uppercase + space (27 chars)
    "0123456789",                    // numbers (10 chars)
    ".-_@/:?!#$%^&*+=~",            // symbols (17 chars)
};

const char* const ScrollKeyboardActivity::charSetLabels[NUM_CHAR_SETS] = {"abc", "ABC", "123", ".@#"};

void ScrollKeyboardActivity::taskTrampoline(void* param) {
  auto* self = static_cast<ScrollKeyboardActivity*>(param);
  self->displayTaskLoop();
}

void ScrollKeyboardActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void ScrollKeyboardActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  updateRequired = true;

  xTaskCreate(&ScrollKeyboardActivity::taskTrampoline, "ScrollKeyboardActivity",
              2048,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void ScrollKeyboardActivity::onExit() {
  Activity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

int ScrollKeyboardActivity::getCharSetLength() const {
  return static_cast<int>(strlen(charSets[currentCharSet]));
}

char ScrollKeyboardActivity::getSelectedChar() const {
  const int len = getCharSetLength();
  if (selectedCharIndex < 0 || selectedCharIndex >= len) return '\0';
  return charSets[currentCharSet][selectedCharIndex];
}

void ScrollKeyboardActivity::loop() {
  // Scroll left through characters (with continuous hold for fast scrolling)
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] {
    const int len = getCharSetLength();
    selectedCharIndex = ButtonNavigator::previousIndex(selectedCharIndex, len);
    updateRequired = true;
  });

  // Scroll right through characters
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] {
    const int len = getCharSetLength();
    selectedCharIndex = ButtonNavigator::nextIndex(selectedCharIndex, len);
    updateRequired = true;
  });

  // Type the selected character
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    const char c = getSelectedChar();
    if (c != '\0' && (maxLength == 0 || text.length() < maxLength)) {
      text += c;
    }
    updateRequired = true;
  }

  // Backspace on initial press (also cancel if text is empty)
  buttonNavigator.onPress({MappedInputManager::Button::Back}, [this] {
    if (!text.empty()) {
      text.pop_back();
      updateRequired = true;
    } else if (onCancel) {
      onCancel();
      finish();
    } else {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    }
  });

  // Rapid backspace on continuous hold (but never cancel)
  buttonNavigator.onContinuous({MappedInputManager::Button::Back}, [this] {
    if (!text.empty()) {
      text.pop_back();
      updateRequired = true;
    }
  });

  // Cycle character set (Up volume button)
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    currentCharSet = (currentCharSet + 1) % NUM_CHAR_SETS;
    // Clamp selection if new set is shorter
    const int len = getCharSetLength();
    if (selectedCharIndex >= len) {
      selectedCharIndex = len - 1;
    }
    updateRequired = true;
  }

  // Done / Submit (Down volume button)
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (onComplete) {
      onComplete(text);
      finish();
    } else {
      setResult(KeyboardResult{text});
      finish();
    }
  }
}

void ScrollKeyboardActivity::render() const {
  const auto pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();

  // Draw title
  renderer.drawCenteredText(UI_10_FONT_ID, startY, title.c_str());

  // Draw input field
  const int inputStartY = startY + 22;
  int inputEndY = inputStartY;
  renderer.drawText(UI_10_FONT_ID, 10, inputStartY, "[");

  std::string displayText;
  if (isPassword) {
    displayText = std::string(text.length(), '*');
  } else {
    displayText = text;
  }
  displayText += "_";

  // Render input text across multiple lines if needed
  int lineStartIdx = 0;
  int lineEndIdx = static_cast<int>(displayText.length());
  while (true) {
    std::string lineText = displayText.substr(lineStartIdx, lineEndIdx - lineStartIdx);
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, lineText.c_str());
    if (textWidth <= pageWidth - 40) {
      renderer.drawText(UI_10_FONT_ID, 20, inputEndY, lineText.c_str());
      if (lineEndIdx == static_cast<int>(displayText.length())) {
        break;
      }
      inputEndY += renderer.getLineHeight(UI_10_FONT_ID);
      lineStartIdx = lineEndIdx;
      lineEndIdx = static_cast<int>(displayText.length());
    } else {
      lineEndIdx -= 1;
    }
  }
  renderer.drawText(UI_10_FONT_ID, pageWidth - 15, inputEndY, "]");

  // --- Character strip ---
  const char* chars = charSets[currentCharSet];
  const int len = getCharSetLength();

  constexpr int slotWidth = 22;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int stripY = inputEndY + 35;

  const int maxVisible = pageWidth / slotWidth;
  const bool allFit = (len <= maxVisible);
  const int visibleCount = allFit ? len : maxVisible;
  const int halfVisible = visibleCount / 2;

  const int stripStartX = (pageWidth - visibleCount * slotWidth) / 2;

  for (int i = 0; i < visibleCount; i++) {
    int charIdx;
    bool isSelected;

    if (allFit) {
      // All characters fit on screen - no scrolling needed
      charIdx = i;
      isSelected = (i == selectedCharIndex);
    } else {
      // Scrolling mode - center on selected character
      charIdx = (selectedCharIndex - halfVisible + i + len) % len;
      isSelected = (i == halfVisible);
    }

    const char c = chars[charIdx];
    std::string label(1, c);
    if (c == ' ') label = "_";  // Visual representation of space

    const int charWidth = renderer.getTextWidth(UI_10_FONT_ID, label.c_str());
    const int textX = stripStartX + i * slotWidth + (slotWidth - charWidth) / 2;

    if (isSelected) {
      // Draw inverted highlight: black rectangle with white text
      renderer.fillRect(stripStartX + i * slotWidth, stripY - 3, slotWidth - 2, lineHeight + 4);
      renderer.drawText(UI_10_FONT_ID, textX, stripY, label.c_str(), false);
    } else {
      renderer.drawText(UI_10_FONT_ID, textX, stripY, label.c_str());
    }
  }

  // Draw scroll arrows if not all characters fit
  if (!allFit) {
    const int arrowY = stripY;
    renderer.drawText(UI_10_FONT_ID, stripStartX - 16, arrowY, "<");
    renderer.drawText(UI_10_FONT_ID, stripStartX + visibleCount * slotWidth + 4, arrowY, ">");
  }

  // --- Character set indicator ---
  const int setY = stripY + lineHeight + 20;

  // Calculate total width of set labels for centering
  int totalLabelWidth = 0;
  constexpr int labelSpacing = 16;
  for (int i = 0; i < NUM_CHAR_SETS; i++) {
    totalLabelWidth += renderer.getTextWidth(UI_10_FONT_ID, charSetLabels[i]);
    if (i < NUM_CHAR_SETS - 1) totalLabelWidth += labelSpacing;
  }

  int setX = (pageWidth - totalLabelWidth) / 2;
  for (int i = 0; i < NUM_CHAR_SETS; i++) {
    const int labelWidth = renderer.getTextWidth(UI_10_FONT_ID, charSetLabels[i]);
    if (i == currentCharSet) {
      // Highlight active set with inverted style
      renderer.fillRect(setX - 3, setY - 2, labelWidth + 6, lineHeight + 3);
      renderer.drawText(UI_10_FONT_ID, setX, setY, charSetLabels[i], false);
    } else {
      renderer.drawText(UI_10_FONT_ID, setX, setY, charSetLabels[i]);
    }
    setX += labelWidth + labelSpacing;
  }

  // Draw button hints
  const auto labels = mappedInput.mapLabels("< Del", "Type", "< Prev", "Next >");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, "Mode", "OK");

  renderer.displayBuffer();
}
