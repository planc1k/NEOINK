#pragma once

#include <EpdFontFamily.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "flashcards/FlashcardDeck.h"
#include "util/ButtonNavigator.h"

class FlashcardActivity final : public Activity {
  enum class Screen { DeckSelect, CardFront, CardBack, Stats, Error };

  ButtonNavigator buttonNavigator;
  Screen screen = Screen::DeckSelect;
  std::vector<flashcards::DeckSummary> decks;
  flashcards::Deck deck;
  std::vector<int> reviewQueue;
  int deckIndex = 0;
  int queueIndex = 0;
  int againCount = 0;
  int hardCount = 0;
  int goodCount = 0;
  int easyCount = 0;
  int cardsUntilFullRefresh = 30;
  bool fullRefreshNextRender = false;
  bool easyLongPressFired = false;
  std::string errorMessage;

  void loadDeckList();
  void startSelectedDeck();
  void rateCurrentCard(flashcards::Rating rating);
  void markReviewedCardForRefreshCycle();
  void displayWithRefreshPolicy();
  void finishSession();
  bool loadCurrentCard(flashcards::Card& card, std::string* error = nullptr) const;
  void drawDeckSelect();
  void drawStudyCard(bool showingBack);
  void drawStats();
  void drawMessage(const char* title, const std::string& message, const char* confirmLabel = "");
  void drawWrappedCentered(const std::string& text, int y, int maxWidth, int maxLines, int fontId,
                           EpdFontFamily::Style style = EpdFontFamily::REGULAR);

 public:
  explicit FlashcardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Flashcards", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
