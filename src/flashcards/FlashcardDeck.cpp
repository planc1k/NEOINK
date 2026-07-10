#include "FlashcardDeck.h"

#include <HalStorage.h>
#include <Logging.h>
#include <MemoryBudget.h>
#include <common/FsApiConstants.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>
#include <vector>

namespace flashcards {
namespace {

constexpr char TAG[] = "FCARD";
constexpr uint32_t kProgressMagic = 0x31434658;  // "XFC1"
constexpr uint16_t kProgressVersion = 2;
constexpr uint16_t kProgressVersionV1 = 1;

struct ProgressHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t recordCount;
  uint32_t deckHash;
  uint32_t totalReviews;
  uint16_t sessionCount;
  uint16_t totalSessions;
  uint32_t totalAgain;
  uint32_t totalHard;
  uint32_t totalGood;
  uint32_t totalEasy;
  uint32_t totalLapses;
  uint16_t lastStudiedSession;
};

struct ProgressHeaderV1 {
  uint32_t magic;
  uint16_t version;
  uint16_t recordCount;
  uint32_t deckHash;
  uint32_t totalReviews;
  uint16_t sessionCount;
  uint16_t totalSessions;
};

struct ProgressRecordV1 {
  uint32_t cardHash = 0;
  uint16_t reviewCount = 0;
  uint16_t lapseCount = 0;
  uint16_t intervalSessions = 0;
  uint16_t easePermille = 2500;
  uint16_t dueSession = 0;
  uint8_t lastRating = 0;
};

struct ProgressRecordV2 {
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

static_assert(sizeof(ProgressHeaderV1) == 20, "Unexpected flashcard v1 header layout");
static_assert(sizeof(ProgressRecordV1) == 16, "Unexpected flashcard v1 record layout");

uint32_t fnv1a32(const void* data, size_t len, uint32_t hash = 2166136261UL) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t hashString(const std::string& value, uint32_t hash = 2166136261UL) {
  return fnv1a32(value.data(), value.size(), hash);
}

bool hasSuffixIgnoreCase(const std::string& text, const char* suffix) {
  const size_t textLen = text.size();
  const size_t suffixLen = std::strlen(suffix);
  if (textLen < suffixLen) return false;
  for (size_t i = 0; i < suffixLen; ++i) {
    const auto a = static_cast<unsigned char>(text[textLen - suffixLen + i]);
    const auto b = static_cast<unsigned char>(suffix[i]);
    if (std::tolower(a) != std::tolower(b)) return false;
  }
  return true;
}

std::string basename(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string withoutExtension(const std::string& name) {
  const size_t dot = name.find_last_of('.');
  if (dot == std::string::npos || dot == 0) return name;
  return name.substr(0, dot);
}

std::string trim(const std::string& value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
  return value.substr(start, end - start);
}

std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string truncateBytes(const std::string& value, size_t maxBytes) {
  if (value.size() <= maxBytes) return value;
  return value.substr(0, maxBytes);
}

bool readLine(HalFile& file, std::string& line, size_t* offset = nullptr, size_t* bytesReadOut = nullptr) {
  line.clear();
  if (offset != nullptr) {
    *offset = file.position();
  }
  size_t bytesRead = 0;
  while (file.available()) {
    const int ch = file.read();
    if (ch < 0) break;
    bytesRead++;
    if (ch == '\n') {
      if (bytesReadOut != nullptr) {
        *bytesReadOut = bytesRead;
      }
      return true;
    }
    if (ch != '\r' && line.size() < 4096) line.push_back(static_cast<char>(ch));
  }
  if (bytesReadOut != nullptr) {
    *bytesReadOut = bytesRead;
  }
  return !line.empty();
}

bool parseDelimitedLine(const std::string& line, char delimiter, std::string& front, std::string& back) {
  std::vector<std::string> fields;
  fields.reserve(3);
  std::string current;
  bool inQuotes = false;

  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (delimiter == ',' && c == '"') {
      if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
        current.push_back('"');
        ++i;
      } else {
        inQuotes = !inQuotes;
      }
      continue;
    }
    if (c == delimiter && !inQuotes) {
      fields.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(c);
  }
  fields.push_back(current);

  if (fields.size() < 2) return false;
  front = trim(fields[0]);
  back = trim(fields[1]);
  return !front.empty() && !back.empty();
}

bool isHeaderRow(const std::string& front, const std::string& back) {
  const std::string f = lowerAscii(front);
  const std::string b = lowerAscii(back);
  return (f == "front" || f == "question" || f == "term") && (b == "back" || b == "answer" || b == "definition");
}

uint32_t cardHashFor(const std::string& front, const std::string& back) {
  uint32_t hash = hashString(front);
  const char sep = '\0';
  hash = fnv1a32(&sep, 1, hash);
  return hashString(back, hash);
}

uint16_t clampU16(uint32_t value) {
  return value > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(value);
}

std::string sanitizedDeckKey(const std::string& path) {
  const std::string stem = withoutExtension(basename(path));
  std::string out;
  out.reserve(stem.size());
  for (char c : stem) {
    const auto uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc) || c == '-' || c == '_') {
      out.push_back(c);
    } else if (!out.empty() && out.back() != '_') {
      out.push_back('_');
    }
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  if (!out.empty()) return out;
  return "deck_" + std::to_string(hashString(path));
}

void setError(std::string* error, const char* value) {
  if (error) *error = value;
}

bool hasHeapForDeckIndex(size_t expectedIndexBytes) {
#ifdef SIMULATOR
  return true;
#else
  const auto heap = MemoryBudget::snapshot();
  const uint32_t minFree = static_cast<uint32_t>(expectedIndexBytes) + kHeapReserveBytes;
  if (MemoryBudget::hasHeap(heap, minFree, kMinLargestHeapBlockBytes)) {
    return true;
  }

  LOG_ERR(TAG, "Low heap for flashcards (%u free, %u max alloc, need %u/%u)", heap.freeHeap, heap.maxAllocHeap,
          minFree, kMinLargestHeapBlockBytes);
  return false;
#endif
}

}  // namespace

