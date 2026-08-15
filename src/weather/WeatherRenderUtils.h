#pragma once

#include <GfxRenderer.h>
#include <HalGPIO.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/WeatherHintIcons.h"
#include "weather/WxSettings.h"

// Shared rendering helpers for the weather app.

// Portrait-only: the weather app no longer supports landscape mode.
// The device may physically be in landscape, but the weather content
// and hint bars always render in portrait orientation.
inline GfxRenderer::Orientation toWeatherRendererOrientation(uint8_t /*orientation*/) {
  return GfxRenderer::Orientation::Portrait;
}

// Draws the four front-button hints. The theme's drawButtonHints now handles
// landscape itself: in portrait it draws the standard bottom bar, in landscape
// it draws the same buttons along the left/right gutter with the labels
// rotated 90° so they read top-to-bottom.
inline void drawWeatherButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                   const char* btn4) {
  GUI.drawButtonHints(renderer, btn1, btn2, btn3, btn4);
}

// Returns the 90° CCW pre-rotated icon for the portrait-mode hint boxes.
// GfxRenderer::drawImage only rotates the origin corner — it does NOT rotate
// the bitmap bits (the "// TODO: Rotate bits" in GfxRenderer.cpp). In Portrait
// orientation that transform applies 90° CW to the bitmap, so a 90° CW
// pre-rotation (the previous CW variants) displayed the icons upside down.
// The CCW variants therefore display exactly like the source PNGs.
inline const uint8_t* iconForPortraitFrame(ButtonHintSymbol symbol) {
  switch (symbol) {
    case ButtonHintSymbol::Refresh: return WxIconRefreshCCW;
    case ButtonHintSymbol::Menu: return WxIconMenuCCW;
    case ButtonHintSymbol::Close: return WxIconCloseCCW;
    case ButtonHintSymbol::Select: return WxIconSelectCCW;
    case ButtonHintSymbol::Up: return WxIconUpCCW;
    case ButtonHintSymbol::Down: return WxIconDownCCW;
    case ButtonHintSymbol::Back: return WxIconBackCCW;
    default: return nullptr;
  }
}

// Symbol variant of drawWeatherButtonHints: each hint box shows a centered
// 32×32 PNG icon instead of a theme-drawn glyph. The boxes are drawn in the
// Portrait frame so they always sit at the bottom of the physical device,
// regardless of the device's screen orientation. Icons are pre-rotated 90° CCW
// so they appear upright after the Portrait coordinate transform.
inline void drawWeatherSymbolHints(GfxRenderer& renderer, const MappedInputManager::Symbols& symbols) {
  const GfxRenderer::Orientation origOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 80;
  constexpr int buttonHeight = 40;
  constexpr int buttonY = 40;
  constexpr int smallButtonHeight = 15;
  constexpr int cornerRadius = 6;
  constexpr int x4ButtonPositions[] = {58, 146, 254, 342};
  constexpr int x3ButtonPositions[] = {65, 157, 291, 383};
  const int* buttonPositions = gpio.deviceIsX3() ? x3ButtonPositions : x4ButtonPositions;
  const ButtonHintSymbol hintSymbols[] = {symbols.btn1, symbols.btn2, symbols.btn3, symbols.btn4};

  for (int i = 0; i < 4; i++) {
    const int x = buttonPositions[i];
    if (hintSymbols[i] != ButtonHintSymbol::None) {
      renderer.fillRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, cornerRadius, Color::White);
      renderer.drawRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, 1, cornerRadius, true, true, false,
                               false, true);
    } else {
      const int smallButtonY = pageHeight - smallButtonHeight;
      renderer.fillRoundedRect(x, smallButtonY, buttonWidth, smallButtonHeight, cornerRadius, Color::White);
      renderer.drawRoundedRect(x, smallButtonY, buttonWidth, smallButtonHeight, 1, cornerRadius, true, true, false,
                               false, true);
    }
  }

  for (int i = 0; i < 4; i++) {
    const uint8_t* icon = iconForPortraitFrame(hintSymbols[i]);
    if (icon == nullptr) continue;
    const int boxX = buttonPositions[i];
    const int boxY = pageHeight - buttonY;
    const int iconX = boxX + (buttonWidth - kWeatherHintIconSize) / 2;
    const int iconY = boxY + (buttonHeight - kWeatherHintIconSize) / 2;
    renderer.drawImage(icon, iconX, iconY, kWeatherHintIconSize, kWeatherHintIconSize);
  }
  renderer.setOrientation(origOrientation);
}
