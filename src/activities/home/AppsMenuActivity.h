#pragma once
#include <I18n.h>

#include <string>
#include <vector>

#include "../Activity.h"
#include "apps/AppManifest.h"
#include "util/ButtonNavigator.h"

class AppsMenuActivity final : public Activity {
 private:
  static constexpr unsigned long DELETE_ARM_MS = 1000;

  ButtonNavigator buttonNavigator;

  int selectorIndex = 0;

  std::vector<AppManifest> loadedApps;
  bool deleteArmed = false;
  int deleteArmedIndex = -1;
  bool skipNextConfirmRelease = false;
  std::string deleteStatus;

  void clearDeleteMode();
  void promptDeleteSelectedApp();
  void deleteAppAtIndex(size_t appIndex);

 public:
  explicit AppsMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AppsMenu", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