void ensureDirectories() {
  Storage.mkdir("/.crosspoint", true);
  Storage.mkdir("/.crosspoint/flashcards", true);
  Storage.mkdir(kDeckDirectory, true);
  Storage.mkdir(kProgressDirectory, true);
}

std::vector<std::string> scanDeckFiles() {
  ensureDirectories();
  std::vector<std::string> decks;
  HalFile dir = Storage.open(kDeckDirectory, O_RDONLY);
  if (!dir || !dir.isDirectory()) return decks;

  while (true) {
    HalFile entry = dir.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      char name[160] = {};
      entry.getName(name, sizeof(name));
      std::string path = name;
      if (!path.empty() && path[0] != '/') {
        path = std::string(kDeckDirectory) + "/" + path;
      }
      if (hasSuffixIgnoreCase(path, ".tsv") || hasSuffixIgnoreCase(path, ".csv")) {
        decks.push_back(path);
      }
    }
    entry.close();
  }
  dir.close();
  std::sort(decks.begin(), decks.end());
  return decks;
}

std::string deckDisplayName(const std::string& path) { return withoutExtension(basename(path)); }

std::string progressPathForDeck(const std::string& path) {
  return std::string(kProgressDirectory) + "/" + sanitizedDeckKey(path) + ".xfc";
}

