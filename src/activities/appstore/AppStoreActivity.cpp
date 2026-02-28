#include "AppStoreActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/GitHubRepoConfig.h"
#include "network/HttpDownloader.h"

namespace {
constexpr int PAGE_ITEMS = 15;
constexpr const char* INSTALL_ALL_LABEL = "Install all";

// Minimum free heap required for TLS connections (~40-50KB for TLS + working memory)
constexpr size_t MIN_FREE_HEAP_FOR_TLS = 60000;
// TLS handshakes also need a sufficiently large contiguous block; free heap
// alone can be misleading when the heap is fragmented after many installs.
constexpr size_t MIN_MAX_ALLOC_HEAP_FOR_TLS = 50000;
constexpr uint32_t INTER_APP_COOLDOWN_MS = 35;

bool hasSufficientTlsMemory(const char* phase) {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_TLS || maxAllocHeap < MIN_MAX_ALLOC_HEAP_FOR_TLS) {
    LOG_ERR("STORE", "Insufficient memory for %s (free: %u/%zu, max alloc: %u/%zu)", phase, freeHeap,
            MIN_FREE_HEAP_FOR_TLS, maxAllocHeap, MIN_MAX_ALLOC_HEAP_FOR_TLS);
    return false;
  }
  return true;
}

std::string toDisplayName(const std::string& folderName) {
  std::string display = folderName;
  bool capitalizeNext = true;
  for (char& ch : display) {
    if (ch == '-' || ch == '_') {
      ch = ' ';
      capitalizeNext = true;
      continue;
    }

    if (capitalizeNext && ch >= 'a' && ch <= 'z') {
      ch = static_cast<char>(ch - ('a' - 'A'));
    }

    capitalizeNext = (ch == ' ');
  }
  return display;
}

std::string appManifestUrl(const std::string& appName) {
  return GitHubRepoConfig::appManifestRawUrl(appName);
}
}  // namespace

void AppStoreActivity::onEnter() {
  Activity::onEnter();

  state = StoreState::CHECK_WIFI;
  apps.clear();
  selectedApps.clear();
  selectorIndex = 0;
  errorMessage.clear();
  completionMessage.clear();
  batchInstallProgress = 0;
  batchInstallTotal = 0;
  progressByFileCount = false;
  fetchPending = false;
  statusMessage = tr(STR_CHECKING_WIFI);
  requestUpdate();

  checkAndConnectWifi();
}

void AppStoreActivity::onExit() {
  Activity::onExit();

  WiFi.mode(WIFI_OFF);
  apps.clear();
  selectedApps.clear();
  completionMessage.clear();
  batchInstallProgress = 0;
  batchInstallTotal = 0;
  progressByFileCount = false;
}

void AppStoreActivity::loop() {
  if (state == StoreState::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        state = StoreState::LOADING;
        statusMessage = tr(STR_LOADING);
        requestUpdate();
        fetchAppList();
      } else {
        launchWifiSelection();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    }
    return;
  }

  if (state == StoreState::CHECK_WIFI || state == StoreState::LOADING) {
    // Process deferred fetch - runs with a clean, shallow call stack
    // instead of deep inside the WifiSelectionActivity callback chain
    if (state == StoreState::LOADING && fetchPending) {
      fetchPending = false;
      fetchAppList();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    }
    return;
  }

  if (state == StoreState::DOWNLOADING) {
    return;
  }

  if (state == StoreState::DOWNLOAD_COMPLETE) {
    if (mappedInput.wasAnyReleased()) {
      state = StoreState::BROWSING;
      completionMessage.clear();
      focusFirstInstallable();
      requestUpdate();
    }
    return;
  }

  // Browsing state
  if (state == StoreState::BROWSING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      const auto selectedIndexes = selectedInstallableIndexes();
      if (!selectedIndexes.empty()) {
        installApps(selectedIndexes);
      } else if (!apps.empty() && selectorIndex >= 0 && selectorIndex < static_cast<int>(apps.size()) &&
                 !apps[selectorIndex].installed) {
        installApps({static_cast<size_t>(selectorIndex)});
      }
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      if (!apps.empty() && selectorIndex >= 0 && selectorIndex < static_cast<int>(apps.size()) &&
          !apps[selectorIndex].installed && selectorIndex < static_cast<int>(selectedApps.size())) {
        selectedApps[selectorIndex] = !selectedApps[selectorIndex];
        requestUpdate();
      }
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      const auto installable = allInstallableIndexes();
      if (!installable.empty()) {
        installApps(installable);
      }
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
      return;
    }

    if (!apps.empty()) {
      buttonNavigator.onNextRelease([this] {
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, apps.size());
        requestUpdate();
      });

      buttonNavigator.onPreviousRelease([this] {
        selectorIndex = ButtonNavigator::previousIndex(selectorIndex, apps.size());
        requestUpdate();
      });

      buttonNavigator.onNextContinuous([this] {
        selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, apps.size(), PAGE_ITEMS);
        requestUpdate();
      });

      buttonNavigator.onPreviousContinuous([this] {
        selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, apps.size(), PAGE_ITEMS);
        requestUpdate();
      });
    }
  }
}

void AppStoreActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.drawCenteredText(UI_12_FONT_ID, 15, tr(STR_APP_STORE_TITLE), true, EpdFontFamily::BOLD);

  if (state == StoreState::CHECK_WIFI || state == StoreState::LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == StoreState::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == StoreState::DOWNLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, tr(STR_INSTALLING_APP));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, statusMessage.c_str());
    const int barWidth = pageWidth - 100;
    constexpr int barHeight = 20;
    constexpr int barX = 50;
    const int barY = pageHeight / 2 + 20;
    size_t barProgress = 0;
    size_t barTotal = 1;  // Always render a visible progress bar while downloading/preparing.
    if (downloadTotal > 0) {
      barProgress = downloadProgress;
      barTotal = downloadTotal;
    } else if (batchInstallTotal > 1) {
      barProgress = batchInstallProgress;
      barTotal = batchInstallTotal;
    }
    GUI.drawProgressBar(renderer, Rect{barX, barY, barWidth, barHeight}, barProgress, barTotal);
    renderer.displayBuffer();
    return;
  }

  if (state == StoreState::DOWNLOAD_COMPLETE) {
    const char* completionText = completionMessage.empty() ? tr(STR_INSTALL_COMPLETE) : completionMessage.c_str();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, completionText);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 20, tr(STR_PRESS_ANY_CONTINUE));
    renderer.displayBuffer();
    return;
  }

  // Browsing state
  const char* confirmLabel = "";
  bool hasSelectedApps = false;
  bool hasInstallableApps = false;
  const size_t count = std::min(apps.size(), selectedApps.size());
  for (size_t i = 0; i < apps.size(); i++) {
    if (!apps[i].installed) {
      hasInstallableApps = true;
      if (i < count && selectedApps[i]) {
        hasSelectedApps = true;
      }
    }
    if (hasSelectedApps && hasInstallableApps) {
      break;
    }
  }
  const bool canToggleSelection =
      !apps.empty() && selectorIndex >= 0 && selectorIndex < static_cast<int>(apps.size()) && !apps[selectorIndex].installed;
  if (hasSelectedApps || canToggleSelection) {
    confirmLabel = tr(STR_INSTALL);
  }
  const char* toggleLabel = canToggleSelection ? tr(STR_TOGGLE) : "";
  const char* installAllLabel = hasInstallableApps ? INSTALL_ALL_LABEL : "";
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, toggleLabel, installAllLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (apps.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NO_APPS_AVAILABLE));
    renderer.displayBuffer();
    return;
  }

  const auto pageStartIndex = selectorIndex / PAGE_ITEMS * PAGE_ITEMS;
  renderer.fillRect(0, 60 + (selectorIndex % PAGE_ITEMS) * 45 - 2, pageWidth - 1, 45);

  for (size_t i = pageStartIndex; i < apps.size() && i < static_cast<size_t>(pageStartIndex + PAGE_ITEMS); i++) {
    const auto& app = apps[i];

    std::string displayText = app.displayName;
    std::string statusText;
    if (app.installed) {
      statusText = std::string("  [") + tr(STR_APP_ALREADY_INSTALLED) + "]";
    } else if (i < selectedApps.size() && selectedApps[i]) {
      statusText = std::string("  [") + tr(STR_SELECTED) + "]";
    }

    auto nameItem = renderer.truncatedText(UI_10_FONT_ID, displayText.c_str(), pageWidth - 40);
    renderer.drawText(UI_10_FONT_ID, 20, 60 + (i % PAGE_ITEMS) * 45, nameItem.c_str(),
                      i != static_cast<size_t>(selectorIndex));

    if (!statusText.empty()) {
      renderer.drawText(SMALL_FONT_ID, 30, 60 + (i % PAGE_ITEMS) * 45 + 22, statusText.c_str(),
                        i != static_cast<size_t>(selectorIndex));
    }
  }

  renderer.displayBuffer();
}

