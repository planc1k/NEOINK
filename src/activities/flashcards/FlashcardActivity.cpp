#include "FlashcardActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr int kVisibleStudyLines = 8;

std::string countLabel(const flashcards::DeckSummary& deck) {
  char label[48];
  snprintf(label, sizeof(label), "%u due / %u new", deck.dueCards, deck.newCards);
  return label;
}

std::string progressLabel(int current, int total) {
  char label[32];
  snprintf(label, sizeof(label), "%d/%d", current, total);
  return label;
}

}  // namespace

void FlashcardActivity::onEnter() {
  Activity::onEnter();
  loadDeckList();
  requestUpdate();
}

void FlashcardActivity::loadDeckList() {
  flashcards::ensureDirectories();
  decks.clear();
  for (const std::string& path : flashcards::scanDeckFiles()) {
    decks.push_back(flashcards::summarizeDeck(path));
  }
  deckIndex = std::clamp(deckIndex, 0, std::max(0, static_cast<int>(decks.size()) - 1));
  screen = Screen::DeckSelect;
  errorMessage.clear();
}

void FlashcardActivity::startSelectedDeck() {
  if (decks.empty()) return;

  std::string error;
  deck = flashcards::Deck{};
  if (!flashcards::loadDeck(decks[deckIndex].path, deck, &error)) {
    screen = Screen::Error;
    errorMessage = error;
    requestUpdate();
    return;
  }
  flashcards::loadProgress(deck, nullptr);
  flashcards::startSession(deck);
  flashcards::saveProgress(deck);

  reviewQueue = flashcards::buildReviewQueue(deck);
  queueIndex = 0;
  againCount = 0;
  goodCount = 0;
  easyCount = 0;
  screen = reviewQueue.empty() ? Screen::Stats : Screen::CardFront;
  requestUpdate();
}

bool FlashcardActivity::loadCurrentCard(flashcards::Card& card, std::string* error) const {
  if (queueIndex < 0 || queueIndex >= static_cast<int>(reviewQueue.size())) return false;
  const int cardIndex = reviewQueue[queueIndex];
  if (cardIndex < 0 || cardIndex >= static_cast<int>(deck.cardRefs.size())) return false;
  return flashcards::loadCard(deck, static_cast<size_t>(cardIndex), card, error);
}

void FlashcardActivity::rateCurrentCard(flashcards::Rating rating) {
  flashcards::Card card;
  std::string error;
  if (!loadCurrentCard(card, &error)) {
    screen = Screen::Error;
    errorMessage = error.empty() ? "Cannot read card" : error;
    requestUpdate();
    return;
  }

  flashcards::applyRating(deck, card, rating);
  flashcards::saveProgress(deck);

  switch (rating) {
    case flashcards::Rating::Again:
      ++againCount;
      break;
    case flashcards::Rating::Easy:
      ++easyCount;
      break;
    case flashcards::Rating::Hard:
    case flashcards::Rating::Good:
      ++goodCount;
      break;
  }

  ++queueIndex;
  if (queueIndex >= static_cast<int>(reviewQueue.size())) {
    finishSession();
  } else {
    screen = Screen::CardFront;
  }
  requestUpdate();
}

void FlashcardActivity::finishSession() {
  screen = Screen::Stats;
}

void FlashcardActivity::loop() {
  if (screen == Screen::DeckSelect) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      startSelectedDeck();
      return;
    }
    if (!decks.empty()) {
      buttonNavigator.onNextRelease([this] {
        deckIndex = ButtonNavigator::nextIndex(deckIndex, static_cast<int>(decks.size()));
        requestUpdate();
      });
      buttonNavigator.onPreviousRelease([this] {
        deckIndex = ButtonNavigator::previousIndex(deckIndex, static_cast<int>(decks.size()));
        requestUpdate();
      });
    }
    return;
  }

  if (screen == Screen::CardFront) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      loadDeckList();
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      screen = Screen::CardBack;
      requestUpdate();
    }
    return;
  }

  if (screen == Screen::CardBack) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      screen = Screen::CardFront;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      rateCurrentCard(flashcards::Rating::Good);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
               mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      rateCurrentCard(flashcards::Rating::Again);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
               mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      rateCurrentCard(flashcards::Rating::Easy);
    }
    return;
  }

  if (screen == Screen::Stats || screen == Screen::Error) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      loadDeckList();
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (screen == Screen::Stats && !reviewQueue.empty()) {
        startSelectedDeck();
      } else {
        loadDeckList();
      }
      requestUpdate();
    }
  }
}

