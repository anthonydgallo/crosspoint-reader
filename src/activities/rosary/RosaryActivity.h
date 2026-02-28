#pragma once

#include "RosaryData.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class RosaryActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;

  void startRosary(RosaryData::DayOfWeek day);
  void showMysteryList(RosaryData::DayOfWeek day);
  void showPrayerReference();

 public:
  explicit RosaryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Rosary", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
