#include "CrossPointWebServer.h"

#include <ArduinoJson.h>
#ifdef SIMULATOR
#include <ArduinoJsonStringCompat.h>
#endif
#include <Epub.h>
#include <FsHelpers.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Txt.h>
#include <WiFi.h>
#include <Xtc.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cctype>
#include <iterator>

#include "AppVersion.h"
#include "BookmarkStore.h"
#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/reader/ReadingStatsUtils.h"
#include "flashcards/FlashcardDeck.h"
#include "FontInstaller.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "WebDAVHandler.h"
#include "WifiCredentialStore.h"
#include "html/FilesPageHtml.generated.h"
#include "html/FlashcardsPageHtml.generated.h"
#include "html/FontsPageHtml.generated.h"
#include "html/HomePageHtml.generated.h"
#include "html/LibraryPageHtml.generated.h"
#include "html/LogoPng.generated.h"
#include "html/SettingsPageHtml.generated.h"
#include "html/StyleCss.generated.h"
#include "html/js/jszip_minJs.generated.h"
#include "util/BookMoveUtils.h"
#include "util/BookCacheUtils.h"
#include "util/StringUtils.h"

namespace {
// Folders/files to hide from the web interface file browser.
// Dot-prefixed items are hidden unless showHiddenFiles is enabled.
constexpr const char* HIDDEN_ITEMS[] = {"System Volume Information", "XTCache"};
constexpr char FLASHCARD_ROOT_DIR[] = "/.crosspoint";
constexpr char FLASHCARD_DIR[] = "/.crosspoint/flashcards";
constexpr char FLASHCARD_DECKS_DIR[] = "/.crosspoint/flashcards/decks";
constexpr size_t FLASHCARD_IMPORT_MAX_BYTES = 48U * 1024U;
constexpr size_t LIBRARY_SCAN_MAX_FILES = 800U;
constexpr uint16_t UDP_PORTS[] = {54982, 48123, 39001, 44044, 59678};
constexpr uint16_t LOCAL_UDP_PORT = 8134;

// Static pointer for WebSocket callback (WebSocketsServer requires C-style callback)
CrossPointWebServer* wsInstance = nullptr;

uint8_t enumDisplayIndexForRawValue(const SettingInfo& setting, uint8_t rawValue) {
  if (setting.enumRawValues.empty()) {
    return rawValue;
  }

  auto it = std::find(setting.enumRawValues.begin(), setting.enumRawValues.end(), rawValue);
  if (it == setting.enumRawValues.end()) {
    return 0;
  }
  return static_cast<uint8_t>(std::distance(setting.enumRawValues.begin(), it));
}

uint8_t enumRawValueForDisplayIndex(const SettingInfo& setting, uint8_t displayIndex) {
  if (setting.enumRawValues.empty()) {
    return displayIndex;
  }
  if (displayIndex >= setting.enumRawValues.size()) {
    return setting.enumRawValues.front();
  }
  return setting.enumRawValues[displayIndex];
}

// WebSocket upload state
HalFile wsUploadFile;
String wsUploadFileName;
String wsUploadPath;
size_t wsUploadSize = 0;
size_t wsUploadReceived = 0;
unsigned long wsUploadStartTime = 0;
bool wsUploadInProgress = false;
uint8_t wsUploadClientNum = 255;  // 255 = no active upload client
size_t wsLastProgressSent = 0;
String wsLastCompleteName;
size_t wsLastCompleteSize = 0;
unsigned long wsLastCompleteAt = 0;

String normalizeWebPath(const String& inputPath) {
  if (inputPath.isEmpty() || inputPath == "/") {
    return "/";
  }
  std::string normalized = FsHelpers::normalisePath(inputPath.c_str());
  String result = normalized.c_str();
  if (result.isEmpty()) {
    return "/";
  }
  if (!result.startsWith("/")) {
    result = "/" + result;
  }
  if (result.length() > 1 && result.endsWith("/")) {
    result = result.substring(0, result.length() - 1);
  }
  return result;
}

bool isProtectedPath(const String& path) {
  if (path == FLASHCARD_DECKS_DIR || path.startsWith(String(FLASHCARD_DECKS_DIR) + "/")) {
    return false;
  }

  // Check every segment of the path, not just the last one.
  // This prevents access to e.g. /.hidden/somefile or /System Volume Information/foo
  int start = 0;
  while (start < (int)path.length()) {
    if (path.charAt(start) == '/') {
      start++;
      continue;
    }
    int end = path.indexOf('/', start);
    if (end == -1) end = path.length();

    String segment = path.substring(start, end);

    if (!SETTINGS.showHiddenFiles && segment.startsWith(".")) return true;

    for (const auto* item : HIDDEN_ITEMS) {
      if (segment.equals(item)) return true;
    }

    start = end + 1;
  }

  return false;
}

bool ensureFlashcardDecksDir() {
  if (!Storage.exists(FLASHCARD_ROOT_DIR) && !Storage.mkdir(FLASHCARD_ROOT_DIR)) return false;
  if (!Storage.exists(FLASHCARD_DIR) && !Storage.mkdir(FLASHCARD_DIR)) return false;
  if (!Storage.exists(FLASHCARD_DECKS_DIR) && !Storage.mkdir(FLASHCARD_DECKS_DIR)) return false;
  return Storage.exists(FLASHCARD_DECKS_DIR);
}

bool isFlashcardDecksPath(const String& path) {
  return path == FLASHCARD_DECKS_DIR || path.startsWith(String(FLASHCARD_DECKS_DIR) + "/");
}

bool isFlashcardDeckFilePath(const String& path) {
  if (!isFlashcardDecksPath(path) || path == FLASHCARD_DECKS_DIR) return false;
  return FsHelpers::checkFileExtension(path, ".tsv") || FsHelpers::checkFileExtension(path, ".csv");
}

bool isLibraryBookFile(const String& path) {
  return FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path) || FsHelpers::hasTxtExtension(path) ||
         FsHelpers::hasMarkdownExtension(path);
}

String parentPathOf(const String& path) {
  const int slash = path.lastIndexOf('/');
  if (slash <= 0) return "/";
  return path.substring(0, slash);
}

String fileNameOf(const String& path) {
  const int slash = path.lastIndexOf('/');
  return slash < 0 ? path : path.substring(slash + 1);
}

String extensionOf(const String& name) {
  const int dot = name.lastIndexOf('.');
  return dot < 0 ? "" : name.substring(dot);
}

String baseNameWithoutExtension(const String& name) {
  const int dot = name.lastIndexOf('.');
  return dot < 0 ? name : name.substring(0, dot);
}

String normalizedDuplicateKey(const String& title, const String& name, const size_t size) {
  String basis = title.isEmpty() ? baseNameWithoutExtension(name) : title;
  basis.toLowerCase();
  String normalized;
  normalized.reserve(basis.length() + 16);
  for (size_t i = 0; i < basis.length(); ++i) {
    const char c = basis.charAt(i);
    if (std::isalnum(static_cast<unsigned char>(c))) {
      normalized += c;
    }
  }
  normalized += ":";
  normalized += String(static_cast<unsigned long>(size));
  return normalized;
}

const char* readStateForPath(const String& path) {
  if (path == "/Read" || path.startsWith("/Read/")) return "read";
  if (path == "/Unread" || path.startsWith("/Unread/")) return "unread";
  return "unknown";
}

std::string cachePathForBookPath(const String& path) {
  const std::string p = path.c_str();
  if (FsHelpers::hasEpubExtension(p)) return Epub::cachePathForFilePath(p, "/.crosspoint");
  if (FsHelpers::hasXtcExtension(p)) return Xtc(p, "/.crosspoint").getCachePath();
  if (FsHelpers::hasTxtExtension(p) || FsHelpers::hasMarkdownExtension(p)) return Txt(p, "/.crosspoint").getCachePath();
  return "";
}

bool ensureDirectoryPath(const String& rawPath) {
  const String path = normalizeWebPath(rawPath);
  if (path.isEmpty() || path == "/") return true;
  if (isProtectedPath(path)) return false;

  String partial = "";
  int start = 1;
  while (start <= static_cast<int>(path.length())) {
    int slash = path.indexOf('/', start);
    if (slash < 0) slash = path.length();
    const String segment = path.substring(start, slash);
    if (!segment.isEmpty()) {
      const String sanitized = StringUtils::sanitizeFilename(segment.c_str()).c_str();
      if (sanitized != segment) {
        return false;
      }
      partial += "/";
      partial += segment;
      if (!Storage.exists(partial.c_str()) && !Storage.mkdir(partial.c_str())) {
        return false;
      }
      HalFile dir = Storage.open(partial.c_str());
      if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return false;
      }
      dir.close();
    }
    start = slash + 1;
    if (slash == static_cast<int>(path.length())) break;
  }
  return true;
}

String uniqueDestinationPath(const String& destinationDir, const String& itemName) {
  String path = destinationDir;
  if (!path.endsWith("/")) path += "/";
  path += itemName;
  if (!Storage.exists(path.c_str())) return path;

  const String base = baseNameWithoutExtension(itemName);
  const String ext = extensionOf(itemName);
  for (int suffix = 2; suffix < 1000; ++suffix) {
    String candidate = destinationDir;
    if (!candidate.endsWith("/")) candidate += "/";
    candidate += base;
    candidate += " (";
    candidate += suffix;
    candidate += ")";
    candidate += ext;
    if (!Storage.exists(candidate.c_str())) return candidate;
  }
  return "";
}

bool migrateMovedBookState(const String& oldPath, const String& newPath) {
  const std::string oldPathStd = oldPath.c_str();
  const std::string newPathStd = newPath.c_str();
  if (FsHelpers::hasEpubExtension(oldPathStd)) {
    Epub epub(oldPathStd, "/.crosspoint");
    epub.load(false, true);
    return BookMoveUtils::migrateMovedEpubState(oldPathStd, newPathStd, epub.getCachePath(), epub.getTitle(),
                                                epub.getAuthor(), true);
  }

  const std::string oldCache = cachePathForBookPath(oldPath);
  const std::string newCache = cachePathForBookPath(newPath);
  bool ok = true;
  if (!oldCache.empty() && !newCache.empty() && Storage.exists(oldCache.c_str())) {
    if (Storage.exists(newCache.c_str())) Storage.removeDir(newCache.c_str());
    if (!Storage.rename(oldCache.c_str(), newCache.c_str())) {
      LOG_ERR("WEB", "Failed to migrate cache dir %s -> %s", oldCache.c_str(), newCache.c_str());
      ok = false;
    }
  }
  RECENT_BOOKS.updatePath(oldPathStd, newPathStd, oldCache, newCache);
  if (APP_STATE.openEpubPath == oldPathStd) {
    APP_STATE.openEpubPath = newPathStd;
    APP_STATE.saveToFile();
  }
  return ok;
}

bool moveLibraryFile(const String& itemPath, const String& destinationDir, const bool renameCollisions, String& newPath,
                     String& error) {
  if (itemPath.isEmpty() || itemPath == "/" || destinationDir.isEmpty()) {
    error = "Invalid path";
    return false;
  }
  if (isProtectedPath(itemPath) || isProtectedPath(destinationDir)) {
    error = "Protected path";
    return false;
  }
  if (!Storage.exists(itemPath.c_str())) {
    error = "Item not found";
    return false;
  }
  if (!ensureDirectoryPath(destinationDir)) {
    error = "Could not create destination";
    return false;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file || file.isDirectory()) {
    if (file) file.close();
    error = "Only files can be moved";
    return false;
  }

  newPath = renameCollisions ? uniqueDestinationPath(destinationDir, fileNameOf(itemPath)) : destinationDir;
  if (!renameCollisions) {
    if (!newPath.endsWith("/")) newPath += "/";
    newPath += fileNameOf(itemPath);
    if (Storage.exists(newPath.c_str())) {
      file.close();
      error = "Target already exists";
      return false;
    }
  }
  if (newPath.isEmpty() || newPath == itemPath) {
    file.close();
    error = newPath == itemPath ? "Already in destination" : "No available destination name";
    return newPath == itemPath;
  }

  const bool success = file.rename(newPath.c_str());
  file.close();
  if (!success) {
    error = "Move failed";
    return false;
  }
  migrateMovedBookState(itemPath, newPath);
  return true;
}

String normalizeFlashcardDeckPathArg(WebServer* server) {
  if (server == nullptr || !server->hasArg("path")) return "";
  const String path = normalizeWebPath(server->arg("path"));
  return isFlashcardDeckFilePath(path) ? path : "";
}

String flashcardDeckPathForName(const String& rawName, const char* fallbackExt = ".tsv") {
  String name = StringUtils::sanitizeFilename(rawName.c_str()).c_str();
  if (name.isEmpty()) return "";
  if (!FsHelpers::checkFileExtension(name, ".tsv") && !FsHelpers::checkFileExtension(name, ".csv")) {
    name += fallbackExt;
  }
  String path = FLASHCARD_DECKS_DIR;
  path += "/";
  path += name;
  return isFlashcardDeckFilePath(path) ? path : "";
}

