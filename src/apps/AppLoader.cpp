#include "AppLoader.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <array>
#include <cstring>

#include "util/TaskWatchdog.h"

namespace {
bool isSupportedType(const std::string& type) {
  return type == "rosary" || type == "art" || type == "calculator" || type == "minesweeper" ||
         type == "textviewer" || type == "randomquote" || type == "bookhighlights" || type == "flashcard" ||
         type == "texteditor" || type == "imageviewer";
}

bool isSafeLeafName(const char* value) {
  if (!value || value[0] == '\0' || value[0] == '/' || strstr(value, "..") != nullptr) return false;
  return strchr(value, '/') == nullptr && strchr(value, '\\') == nullptr;
}
}  // namespace

std::vector<AppManifest> AppLoader::scanApps() {
  std::vector<AppManifest> apps;
  apps.reserve(16);

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
  size_t scanned = 0;
  for (auto entry = appsDir.openNextFile(); entry && apps.size() < MAX_APPS; entry = appsDir.openNextFile()) {
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

    if (!isSafeLeafName(name)) {
      entry.close();
      continue;
    }

    std::string appPath = std::string(APPS_DIR) + "/" + name;
    std::string manifestPath = appPath + "/" + MANIFEST_FILE;

    if (Storage.exists(manifestPath.c_str())) {
      AppManifest manifest;
      if (parseManifest(appPath, manifest, false)) {
        LOG_DBG("APPS", "Found app: %s (type: %s) at %s", manifest.name.c_str(), manifest.type.c_str(),
                manifest.path.c_str());
        apps.push_back(std::move(manifest));
      }
    }

    entry.close();
    if ((++scanned & 0x07U) == 0) {
      yield();
      resetTaskWatchdogIfSubscribed();
    }
  }

  appsDir.close();

  // Sort apps alphabetically by name
  std::sort(apps.begin(), apps.end(),
            [](const AppManifest& a, const AppManifest& b) { return a.name < b.name; });

  LOG_DBG("APPS", "Found %d app(s)", static_cast<int>(apps.size()));
  return apps;
}

bool AppLoader::loadManifest(const AppManifest& summary, AppManifest& out) {
  if (summary.path.rfind(std::string(APPS_DIR) + "/", 0) != 0 || summary.path == APPS_DIR) return false;
  return parseManifest(summary.path, out, true);
}

bool AppLoader::parseManifest(const std::string& appPath, AppManifest& out, const bool includeEntries) {
  std::string manifestPath = appPath + "/" + MANIFEST_FILE;

  HalFile manifestFile;
  if (!Storage.openFileForRead("APPS", manifestPath, manifestFile)) return false;
  const size_t manifestSize = manifestFile.size();
  manifestFile.close();
  if (manifestSize == 0 || manifestSize > MAX_MANIFEST_BYTES) {
    LOG_ERR("APPS", "Manifest has invalid size (%u): %s", static_cast<unsigned>(manifestSize), manifestPath.c_str());
    return false;
  }

  std::array<char, MAX_MANIFEST_BYTES + 1> buffer{};
  size_t bytesRead = Storage.readFileToBuffer(manifestPath.c_str(), buffer.data(), buffer.size());
  if (bytesRead == 0) {
    LOG_ERR("APPS", "Failed to read manifest: %s", manifestPath.c_str());
    return false;
  }

  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, buffer.data(), bytesRead);
  if (error) {
    LOG_ERR("APPS", "JSON parse error in %s: %s", manifestPath.c_str(), error.c_str());
    return false;
  }

  // Required fields
  const char* name = doc["name"];
  const char* type = doc["type"];
  if (!name || !type || name[0] == '\0' || !isSupportedType(type)) {
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

  if (!includeEntries) return true;

  // Parse entries only while opening an app, never while building the menu.
  if (out.type == "textviewer" || out.type == "randomquote" || out.type == "flashcard") {
    JsonArray entries = doc["entries"];
    if (entries) {
      for (JsonObject entry : entries) {
        const char* title = entry["title"];
        const char* file = entry["file"];
        if (title && title[0] != '\0' && isSafeLeafName(file) && out.entries.size() < MAX_ENTRIES) {
          out.entries.push_back({title, file});
        }
      }
    }

    if (out.entries.empty()) {
      LOG_ERR("APPS", "App %s (%s) has no valid entries", manifestPath.c_str(), out.type.c_str());
      return false;
    }
  }

  return true;
}
