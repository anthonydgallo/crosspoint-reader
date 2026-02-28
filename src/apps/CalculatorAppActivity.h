#pragma once

#include "AppManifest.h"
#include "activities/Activity.h"

class CalculatorAppActivity final : public Activity {
  static constexpr int kDigitCount = 5;

  enum class Mode {
    EnterFirst,
    SelectOperator,
    EnterSecond,
    ShowResult,
  };

  static constexpr char kOperators[4] = {'+', '-', '*', '/'};

  int digits[kDigitCount] = {0, 0, 0, 0, 0};
  int digitCursor = 0;
  int operatorIndex = 0;
  long firstValue = 0;
  long secondValue = 0;
  long resultValue = 0;
  bool divideByZero = false;
  Mode mode = Mode::EnterFirst;

  const AppManifest manifest;

  void resetCalculator();
  void resetDigits();
  long digitsToValue() const;
  void setDigitsFromValue(long value);
  void cycleOperator(int delta);
  char currentOperator() const;
  void computeResult();

 public:
  explicit CalculatorAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const AppManifest& manifest)
      : Activity("Calculator", renderer, mappedInput), manifest(manifest) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
