#pragma once

#include "components/themes/lyra/Lyra3CoversTheme.h"
#include "components/themes/lyra/LyraTheme.h"
#include "components/themes/neobrutalist/NeobrutalistTheme.h"

class GfxRenderer;

namespace InxMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  v.topPadding = 4;
  v.headerHeight = 48;
  v.verticalSpacing = 10;
  v.contentSidePadding = 18;
  v.listRowHeight = 34;
  v.listWithSubtitleRowHeight = 56;
  v.menuRowHeight = 44;
  v.menuSpacing = 5;
  v.tabBarHeight = 34;
  v.homeTopPadding = 92;
  v.homeCoverHeight = 226;
  v.homeCoverTileHeight = 258;
  v.homeRecentBooksCount = 1;
  v.homeMenuTopOffset = 10;
  v.buttonHintsHeight = 38;
  v.keyboardKeyCornerRadius = 0;
  v.popupCornerRadius = 0;
  return v;
}();
}  // namespace InxMetrics

namespace InxFlowMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = InxMetrics::values;
  v.homeCoverHeight = 220;
  v.homeCoverTileHeight = 300;
  v.homeRecentBooksCount = 3;
  v.menuRowHeight = 42;
  return v;
}();
}  // namespace InxFlowMetrics

namespace InxNeobrutalistMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = NeobrutalistMetrics::values;
  v.headerHeight = 52;
  v.tabBarHeight = 42;
  v.homeTopPadding = 102;
  v.homeCoverHeight = 214;
  v.homeCoverTileHeight = 312;
  v.homeRecentBooksCount = 3;
  v.homeMenuTopOffset = 10;
  return v;
}();
}  // namespace InxNeobrutalistMetrics

class InxTheme : public LyraTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                  bool readerContext = false) const override;
  void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                  bool selected) const override;
  void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                const std::function<std::string(int index)>& rowTitle,
                const std::function<std::string(int index)>& rowSubtitle,
                const std::function<UIIcon(int index)>& rowIcon, const std::function<std::string(int index)>& rowValue,
                bool highlightValue, const std::function<bool(int index)>& rowDimmed = nullptr,
                const std::function<bool(int index)>& isHeader = nullptr) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3, const char* btn4,
                       bool allowInvertedText = false) const override;
  Rect drawPopup(const GfxRenderer& renderer, const char* message) const override;
  void fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, int progress) const override;
  bool showsFileIcons() const override { return true; }

 protected:
  virtual const ThemeMetrics& inxMetrics() const { return InxMetrics::values; }
};

class InxFlowTheme : public InxTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           const std::function<bool()>& storeCoverBuffer, const BookReadingStats* stats = nullptr,
                           float progressPercent = -1.0f) const override;

 protected:
  const ThemeMetrics& inxMetrics() const override { return InxFlowMetrics::values; }
};

class InxNeobrutalistTheme : public NeobrutalistTheme {
 public:
  void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                  bool selected) const override;
};
