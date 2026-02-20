#pragma once

#include <vector>

#include "AppManifest.h"

// Scans /apps/ directory on the SD card for valid app folders.
// Each subfolder must contain an app.json manifest file to be recognized.
class AppLoader {
 public:
  // Scan the SD card /apps/ directory and return all valid app manifests.
  // Apps are sorted alphabetically by name.
  static std::vector<AppManifest> scanApps();

 private:
  static constexpr const char* APPS_DIR = "/apps";
  static constexpr const char* MANIFEST_FILE = "app.json";

  // Parse a single app.json file. Returns true on success.
  static bool parseManifest(const std::string& appPath, AppManifest& out);
};