String jsonBody(WebServer* server) {
  if (server == nullptr) return "";
  if (server->hasArg("plain")) return server->arg("plain");
  return "";
}

void addFlashcardSummaryJson(JsonDocument& doc, const flashcards::DeckSummary& summary) {
  doc["path"] = summary.path;
  doc["title"] = summary.title;
  doc["valid"] = summary.valid;
  doc["totalCards"] = summary.totalCards;
  doc["newCards"] = summary.newCards;
  doc["dueCards"] = summary.dueCards;
  doc["reviewedCards"] = summary.reviewedCards;
  doc["learningCards"] = summary.learningCards;
  doc["matureCards"] = summary.matureCards;
  doc["totalReviews"] = summary.totalReviews;
  doc["totalAgain"] = summary.totalAgain;
  doc["totalHard"] = summary.totalHard;
  doc["totalGood"] = summary.totalGood;
  doc["totalEasy"] = summary.totalEasy;
  doc["totalLapses"] = summary.totalLapses;
  doc["sessionCount"] = summary.sessionCount;
  doc["totalSessions"] = summary.totalSessions;
  doc["lastStudiedSession"] = summary.lastStudiedSession;
  doc["retentionPermille"] = summary.retentionPermille;
  if (!summary.error.empty()) {
    doc["error"] = summary.error;
  }
}

String dateString(const ReadingStatsDate& date) {
  if (!date.isValid()) return "";
  char buf[16];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u", static_cast<unsigned>(date.year), static_cast<unsigned>(date.month),
           static_cast<unsigned>(date.day));
  return buf;
}

void addGlobalStatsJson(JsonObject doc, const GlobalReadingStats& stats) {
  ReadingStatsDateTime today;
  const bool hasToday = getCurrentLocalReadingStatsDateTime(today);
  doc["totalSessions"] = stats.totalSessions;
  doc["totalReadingSeconds"] = stats.totalReadingSeconds;
  doc["totalPagesTurned"] = stats.totalPagesTurned;
  doc["completedBooks"] = stats.completedBooks;
  doc["currentStreak"] = hasToday ? stats.currentReadingStreak(&today.date) : stats.currentReadingStreak(nullptr);
  doc["longestStreak"] = stats.displayLongestReadingStreak();
  JsonArray timeBuckets = doc["timeOfDaySeconds"].to<JsonArray>();
  for (const uint32_t value : stats.timeOfDaySeconds) timeBuckets.add(value);
  JsonArray dayBuckets = doc["dayOfWeekSeconds"].to<JsonArray>();
  for (const uint32_t value : stats.dayOfWeekSeconds) dayBuckets.add(value);
}

void sendJsonDoc(WebServer* server, JsonDocument& doc, char* output, const size_t outputSize, bool& seenFirst) {
  const size_t written = serializeJson(doc, output, outputSize);
  if (written >= outputSize) return;
  if (seenFirst) {
    server->sendContent(",");
  } else {
    seenFirst = true;
  }
  server->sendContent(output);
}

bool isSupportedBookmarkType(const String& type) { return type == "epub" || type == "xtc" || type == "txt"; }

bool isSupportedClippingType(const String& type) { return type == "epub"; }
}  // namespace

// File listing page template - now using generated headers:
// - HomePageHtml (from html/HomePage.html)
// - FilesPageHeaderHtml (from html/FilesPageHeader.html)
// - FilesPageFooterHtml (from html/FilesPageFooter.html)
CrossPointWebServer::CrossPointWebServer() {}

CrossPointWebServer::~CrossPointWebServer() { stop(); }

void CrossPointWebServer::begin() {
  if (running) {
    LOG_DBG("WEB", "Web server already running");
    return;
  }

  // Check if we have a valid network connection (either STA connected or AP mode)
  const wifi_mode_t wifiMode = WiFi.getMode();
  const bool isStaConnected = (wifiMode & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
  const bool isInApMode = (wifiMode & WIFI_MODE_AP) && (WiFi.softAPgetStationNum() >= 0);  // AP is running

  if (!isStaConnected && !isInApMode) {
    LOG_DBG("WEB", "Cannot start webserver - no valid network (mode=%d, status=%d)", wifiMode, WiFi.status());
    return;
  }

  // Store AP mode flag for later use (e.g., in handleStatus)
  apMode = isInApMode;

  LOG_DBG("WEB", "[MEM] Free heap before begin: %d bytes", ESP.getFreeHeap());
  LOG_DBG("WEB", "Network mode: %s", apMode ? "AP" : "STA");

  LOG_DBG("WEB", "Creating web server on port %d...", port);
  server.reset(new WebServer(port));

  // Disable WiFi sleep to improve responsiveness and prevent 'unreachable' errors.
  // This is critical for reliable web server operation on ESP32.
  WiFi.setSleep(false);
  // Default varies by ESP32 core version. The activity's loss-recovery loop
  // relies on driver retries during transient disconnects.
  WiFi.setAutoReconnect(true);

  // Note: WebServer class doesn't have setNoDelay() in the standard ESP32 library.
  // We rely on disabling WiFi sleep for responsiveness.

  LOG_DBG("WEB", "[MEM] Free heap after WebServer allocation: %d bytes", ESP.getFreeHeap());

  if (!server) {
    LOG_ERR("WEB", "Failed to create WebServer!");
    return;
  }

  // Setup routes
  LOG_DBG("WEB", "Setting up routes...");
  server->on("/", HTTP_GET, [this] { handleRoot(); });
  server->on("/files", HTTP_GET, [this] { handleFileList(); });
  server->on("/library", HTTP_GET, [this] { handleLibraryPage(); });
  server->on("/flashcards", HTTP_GET, [this] { handleFlashcardsPage(); });
  server->on("/js/jszip.min.js", HTTP_GET, [this] { handleJszip(); });
  server->on("/style.css", HTTP_GET, [this] { handleStyleCss(); });
  server->on("/logo.png", HTTP_GET, [this] { handleLogo(); });

  server->on("/api/status", HTTP_GET, [this] { handleStatus(); });
  server->on("/api/files", HTTP_GET, [this] { handleFileListData(); });
  server->on("/api/library/scan", HTTP_GET, [this] { handleLibraryScan(); });
  server->on("/api/library/saved", HTTP_GET, [this] { handleLibrarySaved(); });
  server->on("/api/library/saved/delete", HTTP_POST, [this] { handleLibrarySavedDelete(); });
  server->on("/api/library/stats", HTTP_GET, [this] { handleLibraryStats(); });
  server->on("/api/library/ensure-folder", HTTP_POST, [this] { handleLibraryEnsureFolder(); });
  server->on("/api/library/bulk-move", HTTP_POST, [this] { handleLibraryBulkMove(); });
  server->on("/api/flashcards/decks", HTTP_GET, [this] { handleFlashcardDecks(); });
  server->on("/api/flashcards/deck", HTTP_GET, [this] { handleFlashcardDeckDetail(); });
  server->on("/api/flashcards/export", HTTP_GET, [this] { handleFlashcardExport(); });
  server->on("/api/flashcards/import", HTTP_POST, [this] { handleFlashcardImport(); });
  server->on("/api/flashcards/reset", HTTP_POST, [this] { handleFlashcardReset(); });
  server->on("/api/flashcards/delete", HTTP_POST, [this] { handleFlashcardDelete(); });
  server->on("/api/flashcards/rename", HTTP_POST, [this] { handleFlashcardRename(); });
  server->on("/download", HTTP_GET, [this] { handleDownload(); });

  // Upload endpoint with special handling for multipart form data
  server->on("/upload", HTTP_POST, [this] { handleUploadPost(upload); }, [this] { handleUpload(upload); });

  // Create folder endpoint
  server->on("/mkdir", HTTP_POST, [this] { handleCreateFolder(); });

  // Rename file endpoint
  server->on("/rename", HTTP_POST, [this] { handleRename(); });

  // Move file endpoint
  server->on("/move", HTTP_POST, [this] { handleMove(); });

  // Delete file/folder endpoint
  server->on("/delete", HTTP_POST, [this] { handleDelete(); });

  // Settings endpoints
  server->on("/settings", HTTP_GET, [this] { handleSettingsPage(); });
  server->on("/api/settings", HTTP_GET, [this] { handleGetSettings(); });
  server->on("/api/settings", HTTP_POST, [this] { handlePostSettings(); });

  // Font management endpoints
  server->on("/fonts", HTTP_GET, [this] { handleFontsPage(); });
  server->on("/api/fonts", HTTP_GET, [this] { handleFontList(); });
  server->on("/api/fonts/upload", HTTP_POST, [this] { handleFontUpload(); }, [this] { handleFontUploadData(); });
  server->on("/api/fonts/delete", HTTP_POST, [this] { handleFontDelete(); });

  // OPDS server endpoints
  server->on("/api/opds", HTTP_GET, [this] { handleGetOpdsServers(); });
  server->on("/api/opds", HTTP_POST, [this] { handlePostOpdsServer(); });
  server->on("/api/opds/delete", HTTP_POST, [this] { handleDeleteOpdsServer(); });

  // Wi-Fi credential endpoints
  server->on("/api/wifi", HTTP_GET, [this] { handleGetWifiNetworks(); });
  server->on("/api/wifi", HTTP_POST, [this] { handlePostWifiNetwork(); });
  server->on("/api/wifi/delete", HTTP_POST, [this] { handleDeleteWifiNetwork(); });

  server->onNotFound([this] { handleNotFound(); });
  LOG_DBG("WEB", "[MEM] Free heap after route setup: %d bytes", ESP.getFreeHeap());

  // Collect WebDAV headers and register handler
  const char* davHeaders[] = {"Depth", "Destination", "Overwrite", "If", "Lock-Token", "Timeout"};
  server->collectHeaders(davHeaders, 6);
  server->addHandler(new WebDAVHandler());  // Note: WebDAVHandler will be deleted by WebServer when server is stopped
  LOG_DBG("WEB", "WebDAV handler initialized");

  server->begin();

  // Start WebSocket server for fast binary uploads
  LOG_DBG("WEB", "Starting WebSocket server on port %d...", wsPort);
  wsServer.reset(new WebSocketsServer(wsPort));
  wsInstance = const_cast<CrossPointWebServer*>(this);
  wsServer->begin();
  wsServer->onEvent(wsEventCallback);
  LOG_DBG("WEB", "WebSocket server started");

  udpActive = udp.begin(LOCAL_UDP_PORT);
  LOG_DBG("WEB", "Discovery UDP %s on port %d", udpActive ? "enabled" : "failed", LOCAL_UDP_PORT);

  running = true;

  LOG_DBG("WEB", "Web server started on port %d", port);
  // Show the correct IP based on network mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  LOG_DBG("WEB", "Access at http://%s/", ipAddr.c_str());
  LOG_DBG("WEB", "WebSocket at ws://%s:%d/", ipAddr.c_str(), wsPort);
  LOG_DBG("WEB", "[MEM] Free heap after server.begin(): %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServer::abortWsUpload(const char* tag) {
  // Explicit close() required: file-scope global persists beyond function scope
  wsUploadFile.close();
  String filePath = wsUploadPath;
  if (!filePath.endsWith("/")) filePath += "/";
  filePath += wsUploadFileName;
  if (Storage.remove(filePath.c_str())) {
    LOG_DBG(tag, "Deleted incomplete upload: %s", filePath.c_str());
  } else {
    LOG_DBG(tag, "Failed to delete incomplete upload: %s", filePath.c_str());
  }
  wsUploadInProgress = false;
  wsUploadClientNum = 255;
  wsLastProgressSent = 0;
}

void CrossPointWebServer::stop() {
  if (!running || !server) {
    LOG_DBG("WEB", "stop() called but already stopped (running=%d, server=%p)", running, server.get());
    return;
  }

  LOG_DBG("WEB", "STOP INITIATED - setting running=false first");
  running = false;  // Set this FIRST to prevent handleClient from using server

  LOG_DBG("WEB", "[MEM] Free heap before stop: %d bytes", ESP.getFreeHeap());

  // Close any in-progress WebSocket upload and remove partial file
  if (wsUploadInProgress && wsUploadFile) {
    abortWsUpload("WEB");
  }

  // Stop WebSocket server
  if (wsServer) {
    LOG_DBG("WEB", "Stopping WebSocket server...");
    wsServer->close();
    wsServer.reset();
    wsInstance = nullptr;
    LOG_DBG("WEB", "WebSocket server stopped");
  }

  if (udpActive) {
    udp.stop();
    udpActive = false;
  }

  // Brief delay to allow any in-flight handleClient() calls to complete
  delay(20);

  server->stop();
  LOG_DBG("WEB", "[MEM] Free heap after server->stop(): %d bytes", ESP.getFreeHeap());

  // Brief delay before deletion
  delay(10);

  server.reset();
  LOG_DBG("WEB", "Web server stopped and deleted");
  LOG_DBG("WEB", "[MEM] Free heap after delete server: %d bytes", ESP.getFreeHeap());

  // Note: Static upload variables (uploadFileName, uploadPath, uploadError) are declared
  // later in the file and will be cleared when they go out of scope or on next upload
  LOG_DBG("WEB", "[MEM] Free heap final: %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServer::handleClient() {
  static unsigned long lastDebugPrint = 0;

  // Check running flag FIRST before accessing server
  if (!running) {
    return;
  }

  // Double-check server pointer is valid
  if (!server) {
    LOG_DBG("WEB", "WARNING: handleClient called with null server!");
    return;
  }

  // Print debug every 10 seconds to confirm handleClient is being called
  if (millis() - lastDebugPrint > 10000) {
    LOG_DBG("WEB", "handleClient active, server running on port %d", port);
    lastDebugPrint = millis();
  }

  server->handleClient();

  // Handle WebSocket events
  if (wsServer) {
    wsServer->loop();
  }

  // Respond to discovery broadcasts
  if (udpActive) {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      char buffer[16];
      int len = udp.read(buffer, sizeof(buffer) - 1);
      if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "hello") == 0) {
          String hostname = WiFi.getHostname();
          if (hostname.isEmpty()) {
            hostname = "crosspoint";
          }
          String message = "crosspoint (on " + hostname + ");" + String(wsPort);
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write(reinterpret_cast<const uint8_t*>(message.c_str()), message.length());
          udp.endPacket();
        }
      }
    }
  }
}