void AppStoreActivity::fetchAppList() {
  LOG_DBG("STORE", "Fetching app list from GitHub (free heap: %d, max alloc: %d)", ESP.getFreeHeap(),
          ESP.getMaxAllocHeap());

  // Check heap before attempting TLS connection - the initial API call also
  // needs ~40-50KB for TLS, not just the per-manifest fetches checked below
  if (!hasSufficientTlsMemory("app list fetch")) {
    state = StoreState::ERROR;
    errorMessage = tr(STR_FETCH_FEED_FAILED);
    requestUpdate();
    return;
  }

  // Parse directory listing in its own scope so response + JsonDocument are freed
  // before we start making additional HTTPS requests for manifests
  {
    std::string response;
    const std::string listUrl = GitHubRepoConfig::appsApiUrl();
    LOG_DBG("STORE", "App list URL: %s", listUrl.c_str());
    if (!HttpDownloader::fetchUrl(listUrl, response)) {
      state = StoreState::ERROR;
      errorMessage = tr(STR_FETCH_FEED_FAILED);
      requestUpdate();
      return;
    }

    // Parse JSON array of directory entries
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
      LOG_ERR("STORE", "JSON parse error: %s", error.c_str());
      state = StoreState::ERROR;
      errorMessage = tr(STR_FETCH_FEED_FAILED);
      requestUpdate();
      return;
    }

    if (!doc.is<JsonArray>()) {
      LOG_ERR("STORE", "Expected JSON array from GitHub API");
      state = StoreState::ERROR;
      errorMessage = tr(STR_FETCH_FEED_FAILED);
      requestUpdate();
      return;
    }

    apps.clear();
    JsonArray entries = doc.as<JsonArray>();
    for (JsonObject entry : entries) {
      const char* type = entry["type"];
      const char* name = entry["name"];

      if (!type || !name) continue;

      // Only include directories (app folders)
      if (strcmp(type, "dir") != 0) continue;

      // Skip hidden directories
      if (name[0] == '.') continue;

      RemoteApp app;
      app.name = name;
      app.displayName = toDisplayName(app.name);  // Will be updated if we can fetch the manifest

      // Check if already installed
      std::string appPath = std::string("/apps/") + name;
      app.installed = Storage.exists(appPath.c_str());

      apps.push_back(std::move(app));
    }
  }  // response and doc are freed here before manifest fetches

  LOG_DBG("STORE", "Found %d app(s), fetching manifests (free heap: %d)", static_cast<int>(apps.size()),
          ESP.getFreeHeap());

  // Fetch display names from app.json manifests, but only if we have enough heap
  // for TLS connections. Each HTTPS request temporarily needs ~40-50KB for TLS.
  for (auto& app : apps) {
    esp_task_wdt_reset();
    if (!hasSufficientTlsMemory("manifest fetch")) {
      LOG_INF("STORE", "Low memory, skipping remaining manifest fetches");
      break;
    }

    const std::string manifestUrl = appManifestUrl(app.name);
    std::string manifestContent;
    if (HttpDownloader::fetchUrl(manifestUrl, manifestContent)) {
      JsonDocument manifestDoc;
      if (deserializeJson(manifestDoc, manifestContent) == DeserializationError::Ok) {
        const char* displayName = manifestDoc["name"];
        if (displayName) {
          app.displayName = displayName;
        }
      }
    }

    yield();  // Let FreeRTOS process other tasks between network requests
  }

  LOG_DBG("STORE", "App list ready: %d app(s) (free heap: %d)", static_cast<int>(apps.size()), ESP.getFreeHeap());

  if (apps.empty()) {
    state = StoreState::ERROR;
    errorMessage = tr(STR_NO_APPS_AVAILABLE);
    requestUpdate();
    return;
  }

  selectedApps.assign(apps.size(), false);
  completionMessage.clear();
  batchInstallProgress = 0;
  batchInstallTotal = 0;
  progressByFileCount = false;
  state = StoreState::BROWSING;
  focusFirstInstallable();
  requestUpdate();
}