bool loadDeck(const std::string& path, Deck& deck, std::string* error) {
  deck = Deck{};
  deck.path = path;
  deck.title = deckDisplayName(path);

  HalFile file;
  if (!Storage.openFileForRead(TAG, path, file)) {
    setError(error, "Cannot open deck");
    return false;
  }
  if (file.size() > kMaxDeckFileBytes) {
    file.close();
    setError(error, "Deck file is too large");
    return false;
  }
  const size_t expectedIndexBytes = std::min<size_t>(file.size() / 16 + 8, kMaxLoadedCards) * sizeof(CardRef);
  if (!hasHeapForDeckIndex(expectedIndexBytes)) {
    file.close();
    setError(error, "Not enough memory for deck index");
    return false;
  }

  const char delimiter = hasSuffixIgnoreCase(path, ".tsv") ? '\t' : ',';
  bool skippedHeader = false;
  std::string line;
  while (true) {
    size_t lineOffset = 0;
    size_t lineBytes = 0;
    if (!readLine(file, line, &lineOffset, &lineBytes)) break;
    std::string stripped = trim(line);
    if (stripped.empty() || stripped[0] == '#') continue;

    std::string front;
    std::string back;
    if (!parseDelimitedLine(stripped, delimiter, front, back)) continue;
    if (!skippedHeader && isHeaderRow(front, back)) {
      skippedHeader = true;
      continue;
    }
    skippedHeader = true;

    front = truncateBytes(front, kMaxFrontBytes);
    back = truncateBytes(back, kMaxBackBytes);
    if (deck.cardRefs.size() >= kMaxLoadedCards) break;
    if ((deck.cardRefs.size() % 32) == 0 && !hasHeapForDeckIndex((deck.cardRefs.size() + 32) * sizeof(CardRef))) {
      file.close();
      setError(error, "Not enough memory for deck index");
      return false;
    }

    const uint32_t cardHash = cardHashFor(front, back);
    deck.deckHash = fnv1a32(&cardHash, sizeof(cardHash), deck.deckHash == 0 ? 2166136261UL : deck.deckHash);
    CardRef ref;
    ref.offset = lineOffset > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(lineOffset);
    ref.lineBytes = lineBytes > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(lineBytes);
    ref.hash = cardHash;
    deck.cardRefs.push_back(ref);
  }
  file.close();

  if (deck.cardRefs.empty()) {
    setError(error, "No cards found");
    return false;
  }
  return true;
}

bool loadCard(const Deck& deck, const size_t index, Card& card, std::string* error) {
  card = Card{};
  if (index >= deck.cardRefs.size()) {
    setError(error, "Card index out of range");
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead(TAG, deck.path, file)) {
    setError(error, "Cannot open deck");
    return false;
  }

  const CardRef& ref = deck.cardRefs[index];
  if (!file.seek(ref.offset)) {
    file.close();
    setError(error, "Cannot seek deck");
    return false;
  }

  std::string line;
  readLine(file, line);
  file.close();

  const char delimiter = hasSuffixIgnoreCase(deck.path, ".tsv") ? '\t' : ',';
  std::string front;
  std::string back;
  if (!parseDelimitedLine(trim(line), delimiter, front, back)) {
    setError(error, "Cannot read card");
    return false;
  }

  card.front = truncateBytes(front, kMaxFrontBytes);
  card.back = truncateBytes(back, kMaxBackBytes);
  card.hash = ref.hash;
  return true;
}

