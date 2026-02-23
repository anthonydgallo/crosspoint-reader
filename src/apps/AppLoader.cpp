#include "AppLoader.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

std::vector<AppManifest> AppLoader::scanApps() {
  std::vector<AppManifest> apps;

  if (!Storage.exists(APPS_DIR)) {
    LOG_DBG("APPS", "No /apps directory found on SD card");
    return apps;
  }

  auto appsDir = Storage.open(APPS_DIR);
  if (!appsDir || !appsDir.isDirectory()) {
    if (appsDir) appsDir.close();
    LOG_ERR("APPS", "Failed to open /apps directory");
    return apps;
  }

  appsDir.rewindDirectory();

  char name[256];
  for (auto entry = appsDir.openNextFile(); entry; entry = appsDir.openNextFile()) {
    if (!entry.isDirectory()) {
      entry.close();
      continue;
    }

    entry.getName(name, sizeof(name));

    // Skip hidden directories
    if (name[0] == '.') {
      entry.close();
      continue;
    }

    std::string appPath = std::string(APPS_DIR) + "/" + name;
    std::string manifestPath = appPath + "/" + MANIFEST_FILE;

    if (Storage.exists(manifestPath.c_str())) {
      AppManifest manifest;
      if (parseManifest(appPath, manifest)) {
        LOG_DBG("APPS", "Found app: %s (type: %s) at %s", manifest.name.c_str(), manifest.type.c_str(),
                manifest.path.c_str());
        apps.push_back(std::move(manifest));
      }
    }

    entry.close();
  }

  appsDir.close();

  // Sort apps alphabetically by name
  std::sort(apps.begin(), apps.end(),
            [](const AppManifest& a, const AppManifest& b) { return a.name < b.name; });

  LOG_DBG("APPS", "Found %d app(s)", static_cast<int>(apps.size()));
  return apps;
}

bool AppLoader::parseManifest(const std::string& appPath, AppManifest& out) {
  std::string manifestPath = appPath + "/" + MANIFEST_FILE;

  // Read manifest file into buffer
  char buffer[2048];
  size_t bytesRead = Storage.readFileToBuffer(manifestPath.c_str(), buffer, sizeof(buffer));
  if (bytesRead == 0) {
    LOG_ERR("APPS", "Failed to read manifest: %s", manifestPath.c_str());
    return false;
  }

  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, buffer, bytesRead);
  if (error) {
    LOG_ERR("APPS", "JSON parse error in %s: %s", manifestPath.c_str(), error.c_str());
    return false;
  }

  // Required fields
  const char* name = doc["name"];
  const char* type = doc["type"];
  if (!name || !type) {
    LOG_ERR("APPS", "Missing required fields (name/type) in %s", manifestPath.c_str());
    return false;
  }

  out.name = name;
  out.type = type;
  out.path = appPath;

  const char* version = doc["version"];
  if (version) {
    out.version = version;
  }

  // Parse entries for app types that use external text files
  if (out.type == "textviewer" || out.type == "randomquote" || out.type == "flashcard") {
    JsonArray entries = doc["entries"];
    if (entries) {
      for (JsonObject entry : entries) {
        const char* title = entry["title"];
        const char* file = entry["file"];
        if (title && file) {
          out.entries.push_back({title, file});
        }
      }
    }

    if (out.entries.empty()) {
      LOG_ERR("APPS", "App type %s in %s has no valid entries", out.type.c_str(), manifestPath.c_str());
      return false;
    }
  }

  return true;
}
