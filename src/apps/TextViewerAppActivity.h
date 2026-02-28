#pragma once

#include <string>
#include <vector>

#include "AppManifest.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Generic text viewer app activity for "textviewer" type apps.
// Shows a list of entries from the app manifest, and when selected,
// displays the text content loaded from the corresponding file on the SD card.
class TextViewerAppActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool showingText = false;
  int selectedEntry = 0;
  int scrollLine = 0;              // Current scroll position (top line index)
  std::vector<std::string> lines;  // Word-wrapped lines of the current text
  int linesPerPage = 0;            // How many lines fit on screen

  const AppManifest manifest;

  void renderList();
  void renderText();
  void loadAndWrapText(int entryIndex);

  // Word-wrap text into lines that fit within maxWidth pixels
  void wrapText(const char* text, int fontId, int maxWidth);

 public:
  explicit TextViewerAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const AppManifest& manifest)
      : Activity("TextViewer", renderer, mappedInput), manifest(manifest) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