CrossPointWebServer::WsUploadStatus CrossPointWebServer::getWsUploadStatus() const {
  WsUploadStatus status;
  status.inProgress = wsUploadInProgress;
  status.received = wsUploadReceived;
  status.total = wsUploadSize;
  status.filename = wsUploadFileName.c_str();
  status.lastCompleteName = wsLastCompleteName.c_str();
  status.lastCompleteSize = wsLastCompleteSize;
  status.lastCompleteAt = wsLastCompleteAt;
  return status;
}

static void sendHtmlContent(WebServer* server, const char* data, size_t len) {
  server->sendHeader("Content-Encoding", "gzip");
  server->send_P(200, "text/html", data, len);
}

void CrossPointWebServer::handleRoot() const {
  sendHtmlContent(server.get(), HomePageHtml, sizeof(HomePageHtml));
  LOG_DBG("WEB", "Served root page");
}

void CrossPointWebServer::handleJszip() const {
  server->sendHeader("Content-Encoding", "gzip");
  server->send_P(200, "application/javascript", jszip_minJs, jszip_minJsCompressedSize);
  LOG_DBG("WEB", "Served jszip.min.js");
}

// Shared stylesheet and logo are referenced with a content-hashed ?v= query,
// so they can be cached aggressively: a new build changes the URL.
void CrossPointWebServer::handleStyleCss() const {
  server->sendHeader("Content-Encoding", "gzip");
  server->sendHeader("Cache-Control", "public, max-age=31536000, immutable");
  server->send_P(200, "text/css", StyleCss, StyleCssCompressedSize);
  LOG_DBG("WEB", "Served style.css");
}

void CrossPointWebServer::handleLogo() const {
  // Raw PNG (already compressed); no Content-Encoding.
  server->sendHeader("Cache-Control", "public, max-age=31536000, immutable");
  server->send_P(200, "image/png", LogoPng, LogoPngSize);
  LOG_DBG("WEB", "Served logo.png");
}

void CrossPointWebServer::handleNotFound() const {
  String message = "404 Not Found\n\n";
  message += "URI: " + server->uri() + "\n";
  server->send(404, "text/plain", message);
}

void CrossPointWebServer::handleStatus() const {
  // Get correct IP based on AP vs STA mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();

  JsonDocument doc;
  doc["version"] = CROSSINK_VERSION;
  doc["ip"] = ipAddr;
  doc["mode"] = apMode ? "AP" : "STA";
  doc["rssi"] = apMode ? 0 : WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  doc["device"] = gpio.deviceIsX3() ? "X3" : "X4";

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void CrossPointWebServer::scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const {
  HalFile root = Storage.open(path);
  if (!root) {
    LOG_DBG("WEB", "Failed to open directory: %s", path);
    return;
  }

  if (!root.isDirectory()) {
    LOG_DBG("WEB", "Not a directory: %s", path);
    root.close();
    return;
  }

  LOG_DBG("WEB", "Scanning files in: %s", path);

  HalFile file = root.openNextFile();
  char name[500];
  while (file) {
    file.getName(name, sizeof(name));
    auto fileName = String(name);

    // Skip hidden items (starting with ".")
    bool shouldHide = !SETTINGS.showHiddenFiles && fileName.startsWith(".");

    // Check against explicitly hidden items list
    if (!shouldHide) {
      for (const auto* item : HIDDEN_ITEMS) {
        if (fileName.equals(item)) {
          shouldHide = true;
          break;
        }
      }
    }

    if (!shouldHide) {
      FileInfo info;
      info.name = fileName;
      info.isDirectory = file.isDirectory();

      if (info.isDirectory) {
        info.size = 0;
        info.isEpub = false;
      } else {
        info.size = file.size();
        info.isEpub = isEpubFile(info.name);
      }

      callback(info);
    }

    file.close();
    yield();               // Yield to allow WiFi and other tasks to process during long scans
    esp_task_wdt_reset();  // Reset watchdog to prevent timeout on large directories
    file = root.openNextFile();
  }
  root.close();
}

bool CrossPointWebServer::isEpubFile(const String& filename) const { return FsHelpers::hasEpubExtension(filename); }

void CrossPointWebServer::handleFileList() const {
  sendHtmlContent(server.get(), FilesPageHtml, sizeof(FilesPageHtml));
}

void CrossPointWebServer::handleLibraryPage() const {
  sendHtmlContent(server.get(), LibraryPageHtml, sizeof(LibraryPageHtml));
}

void CrossPointWebServer::handleFlashcardsPage() const {
  sendHtmlContent(server.get(), FlashcardsPageHtml, sizeof(FlashcardsPageHtml));
}

void CrossPointWebServer::handleFlashcardDecks() const {
  flashcards::ensureDirectories();

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[512];
  constexpr size_t outputSize = sizeof(output);
  JsonDocument doc;
  bool seenFirst = false;

  for (const std::string& path : flashcards::scanDeckFiles()) {
    const flashcards::DeckSummary summary = flashcards::summarizeDeck(path);
    doc.clear();
    addFlashcardSummaryJson(doc, summary);

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      LOG_DBG("WEB", "Skipping oversized flashcard deck JSON for: %s", path.c_str());
      continue;
    }

    if (seenFirst) {
      server->sendContent(",");
    } else {
      seenFirst = true;
    }
    server->sendContent(output);
  }

  server->sendContent("]");
  server->sendContent("");
}

void CrossPointWebServer::handleFlashcardDeckDetail() const {
  const String path = normalizeFlashcardDeckPathArg(server.get());
  if (path.isEmpty()) {
    server->send(400, "application/json", "{\"error\":\"Invalid deck path\"}");
    return;
  }

  flashcards::Deck deck;
  std::string error;
  const bool valid = flashcards::loadDeck(path.c_str(), deck, &error);
  if (valid) {
    flashcards::loadProgress(deck, nullptr);
  }
  const flashcards::DeckSummary summary = flashcards::summarizeDeck(path.c_str());

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("{\"summary\":");

  char output[768];
  JsonDocument doc;
  doc.clear();
  addFlashcardSummaryJson(doc, summary);
  serializeJson(doc, output, sizeof(output));
  server->sendContent(output);

  server->sendContent(",\"cards\":[");
  if (valid) {
    bool seenFirst = false;
    for (size_t i = 0; i < deck.cardRefs.size(); ++i) {
      flashcards::Card card;
      if (!flashcards::loadCard(deck, i, card, nullptr)) continue;
      const flashcards::ProgressRecord* record = flashcards::findProgress(deck, card.hash);
      doc.clear();
      doc["hash"] = card.hash;
      doc["front"] = card.front;
      if (record != nullptr) {
        doc["reviewCount"] = record->reviewCount;
        doc["lapseCount"] = record->lapseCount;
        doc["intervalSessions"] = record->intervalSessions;
        doc["easePermille"] = record->easePermille;
        doc["dueSession"] = record->dueSession;
        doc["lastReviewedSession"] = record->lastReviewedSession;
        doc["lastRating"] = record->lastRating;
        doc["againCount"] = record->againCount;
        doc["hardCount"] = record->hardCount;
        doc["goodCount"] = record->goodCount;
        doc["easyCount"] = record->easyCount;
      } else {
        doc["reviewCount"] = 0;
      }
      const size_t written = serializeJson(doc, output, sizeof(output));
      if (written >= sizeof(output)) continue;
      if (seenFirst) {
        server->sendContent(",");
      } else {
        seenFirst = true;
      }
      server->sendContent(output);
      yield();
      esp_task_wdt_reset();
    }
  }
  server->sendContent("]}");
  server->sendContent("");
}

void CrossPointWebServer::handleFlashcardExport() const {
  flashcards::ensureDirectories();
  const bool singleDeck = server->hasArg("path");
  const String requestedPath = singleDeck ? normalizeFlashcardDeckPathArg(server.get()) : "";
  if (singleDeck && requestedPath.isEmpty()) {
    server->send(400, "application/json", "{\"error\":\"Invalid deck path\"}");
    return;
  }

  server->sendHeader("Content-Disposition", "attachment; filename=\"crossink-flashcards-progress.json\"");
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("{\"version\":1,\"format\":\"crossink-flashcards-progress\",\"decks\":[");

  std::vector<std::string> paths;
  if (singleDeck) {
    paths.push_back(requestedPath.c_str());
  } else {
    paths = flashcards::scanDeckFiles();
  }

  JsonDocument doc;
  char output[768];
  bool seenDeck = false;
  for (const std::string& path : paths) {
    flashcards::Deck deck;
    std::string error;
    if (!flashcards::loadDeck(path, deck, &error)) continue;
    flashcards::loadProgress(deck, nullptr);

    if (seenDeck) {
      server->sendContent(",");
    } else {
      seenDeck = true;
    }

    doc.clear();
    doc["path"] = deck.path;
    doc["title"] = deck.title;
    doc["deckHash"] = deck.deckHash;
    doc["totalReviews"] = deck.totalReviews;
    doc["totalAgain"] = deck.totalAgain;
    doc["totalHard"] = deck.totalHard;
    doc["totalGood"] = deck.totalGood;
    doc["totalEasy"] = deck.totalEasy;
    doc["totalLapses"] = deck.totalLapses;
    doc["sessionCount"] = deck.sessionCount;
    doc["totalSessions"] = deck.totalSessions;
    doc["lastStudiedSession"] = deck.lastStudiedSession;
    serializeJson(doc, output, sizeof(output));
    server->sendContent(output);
    server->sendContent(",\"records\":[");

    bool seenRecord = false;
    for (const flashcards::ProgressRecord& record : deck.progress) {
      doc.clear();
      doc["cardHash"] = record.cardHash;
      doc["reviewCount"] = record.reviewCount;
      doc["lapseCount"] = record.lapseCount;
      doc["intervalSessions"] = record.intervalSessions;
      doc["easePermille"] = record.easePermille;
      doc["dueSession"] = record.dueSession;
      doc["lastReviewedSession"] = record.lastReviewedSession;
      doc["lastRating"] = record.lastRating;
      doc["againCount"] = record.againCount;
      doc["hardCount"] = record.hardCount;
      doc["goodCount"] = record.goodCount;
      doc["easyCount"] = record.easyCount;
      const size_t written = serializeJson(doc, output, sizeof(output));
      if (written >= sizeof(output)) continue;
      if (seenRecord) {
        server->sendContent(",");
      } else {
        seenRecord = true;
      }
      server->sendContent(output);
    }
    server->sendContent("]}");
    yield();
    esp_task_wdt_reset();
  }

  server->sendContent("]}");
  server->sendContent("");
}

