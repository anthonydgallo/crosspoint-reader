#include "OtaUpdater.h"

#include <ArduinoJson.h>
#include <Logging.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "network/GitHubRepoConfig.h"
#include "network/HttpDownloader.h"

#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_wifi.h"

namespace {
/*
 * When esp_crt_bundle.h included, it is pointing wrong header file
 * which is something under WifiClientSecure because of our framework based on arduino platform.
 * To manage this obstacle, don't include anything, just extern and it will point correct one.
 */
extern "C" {
extern esp_err_t esp_crt_bundle_attach(void* conf);
}

esp_err_t http_client_set_header_cb(esp_http_client_handle_t http_client) {
  return esp_http_client_set_header(http_client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
}

bool parseSemver(const char* version, int& major, int& minor, int& patch) {
  if (!version) {
    return false;
  }

  while (*version != '\0' && !std::isdigit(static_cast<unsigned char>(*version))) {
    version++;
  }

  return std::sscanf(version, "%d.%d.%d", &major, &minor, &patch) == 3;
}

bool isPreReleaseVersion(const char* version) {
  if (!version) {
    return false;
  }
  return std::strstr(version, "-rc") != nullptr || std::strstr(version, "-dev") != nullptr ||
         std::strstr(version, "-slim") != nullptr;
}

bool isPreReleaseVersion(const std::string& version) { return isPreReleaseVersion(version.c_str()); }
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  updateAvailable = false;
  latestVersion.clear();
  otaUrl.clear();
  otaSize = 0;
  processedSize = 0;
  totalSize = 0;

  const std::string releaseUrl = GitHubRepoConfig::latestReleaseApiUrl();
  LOG_DBG("OTA", "Checking latest release: %s", releaseUrl.c_str());

  std::string response;
  if (!HttpDownloader::fetchUrl(releaseUrl, response)) {
    LOG_ERR("OTA", "Failed to fetch release metadata");
    return HTTP_ERROR;
  }

  JsonDocument filter;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  filter["assets"][0]["size"] = true;

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, response, DeserializationOption::Filter(filter));
  if (error) {
    LOG_ERR("OTA", "JSON parse failed: %s", error.c_str());
    return JSON_PARSE_ERROR;
  }

  const char* tagName = doc["tag_name"];
  if (!tagName || tagName[0] == '\0') {
    LOG_ERR("OTA", "No tag_name found in latest release");
    return JSON_PARSE_ERROR;
  }
  latestVersion = tagName;

  // Strip optional leading "v" prefix (e.g. v1.2.0 -> 1.2.0)
  if (!latestVersion.empty() && (latestVersion[0] == 'v' || latestVersion[0] == 'V')) {
    latestVersion.erase(0, 1);
  }

  JsonArray assets = doc["assets"].as<JsonArray>();
  if (assets.isNull()) {
    LOG_ERR("OTA", "No assets found in latest release");
    return JSON_PARSE_ERROR;
  }

  for (JsonObject asset : assets) {
    const char* name = asset["name"];
    const char* downloadUrl = asset["browser_download_url"];
    if (!name || !downloadUrl) {
      continue;
    }

    if (std::strcmp(name, "firmware.bin") == 0) {
      otaUrl = downloadUrl;
      otaSize = asset["size"] | 0;
      totalSize = otaSize;
      updateAvailable = true;
      break;
    }
  }

  if (!updateAvailable) {
    LOG_ERR("OTA", "No firmware.bin asset found in latest release");
    return NO_UPDATE;
  }

  LOG_DBG("OTA", "Found update: %s", latestVersion.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty() || latestVersion == CROSSPOINT_VERSION) {
    return false;
  }

  int currentMajor = 0;
  int currentMinor = 0;
  int currentPatch = 0;
  int latestMajor = 0;
  int latestMinor = 0;
  int latestPatch = 0;

  const char* currentVersion = CROSSPOINT_VERSION;
  if (!parseSemver(currentVersion, currentMajor, currentMinor, currentPatch) ||
      !parseSemver(latestVersion.c_str(), latestMajor, latestMinor, latestPatch)) {
    LOG_ERR("OTA", "Failed to parse version strings (current=%s latest=%s)", currentVersion,
            latestVersion.c_str());
    return false;
  }

  if (latestMajor != currentMajor) {
    return latestMajor > currentMajor;
  }
  if (latestMinor != currentMinor) {
    return latestMinor > currentMinor;
  }
  if (latestPatch != currentPatch) {
    return latestPatch > currentPatch;
  }

  // Same numeric version: stable release is newer than pre-release firmware builds.
  return isPreReleaseVersion(currentVersion) && !isPreReleaseVersion(latestVersion);
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate() {
  if (!isUpdateNewer()) {
    return UPDATE_OLDER_ERROR;
  }

  esp_https_ota_handle_t ota_handle = NULL;
  esp_err_t esp_err;
  /* Signal for OtaUpdateActivity */
  render = false;

  esp_http_client_config_t client_config = {
      .url = otaUrl.c_str(),
      .timeout_ms = 15000,
      /* Default HTTP client buffer size 512 byte only
       * not sufficent to handle URL redirection cases or
       * parsing of large HTTP headers.
       */
      .buffer_size = 8192,
      .buffer_size_tx = 8192,
      .skip_cert_common_name_check = true,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .keep_alive_enable = true,
  };

  esp_https_ota_config_t ota_config = {
      .http_config = &client_config,
      .http_client_init_cb = http_client_set_header_cb,
  };

  /* For better timing and connectivity, we disable power saving for WiFi */
  esp_wifi_set_ps(WIFI_PS_NONE);

  esp_err = esp_https_ota_begin(&ota_config, &ota_handle);
  if (esp_err != ESP_OK) {
    LOG_DBG("OTA", "HTTP OTA Begin Failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  do {
    esp_err = esp_https_ota_perform(ota_handle);
    processedSize = esp_https_ota_get_image_len_read(ota_handle);
    /* Sent signal to OtaUpdateActivity */
    render = true;
    delay(100);  // TODO: should we replace this with something better?
  } while (esp_err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

  /* Return back to default power saving for WiFi in case of failing */
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_https_ota_perform Failed: %s", esp_err_to_name(esp_err));
    esp_https_ota_finish(ota_handle);
    return HTTP_ERROR;
  }

  if (!esp_https_ota_is_complete_data_received(ota_handle)) {
    LOG_ERR("OTA", "esp_https_ota_is_complete_data_received Failed: %s", esp_err_to_name(esp_err));
    esp_https_ota_finish(ota_handle);
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_https_ota_finish(ota_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_https_ota_finish Failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}