bool loadProgress(Deck& deck, std::string* error) {
  deck.progress.clear();
  deck.totalReviews = 0;
  deck.totalAgain = 0;
  deck.totalHard = 0;
  deck.totalGood = 0;
  deck.totalEasy = 0;
  deck.totalLapses = 0;
  deck.sessionCount = 0;
  deck.totalSessions = 0;
  deck.lastStudiedSession = 0;

  HalFile file;
  const std::string path = progressPathForDeck(deck.path);
  if (!Storage.openFileForRead(TAG, path, file)) {
    return true;
  }

  ProgressHeaderV1 headerPrefix{};
  if (file.read(&headerPrefix, sizeof(headerPrefix)) != static_cast<int>(sizeof(headerPrefix)) ||
      headerPrefix.magic != kProgressMagic ||
      (headerPrefix.version != kProgressVersionV1 && headerPrefix.version != kProgressVersion)) {
    file.close();
    setError(error, "Progress file is incompatible");
    return false;
  }

  deck.totalReviews = headerPrefix.totalReviews;
  deck.sessionCount = headerPrefix.sessionCount;
  deck.totalSessions = headerPrefix.totalSessions;

  if (headerPrefix.version == kProgressVersion) {
    ProgressHeader rest{};
    rest.magic = headerPrefix.magic;
    rest.version = headerPrefix.version;
    rest.recordCount = headerPrefix.recordCount;
    rest.deckHash = headerPrefix.deckHash;
    rest.totalReviews = headerPrefix.totalReviews;
    rest.sessionCount = headerPrefix.sessionCount;
    rest.totalSessions = headerPrefix.totalSessions;
    if (file.read(reinterpret_cast<uint8_t*>(&rest) + sizeof(headerPrefix), sizeof(rest) - sizeof(headerPrefix)) !=
        static_cast<int>(sizeof(rest) - sizeof(headerPrefix))) {
      file.close();
      setError(error, "Progress file is truncated");
      return false;
    }
    deck.totalAgain = rest.totalAgain;
    deck.totalHard = rest.totalHard;
    deck.totalGood = rest.totalGood;
    deck.totalEasy = rest.totalEasy;
    deck.totalLapses = rest.totalLapses;
    deck.lastStudiedSession = rest.lastStudiedSession;
  }

  const uint16_t count = std::min<uint16_t>(headerPrefix.recordCount, static_cast<uint16_t>(kMaxLoadedCards));
  deck.progress.reserve(count);
  for (uint16_t i = 0; i < count; ++i) {
    ProgressRecord record{};
    if (headerPrefix.version == kProgressVersionV1) {
      ProgressRecordV1 diskRecord{};
      if (file.read(&diskRecord, sizeof(diskRecord)) != static_cast<int>(sizeof(diskRecord))) break;
      record.cardHash = diskRecord.cardHash;
      record.reviewCount = diskRecord.reviewCount;
      record.lapseCount = diskRecord.lapseCount;
      record.intervalSessions = diskRecord.intervalSessions;
      record.easePermille = diskRecord.easePermille;
      record.dueSession = diskRecord.dueSession;
      record.lastRating = diskRecord.lastRating;
      if (record.lastRating == static_cast<uint8_t>(Rating::Again)) {
        record.againCount = record.lapseCount > 0 ? record.lapseCount : 1;
      } else if (record.lastRating == static_cast<uint8_t>(Rating::Hard)) {
        record.hardCount = 1;
      } else if (record.lastRating == static_cast<uint8_t>(Rating::Good)) {
        record.goodCount = 1;
      } else if (record.lastRating == static_cast<uint8_t>(Rating::Easy)) {
        record.easyCount = 1;
      }
      if (record.reviewCount > 0) record.lastReviewedSession = deck.sessionCount;
    } else {
      ProgressRecordV2 diskRecord{};
      if (file.read(&diskRecord, sizeof(diskRecord)) != static_cast<int>(sizeof(diskRecord))) break;
      record.cardHash = diskRecord.cardHash;
      record.reviewCount = diskRecord.reviewCount;
      record.lapseCount = diskRecord.lapseCount;
      record.intervalSessions = diskRecord.intervalSessions;
      record.easePermille = diskRecord.easePermille;
      record.dueSession = diskRecord.dueSession;
      record.lastReviewedSession = diskRecord.lastReviewedSession;
      record.againCount = diskRecord.againCount;
      record.hardCount = diskRecord.hardCount;
      record.goodCount = diskRecord.goodCount;
      record.easyCount = diskRecord.easyCount;
      record.lastRating = diskRecord.lastRating;
    }
    if (record.cardHash != 0) deck.progress.push_back(record);
  }
  file.close();

  if (headerPrefix.version == kProgressVersionV1) {
    for (const ProgressRecord& record : deck.progress) {
      deck.totalAgain += record.againCount;
      deck.totalHard += record.hardCount;
      deck.totalGood += record.goodCount;
      deck.totalEasy += record.easyCount;
      deck.totalLapses += record.lapseCount;
    }
    if (deck.lastStudiedSession == 0 && deck.totalReviews > 0) {
      deck.lastStudiedSession = deck.sessionCount;
    }
  }
  return true;
}

