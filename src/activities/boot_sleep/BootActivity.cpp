#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/CrossInkLogo120.h"
#include "images/Logo120.h"
#include "util/BootPartition.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // Dual-boot boot splash: the active firmware's logo is shown on top, the
  // other firmware's logo below it with a "+" between them.
  // CrossInk (app0): CrossInk logo (top) → "+" → InkStorm logo (bottom)
  // InkStorm (app1): InkStorm logo (top) → "+" → CrossInk logo (bottom)
  constexpr int kLogoSize = 120;
  constexpr int kPlusSize = 48;
  constexpr int kLogoPlusGap = 20;
  constexpr int kTextGap = 25;

  const int totalContentHeight =
      kLogoSize + kLogoPlusGap + kPlusSize + kLogoPlusGap + kLogoSize + kTextGap +
      renderer.getLineHeight(UI_10_FONT_ID);
  const int startY = (pageHeight - totalContentHeight) / 2;

  if (isRunningInkStorm()) {
    // InkStorm logo (top)
    const int inkStormLogoY = startY;
    renderer.drawImage(Logo120, (pageWidth - kLogoSize) / 2, inkStormLogoY, kLogoSize, kLogoSize);

    // Large "+" in middle
    const int plusY = inkStormLogoY + kLogoSize + kLogoPlusGap;
    renderer.drawCenteredText(BITTER_16_FONT_ID,
                              plusY + kPlusSize / 2 - renderer.getLineHeight(BITTER_16_FONT_ID) / 2, "+", true,
                              EpdFontFamily::BOLD);

    // CrossInk logo (bottom)
    const int crossInkLogoY = plusY + kPlusSize + kLogoPlusGap;
    renderer.drawImage(CrossInkLogo120, (pageWidth - kLogoSize) / 2, crossInkLogoY, kLogoSize, kLogoSize);
  } else {
    // CrossInk logo (top)
    const int crossInkLogoY = startY;
    renderer.drawImage(CrossInkLogo120, (pageWidth - kLogoSize) / 2, crossInkLogoY, kLogoSize, kLogoSize);

    // Large "+" in middle
    const int plusY = crossInkLogoY + kLogoSize + kLogoPlusGap;
    renderer.drawCenteredText(BITTER_16_FONT_ID,
                              plusY + kPlusSize / 2 - renderer.getLineHeight(BITTER_16_FONT_ID) / 2, "+", true,
                              EpdFontFamily::BOLD);

    // InkStorm logo (bottom)
    const int inkStormLogoY = plusY + kPlusSize + kLogoPlusGap;
    renderer.drawImage(Logo120, (pageWidth - kLogoSize) / 2, inkStormLogoY, kLogoSize, kLogoSize);
  }

  // Dual-boot text below logos
  const int textY = startY + totalContentHeight - renderer.getLineHeight(UI_10_FONT_ID);
  renderer.drawCenteredText(UI_10_FONT_ID, textY, "CrossInk v1.5.0 / InkStorm v1.0.0", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, textY + renderer.getLineHeight(UI_10_FONT_ID) + 4, "Dual Boot", true,
                            EpdFontFamily::BOLD);

  renderer.displayBuffer();
}
