#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../ActivityWithSubactivity.h"
#include "util/ButtonNavigator.h"

// Represents a remote app available for download from GitHub
struct RemoteApp {
  std::string name;       // Folder name on GitHub (e.g., "rosary")
  std::string displayName;  // Name parsed from app.json, or folder name as fallback
  bool installed;         // Whether this app is already on the SD card
};

/**
 * Activity for browsing and downloading apps from the CrossPoint GitHub repository.
 * Uses the GitHub Contents API to list available app folders, then downloads
 * all files for a selected app to /apps/<name>/ on the SD card.
 */
class AppStoreActivity final : public ActivityWithSubactivity {
 public:
  enum class StoreState {
    CHECK_WIFI,
    WIFI_SELECTION,
    LOADING,
    BROWSING,
    DOWNLOADING,
    DOWNLOAD_COMPLETE,
    ERROR
  };

  explicit AppStoreActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                            const std::function<void()>& onGoHome)
      : ActivityWithSubactivity("AppStore", renderer, mappedInput), onGoHome(onGoHome) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  StoreState state = StoreState::CHECK_WIFI;
  std::vector<RemoteApp> apps;
  int selectorIndex = 0;
  std::string errorMessage;
  std::string statusMessage;
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;
  int lastRenderedPercent = -1;  // Track last rendered percentage for e-ink refresh throttling

  const std::function<void()> onGoHome;

  // When true, fetchAppList() runs on the next loop() iteration instead of
  // being called directly from a callback.  This avoids stack overflow: the
  // onComplete callback fires deep inside the WifiSelectionActivity call chain,
  // and adding TLS/HTTPS operations on top exceeds the 8 KB main-task stack.
  bool fetchPending = false;

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchAppList();
  void installApp(const RemoteApp& app);
  bool downloadFile(const std::string& url, const std::string& destPath, size_t fileSize);
  bool preventAutoSleep() override { return true; }
};
