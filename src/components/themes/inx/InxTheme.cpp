#include "InxTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalDisplay.h>

#include <algorithm>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kTitleFont = UI_12_FONT_ID;
constexpr int kBodyFont = UI_10_FONT_ID;
constexpr int kMetaFont = SMALL_FONT_ID;
constexpr int kPanelPadX = 12;

int centeredY(const GfxRenderer& renderer, int fontId, Rect rect) {
  return rect.y + (rect.height - renderer.getLineHeight(fontId)) / 2;
}

void drawSquarePanel(const GfxRenderer& renderer, Rect rect, bool selected, bool border = true) {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, selected);
  if (border) {
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height, selected ? 2 : 1, true);
  }
}

void drawCompactScrollBar(const GfxRenderer& renderer, Rect rect, int itemCount, int pageStart, int pageItems) {
  if (itemCount <= pageItems || pageItems <= 0) return;
  constexpr int barW = 3;
  const int barX = rect.x + rect.width - 6;
  const int thumbH = std::max(12, rect.height * pageItems / itemCount);
  const int maxStart = std::max(1, itemCount - pageItems);
  const int travel = std::max(1, rect.height - thumbH);
  const int thumbY = rect.y + std::clamp(pageStart, 0, maxStart) * travel / maxStart;
  renderer.fillRect(barX, rect.y, barW, rect.height, false);
  renderer.drawRect(barX, rect.y, barW, rect.height, 1, true);
  renderer.fillRect(barX, thumbY, barW, thumbH, true);
}
}  // namespace

void InxTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle,
                          const bool readerContext) const {
  const ThemeMetrics& metrics = inxMetrics();
  const int side = metrics.contentSidePadding;
  const Rect header{rect.x + side, rect.y + 5, rect.width - side * 2, std::max(30, rect.height - 10)};

  if (title && title[0] != '\0') {
    renderer.drawLine(header.x, header.y + header.height - 1, header.x + header.width, header.y + header.height - 1);
    const bool showBattery =
        SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
    const int batteryX = header.x + header.width - metrics.batteryWidth;
    const int batteryY = header.y + (header.height - metrics.batteryHeight) / 2;
    int titleWidth = batteryX - header.x - kPanelPadX * 2;
    if (showBattery) titleWidth -= renderer.getTextWidth(kMetaFont, "100%") + batteryPercentSpacing;
    const std::string clipped = renderer.truncatedText(kTitleFont, title, std::max(0, titleWidth), EpdFontFamily::BOLD);
    renderer.drawText(kTitleFont, header.x + kPanelPadX, centeredY(renderer, kTitleFont, header), clipped.c_str(), true,
                      EpdFontFamily::BOLD);
    if (subtitle && subtitle[0] != '\0') {
      const std::string sub = renderer.truncatedText(kMetaFont, subtitle, std::max(0, titleWidth));
      renderer.drawText(kMetaFont, header.x + kPanelPadX, header.y + header.height - renderer.getLineHeight(kMetaFont),
                        sub.c_str());
    }
    drawBatteryRight(renderer, Rect{batteryX, batteryY, metrics.batteryWidth, metrics.batteryHeight}, showBattery);
  }

  drawTopStatusBarClock(renderer, rect.y, nullptr, readerContext, readerContext ? 0 : 2);
}