void FlashcardActivity::render(RenderLock&&) {
  renderer.clearScreen();
  switch (screen) {
    case Screen::DeckSelect:
      drawDeckSelect();
      break;
    case Screen::CardFront:
      drawStudyCard(false);
      break;
    case Screen::CardBack:
      drawStudyCard(true);
      break;
    case Screen::Stats:
      drawStats();
      break;
    case Screen::Error:
      drawMessage("Flashcards", errorMessage, "Reload");
      break;
  }
  renderer.displayBuffer();
}

void FlashcardActivity::drawDeckSelect() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Flashcards");

  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (decks.empty()) {
    drawWrappedCentered(std::string("Add .tsv or .csv decks to ") + flashcards::kDeckDirectory, pageHeight / 2 - 20,
                        pageWidth - 60, 4, UI_10_FONT_ID);
    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }

  GUI.drawList(renderer, Rect{0, listTop, pageWidth, listHeight}, static_cast<int>(decks.size()), deckIndex,
               [this](int index) { return decks[index].title; },
               [this](int index) {
                 if (!decks[index].valid) return decks[index].error;
                 return std::to_string(decks[index].totalCards) + " cards";
               },
               [](int) { return Book; },
               [this](int index) { return decks[index].valid ? countLabel(decks[index]) : ""; }, false,
               [this](int index) { return !decks[index].valid; });

  const auto labels = mappedInput.mapLabels("Back", "Study", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FlashcardActivity::drawStudyCard(bool showingBack) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  flashcards::Card card;
  std::string error;
  if (!loadCurrentCard(card, &error)) {
    drawMessage("Flashcards", error.empty() ? "Cannot read card" : error, "Back");
    return;
  }

  const std::string subtitle = progressLabel(queueIndex + 1, static_cast<int>(reviewQueue.size()));
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, deck.title.c_str(),
                 subtitle.c_str());

  const int labelY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 8;
  renderer.drawCenteredText(SMALL_FONT_ID, labelY, showingBack ? "Answer" : "Question");

  const int contentY = labelY + 34;
  const int contentHeight = pageHeight - contentY - metrics.buttonHintsHeight - 20;
  const int fontId = showingBack ? UI_10_FONT_ID : UI_12_FONT_ID;
  const std::string& text = showingBack ? card.back : card.front;
  const int lineHeight = renderer.getLineHeight(fontId);
  const int maxLines = std::max(2, std::min(kVisibleStudyLines, contentHeight / std::max(1, lineHeight + 4)));
  drawWrappedCentered(text, contentY, pageWidth - 50, maxLines, fontId, showingBack ? EpdFontFamily::REGULAR
                                                                                   : EpdFontFamily::BOLD);

  const auto labels = showingBack ? mappedInput.mapLabels("Back", "Good", "Again", "Easy")
                                  : mappedInput.mapLabels("Decks", "Flip", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FlashcardActivity::drawStats() {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, UITheme::getInstance().getMetrics().topPadding, pageWidth,
                                UITheme::getInstance().getMetrics().headerHeight},
                 "Session");

  if (reviewQueue.empty()) {
    drawWrappedCentered("No cards are due in this deck.", pageHeight / 2 - 30, pageWidth - 60, 3, UI_10_FONT_ID);
  } else {
    char stats[96];
    snprintf(stats, sizeof(stats), "Reviewed %d cards\nAgain %d   Good %d   Easy %d",
             static_cast<int>(reviewQueue.size()), againCount, goodCount, easyCount);
    drawWrappedCentered(stats, pageHeight / 2 - 45, pageWidth - 60, 4, UI_10_FONT_ID);
  }

  const auto labels = mappedInput.mapLabels("Decks", reviewQueue.empty() ? "" : "Again", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FlashcardActivity::drawMessage(const char* title, const std::string& message, const char* confirmLabel) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);
  drawWrappedCentered(message, pageHeight / 2 - 30, pageWidth - 60, 4, UI_10_FONT_ID);
  const auto labels = mappedInput.mapLabels("Back", confirmLabel, "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FlashcardActivity::drawWrappedCentered(const std::string& text, int y, int maxWidth, int maxLines, int fontId,
                                            EpdFontFamily::Style style) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= text.size() && static_cast<int>(lines.size()) < maxLines) {
    const size_t next = text.find('\n', start);
    const std::string segment = text.substr(start, next == std::string::npos ? std::string::npos : next - start);
    std::vector<std::string> wrapped = renderer.wrappedText(fontId, segment.c_str(), maxWidth,
                                                            maxLines - static_cast<int>(lines.size()), style);
    lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    if (next == std::string::npos) break;
    start = next + 1;
  }

  const int lineHeight = renderer.getLineHeight(fontId) + 4;
  for (size_t i = 0; i < lines.size(); ++i) {
    renderer.drawCenteredText(fontId, y + static_cast<int>(i) * lineHeight, lines[i].c_str(), true, style);
  }
}
