#include "AppsMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "../ActivityManager.h"
#include "MappedInputManager.h"
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
  requestUpdate();
}

void AppsMenuActivity::onExit() {
  Activity::onExit();
  loadedApps.clear();
}

void AppsMenuActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);

  const int listSize = static_cast<int>(loadedApps.size());

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectorIndex < listSize) {
      activityManager.goToOpenApp(loadedApps[selectorIndex]);
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

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