void InxTheme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                          const bool selected) const {
  if (tabs.empty()) return;
  const int count = static_cast<int>(tabs.size());
  const int gap = 2;
  const int labelFont = kMetaFont;
  const int y = rect.y + 4;
  const int h = rect.height - 8;
  for (int i = 0; i < count; ++i) {
    const int x1 = rect.x + i * rect.width / count;
    const int x2 = rect.x + (i + 1) * rect.width / count;
    Rect tab{x1 + gap, y, std::max(1, x2 - x1 - 2 * gap), h};
    const bool active = tabs[i].selected;
    if (active && selected) {
      renderer.fillRect(tab.x, tab.y + tab.height - 5, tab.width, 5, true);
    } else if (active) {
      renderer.fillRectDither(tab.x, tab.y + tab.height - 5, tab.width, 5, Color::LightGray);
    }
    const std::string clipped = renderer.truncatedText(labelFont, tabs[i].label, tab.width - 4,
                                                       active ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    const int tw = renderer.getTextWidth(labelFont, clipped.c_str(), active ? EpdFontFamily::BOLD
                                                                            : EpdFontFamily::REGULAR);
    renderer.drawText(labelFont, tab.x + (tab.width - tw) / 2, tab.y + 3, clipped.c_str(), true,
                      active ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }
}

void InxTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                        const std::function<std::string(int index)>& rowTitle,
                        const std::function<std::string(int index)>& rowSubtitle,
                        const std::function<UIIcon(int index)>& rowIcon,
                        const std::function<std::string(int index)>& rowValue, const bool highlightValue,
                        const std::function<bool(int index)>& rowDimmed,
                        const std::function<bool(int index)>& isHeader) const {
  (void)highlightValue;
  if (itemCount <= 0) return;

  const ThemeMetrics& metrics = inxMetrics();
  const bool hasSubtitle = static_cast<bool>(rowSubtitle);
  const int rowH = hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight;
  const int pageItems = std::max(1, rect.height / rowH);
  const int safeSelected = std::max(0, selectedIndex);
  const int pageStart = (safeSelected / pageItems) * pageItems;
  const int rowW = rect.width - (itemCount > pageItems ? 14 : 0);

  for (int i = pageStart; i < itemCount && i < pageStart + pageItems; ++i) {
    const bool header = isHeader && isHeader(i);
    const bool selected = i == selectedIndex && !header;
    Rect row{rect.x + metrics.contentSidePadding, rect.y + (i - pageStart) * rowH,
             rowW - metrics.contentSidePadding * 2, rowH - 2};
    if (header) {
      renderer.drawText(kMetaFont, row.x, row.y + 3,
                        renderer.truncatedText(kMetaFont, rowTitle(i).c_str(), row.width, EpdFontFamily::BOLD).c_str(),
                        true, EpdFontFamily::BOLD);
      renderer.drawLine(row.x, row.y + row.height - 1, row.x + row.width, row.y + row.height - 1);
      continue;
    }

    if (selected) drawSquarePanel(renderer, row, true, false);
    int textX = row.x + 8;
    int textW = row.width - 16;
    if (rowIcon) {
      const uint8_t* icon = iconForName(rowIcon(i), 20);
      if (icon) {
        if (selected) {
          renderer.drawIconInverted(icon, textX, row.y + (row.height - 20) / 2, 20, 20);
        } else {
          renderer.drawIcon(icon, textX, row.y + (row.height - 20) / 2, 20, 20);
        }
        textX += 28;
        textW -= 28;
      }
    }
    if (rowValue) {
      const std::string value = renderer.truncatedText(kMetaFont, rowValue(i).c_str(), std::max(0, row.width / 3));
      if (!value.empty()) {
        const int vw = renderer.getTextWidth(kMetaFont, value.c_str());
        renderer.drawText(kMetaFont, row.x + row.width - vw - 8, centeredY(renderer, kMetaFont, row), value.c_str(),
                          !selected, EpdFontFamily::BOLD);
        textW -= vw + 12;
      }
    }
    const std::string title = renderer.truncatedText(kBodyFont, rowTitle(i).c_str(), std::max(1, textW),
                                                     hasSubtitle ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    if (hasSubtitle) {
      renderer.drawText(kBodyFont, textX, row.y + 7, title.c_str(), !selected, EpdFontFamily::BOLD);
      const std::string sub = renderer.truncatedText(kMetaFont, rowSubtitle(i).c_str(), std::max(1, textW));
      renderer.drawText(kMetaFont, textX, row.y + 28, sub.c_str(), !selected);
    } else {
      renderer.drawText(kBodyFont, textX, centeredY(renderer, kBodyFont, row), title.c_str(), !selected);
    }
    if (rowDimmed && rowDimmed(i) && !selected) {
      renderer.fillRectDither(row.x, row.y, row.width, row.height, Color::White);
    }
  }
  drawCompactScrollBar(renderer, rect, itemCount, pageStart, pageItems);
}

void InxTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                              const std::function<std::string(int index)>& buttonLabel,
                              const std::function<UIIcon(int index)>& rowIcon) const {
  drawList(renderer, rect, buttonCount, selectedIndex, buttonLabel, nullptr, rowIcon, nullptr, false);
}

void InxTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                               const char* btn4, const bool allowInvertedText) const {
  const GfxRenderer::Orientation original = renderer.getOrientation();
  const bool inverted = allowInvertedText && original == GfxRenderer::Orientation::PortraitInverted;
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageW = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();
  const int count = 4;
  const int gap = 6;
  const int side = 14;
  const int h = inxMetrics().buttonHintsHeight - 8;
  const int y = pageH - h - 5;
  const int w = (pageW - 2 * side - (count - 1) * gap) / count;
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < count; ++i) {
    if (!labels[i] || labels[i][0] == '\0') continue;
    const int x = side + i * (w + gap);
    renderer.drawLine(x, y, x + w, y);
    renderer.drawLine(x, y + h - 1, x + w, y + h - 1);
  }

  renderer.setOrientation(inverted ? GfxRenderer::Orientation::PortraitInverted : GfxRenderer::Orientation::Portrait);
  for (int i = 0; i < count; ++i) {
    if (!labels[i] || labels[i][0] == '\0') continue;
    const int drawIndex = inverted ? count - 1 - i : i;
    const int x = side + drawIndex * (w + gap);
    const std::string clipped = renderer.truncatedText(kMetaFont, labels[i], w - 4);
    const int tw = renderer.getTextWidth(kMetaFont, clipped.c_str());
    renderer.drawText(kMetaFont, x + (w - tw) / 2, inverted ? 7 : y + 8, clipped.c_str());
  }

  renderer.setOrientation(original);
}

