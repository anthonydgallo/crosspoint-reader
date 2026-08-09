#include "AppsMenuActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include "../ActivityManager.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "apps/AppLoader.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TaskWatchdog.h"

namespace {
UIIcon iconForAppType(const std::string& type) {
  if (type == "art" || type == "imageviewer") return UIIcon::Image;
  if (type == "randomquote" || type == "bookhighlights" || type == "textviewer") return UIIcon::Text;
  if (type == "texteditor") return UIIcon::File;
  return UIIcon::File;
}
}  // namespace

void AppsMenuActivity::onEnter() {
  Activity::onEnter();

  scanning = true;
  requestUpdateAndWait();
  loadedApps = AppLoader::scanApps();
  scanning = false;
  selectorIndex = 0;
  deleteArmed = false;
  deleteArmedIndex = -1;
  skipNextConfirmRelease = false;
  deleteStatus.clear();
  requestUpdate();
}

void AppsMenuActivity::onExit() {
  Activity::onExit();
  loadedApps.clear();
  clearDeleteMode();
  deleteStatus.clear();
}

void AppsMenuActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);

  const int listSize = static_cast<int>(loadedApps.size());

  if (!deleteArmed && listSize > 0 && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= DELETE_ARM_MS) {
    deleteArmed = true;
    deleteArmedIndex = selectorIndex;
    skipNextConfirmRelease = true;
    deleteStatus.clear();
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (skipNextConfirmRelease) {
      skipNextConfirmRelease = false;
      return;
    }

    if (selectorIndex < listSize) {
      if (deleteArmed) {
        promptDeleteSelectedApp();
      } else {
        activityManager.goToOpenApp(loadedApps[selectorIndex]);
      }
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (deleteArmed) {
      clearDeleteMode();
      requestUpdate();
    } else {
      onGoHome();
    }
    return;
  }

  buttonNavigator.onNextRelease([this, listSize] {
    clearDeleteMode();
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    clearDeleteMode();
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    clearDeleteMode();
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    clearDeleteMode();
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, listSize, pageItems);
    requestUpdate();
  });
}

void AppsMenuActivity::clearDeleteMode() {
  deleteArmed = false;
  deleteArmedIndex = -1;
  skipNextConfirmRelease = false;
}

void AppsMenuActivity::promptDeleteSelectedApp() {
  if (selectorIndex < 0 || selectorIndex >= static_cast<int>(loadedApps.size())) {
    clearDeleteMode();
    requestUpdate();
    return;
  }

  const int appIndex = selectorIndex;
  const std::string appName = loadedApps[appIndex].name;
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, "Delete app?", appName),
                         [this, appIndex](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             deleteAppAtIndex(static_cast<size_t>(appIndex));
                           } else {
                             requestUpdate();
                           }
                         });
}

void AppsMenuActivity::deleteAppAtIndex(const size_t appIndex) {
  if (appIndex >= loadedApps.size()) {
    clearDeleteMode();
    requestUpdate();
    return;
  }

  const std::string appPath = loadedApps[appIndex].path;
  const std::string appName = loadedApps[appIndex].name;

  if (removeAppTree(appPath)) {
    LOG_DBG("APPS", "Deleted app: %s (%s)", appName.c_str(), appPath.c_str());
    loadedApps.erase(loadedApps.begin() + static_cast<int>(appIndex));
    if (selectorIndex >= static_cast<int>(loadedApps.size()) && !loadedApps.empty()) {
      selectorIndex = static_cast<int>(loadedApps.size()) - 1;
    } else if (loadedApps.empty()) {
      selectorIndex = 0;
    }
    deleteStatus.clear();
  } else {
    LOG_ERR("APPS", "Failed to delete app: %s (%s)", appName.c_str(), appPath.c_str());
    deleteStatus = "Failed to delete app";
  }

  clearDeleteMode();
  requestUpdate();
}

bool AppsMenuActivity::removeAppTree(const std::string& appPath) {
  constexpr const char* prefix = "/apps/";
  if (appPath.rfind(prefix, 0) != 0 || appPath.size() <= strlen(prefix) || appPath.find("..") != std::string::npos) {
    LOG_ERR("APPS", "Refusing unsafe app delete path: %s", appPath.c_str());
    return false;
  }

  std::vector<std::pair<std::string, bool>> stack;
  stack.reserve(8);
  stack.push_back({appPath, false});
  char name[256];
  size_t operations = 0;
  while (!stack.empty()) {
    auto [path, postOrder] = std::move(stack.back());
    stack.pop_back();
    if (postOrder) {
      if (!Storage.rmdir(path.c_str())) return false;
    } else {
      auto dir = Storage.open(path.c_str());
      if (!dir || !dir.isDirectory()) return false;
      stack.push_back({path, true});
      dir.rewindDirectory();
      for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
        entry.getName(name, sizeof(name));
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        std::string child = path + "/" + name;
        const bool isDirectory = entry.isDirectory();
        entry.close();
        if (isDirectory) {
          stack.push_back({std::move(child), false});
        } else if (!Storage.remove(child.c_str())) {
          return false;
        }
        if ((++operations & 0x07U) == 0) {
          yield();
          resetTaskWatchdogIfSubscribed();
        }
      }
    }
  }
  return true;
}

void AppsMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_APPS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const int totalItems = static_cast<int>(loadedApps.size());

  if (scanning) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Scanning apps...");
  } else if (loadedApps.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Copy apps to /apps on the SD card");
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, totalItems, selectorIndex,
        [this](int index) { return loadedApps[index].name; }, nullptr,
        [this](int index) { return iconForAppType(loadedApps[index].type); }, nullptr);
  }

  if (!deleteStatus.empty()) {
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - metrics.buttonHintsHeight - 20, deleteStatus.c_str(), true,
                              EpdFontFamily::BOLD);
  }

  const char* backLabel = deleteArmed ? tr(STR_CANCEL) : tr(STR_HOME);
  const char* confirmLabel = deleteArmed ? tr(STR_DELETE) : tr(STR_OPEN);
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
