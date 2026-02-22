#pragma once

#include <functional>
#include <string>
#include <vector>

#include "AppManifest.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Full-screen random quote activity for "randomquote" apps.
// Uses manifest entries where each entry points to a text file containing one quote.
class RandomQuoteAppActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedEntry = -1;
  std::vector<std::string> lines;
  std::string quoteReference;

  const AppManifest manifest;
  const std::function<void()> onGoHome;

  void pickRandomQuote();
  void loadAndWrapQuote(int entryIndex);
  void wrapText(const char* text, int fontId, int maxWidth);

 public:
  explicit RandomQuoteAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const AppManifest& manifest,
                                  const std::function<void()>& onGoHome)
      : Activity("RandomQuote", renderer, mappedInput), manifest(manifest), onGoHome(onGoHome) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
};