void AppStoreActivity::installApps(const std::vector<size_t>& appIndexes) {
  if (appIndexes.empty()) return;

  batchInstallTotal = 0;
  for (const size_t appIndex : appIndexes) {
    if (appIndex < apps.size() && !apps[appIndex].installed) {
      batchInstallTotal++;
    }
  }
  batchInstallProgress = 0;

  if (batchInstallTotal == 0) return;

  size_t completedInstallCount = 0;
  completionMessage.clear();

  for (const size_t appIndex : appIndexes) {
    esp_task_wdt_reset();
    if (appIndex >= apps.size()) continue;
    auto& app = apps[appIndex];
    if (app.installed) continue;

    if (!hasSufficientTlsMemory("bulk install")) {
      state = StoreState::ERROR;
      errorMessage = std::string(tr(STR_INSTALL_FAILED)) + ": " + app.displayName;
      requestUpdate(true);
      return;
    }

    if (!installSingleApp(app)) {
      return;
    }

    app.installed = true;
    if (appIndex < selectedApps.size()) {
      selectedApps[appIndex] = false;
    }
    completedInstallCount++;
    batchInstallProgress = completedInstallCount;
    yield();
    delay(INTER_APP_COOLDOWN_MS);
  }

  if (completedInstallCount == 0) return;

  if (completedInstallCount == 1) {
    completionMessage = tr(STR_INSTALL_COMPLETE);
  } else {
    completionMessage = std::to_string(completedInstallCount) + " apps installed!";
  }

  state = StoreState::DOWNLOAD_COMPLETE;
  requestUpdate();
}

