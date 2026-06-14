#include "ClipTextBuilder.h"

#include <Logging.h>

#include <algorithm>
#include <cctype>

namespace ClipTextBuilder {
namespace {

bool hasEmSpace(const std::string& word) {
  return word.size() >= 3 && static_cast<unsigned char>(word[0]) == 0xE2 &&
         static_cast<unsigned char>(word[1]) == 0x80 && static_cast<unsigned char>(word[2]) == 0x83;
}

std::string stripEmSpace(const std::string& word) { return hasEmSpace(word) ? word.substr(3) : word; }

std::string stripTrailingHyphen(std::string word) {
  while (!word.empty() && word.back() == '-') {
    word.pop_back();
  }
  return word;
}

}  // namespace

ClippingResult build(const std::vector<WordRef>& words, const int from, const int to, const int total,
                     const int startPageInSection) {
  std::string text;
  text.reserve(256);

  constexpr int ANCHOR_WORDS = 4;
  std::string startAnchor;
  int anchorCount = 0;

  for (int i = from; i <= to; ++i) {
    const auto wordText = stripEmSpace(words[i].text);
    const bool yGap =
        i > from && words[i].pageIdx == words[i - 1].pageIdx && words[i].y > words[i - 1].y + words[i - 1].h;
    const bool paragraphStart = i > from && (hasEmSpace(words[i].text) || words[i].paragraphStart || yGap);

    if (i > from && !text.empty() && !paragraphStart) {
      const auto prevStripped = stripEmSpace(words[i - 1].text);
      if (!prevStripped.empty() && prevStripped.back() == '-' && !wordText.empty() &&
          !std::isspace(static_cast<unsigned char>(wordText[0])) &&
          !std::ispunct(static_cast<unsigned char>(wordText[0]))) {
        text.pop_back();
        text += wordText;
        continue;
      }
    }

    if (paragraphStart) {
      text += '\n';
    } else if (!text.empty()) {
      const bool attached = words[i].pageIdx == words[i - 1].pageIdx && words[i].y == words[i - 1].y &&
                            words[i].x <= words[i - 1].x + words[i - 1].w + 2;
      if (!attached) {
        text += ' ';
      }
    }
    text += wordText;

    if (anchorCount < ANCHOR_WORDS) {
      if (!startAnchor.empty()) startAnchor += ' ';
      startAnchor += stripTrailingHyphen(wordText);
      anchorCount++;
    }
  }

  std::string endAnchor;
  anchorCount = 0;
  for (int i = to; i >= from && anchorCount < ANCHOR_WORDS; --i) {
    const auto wordText = stripTrailingHyphen(stripEmSpace(words[i].text));
    endAnchor = endAnchor.empty() ? wordText : wordText + ' ' + endAnchor;
    anchorCount++;
  }

  constexpr int CONTEXT_WORDS = 3;
  std::string beforeStart;
  for (int i = from - 1; i >= 0 && (from - i) <= CONTEXT_WORDS; --i) {
    const auto stripped = stripTrailingHyphen(stripEmSpace(words[i].text));
    if (stripped.find_first_not_of(' ') == std::string::npos) continue;
    beforeStart = beforeStart.empty() ? stripped : stripped + ' ' + beforeStart;
  }
  std::string afterEnd;
  for (int i = to + 1; i < total && (i - to) <= CONTEXT_WORDS; ++i) {
    const auto stripped = stripTrailingHyphen(stripEmSpace(words[i].text));
    if (stripped.find_first_not_of(' ') == std::string::npos) continue;
    afterEnd = afterEnd.empty() ? stripped : afterEnd + ' ' + stripped;
  }

  std::string midText;
  constexpr int MID_WORDS = 4;
  int midStart = (from + to) / 2 - (MID_WORDS / 2);
  int midEnd = midStart + MID_WORDS - 1;
  if (midStart < from) midStart = from;
  if (midEnd > to) midEnd = to;
  for (int i = midStart; i <= midEnd; ++i) {
    const auto wordText = stripTrailingHyphen(stripEmSpace(words[i].text));
    if (!midText.empty()) midText += ' ';
    midText += wordText;
  }

  LOG_DBG("CLIP", "Built clipping: words=%d start=\"%.24s\" end=\"%.24s\"", to - from + 1, startAnchor.c_str(),
          endAnchor.c_str());

  return ClippingResult{std::move(text),
                        from,
                        to,
                        static_cast<uint16_t>(startPageInSection + words[from].pageIdx),
                        static_cast<uint16_t>(startPageInSection + words[to].pageIdx),
                        std::move(startAnchor),
                        std::move(endAnchor),
                        std::move(beforeStart),
                        std::move(afterEnd),
                        std::move(midText),
                        static_cast<uint16_t>(to - from + 1)};
}

}  // namespace ClipTextBuilder
