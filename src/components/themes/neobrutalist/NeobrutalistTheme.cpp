#include "NeobrutalistTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

namespace {
constexpr int kShadowOffset = 4;
constexpr int kBorder = 2;
constexpr int kHeavyBorder = 3;
constexpr int kPadX = 14;
constexpr int kTitleFont = UI_12_FONT_ID;
constexpr int kBodyFont = UI_10_FONT_ID;
constexpr int kMetaFont = SMALL_FONT_ID;

void drawShadow(const GfxRenderer& renderer, const Rect r) {
  renderer.fillRect(r.x + kShadowOffset, r.y + kShadowOffset, r.width, r.height, true);
}

void drawPanel(const GfxRenderer& renderer, Rect r, const bool selected, const bool dither = false,
               const bool shadow = true) {
  if (shadow) {
    drawShadow(renderer, r);
  }
  if (selected) {
    renderer.fillRect(r.x, r.y, r.width, r.height, true);
  } else if (dither) {
    renderer.fillRectDither(r.x, r.y, r.width, r.height, Color::LightGray);
  } else {
    renderer.fillRect(r.x, r.y, r.width, r.height, false);
  }
  renderer.drawRect(r.x, r.y, r.width, r.height, selected ? kHeavyBorder : kBorder, true);
}

void drawScrollBar(const GfxRenderer& renderer, Rect rect, int itemCount, int pageStartIndex, int pageItems) {
  if (itemCount <= 0 || pageItems <= 0 || itemCount <= pageItems) {
    return;
  }
  const int barW = NeobrutalistMetrics::values.scrollBarWidth;
  const int barX = rect.x + rect.width - NeobrutalistMetrics::values.scrollBarRightOffset - barW;
  const int barY = rect.y;
  const int barH = rect.height;
  const int thumbH = std::max(14, (barH * pageItems) / itemCount);
  const int maxStart = std::max(1, itemCount - pageItems);
  const int maxTravel = std::max(1, barH - thumbH);
  const int clampedStart = std::clamp(pageStartIndex, 0, maxStart);
  const int thumbY = barY + (clampedStart * maxTravel) / maxStart;

  renderer.drawRect(barX, barY, barW, barH, 1, true);
  renderer.fillRect(barX, thumbY, barW, thumbH, true);
}

int centeredY(const GfxRenderer& renderer, const int fontId, const Rect r) {
  return r.y + (r.height - renderer.getLineHeight(fontId)) / 2;
}

std::string upperCopy(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return text;
}

void drawBookmarkRibbon(const GfxRenderer& renderer, const Rect r, const bool black) {
  const int ribbonW = std::max(10, r.width / 8);
  const int ribbonH = std::max(28, r.height / 5);
  const int x = r.x + r.width - ribbonW - 10;
  const int y = r.y + 8;
  const int notch = std::max(5, ribbonW / 2);
  const int cx = x + ribbonW / 2;
  const int px[5] = {x, x + ribbonW, x + ribbonW, cx, x};
  const int py[5] = {y, y, y + ribbonH, y + ribbonH - notch, y + ribbonH};
  renderer.fillPolygon(px, py, 5, black);
}
}  // namespace

void NeobrutalistTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle,
                                   const bool readerContext) const {
  if (title == nullptr) {
    drawTopStatusBarClock(renderer, rect.y, nullptr, readerContext, readerContext ? 0 : 2);
    return;
  }

  const auto& metrics = NeobrutalistMetrics::values;
  const int side = metrics.contentSidePadding;
  Rect panel{rect.x + side, rect.y + 8, rect.width - side * 2, rect.height - 16};
  if (panel.height < 34) panel.height = rect.height - 8;
  drawPanel(renderer, panel, false, false, true);

  const bool showBattery =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = panel.x + panel.width - metrics.batteryWidth - 10;
  const int batteryY = panel.y + (panel.height - metrics.batteryHeight) / 2;
  int textRight = batteryX - 10;
  if (showBattery) {
    textRight -= renderer.getTextWidth(SMALL_FONT_ID, "100%") + batteryPercentSpacing;
  }

  const int titleY = panel.y + (subtitle ? 7 : (panel.height - renderer.getLineHeight(kTitleFont)) / 2);
  const int textWidth = std::max(0, textRight - panel.x - kPadX);
  const std::string clipped = renderer.truncatedText(kTitleFont, title, textWidth, EpdFontFamily::BOLD);
  renderer.drawText(kTitleFont, panel.x + kPadX, titleY, clipped.c_str(), true, EpdFontFamily::BOLD);
  if (subtitle && subtitle[0] != '\0') {
    const std::string sub = renderer.truncatedText(kMetaFont, subtitle, textWidth, EpdFontFamily::REGULAR);
    renderer.drawText(kMetaFont, panel.x + kPadX, titleY + renderer.getLineHeight(kTitleFont) + 1, sub.c_str(), true);
  }

  drawBatteryRight(renderer, Rect{batteryX, batteryY, metrics.batteryWidth, metrics.batteryHeight}, showBattery);
  drawTopStatusBarClock(renderer, rect.y, nullptr, readerContext, readerContext ? 0 : 2);
}

void NeobrutalistTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                                      const char* rightLabel) const {
  Rect panel{rect.x + NeobrutalistMetrics::values.contentSidePadding, rect.y + 2,
             rect.width - NeobrutalistMetrics::values.contentSidePadding * 2, rect.height - 4};
  drawPanel(renderer, panel, false, true, false);
  const std::string left = upperCopy(label ? label : "");
  renderer.drawText(kMetaFont, panel.x + 8, centeredY(renderer, kMetaFont, panel), left.c_str(), true,
                    EpdFontFamily::BOLD);
  if (rightLabel && rightLabel[0] != '\0') {
    const int w = renderer.getTextWidth(kMetaFont, rightLabel, EpdFontFamily::BOLD);
    renderer.drawText(kMetaFont, panel.x + panel.width - w - 8, centeredY(renderer, kMetaFont, panel), rightLabel, true,
                      EpdFontFamily::BOLD);
  }
}

void NeobrutalistTheme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                                   const bool selected) const {
  if (tabs.empty()) return;
  const int count = static_cast<int>(tabs.size());
  const int gap = NeobrutalistMetrics::values.tabSpacing;
  const int y = rect.y + 5;
  const int h = rect.height - 12;
  for (int i = 0; i < count; i++) {
    const int slotX = rect.x + (i * rect.width) / count;
    const int nextX = rect.x + ((i + 1) * rect.width) / count;
    Rect tab{slotX + gap / 2, y, std::max(0, nextX - slotX - gap), h};
    const bool active = tabs[i].selected;
    drawPanel(renderer, tab, active && selected, active && !selected, active);
    const int tw = renderer.getTextWidth(kMetaFont, tabs[i].label, EpdFontFamily::BOLD);
    renderer.drawText(kMetaFont, tab.x + (tab.width - tw) / 2, centeredY(renderer, kMetaFont, tab), tabs[i].label,
                      !(active && selected), EpdFontFamily::BOLD);
  }
}

void NeobrutalistTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                                 const std::function<std::string(int index)>& rowTitle,
                                 const std::function<std::string(int index)>& rowSubtitle,
                                 const std::function<UIIcon(int index)>& rowIcon,
                                 const std::function<std::string(int index)>& rowValue, bool highlightValue,
                                 const std::function<bool(int index)>& rowDimmed,
                                 const std::function<bool(int index)>& isHeader) const {
  (void)highlightValue;
  if (itemCount <= 0) return;

  const auto& metrics = NeobrutalistMetrics::values;
  const bool hasSubtitle = static_cast<bool>(rowSubtitle);
  const int rowHeight = hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight;
  const int gap = 5;
  const int rowStep = rowHeight + gap;
  const int pageItems = std::max(1, rect.height / rowStep);
  const int safeSelected = std::max(0, selectedIndex);
  const int pageStart = (safeSelected / pageItems) * pageItems;
  const int rowX = rect.x + metrics.contentSidePadding / 2;
  const int rowW = rect.width - metrics.contentSidePadding - (itemCount > pageItems ? 12 : 0);

  for (int i = pageStart; i < itemCount && i < pageStart + pageItems; i++) {
    const bool header = isHeader && isHeader(i);
    const bool selected = i == selectedIndex && !header;
    Rect row{rowX, rect.y + (i - pageStart) * rowStep, rowW, header ? std::min(rowHeight, 28) : rowHeight};

    if (header) {
      renderer.fillRect(row.x, row.y + row.height - 4, row.width, 4, true);
      const std::string text = upperCopy(rowTitle(i));
      renderer.drawText(kMetaFont, row.x + 4, row.y + 3,
                        renderer.truncatedText(kMetaFont, text.c_str(), row.width - 8, EpdFontFamily::BOLD).c_str(),
                        true, EpdFontFamily::BOLD);
      continue;
    }

    drawPanel(renderer, row, selected, false, true);
    const bool foreground = !selected;
    int textX = row.x + kPadX;
    int textW = row.width - kPadX * 2;
    const int iconSize = hasSubtitle ? 24 : 20;
    if (rowIcon) {
      const uint8_t* icon = iconForName(rowIcon(i), iconSize);
      if (icon != nullptr) {
        Rect iconBox{row.x + 8, row.y + (row.height - iconSize - 6) / 2, iconSize + 6, iconSize + 6};
        renderer.drawRect(iconBox.x, iconBox.y, iconBox.width, iconBox.height, 1, foreground);
        if (selected) {
          renderer.drawIconInverted(icon, iconBox.x + 3, iconBox.y + 3, iconSize, iconSize);
        } else {
          renderer.drawIcon(icon, iconBox.x + 3, iconBox.y + 3, iconSize, iconSize);
        }
        textX = iconBox.x + iconBox.width + 8;
        textW = row.x + row.width - kPadX - textX;
      }
    }

    if (rowValue) {
      const std::string raw = rowValue(i);
      if (!raw.empty()) {
        const std::string value = renderer.truncatedText(kMetaFont, raw.c_str(), std::max(0, row.width / 3));
        const int valueW = renderer.getTextWidth(kMetaFont, value.c_str());
        renderer.drawText(kMetaFont, row.x + row.width - valueW - kPadX, centeredY(renderer, kMetaFont, row),
                          value.c_str(), foreground, EpdFontFamily::BOLD);
        textW = std::max(0, textW - valueW - 8);
      }
    }

    const std::string title =
        renderer.truncatedText(kBodyFont, rowTitle(i).c_str(), textW, EpdFontFamily::BOLD);
    const bool dimmed = rowDimmed && rowDimmed(i) && !selected;
    if (hasSubtitle) {
      const int titleY = row.y + 10;
      renderer.drawText(kBodyFont, textX, titleY, title.c_str(), foreground, EpdFontFamily::BOLD);
      const std::string sub = renderer.truncatedText(kMetaFont, rowSubtitle(i).c_str(), textW);
      renderer.drawText(kMetaFont, textX, titleY + renderer.getLineHeight(kBodyFont) + 5, sub.c_str(), foreground);
    } else {
      renderer.drawText(kBodyFont, textX, centeredY(renderer, kBodyFont, row), title.c_str(), foreground,
                        EpdFontFamily::BOLD);
    }
    if (dimmed) {
      renderer.fillRectDither(row.x + 2, row.y + 2, row.width - 4, row.height - 4, Color::White);
    }
  }

  drawScrollBar(renderer, rect, itemCount, pageStart, pageItems);
}

void NeobrutalistTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                       const std::function<std::string(int index)>& buttonLabel,
                                       const std::function<UIIcon(int index)>& rowIcon) const {
  const auto& metrics = NeobrutalistMetrics::values;
  const int rowStep = metrics.menuRowHeight + metrics.menuSpacing;
  const int pageItems = std::max(1, rect.height / rowStep);
  const int safeSelected = std::max(0, selectedIndex);
  const int pageStart = (safeSelected / pageItems) * pageItems;
  const int rowX = rect.x + metrics.contentSidePadding;
  const int rowW = rect.width - metrics.contentSidePadding * 2 - (buttonCount > pageItems ? 14 : 0);

  for (int i = pageStart; i < buttonCount && i < pageStart + pageItems; i++) {
    Rect row{rowX, rect.y + (i - pageStart) * rowStep, rowW, metrics.menuRowHeight};
    const bool selected = i == selectedIndex;
    drawPanel(renderer, row, selected, false, true);
    int textX = row.x + kPadX;
    int textW = row.width - kPadX * 2;
    if (rowIcon) {
      const uint8_t* icon = iconForName(rowIcon(i), 22);
      if (icon != nullptr) {
        if (selected) {
          renderer.drawIconInverted(icon, textX, row.y + (row.height - 22) / 2, 22, 22);
        } else {
          renderer.drawIcon(icon, textX, row.y + (row.height - 22) / 2, 22, 22);
        }
        textX += 32;
        textW -= 32;
      }
    }
    const std::string label = renderer.truncatedText(kTitleFont, buttonLabel(i).c_str(), textW, EpdFontFamily::BOLD);
    renderer.drawText(kTitleFont, textX, centeredY(renderer, kTitleFont, row), label.c_str(), !selected,
                      EpdFontFamily::BOLD);
  }
  drawScrollBar(renderer, rect, buttonCount, pageStart, pageItems);
}

void NeobrutalistTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                        const char* btn4, const bool allowInvertedText) const {
  const GfxRenderer::Orientation original = renderer.getOrientation();
  const bool inverted = allowInvertedText && original == GfxRenderer::Orientation::PortraitInverted;
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageW = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();
  const int side = 20;
  const int gap = 8;
  const int h = NeobrutalistMetrics::values.buttonHintsHeight - 8;
  const int y = pageH - h - 7;
  const int w = (pageW - side * 2 - gap * 3) / 4;
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    const int logical = inverted ? 3 - i : i;
    Rect box{side + i * (w + gap), y, w, h};
    const bool active = labels[logical] && labels[logical][0] != '\0';
    drawPanel(renderer, box, false, !active, active);
  }

  renderer.setOrientation(inverted ? GfxRenderer::Orientation::PortraitInverted : GfxRenderer::Orientation::Portrait);
  for (int i = 0; i < 4; i++) {
    const int drawIndex = inverted ? 3 - i : i;
    const char* label = labels[i];
    if (!label || label[0] == '\0') continue;
    Rect box{side + drawIndex * (w + gap), inverted ? 7 : y, w, h};
    const std::string clipped = renderer.truncatedText(kMetaFont, label, w - 8, EpdFontFamily::BOLD);
    const int tw = renderer.getTextWidth(kMetaFont, clipped.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(kMetaFont, box.x + (box.width - tw) / 2, centeredY(renderer, kMetaFont, box), clipped.c_str(),
                      true, EpdFontFamily::BOLD);
  }
  renderer.setOrientation(original);
}

void NeobrutalistTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn,
                                            const char* bottomBtn) const {
  const int w = NeobrutalistMetrics::values.sideButtonHintsWidth;
  const int h = 82;
  const int screenW = renderer.getScreenWidth();
  const int y = gpio.deviceIsX3() ? 150 : 190;
  if (topBtn && topBtn[0] != '\0') {
    Rect r{0, y, w, h};
    drawPanel(renderer, r, false, false, false);
    renderer.drawTextRotated90CW(kMetaFont, 6, y + (h + renderer.getTextWidth(kMetaFont, topBtn)) / 2, topBtn);
  }
  if (bottomBtn && bottomBtn[0] != '\0') {
    Rect r{screenW - w, y, w, h};
    drawPanel(renderer, r, false, false, false);
    renderer.drawTextRotated90CW(kMetaFont, screenW - w + 6,
                                 y + (h + renderer.getTextWidth(kMetaFont, bottomBtn)) / 2, bottomBtn);
  }
}

void NeobrutalistTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                            const std::vector<RecentBook>& recentBooks, int selectorIndex,
                                            bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                            const std::function<bool()>& storeCoverBuffer,
                                            const BookReadingStats* stats, float progressPercent) const {
  (void)stats;
  (void)progressPercent;
  const bool hasBook = !recentBooks.empty();
  const bool selected = hasBook && selectorIndex == 0;
  const int side = NeobrutalistMetrics::values.contentSidePadding;
  Rect card{rect.x + side, rect.y + 6, rect.width - side * 2, rect.height - 14};
  drawPanel(renderer, card, false, true, true);

  if (!hasBook) {
    renderer.drawCenteredText(kTitleFont, card.y + card.height / 2 - renderer.getLineHeight(kTitleFont),
                              tr(STR_NO_OPEN_BOOK), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(kBodyFont, card.y + card.height / 2 + 4, tr(STR_START_READING));
    return;
  }

  const RecentBook& book = recentBooks[0];
  const int coverH = std::min(NeobrutalistMetrics::values.homeCoverHeight, card.height - 28);
  int coverW = coverH * 3 / 5;
  Rect cover{card.x + (card.width - coverW) / 2, card.y + 14, coverW, coverH};
  bool drewCover = false;
  if (!book.coverBmpPath.empty() && !coverRendered) {
    const std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverH);
    HalFile file;
    if (Storage.openFileForRead("HOME", coverPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        coverW = std::clamp(bitmap.getWidth(), coverH / 3, std::min(card.width - 32, coverH));
        cover.x = card.x + (card.width - coverW) / 2;
        cover.width = coverW;
        renderer.drawBitmap(bitmap, cover.x, cover.y, cover.width, cover.height);
        drewCover = true;
      }
      file.close();
    }
    if (drewCover) {
      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;
    }
  }

  if (!coverRendered && !bufferRestored) {
    renderer.fillRect(cover.x, cover.y, cover.width, cover.height, selected);
    if (selected) {
      renderer.drawIconInverted(CoverIcon, cover.x + (cover.width - 32) / 2, cover.y + 28, 32, 32);
    } else {
      renderer.drawIcon(CoverIcon, cover.x + (cover.width - 32) / 2, cover.y + 28, 32, 32);
    }
    drawBookmarkRibbon(renderer, cover, !selected);
  }
  renderer.drawRect(cover.x, cover.y, cover.width, cover.height, selected ? kHeavyBorder : kBorder, true);
  renderer.drawRect(cover.x - 5, cover.y - 5, cover.width + 10, cover.height + 10, 1, true);
}

Rect NeobrutalistTheme::drawPopup(const GfxRenderer& renderer, const char* message) const {
  const auto& metrics = NeobrutalistMetrics::values;
  const int y = static_cast<int>(renderer.getScreenHeight() * metrics.popupTopOffsetRatio);
  const int textW = renderer.getTextWidth(kTitleFont, message, EpdFontFamily::BOLD);
  const int textH = renderer.getLineHeight(kTitleFont);
  const int w = textW + metrics.popupMarginX * 2;
  const int h = textH + metrics.popupMarginY * 2;
  Rect r{(renderer.getScreenWidth() - w) / 2, y, w, h};
  drawPanel(renderer, r, false, false, true);
  renderer.drawText(kTitleFont, r.x + (r.width - textW) / 2, r.y + metrics.popupMarginY - 2, message, true,
                    EpdFontFamily::BOLD);
  renderer.displayBuffer();
  return r;
}

