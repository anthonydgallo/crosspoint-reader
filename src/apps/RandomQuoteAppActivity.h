#pragma once

#include <functional>
#include <string>
#include <vector>

#include "AppManifest.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class RandomQuoteAppActivity final : public Activity {
  struct Quote {
    std::string reference;
    std::string text;
  };

  ButtonNavigator buttonNavigator;
  int selectedIndex = -1;
  std::vector<Quote> quotes;
  std::vector<std::string> wrappedLines;

  const AppManifest manifest;

  void loadQuotes();
  void loadQuotesFromEntry(const AppManifest::Entry& entry);
  void pickRandomQuote();
  void wrapQuote(const Quote& quote);
  void wrapText(const char* text, int fontId, int maxWidth);
  static void trim(std::string& s);

 public:
  explicit RandomQuoteAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const AppManifest& manifest)
      : Activity("RandomQuote", renderer, mappedInput), manifest(manifest) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
