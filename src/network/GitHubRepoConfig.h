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

namespace GitHubRepoConfig {

inline std::string repoApiBase() {
  return std::string("https://api.github.com/repos/") + CROSSPOINT_GITHUB_OWNER + "/" + CROSSPOINT_GITHUB_REPO;
}

inline std::string appendBranchRef(const std::string& url) {
  constexpr const char* branch = CROSSPOINT_GITHUB_BRANCH;
  if (branch[0] == '\0') {
    return url;
  }
  return url + "?ref=" + branch;
}

inline std::string appsApiUrl() { return appendBranchRef(repoApiBase() + "/contents/apps"); }

inline std::string appFolderApiUrl(const std::string& appFolder) {
  return appendBranchRef(repoApiBase() + "/contents/apps/" + appFolder);
}

inline std::string latestReleaseApiUrl() { return repoApiBase() + "/releases/latest"; }

}  // namespace GitHubRepoConfig
