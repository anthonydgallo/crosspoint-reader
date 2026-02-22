#include "CalculatorAppActivity.h"

#include <GfxRenderer.h>

#include <cstdlib>
#include <string>

#include "components/UITheme.h"
#include "fontIds.h"

constexpr char CalculatorAppActivity::kOperators[4];

void CalculatorAppActivity::onEnter() {
  Activity::onEnter();
  resetCalculator();
  requestUpdate();
}

void CalculatorAppActivity::onExit() { Activity::onExit(); }

void CalculatorAppActivity::resetDigits() {
  for (int& digit : digits) {
    digit = 0;
  }
  digitCursor = kDigitCount - 1;
}

void CalculatorAppActivity::resetCalculator() {
  firstValue = 0;
  secondValue = 0;
  resultValue = 0;
  operatorIndex = 0;
  divideByZero = false;
  mode = Mode::EnterFirst;
  resetDigits();
}

long CalculatorAppActivity::digitsToValue() const {
  long value = 0;
  for (const int digit : digits) {
    value = value * 10 + digit;
  }
  return value;
}

void CalculatorAppActivity::setDigitsFromValue(long value) {
  if (value < 0) {
    value = std::labs(value);
  }

  for (int i = kDigitCount - 1; i >= 0; i--) {
    digits[i] = static_cast<int>(value % 10);
    value /= 10;
  }
}

void CalculatorAppActivity::cycleOperator(const int delta) {
  constexpr int opCount = sizeof(kOperators) / sizeof(kOperators[0]);
  operatorIndex = (operatorIndex + delta + opCount) % opCount;
}

char CalculatorAppActivity::currentOperator() const { return kOperators[operatorIndex]; }

void CalculatorAppActivity::computeResult() {
  divideByZero = false;

  switch (currentOperator()) {
    case '+':
      resultValue = firstValue + secondValue;
      break;
    case '-':
      resultValue = firstValue - secondValue;
      break;
    case '*':
      resultValue = firstValue * secondValue;
      break;
    case '/':
      if (secondValue == 0) {
        divideByZero = true;
        resultValue = 0;
      } else {
        resultValue = firstValue / secondValue;
      }
      break;
    default:
      resultValue = 0;
      break;
  }
}

void CalculatorAppActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  switch (mode) {
    case Mode::EnterFirst:
    case Mode::EnterSecond:
      if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
        digitCursor = (digitCursor - 1 + kDigitCount) % kDigitCount;
        requestUpdate();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
        digitCursor = (digitCursor + 1) % kDigitCount;
        requestUpdate();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
        digits[digitCursor] = (digits[digitCursor] + 1) % 10;
        requestUpdate();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        digits[digitCursor] = (digits[digitCursor] + 9) % 10;
        requestUpdate();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        if (mode == Mode::EnterFirst) {
          firstValue = digitsToValue();
          mode = Mode::SelectOperator;
        } else {
          secondValue = digitsToValue();
          computeResult();
          mode = Mode::ShowResult;
        }
        requestUpdate();
        return;
      }
      break;

    case Mode::SelectOperator:
      if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
          mappedInput.wasReleased(MappedInputManager::Button::Up)) {
        cycleOperator(-1);
        requestUpdate();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
          mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        cycleOperator(1);
        requestUpdate();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        mode = Mode::EnterSecond;
        resetDigits();
        requestUpdate();
        return;
      }
      break;

    case Mode::ShowResult:
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        firstValue = resultValue;
        secondValue = 0;
        divideByZero = false;
        setDigitsFromValue(0);
        mode = Mode::SelectOperator;
        requestUpdate();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
          mappedInput.wasReleased(MappedInputManager::Button::Right) ||
          mappedInput.wasReleased(MappedInputManager::Button::Up) ||
          mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        resetCalculator();
        requestUpdate();
        return;
      }
      break;
  }
}

void CalculatorAppActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, manifest.name.c_str());

  const int sidePadding = metrics.contentSidePadding;
  int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  const char* modeTitle = "";
  switch (mode) {
    case Mode::EnterFirst:
      modeTitle = "Set first value";
      break;
    case Mode::SelectOperator:
      modeTitle = "Select operator";
      break;
    case Mode::EnterSecond:
      modeTitle = "Set second value";
      break;
    case Mode::ShowResult:
      modeTitle = "Result";
      break;
  }
  renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, modeTitle);
  contentY += renderer.getLineHeight(UI_10_FONT_ID) + 8;

  std::string firstLine = "A = " + std::to_string(firstValue);
  renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, firstLine.c_str());
  contentY += renderer.getLineHeight(UI_10_FONT_ID) + 4;

  std::string opLine = "Op = ";
  opLine += currentOperator();
  renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, opLine.c_str());
  contentY += renderer.getLineHeight(UI_10_FONT_ID) + 4;

  if (mode == Mode::EnterSecond || mode == Mode::ShowResult) {
    std::string secondLine = "B = " + std::to_string(secondValue);
    renderer.drawText(UI_10_FONT_ID, sidePadding, contentY, secondLine.c_str());
    contentY += renderer.getLineHeight(UI_10_FONT_ID) + 4;
  }

  if (mode == Mode::ShowResult) {
    std::string resultLine = divideByZero ? "Result = undefined" : "Result = " + std::to_string(resultValue);
    renderer.drawText(UI_12_FONT_ID, sidePadding, contentY + 4, resultLine.c_str());
  } else if (mode == Mode::EnterFirst || mode == Mode::EnterSecond) {
    const int boxTop = contentY + 8;
    const int boxHeight = 46;
    const int boxWidth = 52;
    const int gap = 8;
    const int totalWidth = boxWidth * kDigitCount + gap * (kDigitCount - 1);
    const int startX = (pageWidth - totalWidth) / 2;

    for (int i = 0; i < kDigitCount; i++) {
      const int boxX = startX + i * (boxWidth + gap);
      const bool selected = (i == digitCursor);

      if (selected) {
        renderer.fillRect(boxX, boxTop, boxWidth, boxHeight);
        renderer.drawRect(boxX, boxTop, boxWidth, boxHeight, false);
      } else {
        renderer.drawRect(boxX, boxTop, boxWidth, boxHeight);
      }

      char digitText[2] = {static_cast<char>('0' + digits[i]), '\0'};
      const int textX = boxX + (boxWidth - renderer.getTextWidth(UI_12_FONT_ID, digitText)) / 2;
      const int textY = boxTop + (boxHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
      renderer.drawText(UI_12_FONT_ID, textX, textY, digitText, !selected);
    }
  } else if (mode == Mode::SelectOperator) {
    const int cx = pageWidth / 2;
    const int cy = contentY + 30;

    const std::string prev(1, kOperators[(operatorIndex + 3) % 4]);
    const std::string current(1, currentOperator());
    const std::string next(1, kOperators[(operatorIndex + 1) % 4]);

    renderer.drawText(UI_12_FONT_ID, cx - 70, cy, prev.c_str());
    renderer.drawText(UI_12_FONT_ID, cx - 8, cy, current.c_str());
    renderer.drawText(UI_12_FONT_ID, cx + 54, cy, next.c_str());
    renderer.drawRect(cx - 20, cy - 6, 40, 34);
  }

  const auto labels = [&]() {
    switch (mode) {
      case Mode::EnterFirst:
      case Mode::EnterSecond:
        return mappedInput.mapLabels("\x11 Back", "Next", "Digit", "Digit");
      case Mode::SelectOperator:
        return mappedInput.mapLabels("\x11 Back", "Use", "Prev", "Next");
      case Mode::ShowResult:
        return mappedInput.mapLabels("\x11 Back", "Chain", "Reset", "Reset");
    }
    return mappedInput.mapLabels("", "", "", "");
  }();

  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (mode == Mode::EnterFirst || mode == Mode::EnterSecond) {
    GUI.drawSideButtonHints(renderer, "+", "-");
  } else if (mode == Mode::SelectOperator) {
    GUI.drawSideButtonHints(renderer, "Prev", "Next");
  } else {
    GUI.drawSideButtonHints(renderer, "", "");
  }

  renderer.displayBuffer();
}