void CrossPointWebServer::handleFlashcardImport() {
  const String body = jsonBody(server.get());
  if (body.isEmpty()) {
    server->send(400, "application/json", "{\"error\":\"Missing JSON body\"}");
    return;
  }
  if (body.length() > static_cast<int>(FLASHCARD_IMPORT_MAX_BYTES)) {
    server->send(413, "application/json", "{\"error\":\"Import is too large\"}");
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "application/json", String("{\"error\":\"Invalid JSON: ") + err.c_str() + "\"}");
    return;
  }

  JsonArray decksJson = doc["decks"].as<JsonArray>();
  if (decksJson.isNull()) {
    server->send(400, "application/json", "{\"error\":\"Missing decks array\"}");
    return;
  }

  int mergedDecks = 0;
  int mergedRecords = 0;
  int skippedDecks = 0;
  const std::vector<std::string> localPaths = flashcards::scanDeckFiles();

  for (JsonObject importedDeck : decksJson) {
    const uint32_t importedDeckHash = importedDeck["deckHash"] | 0UL;
    if (importedDeckHash == 0) {
      skippedDecks++;
      continue;
    }

    flashcards::Deck deck;
    bool found = false;
    for (const std::string& path : localPaths) {
      std::string error;
      if (!flashcards::loadDeck(path, deck, &error)) continue;
      if (deck.deckHash == importedDeckHash) {
        flashcards::loadProgress(deck, nullptr);
        found = true;
        break;
      }
    }
    if (!found) {
      skippedDecks++;
      continue;
    }

    JsonArray records = importedDeck["records"].as<JsonArray>();
    if (records.isNull()) {
      skippedDecks++;
      continue;
    }

    for (JsonObject importedRecord : records) {
      const uint32_t cardHash = importedRecord["cardHash"] | 0UL;
      if (cardHash == 0 || flashcards::findProgress(deck, cardHash) == nullptr) {
        bool cardExists = false;
        for (const flashcards::CardRef& card : deck.cardRefs) {
          if (card.hash == cardHash) {
            cardExists = true;
            break;
          }
        }
        if (!cardExists) continue;
      }

      flashcards::ProgressRecord& local = flashcards::findOrCreateProgress(deck, cardHash);
      const uint16_t importedReviewCount = importedRecord["reviewCount"] | 0;
      const uint16_t importedLastReviewed = importedRecord["lastReviewedSession"] | 0;
      const bool shouldMerge = importedReviewCount > local.reviewCount ||
                               (importedReviewCount == local.reviewCount &&
                                importedLastReviewed > local.lastReviewedSession);
      if (!shouldMerge) continue;

      local.reviewCount = importedReviewCount;
      local.lapseCount = importedRecord["lapseCount"] | 0;
      local.intervalSessions = importedRecord["intervalSessions"] | 0;
      local.easePermille = importedRecord["easePermille"] | 2500;
      local.dueSession = importedRecord["dueSession"] | 0;
      local.lastReviewedSession = importedLastReviewed;
      local.lastRating = importedRecord["lastRating"] | 0;
      local.againCount = importedRecord["againCount"] | 0;
      local.hardCount = importedRecord["hardCount"] | 0;
      local.goodCount = importedRecord["goodCount"] | 0;
      local.easyCount = importedRecord["easyCount"] | 0;
      mergedRecords++;
    }

    deck.totalReviews = std::max<uint32_t>(deck.totalReviews, importedDeck["totalReviews"] | 0UL);
    deck.totalAgain = std::max<uint32_t>(deck.totalAgain, importedDeck["totalAgain"] | 0UL);
    deck.totalHard = std::max<uint32_t>(deck.totalHard, importedDeck["totalHard"] | 0UL);
    deck.totalGood = std::max<uint32_t>(deck.totalGood, importedDeck["totalGood"] | 0UL);
    deck.totalEasy = std::max<uint32_t>(deck.totalEasy, importedDeck["totalEasy"] | 0UL);
    deck.totalLapses = std::max<uint32_t>(deck.totalLapses, importedDeck["totalLapses"] | 0UL);
    deck.sessionCount = std::max<uint16_t>(deck.sessionCount, importedDeck["sessionCount"] | 0);
    deck.totalSessions = std::max<uint16_t>(deck.totalSessions, importedDeck["totalSessions"] | 0);
    deck.lastStudiedSession = std::max<uint16_t>(deck.lastStudiedSession, importedDeck["lastStudiedSession"] | 0);
    flashcards::saveProgress(deck);
    mergedDecks++;
  }

  JsonDocument out;
  out["ok"] = true;
  out["mergedDecks"] = mergedDecks;
  out["mergedRecords"] = mergedRecords;
  out["skippedDecks"] = skippedDecks;
  String json;
  serializeJson(out, json);
  server->send(200, "application/json", json);
}

