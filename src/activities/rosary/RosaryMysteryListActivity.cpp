#include "RosaryMysteryListActivity.h"

#include <GfxRenderer.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void RosaryMysteryListActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  showingAllSets = false;
  requestUpdate();
}

void RosaryMysteryListActivity::onExit() {
  Activity::onExit();
}

void RosaryMysteryListActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (showingAllSets) {
      showingAllSets = false;
      selectorIndex = 0;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  int itemCount;
  if (showingAllSets) {
    // 4 mystery sets to browse
    itemCount = 4;
  } else {
    // 5 mysteries + "View All Sets" option
    itemCount = 6;
  }

  buttonNavigator.onNext([this, itemCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, itemCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, itemCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, itemCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (showingAllSets) {
      // Switch to the selected mystery set
      currentSet = static_cast<RosaryData::MysterySet>(selectorIndex);
      showingAllSets = false;
      selectorIndex = 0;
      requestUpdate();
    } else if (selectorIndex == 5) {
      // "View All Sets"
      showingAllSets = true;
      selectorIndex = 0;
      requestUpdate();
    }
    // Selecting a mystery (0-4) doesn't do anything special; they're displayed inline
  }
}

void RosaryMysteryListActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  if (showingAllSets) {
    // Show list of all 4 mystery sets
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Mystery Sets");

    int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    int contentHeight = pageHeight - contentY - metrics.buttonHintsHeight - metrics.verticalSpacing;

    GUI.drawList(
        renderer, Rect{0, contentY, pageWidth, contentHeight}, 4, selectorIndex,
        [](int index) { return RosaryData::getMysterySetName(static_cast<RosaryData::MysterySet>(index)); }, nullptr,
        nullptr, nullptr);
  } else {
    // Show the 5 mysteries of the current set
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   RosaryData::getMysterySetName(currentSet));

    int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    int contentHeight = pageHeight - contentY - metrics.buttonHintsHeight - metrics.verticalSpacing;

    // 5 mysteries + 1 "View All Sets" action
    auto currentSetCopy = currentSet;
    GUI.drawList(
        renderer, Rect{0, contentY, pageWidth, contentHeight}, 6, selectorIndex,
        [currentSetCopy](int index) -> std::string {
          if (index < 5) {
            std::string label = std::to_string(index + 1);
            label += ". ";
            label += RosaryData::getMysteryName(currentSetCopy, index);
            return label;
          }
          return "View All Sets";
        },
        [currentSetCopy](int index) -> std::string {
          if (index < 5) {
            return RosaryData::getMysteryScripture(currentSetCopy, index);
          }
          return "";
        },
        nullptr, nullptr);
  }

  const auto labels = mappedInput.mapLabels("« Back", "Select", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