bool AppStoreActivity::installSingleApp(RemoteApp& app) {
  state = StoreState::DOWNLOADING;
  statusMessage = app.displayName;
  downloadProgress = 0;
  downloadTotal = 0;
  progressByFileCount = false;
  lastRenderedPercent = -1;
  requestUpdate(true);

  LOG_DBG("STORE", "Installing app: %s (free heap: %d, max alloc: %d)", app.name.c_str(), ESP.getFreeHeap(),
          ESP.getMaxAllocHeap());
  if (!hasSufficientTlsMemory("app install")) {
    state = StoreState::ERROR;
    errorMessage = std::string(tr(STR_INSTALL_FAILED)) + ": " + app.displayName;
    requestUpdate(true);
    return false;
  }

  // Collect file info in its own scope so the API response and JSON document
  // are freed before we start the download loop
  struct FileInfo {
    std::string name;
    std::string downloadUrl;
    size_t size;
  };
  std::vector<FileInfo> files;
  size_t totalSize = 0;

  {
    // Fetch the file list for this app folder
    const std::string apiUrl = GitHubRepoConfig::appFolderApiUrl(app.name);
    std::string response;
    if (!HttpDownloader::fetchUrl(apiUrl, response)) {
      state = StoreState::ERROR;
      errorMessage = std::string(tr(STR_INSTALL_FAILED)) + ": " + app.displayName;
      requestUpdate();
      return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error || !doc.is<JsonArray>()) {
      LOG_ERR("STORE", "Failed to parse app file list");
      state = StoreState::ERROR;
      errorMessage = std::string(tr(STR_INSTALL_FAILED)) + ": " + app.displayName;
      requestUpdate();
      return false;
    }

    // Calculate total download size and collect file info
    JsonArray entries = doc.as<JsonArray>();
    for (JsonObject entry : entries) {
      const char* type = entry["type"];
      const char* name = entry["name"];
      const char* downloadUrl = entry["download_url"];

      if (!type || !name) continue;

      // Only download files (skip subdirectories for now)
      if (strcmp(type, "file") != 0) continue;
      if (!downloadUrl) continue;

      size_t size = entry["size"] | 0;
      files.push_back({name, downloadUrl, size});
      totalSize += size;
    }
  }  // response and doc are freed here before downloads begin

  if (files.empty()) {
    LOG_ERR("STORE", "No files found in app folder");
    state = StoreState::ERROR;
    errorMessage = std::string(tr(STR_INSTALL_FAILED)) + ": " + app.displayName;
    requestUpdate();
    return false;
  }

  progressByFileCount = (totalSize == 0);
  downloadTotal = progressByFileCount ? files.size() : totalSize;
  downloadProgress = 0;
  requestUpdate(true);

  LOG_DBG("STORE", "Downloading %d file(s), %zu bytes total (free heap: %d, max alloc: %d)",
          static_cast<int>(files.size()), totalSize, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // Create the app directory on SD card
  std::string appDir = std::string("/apps/") + app.name;
  Storage.ensureDirectoryExists(appDir.c_str());

  // Download each file
  size_t downloadedSoFar = 0;
  for (const auto& file : files) {
    esp_task_wdt_reset();
    std::string destPath = appDir + "/" + file.name;
    LOG_DBG("STORE", "Downloading: %s (%zu bytes, free heap: %d, max alloc: %d)", file.name.c_str(), file.size,
            ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    if (!hasSufficientTlsMemory("file download")) {
      state = StoreState::ERROR;
      errorMessage = std::string(tr(STR_INSTALL_FAILED)) + ": " + app.displayName;
      requestUpdate(true);
      return false;
    }

    if (!downloadFile(file.downloadUrl, destPath, !progressByFileCount)) {
      // Clean up on failure
      LOG_ERR("STORE", "Failed to download: %s", file.name.c_str());
      state = StoreState::ERROR;
      errorMessage = std::string(tr(STR_INSTALL_FAILED)) + ": " + app.displayName;
      requestUpdate();
      return false;
    }

    downloadedSoFar += progressByFileCount ? 1 : file.size;
    downloadProgress = downloadedSoFar;

    // Refresh display at meaningful intervals for e-ink
    if (downloadTotal > 0) {
      int currentPercent = static_cast<int>((static_cast<uint64_t>(downloadProgress) * 100) / downloadTotal);
      int lastPercent10 = lastRenderedPercent / 10;
      int currentPercent10 = currentPercent / 10;
      if (currentPercent10 > lastPercent10 || currentPercent >= 100) {
        lastRenderedPercent = currentPercent;
        requestUpdate(true);
        delay(50);  // Brief delay to let render task process the update
      }
    }

    yield();  // Let FreeRTOS process other tasks between file downloads
  }

  LOG_DBG("STORE", "App installed successfully: %s (free heap: %d, max alloc: %d)", app.name.c_str(),
          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  return true;
}

std::vector<size_t> AppStoreActivity::selectedInstallableIndexes() const {
  std::vector<size_t> indexes;
  const size_t count = std::min(apps.size(), selectedApps.size());
  for (size_t i = 0; i < count; i++) {
    if (!apps[i].installed && selectedApps[i]) {
      indexes.push_back(i);
    }
  }
  return indexes;
}

std::vector<size_t> AppStoreActivity::allInstallableIndexes() const {
  std::vector<size_t> indexes;
  for (size_t i = 0; i < apps.size(); i++) {
    if (!apps[i].installed) {
      indexes.push_back(i);
    }
  }
  return indexes;
}

void AppStoreActivity::focusFirstInstallable() {
  for (size_t i = 0; i < apps.size(); i++) {
    if (!apps[i].installed) {
      selectorIndex = static_cast<int>(i);
      return;
    }
  }

  selectorIndex = 0;
}

bool AppStoreActivity::downloadFile(const std::string& url, const std::string& destPath, const bool trackByteProgress) {
  const size_t prevProgress = downloadProgress;

  auto result = HttpDownloader::downloadToFile(
      url, destPath, [this, prevProgress, trackByteProgress](size_t downloaded, size_t /*total*/) {
        if (!trackByteProgress) {
          return;
        }

        // Update overall progress based on this file's contribution
        downloadProgress = prevProgress + downloaded;

        // Throttle e-ink refreshes: only refresh every 10% of total progress
        if (downloadTotal > 0) {
          int currentPercent = static_cast<int>((static_cast<uint64_t>(downloadProgress) * 100) / downloadTotal);
          int currentPercent10 = currentPercent / 10;
          int lastPercent10 = lastRenderedPercent / 10;
          if (currentPercent10 > lastPercent10) {
            lastRenderedPercent = currentPercent;
            requestUpdate(true);
          }
        }
      });

  return result == HttpDownloader::OK;
}

void AppStoreActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = StoreState::LOADING;
    statusMessage = tr(STR_FETCHING_APPS);
    // Defer fetch to loop() so it runs with a clean, shallow call stack
    fetchPending = true;
    requestUpdate();
    return;
  }

  launchWifiSelection();
}

void AppStoreActivity::launchWifiSelection() {
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void AppStoreActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    LOG_DBG("STORE", "WiFi connected, deferring app list fetch to loop (free heap: %d)", ESP.getFreeHeap());
    state = StoreState::LOADING;
    statusMessage = tr(STR_FETCHING_APPS);
    // CRITICAL: Do NOT call fetchAppList() here.  This result handler fires
    // from the ActivityManager after the WifiSelectionActivity finishes.
    // Adding HTTPS/TLS operations (which need several KB of stack for mbedTLS)
    // may overflow the stack on ESP32-C3.
    // Setting fetchPending defers the work to the next loop() iteration where
    // the call stack is shallow: main loop → AppStore::loop → fetchAppList.
    fetchPending = true;
    requestUpdate();
  } else {
    LOG_DBG("STORE", "WiFi selection cancelled/failed");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    state = StoreState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}
