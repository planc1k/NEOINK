#pragma once

class GfxRenderer;
struct ThemeMetrics;

int headerDateReservedWidth(const GfxRenderer& renderer);
int headerDateLineBottomY(const GfxRenderer& renderer, const ThemeMetrics& metrics, int headerHeight = -1);
void drawHeaderDate(const GfxRenderer& renderer, int pageWidth, const ThemeMetrics& metrics, int headerHeight = -1);
