#pragma once
#include <I18n.h>

#include <functional>
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

  const std::function<void()> onGoHome;
  const std::function<void(const AppManifest& app)> onAppOpen;
  const std::function<void()> onAppStoreOpen;

 public:
  explicit AppsMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                            const std::function<void()>& onGoHome,
                            const std::function<void(const AppManifest& app)>& onAppOpen,
                            const std::function<void()>& onAppStoreOpen)
      : Activity("AppsMenu", renderer, mappedInput),
        onGoHome(onGoHome),
        onAppOpen(onAppOpen),
        onAppStoreOpen(onAppStoreOpen) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
};
