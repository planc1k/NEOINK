#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace flashcards {

constexpr const char* kDeckDirectory = "/.crosspoint/flashcards/decks";
constexpr const char* kProgressDirectory = "/.crosspoint/flashcards/progress";
constexpr size_t kMaxDeckFileBytes = 512U * 1024U;
constexpr size_t kMaxLoadedCards = 300;
constexpr size_t kMaxFrontBytes = 512;
constexpr size_t kMaxBackBytes = 1024;
constexpr uint32_t kHeapReserveBytes = 24U * 1024U;
constexpr uint32_t kMinLargestHeapBlockBytes = 8U * 1024U;

enum class Rating : uint8_t { Again = 1, Hard = 2, Good = 3, Easy = 4 };

struct Card {
  std::string front;
  std::string back;
  uint32_t hash = 0;
};

struct CardRef {
  uint32_t offset = 0;
  uint16_t lineBytes = 0;
  uint32_t hash = 0;
};

struct ProgressRecord {
  uint32_t cardHash = 0;
  uint16_t reviewCount = 0;
  uint16_t lapseCount = 0;
  uint16_t intervalSessions = 0;
  uint16_t easePermille = 2500;
  uint16_t dueSession = 0;
  uint16_t lastReviewedSession = 0;
  uint16_t againCount = 0;
  uint16_t hardCount = 0;
  uint16_t goodCount = 0;
  uint16_t easyCount = 0;
  uint8_t lastRating = 0;
};

struct Deck {
  std::string path;
  std::string title;
  std::vector<Card> cards;
  std::vector<CardRef> cardRefs;
  std::vector<ProgressRecord> progress;
  uint32_t deckHash = 0;
  uint32_t totalReviews = 0;
  uint32_t totalAgain = 0;
  uint32_t totalHard = 0;
  uint32_t totalGood = 0;
  uint32_t totalEasy = 0;
  uint32_t totalLapses = 0;
  uint16_t sessionCount = 0;
  uint16_t totalSessions = 0;
  uint16_t lastStudiedSession = 0;
};

struct DeckSummary {
  std::string path;
  std::string title;
  uint16_t totalCards = 0;
  uint16_t newCards = 0;
  uint16_t dueCards = 0;
  uint16_t reviewedCards = 0;
  uint16_t learningCards = 0;
  uint16_t matureCards = 0;
  uint32_t totalReviews = 0;
  uint32_t totalAgain = 0;
  uint32_t totalHard = 0;
  uint32_t totalGood = 0;
  uint32_t totalEasy = 0;
  uint32_t totalLapses = 0;
  uint16_t sessionCount = 0;
  uint16_t totalSessions = 0;
  uint16_t lastStudiedSession = 0;
  uint16_t retentionPermille = 0;
  bool valid = false;
  std::string error;
};

void ensureDirectories();
std::vector<std::string> scanDeckFiles();
std::string deckDisplayName(const std::string& path);
std::string progressPathForDeck(const std::string& path);
bool loadDeck(const std::string& path, Deck& deck, std::string* error = nullptr);
bool loadCard(const Deck& deck, size_t index, Card& card, std::string* error = nullptr);
bool loadProgress(Deck& deck, std::string* error = nullptr);
bool saveProgress(const Deck& deck);
DeckSummary summarizeDeck(const std::string& path);
std::vector<int> buildReviewQueue(const Deck& deck);
ProgressRecord* findProgress(Deck& deck, uint32_t cardHash);
const ProgressRecord* findProgress(const Deck& deck, uint32_t cardHash);
ProgressRecord& findOrCreateProgress(Deck& deck, uint32_t cardHash);
void startSession(Deck& deck);
void applyRating(Deck& deck, const Card& card, Rating rating);

}  // namespace flashcards
