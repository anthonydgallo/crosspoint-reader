#pragma once

#include <vector>

#include "AppManifest.h"

// Scans /apps/ directory on the SD card for valid app folders.
// Each subfolder must contain an app.json manifest file to be recognized.
class AppLoader {
 public:
  // Scan only the small metadata fields needed by the Apps menu. Full entry
  // arrays are deliberately deferred until an app is opened.
  static std::vector<AppManifest> scanApps();

  // Load and validate the complete manifest for a previously scanned app.
  static bool loadManifest(const AppManifest& summary, AppManifest& out);

 private:
  static constexpr const char* APPS_DIR = "/apps";
  static constexpr const char* MANIFEST_FILE = "app.json";
  static constexpr size_t MAX_APPS = 64;
  static constexpr size_t MAX_MANIFEST_BYTES = 2048;
  static constexpr size_t MAX_ENTRIES = 128;

  static bool parseManifest(const std::string& appPath, AppManifest& out, bool includeEntries);
};
