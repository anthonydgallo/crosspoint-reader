#pragma once

#include <string>
#include <cstddef>
#include <vector>

#include "../Activity.h"
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
class AppStoreActivity final : public Activity {
 public:
  enum class StoreState {
    CHECK_WIFI,
    LOADING,
    BROWSING,
    DOWNLOADING,
    DOWNLOAD_COMPLETE,
    ERROR
  };

  explicit AppStoreActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AppStore", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  StoreState state = StoreState::CHECK_WIFI;
  std::vector<RemoteApp> apps;
  std::vector<bool> selectedApps;
  int selectorIndex = 0;
  std::string errorMessage;
  std::string statusMessage;
  std::string completionMessage;
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;
  int lastRenderedPercent = -1;  // Track last rendered percentage for e-ink refresh throttling

  // When true, fetchAppList() runs on the next loop() iteration instead of
  // being called directly from a callback.  This avoids stack overflow: the
  // onComplete callback fires deep inside the WifiSelectionActivity call chain,
  // and adding TLS/HTTPS operations on top exceeds the 8 KB main-task stack.
  bool fetchPending = false;

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchAppList();
  bool installSingleApp(RemoteApp& app);
  void installApps(const std::vector<size_t>& appIndexes);
  std::vector<size_t> selectedInstallableIndexes() const;
  std::vector<size_t> allInstallableIndexes() const;
  void focusFirstInstallable();
  bool downloadFile(const std::string& url, const std::string& destPath);
  bool preventAutoSleep() override { return true; }
};
