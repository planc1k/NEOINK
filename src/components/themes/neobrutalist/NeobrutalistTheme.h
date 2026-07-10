#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

namespace NeobrutalistMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 16,
                                 .batteryHeight = 12,
                                 .topPadding = 4,
                                 .batteryBarHeight = 34,
                                 .headerHeight = 62,
                                 .verticalSpacing = 12,
                                 .previewPadding = 12,
                                 .previewHeightPercent = 30,
                                 .contentSidePadding = 18,
                                 .listRowHeight = 42,
                                 .listWithSubtitleRowHeight = 66,
                                 .menuRowHeight = 50,
                                 .menuSpacing = 7,
                                 .tabSpacing = 6,
                                 .tabBarHeight = 44,
                                 .scrollBarWidth = 5,
                                 .scrollBarRightOffset = 6,
                                 .homeTopPadding = 34,
                                 .homeCoverHeight = 214,
                                 .homeCoverTileHeight = 304,
                                 .homeRecentBooksCount = 3,
                                 .homeContinueReadingInMenu = false,
                                 .homeMenuTopOffset = 8,
                                 .buttonHintsHeight = 42,
                                 .sideButtonHintsWidth = 32,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 19,
                                 .keyboardKeyWidth = 30,
                                 .keyboardKeyHeight = 40,
                                 .keyboardKeySpacing = 3,
                                 .keyboardBottomKeyHeight = 35,
                                 .keyboardBottomKeySpacing = 5,
                                 .keyboardBottomAligned = true,
                                 .keyboardCenteredText = false,
                                 .keyboardVerticalOffset = -6,
                                 .keyboardTextFieldWidthPercent = 86,
                                 .keyboardWidthPercent = 92,
                                 .keyboardKeyCornerRadius = 0,
                                 .keyboardFillUnselected = true,
                                 .keyboardOutlineAllUnselected = true,
                                 .keyboardDrawSpecialOutlineWhenUnselected = true,
                                 .keyboardSecondaryLabelRightPadding = 3,
                                 .keyboardSecondaryLabelTopPadding = 1,
                                 .keyboardMinArrowHeadSize = 1,
                                 .popupTopOffsetRatio = 0.13f,
                                 .popupMarginX = 18,
                                 .popupMarginY = 14,
                                 .popupFrameThickness = 3,
                                 .popupCornerRadius = 0,
                                 .popupTextBold = true,
                                 .popupTextInverted = false,
                                 .popupTextBaselineOffsetY = -2,
                                 .popupProgressBarHeight = 5,
                                 .popupProgressDrawOutline = true,
                                 .popupProgressClampPercent = true,
                                 .popupProgressFillInverted = false,
                                 .popupProgressOutlineInverted = false,
                                 .textFieldHorizontalPadding = 8,
                                 .textFieldNormalThickness = 2,
                                 .textFieldCursorThickness = 4,
                                 .textFieldLineEndOffset = 0};
}

class NeobrutalistTheme : public LyraTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                  bool readerContext = false) const override;
  void drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                     const char* rightLabel = nullptr) const override;
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
  void drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           const std::function<bool()>& storeCoverBuffer, const BookReadingStats* stats = nullptr,
                           float progressPercent = -1.0f) const override;
  Rect drawPopup(const GfxRenderer& renderer, const char* message) const override;
  void fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, int progress) const override;
  void drawTextField(const GfxRenderer& renderer, Rect rect, int textWidth, bool cursorMode = false,
                     int contentStartX = 0, int contentWidth = 0) const override;
  void drawKeyboardKey(const GfxRenderer& renderer, Rect rect, const char* label, bool isSelected,
                       const char* secondaryLabel = nullptr, KeyboardKeyType keyType = KeyboardKeyType::Normal,
                       bool inactiveSelection = false) const override;
  bool showsFileIcons() const override { return true; }
  bool homeMenuShowsContinueReading() const { return true; }
};