Rect InxTheme::drawPopup(const GfxRenderer& renderer, const char* message) const {
  const ThemeMetrics& metrics = inxMetrics();
  const int textW = renderer.getTextWidth(kTitleFont, message ? message : "", EpdFontFamily::BOLD);
  const int textH = renderer.getLineHeight(kTitleFont);
  const int w = std::min(renderer.getScreenWidth() - 24, textW + metrics.popupMarginX * 2);
  const int h = textH + metrics.popupMarginY * 2;
  const int x = (renderer.getScreenWidth() - w) / 2;
  const int y = std::max(0, static_cast<int>(renderer.getScreenHeight() * metrics.popupTopOffsetRatio));
  renderer.fillRect(x, y, w, h, true);
  renderer.drawRect(x + 3, y + 3, w, h, 1, true);
  renderer.drawText(kTitleFont, x + (w - textW) / 2, y + metrics.popupMarginY - 2, message ? message : "", false,
                    EpdFontFamily::BOLD);
  renderer.displayBuffer();
  return Rect{x, y, w, h};
}

void InxTheme::fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const {
  const int barX = layout.x + 14;
  const int barW = std::max(1, layout.width - 28);
  const int barY = layout.y + layout.height - 10;
  const int fillW = std::clamp(progress, 0, 100) * barW / 100;
  renderer.fillRect(barX, barY, barW, 4, false);
  renderer.drawRect(barX, barY, barW, 4, 1, false);
  if (fillW > 0) renderer.fillRect(barX, barY, fillW, 4, false);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void InxFlowTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                       int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                       bool& bufferRestored, const std::function<bool()>& storeCoverBuffer,
                                       const BookReadingStats* stats, const float progressPercent) const {
  Lyra3CoversTheme delegate;
  delegate.drawRecentBookCover(renderer, rect, recentBooks, selectorIndex, coverRendered, coverBufferStored,
                               bufferRestored, storeCoverBuffer, stats, progressPercent);
}

void InxNeobrutalistTheme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                                      const bool selected) const {
  if (tabs.empty()) return;
  const int count = static_cast<int>(tabs.size());
  constexpr int gap = 5;
  for (int i = 0; i < count; ++i) {
    const int x1 = rect.x + i * rect.width / count;
    const int x2 = rect.x + (i + 1) * rect.width / count;
    Rect tab{x1 + gap / 2, rect.y + 4, std::max(1, x2 - x1 - gap), rect.height - 10};
    const bool active = tabs[i].selected;
    if (active) {
      renderer.fillRect(tab.x + 3, tab.y + 3, tab.width, tab.height, true);
    }
    renderer.fillRect(tab.x, tab.y, tab.width, tab.height, active && selected);
    renderer.drawRect(tab.x, tab.y, tab.width, tab.height, active ? 2 : 1, true);
    const std::string clipped = renderer.truncatedText(SMALL_FONT_ID, tabs[i].label, tab.width - 6,
                                                       active ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    const int tw = renderer.getTextWidth(SMALL_FONT_ID, clipped.c_str(), active ? EpdFontFamily::BOLD
                                                                                : EpdFontFamily::REGULAR);
    renderer.drawText(SMALL_FONT_ID, tab.x + (tab.width - tw) / 2, centeredY(renderer, SMALL_FONT_ID, tab),
                      clipped.c_str(), !(active && selected),
                      active ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }
}
