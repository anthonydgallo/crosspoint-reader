#pragma once

#include <string>
#include <vector>

#include "AppManifest.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class BookHighlightsAppActivity final : public Activity {
  struct HighlightRecord {
    std::string highlight;
    std::string bookTitle;
    std::string bookAuthor;
    std::string sourceFile;
  };

  ButtonNavigator buttonNavigator;
  const AppManifest manifest;

  std::vector<std::string> csvFiles;
  std::vector<HighlightRecord> history;
  std::vector<std::string> wrappedHighlight;

  int historyIndex = -1;
  bool isLoading = false;
  std::string statusMessage;

  void scanCsvFiles();
  void loadNextRandomHighlight();
  bool pickRandomHighlight(HighlightRecord& out);
  void pushHistory(HighlightRecord&& record);
  void refreshWrappedHighlight();
  void showPreviousHighlight();

 public:
  explicit BookHighlightsAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const AppManifest& manifest)
      : Activity("BookHighlights", renderer, mappedInput), manifest(manifest) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
