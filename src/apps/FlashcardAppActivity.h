#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "AppManifest.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Flashcard study activity with spaced repetition (SM-2 algorithm).
//
// Supports TSV (tab-separated values) card files where each line is:
//   front<TAB>back
//
// Blank lines and lines starting with '#' are skipped.
// This is compatible with standard Anki export format.
//
// The app persists per-deck review state (interval, ease factor, due date)
// to /.crosspoint/flashcards/<hash>.bin on the SD card.

class FlashcardAppActivity final : public Activity {
  // --- Card data ---
  struct Card {
    std::string front;
    std::string back;
  };

  // --- SM-2 review state per card ---
  struct CardState {
    float easeFactor = 2.5f;   // EF >= 1.3
    uint16_t interval = 0;     // days until next review (0 = new)
    uint16_t repetitions = 0;  // consecutive correct answers
    uint32_t dueDay = 0;       // absolute day number when card is due
  };

  // --- UI states ---
  enum class Screen {
    DeckList,    // Pick a deck from manifest entries
    StudyMenu,   // Show deck stats; start study / browse
    ReviewFront, // Showing the front of a card
    ReviewBack,  // Showing the back + rating buttons
    DeckDone,    // All due cards reviewed
    Browse,      // Browse all cards in deck
  };

  Screen screen = Screen::DeckList;

  // Deck list
  ButtonNavigator deckNav;
  int selectedDeck = 0;

  // Loaded deck
  std::vector<Card> cards;
  std::vector<CardState> states;
  std::string deckName;
  std::string deckFilePath;

  // Review session
  std::vector<int> reviewQueue;  // indices into cards[] for this session
  int reviewPos = 0;             // current position in reviewQueue
  int ratingCursor = 0;          // selected rating button (0-3)

  // Browse mode
  ButtonNavigator browseNav;
  int browseIndex = 0;
  bool browseShowBack = false;

  // Word-wrapped lines cache for rendering
  std::vector<std::string> wrappedFront;
  std::vector<std::string> wrappedBack;

  // Day tracking for spaced repetition
  uint32_t today = 0;

  // Session stats
  int sessionReviewed = 0;

  const AppManifest manifest;

  // --- Helpers ---
  void loadDeck(int entryIndex);
  bool parseTsvCards(const char* data, size_t len);
  void buildReviewQueue();
  void advanceReview();
  void rateCard(int quality);  // 0=Again, 1=Hard, 2=Good, 3=Easy
  void wrapText(const std::string& text, int fontId, int maxWidth, std::vector<std::string>& out);

  // Day number (days since a fixed epoch, used for scheduling)
  static uint32_t currentDay();

  // State persistence
  std::string getSaveFilePath() const;
  bool saveState() const;
  bool loadState();

  // Stats
  int countDue() const;
  int countNew() const;
  int countLearned() const;

 public:
  explicit FlashcardAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const AppManifest& manifest)
      : Activity("Flashcard", renderer, mappedInput), manifest(manifest) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
