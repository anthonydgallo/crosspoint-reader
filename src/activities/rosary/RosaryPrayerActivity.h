#pragma once

#include <EpdFontFamily.h>

#include <string>

#include "RosaryData.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class RosaryPrayerActivity final : public Activity {
  ButtonNavigator buttonNavigator;

  RosaryData::DayOfWeek day;
  RosaryData::MysterySet mysterySet;
  int currentStep = 0;

  // Text wrapping helper
  void drawWrappedText(int fontId, int x, int y, int maxWidth, int maxHeight, const char* text,
                       EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;

  // Get display info for current step
  std::string getStepTitle() const;
  std::string getStepSubtitle() const;
  const char* getStepPrayerText() const;
  std::string getProgressText() const;

  // Draw rosary bead visualization
  void drawBeadVisualization(int x, int y, int width, int height) const;

 public:
  explicit RosaryPrayerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, RosaryData::DayOfWeek day)
      : Activity("RosaryPrayer", renderer, mappedInput), day(day) {
    mysterySet = RosaryData::getMysterySetForDay(day);
  }
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
