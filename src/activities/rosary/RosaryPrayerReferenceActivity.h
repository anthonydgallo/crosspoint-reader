#pragma once

#include <EpdFontFamily.h>

#include "RosaryData.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class RosaryPrayerReferenceActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool showingPrayerText = false;
  int selectedPrayer = 0;

  void drawWrappedText(int fontId, int x, int y, int maxWidth, int maxHeight, const char* text,
                       EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;

 public:
  explicit RosaryPrayerReferenceActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RosaryPrayers", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
