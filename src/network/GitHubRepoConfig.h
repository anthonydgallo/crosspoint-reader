#pragma once

#include <string>

#ifndef CROSSPOINT_GITHUB_OWNER
#define CROSSPOINT_GITHUB_OWNER "crosspoint-reader"
#endif

#ifndef CROSSPOINT_GITHUB_REPO
#define CROSSPOINT_GITHUB_REPO "crosspoint-reader"
#endif

// Optional override. If empty, API requests use the repository's default branch.
#ifndef CROSSPOINT_GITHUB_BRANCH
#define CROSSPOINT_GITHUB_BRANCH ""
#endif

// App Store source repository can be configured independently from OTA.
#ifndef CROSSPOINT_APPS_GITHUB_OWNER
#define CROSSPOINT_APPS_GITHUB_OWNER CROSSPOINT_GITHUB_OWNER
#endif

#ifndef CROSSPOINT_APPS_GITHUB_REPO
#define CROSSPOINT_APPS_GITHUB_REPO CROSSPOINT_GITHUB_REPO
#endif

// Optional override for App Store branch.
// If empty, API requests use the repository default branch.
#ifndef CROSSPOINT_APPS_GITHUB_BRANCH
#define CROSSPOINT_APPS_GITHUB_BRANCH CROSSPOINT_GITHUB_BRANCH
#endif

namespace GitHubRepoConfig {

inline std::string firmwareRepoApiBase() {
  return std::string("https://api.github.com/repos/") + CROSSPOINT_GITHUB_OWNER + "/" + CROSSPOINT_GITHUB_REPO;
}

inline std::string appsRepoApiBase() {
  return std::string("https://api.github.com/repos/") + CROSSPOINT_APPS_GITHUB_OWNER + "/" + CROSSPOINT_APPS_GITHUB_REPO;
}

inline std::string appendAppsBranchRef(const std::string& url) {
  constexpr const char* branch = CROSSPOINT_APPS_GITHUB_BRANCH;
  if (branch[0] == '\0') {
    return url;
  }
  return url + "?ref=" + branch;
}

inline std::string appsApiUrl() { return appendAppsBranchRef(appsRepoApiBase() + "/contents/apps"); }

inline std::string appFolderApiUrl(const std::string& appFolder) {
  return appendAppsBranchRef(appsRepoApiBase() + "/contents/apps/" + appFolder);
}

inline std::string appManifestRawUrl(const std::string& appFolder) {
  // raw.githubusercontent requires an explicit branch component.
  constexpr const char* branch = CROSSPOINT_APPS_GITHUB_BRANCH;
  const std::string branchName = (branch[0] != '\0') ? branch : "master";
  return std::string("https://raw.githubusercontent.com/") + CROSSPOINT_APPS_GITHUB_OWNER + "/" +
         CROSSPOINT_APPS_GITHUB_REPO + "/" + branchName + "/apps/" + appFolder + "/app.json";
}

inline std::string latestReleaseApiUrl() { return firmwareRepoApiBase() + "/releases/latest"; }

}  // namespace GitHubRepoConfig
