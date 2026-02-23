#include "AppsMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "apps/AppLoader.h"
#include "components/UITheme.h"
#include "fontIds.h"

void AppsMenuActivity::onEnter() {
  Activity::onEnter();

  loadedApps = AppLoader::scanApps();
  selectorIndex = 0;
  requestUpdate();
}

void AppsMenuActivity::onExit() {
  Activity::onExit();
  loadedApps.clear();
}

void AppsMenuActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);

  // Total items: installed apps + App Store entry
  const int listSize = static_cast<int>(loadedApps.size()) + 1;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectorIndex < static_cast<int>(loadedApps.size())) {
      onAppOpen(loadedApps[selectorIndex]);
    } else {
      onAppStoreOpen();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, listSize, pageItems);
    requestUpdate();
  });
}

void AppsMenuActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_APPS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  // Total items: installed apps + App Store
  const int totalItems = static_cast<int>(loadedApps.size()) + 1;

  if (loadedApps.empty()) {
    // Only show App Store entry
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, totalItems, selectorIndex,
        [](int) { return std::string(tr(STR_APP_STORE)); }, nullptr, nullptr, nullptr);
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, totalItems, selectorIndex,
        [this](int index) {
          if (index < static_cast<int>(loadedApps.size())) {
            return loadedApps[index].name;
          }
          return std::string(tr(STR_APP_STORE));
        },
        nullptr, nullptr, nullptr);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
