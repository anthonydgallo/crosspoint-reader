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

namespace {
UIIcon iconForAppType(const std::string& type) {
  if (type == "art") return UIIcon::Art;
  if (type == "calculator") return UIIcon::Calculator;
  if (type == "minesweeper") return UIIcon::Minesweeper;
  if (type == "rosary") return UIIcon::Rosary;
  if (type == "flashcard") return UIIcon::Flashcard;
  if (type == "randomquote") return UIIcon::Quote;
  if (type == "bookhighlights") return UIIcon::Quote;
  if (type == "texteditor") return UIIcon::TextEditor;
  if (type == "textviewer") return UIIcon::Text;
  if (type == "imageviewer") return UIIcon::Image;
  return UIIcon::File;
}
}  // namespace

void AppsMenuActivity::onEnter() {
  Activity::onEnter();

  loadedApps = AppLoader::scanApps();
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

  if (Storage.removeDir(appPath.c_str())) {
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

void AppsMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_APPS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const int totalItems = static_cast<int>(loadedApps.size());

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, totalItems, selectorIndex,
      [this](int index) { return loadedApps[index].name; }, nullptr,
      [this](int index) { return iconForAppType(loadedApps[index].type); }, nullptr);

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