void NeobrutalistTheme::fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const {
  const int barW = std::max(0, layout.width - NeobrutalistMetrics::values.popupMarginX * 2);
  const int barH = NeobrutalistMetrics::values.popupProgressBarHeight;
  const int x = layout.x + (layout.width - barW) / 2;
  const int y = layout.y + layout.height - NeobrutalistMetrics::values.popupMarginY / 2 - barH / 2;
  renderer.drawRect(x, y, barW, barH, 1, true);
  renderer.fillRect(x, y, barW * std::clamp(progress, 0, 100) / 100, barH, true);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void NeobrutalistTheme::drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth,
                                      const bool cursorMode, int contentStartX, int contentWidth) const {
  const auto& metrics = NeobrutalistMetrics::values;
  const int lineHeight = renderer.getLineHeight(kTitleFont);
  const int h = lineHeight + 12;
  if (contentWidth > 0) {
    Rect field{rect.x + contentStartX - 4, rect.y + rect.height + metrics.verticalSpacing, contentWidth + 8, h};
    drawPanel(renderer, field, false, false, false);
    if (cursorMode) renderer.drawLine(field.x + 4, field.y + h - 5, field.x + field.width - 5, field.y + h - 5, 3);
    return;
  }
  const int w = textWidth + metrics.textFieldHorizontalPadding * 2;
  Rect field{rect.x + (rect.width - w) / 2, rect.y + rect.height + metrics.verticalSpacing, w, h};
  drawPanel(renderer, field, false, false, false);
  if (cursorMode) renderer.drawLine(field.x + 4, field.y + h - 5, field.x + field.width - 5, field.y + h - 5, 3);
}

void NeobrutalistTheme::drawKeyboardKey(const GfxRenderer& renderer, Rect rect, const char* label,
                                        const bool isSelected, const char* secondaryLabel,
                                        const KeyboardKeyType keyType, const bool inactiveSelection) const {
  const bool disabled = keyType == KeyboardKeyType::Disabled;
  const bool selected = isSelected && !inactiveSelection && !disabled;
  drawPanel(renderer, rect, selected, disabled || inactiveSelection, isSelected);

  if (keyType == KeyboardKeyType::Space) {
    const int y = rect.y + rect.height / 2 + 4;
    renderer.drawLine(rect.x + rect.width / 3, y, rect.x + rect.width * 2 / 3, y, 3, !selected);
    return;
  }
  if (keyType == KeyboardKeyType::Del) {
    const int cx = rect.x + rect.width / 2;
    const int cy = rect.y + rect.height / 2;
    renderer.drawLine(cx - 8, cy, cx + 8, cy, 3, !selected);
    renderer.drawLine(cx - 8, cy, cx - 3, cy - 5, 3, !selected);
    renderer.drawLine(cx - 8, cy, cx - 3, cy + 5, 3, !selected);
    return;
  }
  if (label && label[0] != '\0') {
    const bool hasSecondary = secondaryLabel && secondaryLabel[0] != '\0';
    const int font = hasSecondary ? kBodyFont : kTitleFont;
    const int w = renderer.getTextWidth(font, label, EpdFontFamily::BOLD);
    const int y = hasSecondary ? rect.y + rect.height - renderer.getLineHeight(font) - 3 : centeredY(renderer, font, rect);
    renderer.drawText(font, rect.x + (rect.width - w) / 2, y, label, !selected, EpdFontFamily::BOLD);
  }
  if (secondaryLabel && secondaryLabel[0] != '\0') {
    const int w = renderer.getTextWidth(kMetaFont, secondaryLabel);
    renderer.drawText(kMetaFont, rect.x + rect.width - w - 3, rect.y + 2, secondaryLabel, !selected);
  }
}
