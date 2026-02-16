#pragma once
#include <GfxRenderer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <utility>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Scroll-based keyboard entry activity optimized for 6-button navigation.
 *
 * Instead of a full QWERTY grid, characters are displayed in a single
 * horizontal scrolling strip. This dramatically reduces the number of
 * button presses needed to type each character.
 *
 * Button mapping:
 *   Left/Right  - Scroll through characters (with continuous hold)
 *   Confirm     - Type the selected character
 *   Back        - Backspace (hold for rapid delete), Cancel when empty
 *   Up (volume) - Cycle character set (lowercase/uppercase/numbers/symbols)
 *   Down (vol.) - Done / Submit text
 *
 * Has the same constructor signature as KeyboardEntryActivity for
 * drop-in replacement via KeyboardFactory.
 */
class ScrollKeyboardActivity : public Activity {
 public:
  using OnCompleteCallback = std::function<void(const std::string&)>;
  using OnCancelCallback = std::function<void()>;

  explicit ScrollKeyboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                  std::string title = "Enter Text", std::string initialText = "",
                                  const int startY = 10, const size_t maxLength = 0, const bool isPassword = false,
                                  OnCompleteCallback onComplete = nullptr, OnCancelCallback onCancel = nullptr)
      : Activity("ScrollKeyboard", renderer, mappedInput),
        title(std::move(title)),
        text(std::move(initialText)),
        startY(startY),
        maxLength(maxLength),
        isPassword(isPassword),
        onComplete(std::move(onComplete)),
        onCancel(std::move(onCancel)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  std::string title;
  int startY;
  std::string text;
  size_t maxLength;
  bool isPassword;
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  ButtonNavigator buttonNavigator{150, 300};  // Fast scrolling: 300ms start, 150ms repeat
  bool updateRequired = false;

  // Scroll keyboard state
  int selectedCharIndex = 0;
  int currentCharSet = 0;

  // Callbacks
  OnCompleteCallback onComplete;
  OnCancelCallback onCancel;

  // Character sets
  static constexpr int NUM_CHAR_SETS = 4;
  static const char* const charSets[NUM_CHAR_SETS];
  static const char* const charSetLabels[NUM_CHAR_SETS];

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();

  int getCharSetLength() const;
  char getSelectedChar() const;
  void render() const;
};
