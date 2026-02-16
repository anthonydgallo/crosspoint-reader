#pragma once

#include <functional>
#include <string>
#include <utility>

#include "CrossPointSettings.h"
#include "KeyboardEntryActivity.h"
#include "ScrollKeyboardActivity.h"

/**
 * Factory function that creates the appropriate keyboard activity based on
 * the user's keyboard style setting.
 *
 * Returns either a KeyboardEntryActivity (QWERTY grid) or
 * ScrollKeyboardActivity (scrolling character strip) as an Activity*.
 *
 * Both keyboard types have identical constructor signatures and callback
 * behavior, so callers can use this as a drop-in replacement for
 * `new KeyboardEntryActivity(...)`.
 */
inline Activity* createKeyboard(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                std::string title = "Enter Text", std::string initialText = "",
                                const int startY = 10, const size_t maxLength = 0, const bool isPassword = false,
                                KeyboardEntryActivity::OnCompleteCallback onComplete = nullptr,
                                KeyboardEntryActivity::OnCancelCallback onCancel = nullptr) {
  if (SETTINGS.keyboardStyle == CrossPointSettings::KEYBOARD_SCROLL) {
    return new ScrollKeyboardActivity(renderer, mappedInput, std::move(title), std::move(initialText), startY, maxLength,
                                      isPassword, std::move(onComplete), std::move(onCancel));
  }
  return new KeyboardEntryActivity(renderer, mappedInput, std::move(title), std::move(initialText), startY, maxLength,
                                   isPassword, std::move(onComplete), std::move(onCancel));
}
