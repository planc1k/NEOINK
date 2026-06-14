#include "ClippingsManager.h"

#include <HalStorage.h>
#include <Logging.h>
#include <common/FsApiConstants.h>

#include <algorithm>

bool ClippingsManager::saveClipping(const std::string& bookTitle, const std::string& author,
                                    const std::string& chapterTitle, const int pageNumber,
                                    const std::string& selectedText) {
  FsFile file = Storage.open(CLIPPINGS_PATH, O_RDWR | O_CREAT | O_AT_END);
  if (!file) {
    LOG_ERR("CLIP", "Failed to open %s for append", CLIPPINGS_PATH);
    return false;
  }

  std::string location = "- Your Highlight on Page " + std::to_string(pageNumber);
  if (!chapterTitle.empty()) {
    location += " | " + chapterTitle;
  }
  location += "\n";

  static constexpr size_t MAX_TEXT_BYTES = 2000;
  const size_t textLen = std::min(selectedText.size(), MAX_TEXT_BYTES);
  static constexpr char separator[] = "\n==========\n";

  std::string buffer;
  buffer.reserve(bookTitle.size() + author.size() + location.size() + textLen + sizeof(separator) + 8);
  buffer += bookTitle;
  buffer += " (";
  buffer += author;
  buffer += ")\n";
  buffer += location;
  buffer += '\n';
  buffer.append(selectedText.c_str(), textLen);
  buffer += separator;

  const bool ok = file.write(buffer.data(), buffer.size()) == buffer.size();
  file.flush();
  file.close();

  if (!ok) {
    LOG_ERR("CLIP", "Failed to write clipping to %s", CLIPPINGS_PATH);
    return false;
  }

  LOG_DBG("CLIP", "Saved clipping to %s (%zu bytes)", CLIPPINGS_PATH, textLen);
  return true;
}