bool saveProgress(const Deck& deck) {
  ensureDirectories();
  HalFile file;
  const std::string path = progressPathForDeck(deck.path);
  if (!Storage.openFileForWrite(TAG, path, file)) {
    LOG_ERR(TAG, "failed to save progress: %s", path.c_str());
    return false;
  }

  ProgressHeader header{kProgressMagic,
                        kProgressVersion,
                        clampU16(deck.progress.size()),
                        deck.deckHash,
                        deck.totalReviews,
                        deck.sessionCount,
                        deck.totalSessions,
                        deck.totalAgain,
                        deck.totalHard,
                        deck.totalGood,
                        deck.totalEasy,
                        deck.totalLapses,
                        deck.lastStudiedSession};
  bool ok = file.write(&header, sizeof(header)) == sizeof(header);
  for (const ProgressRecord& record : deck.progress) {
    ProgressRecordV2 diskRecord{};
    diskRecord.cardHash = record.cardHash;
    diskRecord.reviewCount = record.reviewCount;
    diskRecord.lapseCount = record.lapseCount;
    diskRecord.intervalSessions = record.intervalSessions;
    diskRecord.easePermille = record.easePermille;
    diskRecord.dueSession = record.dueSession;
    diskRecord.lastReviewedSession = record.lastReviewedSession;
    diskRecord.againCount = record.againCount;
    diskRecord.hardCount = record.hardCount;
    diskRecord.goodCount = record.goodCount;
    diskRecord.easyCount = record.easyCount;
    diskRecord.lastRating = record.lastRating;
    if (file.write(&diskRecord, sizeof(diskRecord)) != sizeof(diskRecord)) {
      ok = false;
      break;
    }
  }
  file.close();
  return ok;
}

DeckSummary summarizeDeck(const std::string& path) {
  DeckSummary summary;
  summary.path = path;
  summary.title = deckDisplayName(path);

  Deck deck;
  std::string error;
  if (!loadDeck(path, deck, &error)) {
    summary.error = error;
    return summary;
  }
  loadProgress(deck, nullptr);

  summary.totalCards = clampU16(deck.cardRefs.size());
  for (const CardRef& card : deck.cardRefs) {
    const ProgressRecord* record = findProgress(deck, card.hash);
    if (record == nullptr) {
      ++summary.newCards;
    } else {
      ++summary.reviewedCards;
      if (record->dueSession <= deck.sessionCount) {
        ++summary.dueCards;
      }
      if (record->reviewCount > 0 && record->intervalSessions < 4) {
        ++summary.learningCards;
      } else if (record->intervalSessions >= 4) {
        ++summary.matureCards;
      }
    }
  }
  summary.totalReviews = deck.totalReviews;
  summary.totalAgain = deck.totalAgain;
  summary.totalHard = deck.totalHard;
  summary.totalGood = deck.totalGood;
  summary.totalEasy = deck.totalEasy;
  summary.totalLapses = deck.totalLapses;
  summary.sessionCount = deck.sessionCount;
  summary.totalSessions = deck.totalSessions;
  summary.lastStudiedSession = deck.lastStudiedSession;
  const uint32_t successful = deck.totalHard + deck.totalGood + deck.totalEasy;
  if (deck.totalReviews > 0) {
    summary.retentionPermille = clampU16((successful * 1000UL) / deck.totalReviews);
  }
  summary.valid = true;
  return summary;
}

