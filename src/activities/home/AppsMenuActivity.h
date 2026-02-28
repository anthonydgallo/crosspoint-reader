#pragma once
#include <I18n.h>

#include <string>
#include <vector>

#include "../Activity.h"
#include "apps/AppManifest.h"
#include "util/ButtonNavigator.h"

class AppsMenuActivity final : public Activity {
 private:
  ButtonNavigator buttonNavigator;

  int selectorIndex = 0;

  std::vector<AppManifest> loadedApps;

 public:
  explicit AppsMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AppsMenu", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