void CrossPointWebServer::handleFlashcardReset() {
  const String body = jsonBody(server.get());
  JsonDocument doc;
  if (!body.isEmpty() && deserializeJson(doc, body)) {
    server->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  const String path = normalizeWebPath(doc["path"] | "");
  if (!isFlashcardDeckFilePath(path)) {
    server->send(400, "application/json", "{\"error\":\"Invalid deck path\"}");
    return;
  }
  const std::string progressPath = flashcards::progressPathForDeck(path.c_str());
  if (Storage.exists(progressPath.c_str())) {
    Storage.remove(progressPath.c_str());
  }
  server->send(200, "application/json", "{\"ok\":true}");
}

void CrossPointWebServer::handleFlashcardDelete() {
  const String body = jsonBody(server.get());
  JsonDocument doc;
  if (body.isEmpty() || deserializeJson(doc, body)) {
    server->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  const String path = normalizeWebPath(doc["path"] | "");
  if (!isFlashcardDeckFilePath(path)) {
    server->send(400, "application/json", "{\"error\":\"Invalid deck path\"}");
    return;
  }
  const bool deleteProgress = doc["deleteProgress"] | true;
  if (deleteProgress) {
    const std::string progressPath = flashcards::progressPathForDeck(path.c_str());
    if (Storage.exists(progressPath.c_str())) {
      Storage.remove(progressPath.c_str());
    }
  }
  if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) {
    server->send(500, "application/json", "{\"error\":\"Failed to delete deck\"}");
    return;
  }
  server->send(200, "application/json", "{\"ok\":true}");
}

void CrossPointWebServer::handleFlashcardRename() {
  const String body = jsonBody(server.get());
  JsonDocument doc;
  if (body.isEmpty() || deserializeJson(doc, body)) {
    server->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  const String oldPath = normalizeWebPath(doc["path"] | "");
  if (!isFlashcardDeckFilePath(oldPath)) {
    server->send(400, "application/json", "{\"error\":\"Invalid deck path\"}");
    return;
  }
  if (!Storage.exists(oldPath.c_str())) {
    server->send(404, "application/json", "{\"error\":\"Deck not found\"}");
    return;
  }

  const char* fallbackExt = FsHelpers::checkFileExtension(oldPath, ".csv") ? ".csv" : ".tsv";
  const String newPath = flashcardDeckPathForName(doc["name"] | "", fallbackExt);
  if (newPath.isEmpty()) {
    server->send(400, "application/json", "{\"error\":\"Invalid deck name\"}");
    return;
  }
  if (newPath == oldPath) {
    server->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  if (Storage.exists(newPath.c_str())) {
    server->send(409, "application/json", "{\"error\":\"Target deck already exists\"}");
    return;
  }

  HalFile file = Storage.open(oldPath.c_str());
  if (!file) {
    server->send(500, "application/json", "{\"error\":\"Failed to open deck\"}");
    return;
  }
  const bool renamed = file.rename(newPath.c_str());
  file.close();
  if (!renamed) {
    server->send(500, "application/json", "{\"error\":\"Failed to rename deck\"}");
    return;
  }

  const std::string oldProgress = flashcards::progressPathForDeck(oldPath.c_str());
  const std::string newProgress = flashcards::progressPathForDeck(newPath.c_str());
  if (Storage.exists(oldProgress.c_str())) {
    if (Storage.exists(newProgress.c_str())) Storage.remove(newProgress.c_str());
    Storage.rename(oldProgress.c_str(), newProgress.c_str());
  }

  JsonDocument out;
  out["ok"] = true;
  out["path"] = newPath;
  String json;
  serializeJson(out, json);
  server->send(200, "application/json", json);
}

void CrossPointWebServer::handleFileListData() const {
  // Get current path from query string (default to root)
  String currentPath = "/";
  if (server->hasArg("path")) {
    currentPath = normalizeWebPath(server->arg("path"));
  }

  if (isProtectedPath(currentPath)) {
    server->send(403, "application/json", "[]");
    return;
  }
  if (isFlashcardDecksPath(currentPath) && !ensureFlashcardDecksDir()) {
    server->send(500, "application/json", "[]");
    return;
  }

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");
  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  scanFiles(currentPath.c_str(), [this, &output, &doc, seenFirst](const FileInfo& info) mutable {
    doc.clear();
    doc["name"] = info.name;
    doc["size"] = info.size;
    doc["isDirectory"] = info.isDirectory;
    doc["isEpub"] = info.isEpub;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      // JSON output truncated; skip this entry to avoid sending malformed JSON
      LOG_DBG("WEB", "Skipping file entry with oversized JSON for name: %s", info.name.c_str());
      return;
    }

    if (seenFirst) {
      server->sendContent(",");
    } else {
      seenFirst = true;
    }
    server->sendContent(output);
  });
  server->sendContent("]");
  // End of streamed response, empty chunk to signal client
  server->sendContent("");
  LOG_DBG("WEB", "Served file listing page for path: %s", currentPath.c_str());
}

void CrossPointWebServer::handleLibraryScan() const {
  String rootPath = "/";
  if (server->hasArg("path")) {
    rootPath = normalizeWebPath(server->arg("path"));
  }
  if (isProtectedPath(rootPath)) {
    server->send(403, "application/json", "{\"error\":\"Protected path\"}");
    return;
  }

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  JsonDocument head;
  head["root"] = rootPath;
  head["limit"] = LIBRARY_SCAN_MAX_FILES;
  String headJson;
  serializeJson(head, headJson);
  if (headJson.endsWith("}")) {
    headJson.remove(headJson.length() - 1);
  }
  server->sendContent(headJson);
  server->sendContent(",\"items\":[");

  bool seenFirst = false;
  size_t scanned = 0;
  char output[768];
  JsonDocument doc;

  std::function<void(String, uint8_t)> scanDir = [&](String dirPath, uint8_t depth) {
    if (scanned >= LIBRARY_SCAN_MAX_FILES || depth > 8) return;
    HalFile dir = Storage.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      return;
    }

    char name[256];
    for (HalFile file = dir.openNextFile(); file && scanned < LIBRARY_SCAN_MAX_FILES; file = dir.openNextFile()) {
      const bool isDir = file.isDirectory();
      file.getName(name, sizeof(name));
      String childPath = dirPath;
      if (!childPath.endsWith("/")) childPath += "/";
      childPath += name;

      if (!isProtectedPath(childPath)) {
        if (isDir) {
          file.close();
          scanDir(childPath, depth + 1);
          yield();
          esp_task_wdt_reset();
          continue;
        }

        if (isLibraryBookFile(childPath)) {
          const size_t size = file.size();
          const String fileName = name;
          String title = baseNameWithoutExtension(fileName);
          String author = "";
          String type = "book";

          if (FsHelpers::hasEpubExtension(childPath)) {
            type = "epub";
            Epub epub(childPath.c_str(), "/.crosspoint");
            epub.load(false, true);
            if (!epub.getTitle().empty()) title = epub.getTitle().c_str();
            if (!epub.getAuthor().empty()) author = epub.getAuthor().c_str();
          } else if (FsHelpers::hasXtcExtension(childPath)) {
            type = "xtc";
            Xtc xtc(childPath.c_str(), "/.crosspoint");
            if (xtc.load()) {
              if (!xtc.getTitle().empty()) title = xtc.getTitle().c_str();
              if (!xtc.getAuthor().empty()) author = xtc.getAuthor().c_str();
            }
          } else if (FsHelpers::hasMarkdownExtension(childPath)) {
            type = "markdown";
            Txt txt(childPath.c_str(), "/.crosspoint");
            if (txt.load() && !txt.getTitle().empty()) title = txt.getTitle().c_str();
          } else if (FsHelpers::hasTxtExtension(childPath)) {
            type = "txt";
            Txt txt(childPath.c_str(), "/.crosspoint");
            if (txt.load() && !txt.getTitle().empty()) title = txt.getTitle().c_str();
          }

          doc.clear();
          doc["path"] = childPath;
          doc["name"] = fileName;
          doc["folder"] = parentPathOf(childPath);
          doc["type"] = type;
          doc["title"] = title;
          doc["author"] = author;
          doc["size"] = size;
          doc["readState"] = readStateForPath(childPath);
          doc["duplicateKey"] = normalizedDuplicateKey(title, fileName, size);

          const size_t written = serializeJson(doc, output, sizeof(output));
          if (written < sizeof(output)) {
            if (seenFirst) {
              server->sendContent(",");
            } else {
              seenFirst = true;
            }
            server->sendContent(output);
            scanned++;
          }
        }
      }

      file.close();
      yield();
      esp_task_wdt_reset();
    }
    dir.close();
  };

  scanDir(rootPath, 0);
  server->sendContent("],\"truncated\":");
  server->sendContent(scanned >= LIBRARY_SCAN_MAX_FILES ? "true" : "false");
  server->sendContent(",\"count\":");
  server->sendContent(String(static_cast<unsigned long>(scanned)));
  server->sendContent("}");
  server->sendContent("");
}

void CrossPointWebServer::handleLibrarySaved() const {
  std::vector<BookmarkedBookEntry> bookmarkedBooks;
  std::vector<ClippedBookEntry> clippedBooks;
  BookmarkStore::getAllBookmarkedBooks(bookmarkedBooks);
  ClippingStore::getAllClippedBooks(clippedBooks);

  uint32_t bookmarkCount = 0;
  for (const auto& book : bookmarkedBooks) bookmarkCount += book.count;
  uint32_t clippingCount = 0;
  for (const auto& book : clippedBooks) clippingCount += book.count;

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("{\"bookmarkCount\":");
  server->sendContent(String(static_cast<unsigned long>(bookmarkCount)));
  server->sendContent(",\"clippingCount\":");
  server->sendContent(String(static_cast<unsigned long>(clippingCount)));
  server->sendContent(",\"bookmarks\":[");

  char output[1024];
  JsonDocument doc;
  bool seenBook = false;
  for (const BookmarkedBookEntry& book : bookmarkedBooks) {
    if (!BookmarkStore::getInstance().loadForBook(book.bookPath, book.bookTitle, book.bookAuthor, book.bookType)) {
      continue;
    }

    doc.clear();
    doc["title"] = book.bookTitle;
    doc["author"] = book.bookAuthor;
    doc["path"] = book.bookPath;
    doc["type"] = book.bookType;
    doc["count"] = book.count;
    const size_t written = serializeJson(doc, output, sizeof(output));
    if (written < sizeof(output)) {
      if (seenBook) {
        server->sendContent(",");
      } else {
        seenBook = true;
      }
      String prefix = output;
      if (prefix.endsWith("}")) prefix.remove(prefix.length() - 1);
      server->sendContent(prefix);
      server->sendContent(",\"items\":[");

      bool seenItem = false;
      const auto& bookmarks = BookmarkStore::getInstance().getBookmarks();
      for (size_t i = 0; i < bookmarks.size(); ++i) {
        const Bookmark& bookmark = bookmarks[i];
        doc.clear();
        doc["index"] = static_cast<unsigned>(i);
        doc["spineIndex"] = bookmark.spineIndex;
        doc["progressPercent"] = bookmark.progress * 100.0f;
        doc["chapter"] = bookmark.chapterTitle;
        doc["paragraphIndex"] = bookmark.paragraphIndex == UINT16_MAX ? -1 : static_cast<int>(bookmark.paragraphIndex);
        doc["snippet"] = bookmark.snippet;
        sendJsonDoc(server.get(), doc, output, sizeof(output), seenItem);
        yield();
        esp_task_wdt_reset();
      }
      server->sendContent("]}");
    }
    BookmarkStore::getInstance().unload();
    yield();
    esp_task_wdt_reset();
  }
  BookmarkStore::getInstance().unload();

  server->sendContent("],\"clippings\":[");
  seenBook = false;
  for (const ClippedBookEntry& book : clippedBooks) {
    if (!ClippingStore::getInstance().loadForBook(book.bookPath, book.bookTitle, book.bookAuthor, book.bookType)) {
      continue;
    }

    doc.clear();
    doc["title"] = book.bookTitle;
    doc["author"] = book.bookAuthor;
    doc["path"] = book.bookPath;
    doc["type"] = book.bookType;
    doc["count"] = book.count;
    const size_t written = serializeJson(doc, output, sizeof(output));
    if (written < sizeof(output)) {
      if (seenBook) {
        server->sendContent(",");
      } else {
        seenBook = true;
      }
      String prefix = output;
      if (prefix.endsWith("}")) prefix.remove(prefix.length() - 1);
      server->sendContent(prefix);
      server->sendContent(",\"items\":[");

      bool seenItem = false;
      const auto& clippings = ClippingStore::getInstance().getClippings();
      for (size_t i = 0; i < clippings.size(); ++i) {
        const Clipping& clipping = clippings[i];
        doc.clear();
        doc["index"] = static_cast<unsigned>(i);
        doc["spineIndex"] = clipping.spineIndex;
        doc["startPage"] = clipping.startPage;
        doc["endPage"] = clipping.endPage;
        doc["pageCount"] = clipping.pageCount;
        doc["chapter"] = clipping.chapterTitle;
        doc["text"] = clipping.text;
        sendJsonDoc(server.get(), doc, output, sizeof(output), seenItem);
        yield();
        esp_task_wdt_reset();
      }
      server->sendContent("]}");
    }
    ClippingStore::getInstance().unload();
    yield();
    esp_task_wdt_reset();
  }
  ClippingStore::getInstance().unload();
  server->sendContent("]}");
  server->sendContent("");
}

void CrossPointWebServer::handleLibrarySavedDelete() {
  const String body = jsonBody(server.get());
  JsonDocument doc;
  if (body.isEmpty() || deserializeJson(doc, body)) {
    server->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  const String kind = doc["kind"] | "";
  const String path = normalizeWebPath(doc["path"] | "");
  const String type = doc["type"] | "";
  const int index = doc["index"] | -1;
  if (path.isEmpty() || path == "/" || isProtectedPath(path)) {
    server->send(400, "application/json", "{\"error\":\"Invalid book path\"}");
    return;
  }

  bool ok = false;
  if (kind == "bookmark") {
    if (!isSupportedBookmarkType(type)) {
      server->send(400, "application/json", "{\"error\":\"Invalid bookmark type\"}");
      return;
    }
    if (index < 0) {
      BookmarkStore::deleteForFilePath(path.c_str(), type.c_str());
      ok = true;
    } else {
      RecentBook meta = RECENT_BOOKS.getDataFromBook(path.c_str());
      if (meta.title.empty()) meta.title = fileNameOf(path).c_str();
      ok = BookmarkStore::getInstance().loadForBook(path.c_str(), meta.title, meta.author, type.c_str()) &&
           BookmarkStore::getInstance().removeBookmarkAt(static_cast<size_t>(index));
      BookmarkStore::getInstance().unload();
    }
  } else if (kind == "clipping") {
    if (!isSupportedClippingType(type)) {
      server->send(400, "application/json", "{\"error\":\"Invalid clipping type\"}");
      return;
    }
    if (index < 0) {
      ClippingStore::deleteForFilePath(path.c_str(), type.c_str());
      ok = true;
    } else {
      RecentBook meta = RECENT_BOOKS.getDataFromBook(path.c_str());
      if (meta.title.empty()) meta.title = fileNameOf(path).c_str();
      ok = ClippingStore::getInstance().loadForBook(path.c_str(), meta.title, meta.author, type.c_str()) &&
           ClippingStore::getInstance().removeClippingAt(static_cast<size_t>(index));
      ClippingStore::getInstance().unload();
    }
  } else {
    server->send(400, "application/json", "{\"error\":\"Invalid saved item kind\"}");
    return;
  }

  server->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"error\":\"Delete failed\"}");
}

void CrossPointWebServer::handleLibraryStats() const {
  const GlobalReadingStats local = GlobalReadingStats::load();
  const GlobalReadingStats aggregated = GlobalReadingStats::loadAggregated(local);

  JsonDocument doc;
  doc["hasSyncedStats"] = GlobalReadingStats::hasSyncedStats();
  addGlobalStatsJson(doc["local"].to<JsonObject>(), local);
  addGlobalStatsJson(doc["aggregated"].to<JsonObject>(), aggregated);
  JsonArray recent = doc["recentBooks"].to<JsonArray>();

  RECENT_BOOKS.loadFromFile();
  for (const RecentBook& book : RECENT_BOOKS.getBooks()) {
    if (RecentBooksStore::isMissing(book)) continue;
    const std::string cachePath = cachePathForBookPath(book.path.c_str());
    if (cachePath.empty()) continue;
    const BookReadingStats stats = BookReadingStats::load(cachePath);
    if (stats.sessionCount == 0 && stats.totalReadingSeconds == 0 && stats.totalPagesTurned == 0 &&
        !stats.isCompleted) {
      continue;
    }
    JsonObject item = recent.add<JsonObject>();
    item["path"] = book.path;
    item["title"] = book.title;
    item["author"] = book.author;
    item["sessionCount"] = stats.sessionCount;
    item["totalReadingSeconds"] = stats.totalReadingSeconds;
    item["totalPagesTurned"] = stats.totalPagesTurned;
    item["completed"] = stats.isCompleted;
    item["avgSecondsPerForwardPage"] = stats.avgSecondsPerForwardPage;
    item["estimatedTimeLeftSeconds"] = stats.estimatedTimeLeftSeconds;
    item["startDate"] = dateString(stats.startDate);
    item["finishedDate"] = dateString(stats.finishedDate);
  }

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void CrossPointWebServer::handleLibraryEnsureFolder() {
  const String body = jsonBody(server.get());
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    server->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  const String path = normalizeWebPath(doc["path"] | "");
  if (path.isEmpty() || isProtectedPath(path)) {
    server->send(400, "application/json", "{\"error\":\"Invalid folder path\"}");
    return;
  }
  if (!ensureDirectoryPath(path)) {
    server->send(500, "application/json", "{\"error\":\"Could not create folder\"}");
    return;
  }
  JsonDocument out;
  out["ok"] = true;
  out["path"] = path;
  String json;
  serializeJson(out, json);
  server->send(200, "application/json", json);
}

void CrossPointWebServer::handleLibraryBulkMove() {
  const String body = jsonBody(server.get());
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    server->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  const String dest = normalizeWebPath(doc["dest"] | "");
  const bool renameCollisions = doc["renameCollisions"] | true;
  JsonArray paths = doc["paths"].as<JsonArray>();
  if (dest.isEmpty() || paths.isNull() || paths.size() == 0) {
    server->send(400, "application/json", "{\"error\":\"Missing destination or paths\"}");
    return;
  }
  if (!ensureDirectoryPath(dest)) {
    server->send(500, "application/json", "{\"error\":\"Could not create destination\"}");
    return;
  }

  JsonDocument out;
  out["ok"] = true;
  JsonArray moved = out["moved"].to<JsonArray>();
  JsonArray failed = out["failed"].to<JsonArray>();

  for (JsonVariant value : paths) {
    String itemPath = normalizeWebPath(value.as<String>());
    String newPath;
    String error;
    if (moveLibraryFile(itemPath, dest, renameCollisions, newPath, error)) {
      JsonObject item = moved.add<JsonObject>();
      item["from"] = itemPath;
      item["to"] = newPath;
    } else {
      out["ok"] = false;
      JsonObject item = failed.add<JsonObject>();
      item["path"] = itemPath;
      item["error"] = error;
    }
    yield();
    esp_task_wdt_reset();
  }

  String json;
  serializeJson(out, json);
  server->send(failed.size() == 0 ? 200 : 207, "application/json", json);
}

void CrossPointWebServer::handleDownload() const {
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }

  if (isProtectedPath(itemPath)) {
    server->send(403, "text/plain", "Access denied to protected path");
    return;
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Path is a directory");
    return;
  }

  String contentType = "application/octet-stream";
  if (isEpubFile(itemPath)) {
    contentType = "application/epub+zip";
  }

  char nameBuf[128] = {0};
  String filename = "download";
  if (file.getName(nameBuf, sizeof(nameBuf))) {
    filename = nameBuf;
  }

  server->setContentLength(file.size());
  server->sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server->send(200, contentType.c_str(), "");

  NetworkClient client = server->client();
  const size_t chunkSize = 4096;
  uint8_t buffer[chunkSize];

  bool downloadOk = true;
  while (downloadOk && file.available()) {
    int result = file.read(buffer, chunkSize);
    if (result <= 0) break;
    size_t bytesRead = static_cast<size_t>(result);
    size_t totalWritten = 0;
    while (totalWritten < bytesRead) {
      esp_task_wdt_reset();
      size_t wrote = client.write(buffer + totalWritten, bytesRead - totalWritten);
      if (wrote == 0) {
        downloadOk = false;
        break;
      }
      totalWritten += wrote;
    }
  }
#ifndef SIMULATOR
  client.clear();
#endif
  file.close();
}

// Diagnostic counters for upload performance analysis
static unsigned long uploadStartTime = 0;
static unsigned long totalWriteTime = 0;
static size_t writeCount = 0;

static bool flushUploadBuffer(CrossPointWebServer::UploadState& state) {
  if (state.bufferPos > 0 && state.file) {
    esp_task_wdt_reset();  // Reset watchdog before potentially slow SD write
    const unsigned long writeStart = millis();
    const size_t written = state.file.write(state.buffer.data(), state.bufferPos);
    totalWriteTime += millis() - writeStart;
    writeCount++;
    esp_task_wdt_reset();  // Reset watchdog after SD write

    if (written != state.bufferPos) {
      LOG_DBG("WEB", "[UPLOAD] Buffer flush failed: expected %d, wrote %d", state.bufferPos, written);
      state.bufferPos = 0;
      return false;
    }
    state.bufferPos = 0;
  }
  return true;
}

void CrossPointWebServer::handleUpload(UploadState& state) const {
  static size_t lastLoggedSize = 0;

  // Reset watchdog at start of every upload callback - HTTP parsing can be slow
  esp_task_wdt_reset();

  // Safety check: ensure server is still valid
  if (!running || !server) {
    LOG_DBG("WEB", "[UPLOAD] ERROR: handleUpload called but server not running!");
    return;
  }

  const HTTPUpload& upload = server->upload();

  if (upload.status == UPLOAD_FILE_START) {
    // Reset watchdog - this is the critical 1% crash point
    esp_task_wdt_reset();

    state.fileName = StringUtils::sanitizeFilename(upload.filename.c_str()).c_str();
    state.size = 0;
    state.success = false;
    state.error = "";
    uploadStartTime = millis();
    lastLoggedSize = 0;
    state.bufferPos = 0;
    totalWriteTime = 0;
    writeCount = 0;

    // Get upload path from query parameter (defaults to root if not specified)
    // Note: We use query parameter instead of form data because multipart form
    // fields aren't available until after file upload completes
    if (server->hasArg("path")) {
      state.path = normalizeWebPath(server->arg("path"));
    } else {
      state.path = "/";
    }

    LOG_DBG("WEB", "[UPLOAD] START: %s to path: %s", state.fileName.c_str(), state.path.c_str());
    LOG_DBG("WEB", "[UPLOAD] Free heap: %d bytes", ESP.getFreeHeap());

    // Create file path
    String filePath = state.path;
    if (!filePath.endsWith("/")) filePath += "/";
    filePath += state.fileName;

    if (isProtectedPath(filePath)) {
      state.error = "Access denied to protected path";
      LOG_DBG("WEB", "[UPLOAD] FAILED: Access denied to protected path: %s", filePath.c_str());
      return;
    }
    if (isFlashcardDecksPath(filePath) && !ensureFlashcardDecksDir()) {
      state.error = "Failed to create flashcard deck folder";
      LOG_DBG("WEB", "[UPLOAD] FAILED: Could not create flashcard deck folder");
      return;
    }

    // Check if file already exists - SD operations can be slow
    esp_task_wdt_reset();
    if (Storage.exists(filePath.c_str())) {
      LOG_DBG("WEB", "[UPLOAD] Overwriting existing file: %s", filePath.c_str());
      esp_task_wdt_reset();
      Storage.remove(filePath.c_str());
    }

    // Open file for writing - this can be slow due to FAT cluster allocation
    esp_task_wdt_reset();
    if (!Storage.openFileForWrite("WEB", filePath, state.file)) {
      state.error = "Failed to create file on SD card";
      LOG_DBG("WEB", "[UPLOAD] FAILED to create file: %s", filePath.c_str());
      return;
    }
    esp_task_wdt_reset();

    LOG_DBG("WEB", "[UPLOAD] File created successfully: %s", filePath.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (state.file && state.error.isEmpty()) {
      // Buffer incoming data and flush when buffer is full
      // This reduces SD card write operations and improves throughput
      const uint8_t* data = upload.buf;
      size_t remaining = upload.currentSize;

      while (remaining > 0) {
        const size_t space = UploadState::UPLOAD_BUFFER_SIZE - state.bufferPos;
        const size_t toCopy = (remaining < space) ? remaining : space;

        memcpy(state.buffer.data() + state.bufferPos, data, toCopy);
        state.bufferPos += toCopy;
        data += toCopy;
        remaining -= toCopy;

        // Flush buffer when full
        if (state.bufferPos >= UploadState::UPLOAD_BUFFER_SIZE) {
          if (!flushUploadBuffer(state)) {
            state.error = "Failed to write to SD card - disk may be full";
            state.file.close();
            return;
          }
        }
      }

      state.size += upload.currentSize;

      // Log progress every 100KB
      if (state.size - lastLoggedSize >= 102400) {
        const unsigned long elapsed = millis() - uploadStartTime;
        const float kbps = (elapsed > 0) ? (state.size / 1024.0) / (elapsed / 1000.0) : 0;
        LOG_DBG("WEB", "[UPLOAD] %d bytes (%.1f KB), %.1f KB/s, %d writes", state.size, state.size / 1024.0, kbps,
                writeCount);
        lastLoggedSize = state.size;
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (state.file) {
      // Flush any remaining buffered data
      if (!flushUploadBuffer(state)) {
        state.error = "Failed to write final data to SD card";
      }
      state.file.close();

      if (state.error.isEmpty()) {
        state.success = true;
        const unsigned long elapsed = millis() - uploadStartTime;
        const float avgKbps = (elapsed > 0) ? (state.size / 1024.0) / (elapsed / 1000.0) : 0;
        const float writePercent = (elapsed > 0) ? (totalWriteTime * 100.0 / elapsed) : 0;
        LOG_DBG("WEB", "[UPLOAD] Complete: %s (%d bytes in %lu ms, avg %.1f KB/s)", state.fileName.c_str(), state.size,
                elapsed, avgKbps);
        LOG_DBG("WEB", "[UPLOAD] Diagnostics: %d writes, total write time: %lu ms (%.1f%%)", writeCount, totalWriteTime,
                writePercent);

        // Clear epub cache to prevent stale metadata issues when overwriting files
        String filePath = state.path;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += state.fileName;
        clearBookCachePreservingUserState(filePath.c_str());
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    state.bufferPos = 0;  // Discard buffered data
    if (state.file) {
      state.file.close();
      // Try to delete the incomplete file
      String filePath = state.path;
      if (!filePath.endsWith("/")) filePath += "/";
      filePath += state.fileName;
      Storage.remove(filePath.c_str());
    }
    state.error = "Upload aborted";
    LOG_DBG("WEB", "Upload aborted");
  }
}

void CrossPointWebServer::handleUploadPost(UploadState& state) const {
  if (state.success) {
    server->send(200, "text/plain", "File uploaded successfully: " + state.fileName);
  } else {
    const String error = state.error.isEmpty() ? "Unknown error during upload" : state.error;
    server->send(400, "text/plain", error);
  }
}

void CrossPointWebServer::handleCreateFolder() const {
  // Get folder name from form data
  if (!server->hasArg("name")) {
    server->send(400, "text/plain", "Missing folder name");
    return;
  }

  const String folderName = StringUtils::sanitizeFilename(server->arg("name").c_str()).c_str();

  // Validate folder name
  if (folderName.isEmpty() || folderName == "book") {
    server->send(400, "text/plain", "Invalid folder name");
    return;
  }

  // Get parent path
  String parentPath = "/";
  if (server->hasArg("path")) {
    parentPath = normalizeWebPath(server->arg("path"));
  }

  // Build full folder path
  String folderPath = parentPath;
  if (!folderPath.endsWith("/")) folderPath += "/";
  folderPath += folderName;

  if (isProtectedPath(folderPath)) {
    server->send(403, "text/plain", "Access denied to protected path");
    return;
  }

  LOG_DBG("WEB", "Creating folder: %s", folderPath.c_str());

  // Check if already exists
  if (Storage.exists(folderPath.c_str())) {
    server->send(400, "text/plain", "Folder already exists");
    return;
  }

  // Create the folder
  if (Storage.mkdir(folderPath.c_str())) {
    LOG_DBG("WEB", "Folder created successfully: %s", folderPath.c_str());
    server->send(200, "text/plain", "Folder created: " + folderName);
  } else {
    LOG_DBG("WEB", "Failed to create folder: %s", folderPath.c_str());
    server->send(500, "text/plain", "Failed to create folder");
  }
}

void CrossPointWebServer::handleRename() const {
  if (!server->hasArg("path") || !server->hasArg("name")) {
    server->send(400, "text/plain", "Missing path or new name");
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  String newName = StringUtils::sanitizeFilename(server->arg("name").c_str()).c_str();

  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  if (newName.isEmpty()) {
    server->send(400, "text/plain", "New name cannot be empty");
    return;
  }
  if (isProtectedPath(itemPath)) {
    server->send(403, "text/plain", "Cannot rename protected item");
    return;
  }

  // Calculate new path to check if it's protected
  String parentPath = itemPath.substring(0, itemPath.lastIndexOf('/'));
  if (parentPath.isEmpty()) {
    parentPath = "/";
  }
  String newPath = parentPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += newName;

  if (isProtectedPath(newPath)) {
    server->send(403, "text/plain", "Cannot rename to protected path");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (newName == itemName) {
    server->send(200, "text/plain", "Name unchanged");
    return;
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Only files can be renamed");
    return;
  }

  if (Storage.exists(newPath.c_str())) {
    file.close();
    server->send(409, "text/plain", "Target already exists");
    return;
  }

  const bool success = file.rename(newPath.c_str());
  file.close();

  if (success) {
    migrateMovedBookState(itemPath, newPath);
    LOG_DBG("WEB", "Renamed file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(200, "text/plain", "Renamed successfully");
  } else {
    LOG_ERR("WEB", "Failed to rename file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(500, "text/plain", "Failed to rename file");
  }
}

void CrossPointWebServer::handleMove() const {
  if (!server->hasArg("path") || !server->hasArg("dest")) {
    server->send(400, "text/plain", "Missing path or destination");
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  String destPath = normalizeWebPath(server->arg("dest"));

  String newPath;
  String error;
  if (moveLibraryFile(itemPath, destPath, true, newPath, error)) {
    LOG_DBG("WEB", "Moved file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(200, "text/plain", "Moved successfully: " + newPath);
  } else {
    LOG_ERR("WEB", "Failed to move file: %s -> %s (%s)", itemPath.c_str(), destPath.c_str(), error.c_str());
    server->send(error == "Item not found" ? 404 : 400, "text/plain", error);
  }
}

void CrossPointWebServer::handleDelete() const {
  // To ensure backwards compatibility, plain `path` is mapped
  // to a single element JSON array.
  bool hasPathArg = server->hasArg("path");
  bool hasPathsArg = server->hasArg("paths");
  // Check 'paths' or `path` argument is provided
  if (!(hasPathArg || hasPathsArg)) {
    server->send(400, "text/plain", "Missing `path` or `paths` argument");
    return;
  }
  if (hasPathArg && hasPathsArg) {
    server->send(400, "text/plain", "Provide either 'path' or 'paths', not both");
    return;
  }

  // Parse paths
  String pathsArg;
  JsonDocument doc;
  DeserializationError error = DeserializationError(DeserializationError::Code::Ok);
  if (hasPathsArg) {
    pathsArg = server->arg("paths");
    error = deserializeJson(doc, pathsArg);
  } else {
    pathsArg = server->arg("path");
    doc.add(pathsArg);
  }
  if (error) {
    server->send(400, "text/plain", "Invalid paths format");
    return;
  }

  auto paths = doc.as<JsonArray>();
  if (paths.isNull() || paths.size() == 0) {
    server->send(400, "text/plain", "No paths provided");
    return;
  }

  // Iterate over paths and delete each item
  bool allSuccess = true;
  String failedItems;

  for (const auto& p : paths) {
    auto itemPath = p.as<String>();

    // Validate path
    if (itemPath.isEmpty() || itemPath == "/") {
      failedItems += itemPath + " (cannot delete root); ";
      allSuccess = false;
      continue;
    }

    // Ensure path starts with /
    if (!itemPath.startsWith("/")) {
      itemPath = "/" + itemPath;
    }

    // Security check: prevent deletion of protected items
    if (isProtectedPath(itemPath)) {
      failedItems += itemPath + " (protected path); ";
      allSuccess = false;
      continue;
    }
    if (itemPath == FLASHCARD_DECKS_DIR) {
      failedItems += itemPath + " (managed folder); ";
      allSuccess = false;
      continue;
    }

    // Check if item exists
    if (!Storage.exists(itemPath.c_str())) {
      failedItems += itemPath + " (not found); ";
      allSuccess = false;
      continue;
    }

    // Decide whether it's a directory or file by opening it
    bool success = false;
    HalFile f = Storage.open(itemPath.c_str());
    if (f && f.isDirectory()) {
      // For folders, ensure empty before removing
      HalFile entry = f.openNextFile();
      if (entry) {
        entry.close();
        f.close();
        failedItems += itemPath + " (folder not empty); ";
        allSuccess = false;
        continue;
      }
      f.close();
      success = Storage.rmdir(itemPath.c_str());
    } else {
      // It's a file (or couldn't open as dir) — remove file
      if (f) f.close();
      success = Storage.remove(itemPath.c_str());
      clearBookCache(itemPath.c_str());
    }

    if (!success) {
      failedItems += itemPath + " (deletion failed); ";
      allSuccess = false;
    }
  }

  if (allSuccess) {
    server->send(200, "text/plain", "All items deleted successfully");
  } else {
    server->send(500, "text/plain", "Failed to delete some items: " + failedItems);
  }
}

void CrossPointWebServer::handleSettingsPage() const {
  sendHtmlContent(server.get(), SettingsPageHtml, sizeof(SettingsPageHtml));
  LOG_DBG("WEB", "Served settings page");
}

void CrossPointWebServer::handleGetSettings() const {
  // Pass the SD font registry so the fontFamily setting's enumStringValues
  // includes SD-resident families — otherwise the web API only exposes the
  // three built-in fonts.
  const auto& settings = getSettingsList(&sdFontSystem.registry());

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  for (const auto& s : settings) {
    if (!s.key) continue;  // Skip ACTION-only entries

    doc.clear();
    doc["key"] = s.key;
    doc["name"] = I18N.get(s.nameId);
    doc["category"] = I18N.get(s.category);

    switch (s.type) {
      case SettingType::TOGGLE: {
        doc["type"] = "toggle";
        if (s.valuePtr) {
          doc["value"] = static_cast<int>(SETTINGS.*(s.valuePtr));
        }
        break;
      }
      case SettingType::ENUM: {
        doc["type"] = "enum";
        if (s.valuePtr) {
          doc["value"] = static_cast<int>(enumDisplayIndexForRawValue(s, SETTINGS.*(s.valuePtr)));
        } else if (s.valueGetter) {
          doc["value"] = static_cast<int>(s.valueGetter());
        }
        JsonArray options = doc["options"].to<JsonArray>();
        if (!s.enumStringValues.empty()) {
          for (const auto& opt : s.enumStringValues) {
            options.add(opt);
          }
        } else {
          for (const auto& opt : s.enumValues) {
            options.add(I18N.get(opt));
          }
        }
        break;
      }
      case SettingType::VALUE: {
        doc["type"] = "value";
        if (s.valuePtr) {
          doc["value"] = static_cast<int>(SETTINGS.*(s.valuePtr));
        }
        doc["min"] = s.valueRange.min;
        doc["max"] = s.valueRange.max;
        doc["step"] = s.valueRange.step;
        break;
      }
      case SettingType::STRING: {
        doc["type"] = "string";
        if (s.stringGetter) {
          doc["value"] = s.stringGetter();
        } else if (s.stringMaxLen > 0) {
          doc["value"] = reinterpret_cast<const char*>(&SETTINGS) + s.stringOffset;
        }
        break;
      }
      default:
        continue;
    }

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      LOG_DBG("WEB", "Skipping oversized setting JSON for: %s", s.key);
      continue;
    }

    if (seenFirst) {
      server->sendContent(",");
    } else {
      seenFirst = true;
    }
    server->sendContent(output);
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served settings API");
}

void CrossPointWebServer::handlePostSettings() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  const auto& settings = getSettingsList(&sdFontSystem.registry());
  int applied = 0;

  for (const auto& s : settings) {
    if (!s.key) continue;
    if (!doc[s.key].is<JsonVariant>()) continue;

    switch (s.type) {
      case SettingType::TOGGLE: {
        const int val = doc[s.key].as<int>() ? 1 : 0;
        if (s.valuePtr) {
          SETTINGS.*(s.valuePtr) = val;
        }
        applied++;
        break;
      }
      case SettingType::ENUM: {
        const int val = doc[s.key].as<int>();
        const int maxVal = s.enumStringValues.empty() ? static_cast<int>(s.enumValues.size())
                                                      : static_cast<int>(s.enumStringValues.size());
        if (val >= 0 && val < maxVal) {
          if (s.valuePtr) {
            SETTINGS.*(s.valuePtr) = enumRawValueForDisplayIndex(s, static_cast<uint8_t>(val));
          } else if (s.valueSetter) {
            s.valueSetter(static_cast<uint8_t>(val));
          }
          applied++;
        }
        break;
      }
      case SettingType::VALUE: {
        const int val = doc[s.key].as<int>();
        if (val >= s.valueRange.min && val <= s.valueRange.max) {
          if (s.valuePtr) {
            SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(val);
          }
          applied++;
        }
        break;
      }
      case SettingType::STRING: {
        const std::string val = doc[s.key].as<std::string>();
        if (std::strcmp(s.key, "deviceName") == 0 && (val.length() < CrossPointSettings::MIN_DEVICE_NAME_LENGTH ||
                                                      val.length() > CrossPointSettings::MAX_DEVICE_NAME_LENGTH)) {
          break;
        }
        if (s.stringSetter) {
          s.stringSetter(val);
        } else if (s.stringMaxLen > 0) {
          char* ptr = reinterpret_cast<char*>(&SETTINGS) + s.stringOffset;
          strncpy(ptr, val.c_str(), s.stringMaxLen - 1);
          ptr[s.stringMaxLen - 1] = '\0';
        }
        applied++;
        break;
      }
      default:
        break;
    }
  }

  SETTINGS.saveToFile();

  LOG_DBG("WEB", "Applied %d setting(s)", applied);
  server->send(200, "text/plain", String("Applied ") + String(applied) + " setting(s)");
}

// ---- OPDS Server API ----

void CrossPointWebServer::handleGetOpdsServers() const {
  const auto& servers = OPDS_STORE.getServers();

  // Stream JSON array incrementally to avoid allocating the full response in memory
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[512];
  constexpr size_t outputSize = sizeof(output);
  JsonDocument doc;

  for (size_t i = 0; i < servers.size(); i++) {
    doc.clear();
    doc["index"] = i;
    doc["name"] = servers[i].name;
    doc["url"] = servers[i].url;
    doc["username"] = servers[i].username;
    doc["filenameFormat"] = opdsFilenameFormatToJson(servers[i].filenameFormat);
    // Never expose passwords over the API — only indicate whether one is set
    doc["hasPassword"] = !servers[i].password.empty();

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) continue;

    if (i > 0) server->sendContent(",");
    server->sendContent(output);
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served OPDS servers API (%zu servers)", servers.size());
}

void CrossPointWebServer::handlePostOpdsServer() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  OpdsServer opdsServer;
  opdsServer.name = doc["name"] | std::string("");
  opdsServer.url = doc["url"] | std::string("");
  opdsServer.username = doc["username"] | std::string("");
  opdsServer.filenameFormat = opdsFilenameFormatFromJson(doc["filenameFormat"] | "");

  // The password field is optional in the JSON payload. When absent (vs. present but empty),
  // we preserve the existing password — the web UI omits it when the user hasn't changed it.
  bool hasPasswordField = doc["password"].is<const char*>() || doc["password"].is<std::string>();
  std::string password = doc["password"] | std::string("");
  const bool hasFilenameFormatField =
      doc["filenameFormat"].is<const char*>() || doc["filenameFormat"].is<std::string>();

  if (doc["index"].is<int>()) {
    int idx = doc["index"].as<int>();
    if (idx < 0 || idx >= static_cast<int>(OPDS_STORE.getCount())) {
      server->send(400, "text/plain", "Invalid server index");
      return;
    }
    const auto* existing = OPDS_STORE.getServer(static_cast<size_t>(idx));
    // Preserve existing values for fields older clients do not know how to send.
    if (existing && !hasPasswordField) {
      password = existing->password;
    }
    if (existing && !hasFilenameFormatField) {
      opdsServer.filenameFormat = existing->filenameFormat;
    }
    opdsServer.password = password;
    OPDS_STORE.updateServer(static_cast<size_t>(idx), opdsServer);
    LOG_DBG("WEB", "Updated OPDS server at index %d", idx);
  } else {
    opdsServer.password = password;
    if (!OPDS_STORE.addServer(opdsServer)) {
      server->send(400, "text/plain", "Cannot add server (limit reached)");
      return;
    }
    LOG_DBG("WEB", "Added new OPDS server: %s", opdsServer.name.c_str());
  }

  server->send(200, "text/plain", "OK");
}

// Uses POST (not HTTP DELETE) because ESP32 WebServer doesn't support DELETE with body.
void CrossPointWebServer::handleDeleteOpdsServer() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  if (!doc["index"].is<int>()) {
    server->send(400, "text/plain", "Missing index");
    return;
  }

  int idx = doc["index"].as<int>();
  if (idx < 0 || idx >= static_cast<int>(OPDS_STORE.getCount())) {
    server->send(400, "text/plain", "Invalid server index");
    return;
  }

  OPDS_STORE.removeServer(static_cast<size_t>(idx));
  LOG_DBG("WEB", "Deleted OPDS server at index %d", idx);
  server->send(200, "text/plain", "OK");
}

// ---- Wi-Fi Credentials API ----

void CrossPointWebServer::handleGetWifiNetworks() const {
  const auto& credentials = WIFI_STORE.getCredentials();
  const std::string& lastConnectedSsid = WIFI_STORE.getLastConnectedSsid();

  // Stream JSON array incrementally to avoid allocating the full response in memory
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[320];
  constexpr size_t outputSize = sizeof(output);
  JsonDocument doc;

  for (size_t i = 0; i < credentials.size(); i++) {
    doc.clear();
    doc["index"] = i;
    doc["ssid"] = credentials[i].ssid;
    // Never expose Wi-Fi passwords over the API — only indicate whether one is set
    doc["hasPassword"] = !credentials[i].password.empty();
    doc["isLastConnected"] = credentials[i].ssid == lastConnectedSsid;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) continue;

    if (i > 0) server->sendContent(",");
    server->sendContent(output);
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served Wi-Fi credentials API (%zu network(s))", credentials.size());
}

void CrossPointWebServer::handlePostWifiNetwork() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  std::string ssid = doc["ssid"] | std::string("");
  if (ssid.empty()) {
    server->send(400, "text/plain", "SSID is required");
    return;
  }

  // The password field is optional in the JSON payload. When absent (vs. present but empty),
  // preserve the existing password for updates. Empty passwords are valid for open networks.
  bool hasPasswordField = doc["password"].is<const char*>() || doc["password"].is<std::string>();
  std::string password = doc["password"] | std::string("");

  if (doc["index"].is<int>()) {
    int idx = doc["index"].as<int>();
    const auto& credentials = WIFI_STORE.getCredentials();
    if (idx < 0 || idx >= static_cast<int>(credentials.size())) {
      server->send(400, "text/plain", "Invalid network index");
      return;
    }

    const std::string oldSsid = credentials[static_cast<size_t>(idx)].ssid;
    if (!hasPasswordField) {
      password = credentials[static_cast<size_t>(idx)].password;
    }

    bool ok = true;
    if (oldSsid != ssid) {
      ok = WIFI_STORE.removeCredential(oldSsid) && WIFI_STORE.addCredential(ssid, password);
    } else {
      ok = WIFI_STORE.addCredential(ssid, password);
    }

    if (!ok) {
      server->send(400, "text/plain", "Failed to update Wi-Fi network");
      return;
    }

    LOG_DBG("WEB", "Updated Wi-Fi network at index %d (SSID: %s)", idx, ssid.c_str());
  } else {
    if (!WIFI_STORE.addCredential(ssid, password)) {
      server->send(400, "text/plain", "Cannot add network (limit reached)");
      return;
    }
    LOG_DBG("WEB", "Added Wi-Fi network: %s", ssid.c_str());
  }

  server->send(200, "text/plain", "OK");
}

// Uses POST (not HTTP DELETE) because ESP32 WebServer doesn't support DELETE with body.
void CrossPointWebServer::handleDeleteWifiNetwork() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  if (!doc["index"].is<int>()) {
    server->send(400, "text/plain", "Missing index");
    return;
  }

  int idx = doc["index"].as<int>();
  const auto& credentials = WIFI_STORE.getCredentials();
  if (idx < 0 || idx >= static_cast<int>(credentials.size())) {
    server->send(400, "text/plain", "Invalid network index");
    return;
  }

  const std::string ssid = credentials[static_cast<size_t>(idx)].ssid;
  if (!WIFI_STORE.removeCredential(ssid)) {
    server->send(400, "text/plain", "Failed to delete Wi-Fi network");
    return;
  }

  LOG_DBG("WEB", "Deleted Wi-Fi network at index %d (SSID: %s)", idx, ssid.c_str());
  server->send(200, "text/plain", "OK");
}

// WebSocket callback trampoline
void CrossPointWebServer::wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (wsInstance) {
    wsInstance->onWebSocketEvent(num, type, payload, length);
  }
}

// WebSocket event handler for fast binary uploads
// Protocol:
//   1. Client sends TEXT message: "START:<filename>:<size>:<path>"
//   2. Client sends BINARY messages with file data chunks
//   3. Server sends TEXT "PROGRESS:<received>:<total>" after each chunk
//   4. Server sends TEXT "DONE" or "ERROR:<message>" when complete
void CrossPointWebServer::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      LOG_DBG("WS", "Client %u disconnected", num);
      // Only clean up if this is the client that owns the active upload.
      // A new client may have already started a fresh upload before this
      // DISCONNECTED event fires (race condition on quick cancel + retry).
      if (num == wsUploadClientNum && wsUploadInProgress && wsUploadFile) {
        abortWsUpload("WS");
      }
      break;

    case WStype_CONNECTED: {
      LOG_DBG("WS", "Client %u connected", num);
      break;
    }

    case WStype_TEXT: {
      // Parse control messages
      String msg = String((char*)payload);
      LOG_DBG("WS", "Text from client %u: %s", num, msg.c_str());

      if (msg.startsWith("START:")) {
        // Reject any START while an upload is already active to prevent
        // leaking the open wsUploadFile handle (owning client re-START included)
        if (wsUploadInProgress) {
          wsServer->sendTXT(num, "ERROR:Upload already in progress");
          break;
        }

        // Parse: START:<filename>:<size>:<path>
        int firstColon = msg.indexOf(':', 6);
        int secondColon = msg.indexOf(':', firstColon + 1);

        if (firstColon > 0 && secondColon > 0) {
          wsUploadFileName = StringUtils::sanitizeFilename(msg.substring(6, firstColon).c_str()).c_str();
          String sizeToken = msg.substring(firstColon + 1, secondColon);
          bool sizeValid = sizeToken.length() > 0;
          int digitStart = (sizeValid && sizeToken[0] == '+') ? 1 : 0;
          if (digitStart > 0 && sizeToken.length() < 2) sizeValid = false;
          for (int i = digitStart; i < (int)sizeToken.length() && sizeValid; i++) {
            if (!isdigit((unsigned char)sizeToken[i])) sizeValid = false;
          }
          if (!sizeValid) {
            LOG_DBG("WS", "START rejected: invalid size token '%s'", sizeToken.c_str());
            wsServer->sendTXT(num, "ERROR:Invalid START format");
            return;
          }
          wsUploadSize = sizeToken.toInt();
          wsUploadPath = normalizeWebPath(msg.substring(secondColon + 1));
          wsUploadReceived = 0;
          wsLastProgressSent = 0;
          wsUploadStartTime = millis();

          // Build file path
          String filePath = wsUploadPath;
          if (!filePath.endsWith("/")) filePath += "/";
          filePath += wsUploadFileName;

          if (isProtectedPath(filePath)) {
            wsServer->sendTXT(num, "ERROR:Access denied to protected path");
            wsUploadInProgress = false;
            wsUploadClientNum = 255;
            return;
          }
          if (isFlashcardDecksPath(filePath) && !ensureFlashcardDecksDir()) {
            wsServer->sendTXT(num, "ERROR:Failed to create flashcard deck folder");
            wsUploadInProgress = false;
            wsUploadClientNum = 255;
            return;
          }

          LOG_DBG("WS", "Starting upload: %s (%d bytes) to %s", wsUploadFileName.c_str(), wsUploadSize,
                  filePath.c_str());

          // Check if file exists and remove it
          esp_task_wdt_reset();
          if (Storage.exists(filePath.c_str())) {
            Storage.remove(filePath.c_str());
          }

          // Open file for writing
          esp_task_wdt_reset();
          if (!Storage.openFileForWrite("WS", filePath, wsUploadFile)) {
            wsServer->sendTXT(num, "ERROR:Failed to create file");
            wsUploadInProgress = false;
            wsUploadClientNum = 255;
            return;
          }
          esp_task_wdt_reset();

          // Zero-byte upload: complete immediately without waiting for BIN frames
          if (wsUploadSize == 0) {
            // Explicit close() required: file-scope global persists beyond function scope
            wsUploadFile.close();
            wsLastCompleteName = wsUploadFileName;
            wsLastCompleteSize = 0;
            wsLastCompleteAt = millis();
            LOG_DBG("WS", "Zero-byte upload complete: %s", filePath.c_str());
            clearBookCachePreservingUserState(filePath.c_str());
            wsServer->sendTXT(num, "DONE");
            wsLastProgressSent = 0;
            break;
          }

          wsUploadClientNum = num;
          wsUploadInProgress = true;
          wsServer->sendTXT(num, "READY");
        } else {
          wsServer->sendTXT(num, "ERROR:Invalid START format");
        }
      }
      break;
    }

    case WStype_BIN: {
      if (!wsUploadInProgress || !wsUploadFile || num != wsUploadClientNum) {
        wsServer->sendTXT(num, "ERROR:No upload in progress");
        return;
      }

      // Write binary data directly to file
      size_t remaining = wsUploadSize - wsUploadReceived;
      if (length > remaining) {
        abortWsUpload("WS");
        wsServer->sendTXT(num, "ERROR:Upload overflow");
        return;
      }
      esp_task_wdt_reset();
      size_t written = wsUploadFile.write(payload, length);
      esp_task_wdt_reset();

      if (written != length) {
        abortWsUpload("WS");
        wsServer->sendTXT(num, "ERROR:Write failed - disk full?");
        return;
      }

      wsUploadReceived += written;

      // Send progress update (every 64KB or at end)
      if (wsUploadReceived - wsLastProgressSent >= 65536 || wsUploadReceived >= wsUploadSize) {
        String progress = "PROGRESS:" + String(wsUploadReceived) + ":" + String(wsUploadSize);
        wsServer->sendTXT(num, progress);
        wsLastProgressSent = wsUploadReceived;
      }

      // Check if upload complete
      if (wsUploadReceived >= wsUploadSize) {
        // Explicit close() required: file-scope global persists beyond function scope
        wsUploadFile.close();
        wsUploadInProgress = false;
        wsUploadClientNum = 255;

        wsLastCompleteName = wsUploadFileName;
        wsLastCompleteSize = wsUploadSize;
        wsLastCompleteAt = millis();

        unsigned long elapsed = millis() - wsUploadStartTime;
        float kbps = (elapsed > 0) ? (wsUploadSize / 1024.0) / (elapsed / 1000.0) : 0;

        LOG_DBG("WS", "Upload complete: %s (%d bytes in %lu ms, %.1f KB/s)", wsUploadFileName.c_str(), wsUploadSize,
                elapsed, kbps);

        // Clear epub cache to prevent stale metadata issues when overwriting files
        String filePath = wsUploadPath;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += wsUploadFileName;
        clearBookCachePreservingUserState(filePath.c_str());

        wsServer->sendTXT(num, "DONE");
        wsLastProgressSent = 0;
      }
      break;
    }

    default:
      break;
  }
}

// --- Font management handlers ---

void CrossPointWebServer::handleFontsPage() const {
  sendHtmlContent(server.get(), FontsPageHtml, sizeof(FontsPageHtml));
  LOG_DBG("WEB", "Served fonts page");
}

void CrossPointWebServer::handleFontList() const {
  // Pick up any uploads/deletes that happened since the last reader load.
  const_cast<SdCardFontSystem&>(sdFontSystem).refreshIfDirty();
  const auto& families = sdFontSystem.registry().getFamilies();

  JsonDocument doc;
  JsonArray arr = doc["families"].to<JsonArray>();
  doc["maxFamilies"] = SdCardFontRegistry::MAX_SD_FAMILIES;

  for (const auto& family : families) {
    JsonObject fObj = arr.add<JsonObject>();
    fObj["name"] = family.name;

    JsonArray sizes = fObj["sizes"].to<JsonArray>();
    for (uint8_t s : family.availableSizes()) {
      sizes.add(s);
    }

    JsonArray files = fObj["files"].to<JsonArray>();
    for (const auto& file : family.files) {
      JsonObject fileObj = files.add<JsonObject>();
      // Extract filename from full path
      const char* name = strrchr(file.path.c_str(), '/');
      fileObj["name"] = name ? name + 1 : file.path.c_str();

      // Stat the file for size
      HalFile f;
      if (Storage.openFileForRead("WEB", file.path.c_str(), f)) {
        fileObj["size"] = static_cast<unsigned long>(f.size());
        f.close();
      } else {
        fileObj["size"] = 0;
      }
    }
  }

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void CrossPointWebServer::handleFontUploadData() {
  HTTPUpload& upload = server->upload();

  switch (upload.status) {
    case UPLOAD_FILE_START: {
      esp_task_wdt_reset();
      String family = server->arg("family");
      fontUpload.file = HalFile();
      fontUpload.familyName.clear();
      fontUpload.filePath.clear();
      fontUpload.valid = false;
      fontUpload.magicChecked = false;
      fontUpload.bytesWritten = 0;
      fontUpload.bufferPos = 0;

      if (!FontInstaller::isValidFamilyName(family.c_str())) {
        LOG_ERR("WEB", "Invalid font family name: %s", family.c_str());
        break;
      }

      String filename = upload.filename;
      filename.replace(' ', '_');
      // Validate filename: rejects path traversal (../, /, \) and enforces
      // a .cpfont basename of alphanumeric + hyphen + underscore. Without
      // this an attacker could supply "../../.crosspoint/settings.json" as
      // a "filename" and have it written outside the fonts directory.
      if (!FontInstaller::isValidCpfontFilename(filename.c_str())) {
        LOG_ERR("WEB", "Invalid font filename: %s", filename.c_str());
        break;
      }

      fontUpload.familyName = family.c_str();

      // Create a temporary FontInstaller for directory creation
      FontInstaller installer(sdFontSystem.registry());
      if (!installer.ensureFamilyDir(family.c_str())) {
        LOG_ERR("WEB", "Failed to create font family dir");
        break;
      }

      char path[192];
      FontInstaller::buildFontPath(family.c_str(), filename.c_str(), path, sizeof(path));
      fontUpload.filePath = path;

      if (!Storage.openFileForWrite("WEB", path, fontUpload.file)) {
        LOG_ERR("WEB", "Failed to open font file for write: %s", path);
        break;
      }

      fontUpload.valid = true;
      LOG_DBG("WEB", "Font upload started: %s -> %s", filename.c_str(), path);
      break;
    }

    case UPLOAD_FILE_WRITE: {
      if (!fontUpload.valid) break;
      esp_task_wdt_reset();

      // Validate magic bytes on first chunk only
      if (!fontUpload.magicChecked && upload.currentSize >= 8) {
        if (memcmp(upload.buf, "CPFONT\0\0", 8) != 0) {
          LOG_ERR("WEB", "Invalid .cpfont magic bytes");
          fontUpload.valid = false;
          break;
        }
        fontUpload.magicChecked = true;
      }

      // Buffer writes for efficiency
      size_t remaining = upload.currentSize;
      const uint8_t* src = upload.buf;
      while (remaining > 0) {
        size_t space = FontUploadState::BUFFER_SIZE - fontUpload.bufferPos;
        size_t chunk = (remaining < space) ? remaining : space;
        memcpy(fontUpload.buffer.data() + fontUpload.bufferPos, src, chunk);
        fontUpload.bufferPos += chunk;
        src += chunk;
        remaining -= chunk;

        if (fontUpload.bufferPos >= FontUploadState::BUFFER_SIZE) {
          fontUpload.file.write(fontUpload.buffer.data(), fontUpload.bufferPos);
          fontUpload.bytesWritten += fontUpload.bufferPos;
          fontUpload.bufferPos = 0;
          esp_task_wdt_reset();
        }
      }
      break;
    }

    case UPLOAD_FILE_END: {
      // Flush remaining buffer
      if (fontUpload.valid && fontUpload.bufferPos > 0) {
        fontUpload.file.write(fontUpload.buffer.data(), fontUpload.bufferPos);
        fontUpload.bytesWritten += fontUpload.bufferPos;
        fontUpload.bufferPos = 0;
      }
      if (fontUpload.file.isOpen()) {
        fontUpload.file.close();
      }

      if (!fontUpload.valid && !fontUpload.filePath.empty()) {
        Storage.remove(fontUpload.filePath.c_str());
      }

      LOG_DBG("WEB", "Font upload end: valid=%d, %zu bytes", fontUpload.valid, fontUpload.bytesWritten);
      break;
    }

    case UPLOAD_FILE_ABORTED: {
      if (fontUpload.file) {
        fontUpload.file.close();
      }
      if (!fontUpload.filePath.empty()) {
        Storage.remove(fontUpload.filePath.c_str());
      }
      fontUpload.valid = false;
      LOG_DBG("WEB", "Font upload aborted");
      break;
    }
  }
}

void CrossPointWebServer::handleFontUpload() {
  if (fontUpload.valid) {
    sdFontSystem.markRegistryDirty();
    server->send(200, "application/json", "{\"ok\":true}");
    LOG_DBG("WEB", "Font upload complete: %s", fontUpload.filePath.c_str());
  } else {
    server->send(400, "application/json", "{\"error\":\"Invalid .cpfont file\"}");
  }
}

void CrossPointWebServer::handleFontDelete() {
  String body = server->arg("plain");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err || !doc["family"].is<const char*>()) {
    server->send(400, "application/json", "{\"error\":\"Invalid request\"}");
    return;
  }

  const char* familyName = doc["family"];
  FontInstaller installer(sdFontSystem.registry());
  auto result = installer.deleteFamily(familyName);

  if (result == FontInstaller::Error::OK) {
    sdFontSystem.markRegistryDirty();
    server->send(200, "application/json", "{\"ok\":true}");
    LOG_DBG("WEB", "Deleted font family: %s", familyName);
  } else {
    server->send(500, "application/json", "{\"error\":\"Delete failed\"}");
    LOG_ERR("WEB", "Failed to delete font family: %s", familyName);
  }
}