std::vector<int> buildReviewQueue(const Deck& deck) {
  std::vector<int> queue;
  queue.reserve(deck.cardRefs.size());
  for (size_t i = 0; i < deck.cardRefs.size(); ++i) {
    const ProgressRecord* record = findProgress(deck, deck.cardRefs[i].hash);
    if (record == nullptr || record->dueSession <= deck.sessionCount) {
      queue.push_back(static_cast<int>(i));
    }
  }
  return queue;
}

ProgressRecord* findProgress(Deck& deck, uint32_t cardHash) {
  for (ProgressRecord& record : deck.progress) {
    if (record.cardHash == cardHash) return &record;
  }
  return nullptr;
}

const ProgressRecord* findProgress(const Deck& deck, uint32_t cardHash) {
  for (const ProgressRecord& record : deck.progress) {
    if (record.cardHash == cardHash) return &record;
  }
  return nullptr;
}

ProgressRecord& findOrCreateProgress(Deck& deck, uint32_t cardHash) {
  if (ProgressRecord* record = findProgress(deck, cardHash)) return *record;
  deck.progress.push_back(ProgressRecord{});
  deck.progress.back().cardHash = cardHash;
  return deck.progress.back();
}

void startSession(Deck& deck) {
  if (deck.sessionCount < UINT16_MAX) ++deck.sessionCount;
  if (deck.totalSessions < UINT16_MAX) ++deck.totalSessions;
}

void applyRating(Deck& deck, const Card& card, Rating rating) {
  ProgressRecord& record = findOrCreateProgress(deck, card.hash);
  if (record.easePermille == 0) record.easePermille = 2500;

  const bool firstReview = record.reviewCount == 0;
  if (record.reviewCount < UINT16_MAX) ++record.reviewCount;
  if (deck.totalReviews < UINT32_MAX) ++deck.totalReviews;
  record.lastRating = static_cast<uint8_t>(rating);

  int ease = record.easePermille;
  uint32_t interval = record.intervalSessions;

  switch (rating) {
    case Rating::Again:
      if (record.lapseCount < UINT16_MAX) ++record.lapseCount;
      if (record.againCount < UINT16_MAX) ++record.againCount;
      if (deck.totalAgain < UINT32_MAX) ++deck.totalAgain;
      if (deck.totalLapses < UINT32_MAX) ++deck.totalLapses;
      ease = std::max(1300, ease - 200);
      interval = 1;
      break;
    case Rating::Hard:
      if (record.hardCount < UINT16_MAX) ++record.hardCount;
      if (deck.totalHard < UINT32_MAX) ++deck.totalHard;
      ease = std::max(1300, ease - 100);
      interval = std::max<uint32_t>(1, interval + 1);
      break;
    case Rating::Good:
      if (record.goodCount < UINT16_MAX) ++record.goodCount;
      if (deck.totalGood < UINT32_MAX) ++deck.totalGood;
      interval = firstReview ? 2 : std::max<uint32_t>(2, (interval * static_cast<uint32_t>(ease)) / 1000U);
      break;
    case Rating::Easy:
      if (record.easyCount < UINT16_MAX) ++record.easyCount;
      if (deck.totalEasy < UINT32_MAX) ++deck.totalEasy;
      ease = std::min(3500, ease + 150);
      interval = firstReview ? 4 : std::max<uint32_t>(4, (interval * static_cast<uint32_t>(ease) * 13U) / 10000U);
      break;
  }

  record.easePermille = static_cast<uint16_t>(ease);
  record.intervalSessions = clampU16(interval);
  record.dueSession = clampU16(static_cast<uint32_t>(deck.sessionCount) + interval);
  record.lastReviewedSession = deck.sessionCount;
  deck.lastStudiedSession = deck.sessionCount;
}

}  // namespace flashcards
