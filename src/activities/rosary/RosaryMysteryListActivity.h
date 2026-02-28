#pragma once

#include "RosaryData.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class RosaryMysteryListActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;

  RosaryData::DayOfWeek day;
  RosaryData::MysterySet currentSet;
  bool showingAllSets = false;

 public:
  explicit RosaryMysteryListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     RosaryData::DayOfWeek day)
      : Activity("RosaryMysteries", renderer, mappedInput),
        day(day),
        currentSet(RosaryData::getMysterySetForDay(day)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
