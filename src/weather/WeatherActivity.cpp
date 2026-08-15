#include "WeatherActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <SdCardFontSystem.h>
#include <WiFi.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <time.h>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "activities/RenderLock.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "CrossPointSettings.h"
#include "fontIds.h"
#include "weather/WeatherRenderUtils.h"
#include "weather/WeatherSettingsActivity.h"
#include "weather/WxSettings.h"
#include "weather/WeatherApi.h"
#include "images/Logo120.h"
#include "weather/WeatherIcons.h"
#include "util/BootPartition.h"

// Minimum gap between WiFi radio power-cycles during framebuffer recovery.
// Prevents a reconnect storm when the realloc stays blocked for a long time.
static constexpr unsigned long kRadioCycleIntervalMs = 30000;

// How long to suppress auto-refresh after a failed or cancelled fetch. The
// blocks that triggered the fetch stay "due" on failure, so without this gate
// the auto-refresh would re-launch the fetch the instant the error screen is
// dismissed and trap the user in a retry loop (they could never reach the
// menu to switch providers). 10 minutes keeps the retry prompt reasonable
// while giving the user time to fix the cause (e.g. set an API key).
static constexpr unsigned long kFailedFetchCooldownMs = 10UL * 60UL * 1000UL;

// ---------------------------------------------------------------------------
// Rendering helpers (1-bit, CrossInk fonts)
// ---------------------------------------------------------------------------

static void drawClippedText(GfxRenderer& r, int font, int x, int y, const char* text, int maxW,
                            bool black = true, EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  if (maxW > 0 && r.getTextWidth(font, text, style) > maxW) {
    std::string t = r.truncatedText(font, text, maxW, style);
    r.drawText(font, x, y, t.c_str(), black, style);
  } else {
    r.drawText(font, x, y, text, black, style);
  }
}

static void drawRightText(GfxRenderer& r, int font, int rightEdge, int y, const char* text,
                          bool black = true, EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  int w = r.getTextWidth(font, text, style);
  r.drawText(font, rightEdge - w, y, text, black, style);
}

// Fonts used by the weather screen for a chosen font family. The 10px/12px
// variants are always compiled in (every firmware build includes them), so the
// weather layout stays identical regardless of the selected weather font.
static int weatherFontSmall(uint8_t family) {
  switch (family) {
    case WX_FONT_LEXENDDECA:
      return LEXENDDECA_10_FONT_ID;
    case WX_FONT_BITTER:
      return BITTER_10_FONT_ID;
    case WX_FONT_CHAREINK:
      return CHAREINK_10_FONT_ID;
    default:
      return SMALL_FONT_ID;
  }
}

static int weatherFontMedium(uint8_t family) {
  switch (family) {
    case WX_FONT_LEXENDDECA:
      return LEXENDDECA_12_FONT_ID;
    case WX_FONT_BITTER:
      return BITTER_12_FONT_ID;
    case WX_FONT_CHAREINK:
      return CHAREINK_12_FONT_ID;
    default:
      return UI_12_FONT_ID;
  }
}

static void renderLocalTime(char* buf, size_t n, time_t epoch, bool use24h) {
  time_t t = epoch + g_tzOffsetSeconds;
  struct tm tm;
  gmtime_r(&t, &tm);
  if (use24h) snprintf(buf, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
  else {
    int h12 = tm.tm_hour % 12; if (h12 == 0) h12 = 12;
    const char* ap = tm.tm_hour < 12 ? "am" : "pm";
    snprintf(buf, n, "%d:%02d%s", h12, tm.tm_min, ap);
  }
}

static const char* dayName(int wd) {
  static const char* names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  if (wd < 0 || wd > 6) return "";
  return names[wd];
}

// Header extras drawn by the weather app itself (the theme clock is disabled):
// the time (top-center), the "Updating..." refresh indicator (top-left, across
// from the battery) and the date (bottom-right, across from the city name on
// the title line). Only the weather screen knows about these weather-specific
// settings, so they are drawn after GUI.drawHeader.
static void drawWeatherHeaderExtras(GfxRenderer& renderer, const Rect& band, bool refreshing) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int titleLh = renderer.getLineHeight(UI_12_FONT_ID);
  const int titleAsc = renderer.getFontAscenderSize(UI_12_FONT_ID);
  int titleBaseline;
  if (metrics.headerBatteryDetached) {
    // fui header pushes the title down so its bottom edge sits `spaceMd` pixels
    // above the underline. The text baseline is `titleAsc` pixels below the
    // text block top, which is at (bottomEdge - titleLh).
    titleBaseline = band.y + band.height - 1 /*underline*/ - 8 /*spaceMd*/ - titleLh + titleAsc;
  } else {
    // Non-detached: title is vertically centred inside the header band.
    titleBaseline = band.y + (band.height - titleLh) / 2 + titleAsc;
  }

  // Top row: "Updating..." (left), time (center), battery % (right).
  const int topRowY = band.y + (metrics.headerBatteryDetached ? 5 : 2);
  if (refreshing) {
    renderer.drawText(SMALL_FONT_ID, band.x + 6, topRowY, tr(STR_WEATHER_REFRESHING));
  }

  // Clock (top-center), formatted for the weather timezone.
  if (g_settings.showClock) {
    char timeBuf[10];
    renderLocalTime(timeBuf, sizeof(timeBuf), time(nullptr), g_settings.use24h);
    const int tw = renderer.getTextWidth(SMALL_FONT_ID, timeBuf);
    renderer.drawText(SMALL_FONT_ID, band.x + (band.width - tw) / 2, topRowY, timeBuf, true);
  }

  // Date (bottom-right, on the title line, same font size/weight as the city name).
  if (g_settings.showDate) {
    char dateBuf[32];
    formatWeatherDate(dateBuf, sizeof(dateBuf), g_settings.dateFormat, time(nullptr));
    const int dw = renderer.getTextWidth(UI_12_FONT_ID, dateBuf, EpdFontFamily::BOLD);
    // Symmetric 6px right margin matches the city's 6px left margin (fui headerSidePadding).
    const int dx = band.x + band.width - 6 - dw;
    const int asc = renderer.getFontAscenderSize(UI_12_FONT_ID);
    renderer.drawText(UI_12_FONT_ID, dx, titleBaseline - asc, dateBuf, true, EpdFontFamily::BOLD);
  }
}

// ---------------------------------------------------------------------------
// 7-segment big digits
// ---------------------------------------------------------------------------

static void segDigit(GfxRenderer& r, int x, int y, int w, int h, int digit, bool black) {
  int t = h / 7; if (t < 2) t = 2;
  bool seg[7] = {false, false, false, false, false, false, false};
  switch (digit) {
    case 0: seg[0]=seg[1]=seg[2]=seg[3]=seg[4]=seg[5]=true; break;
    case 1: seg[1]=seg[2]=true; break;
    case 2: seg[0]=seg[1]=seg[6]=seg[4]=seg[3]=true; break;
    case 3: seg[0]=seg[1]=seg[6]=seg[2]=seg[3]=true; break;
    case 4: seg[5]=seg[6]=seg[1]=seg[2]=true; break;
    case 5: seg[0]=seg[5]=seg[6]=seg[2]=seg[3]=true; break;
    case 6: seg[0]=seg[5]=seg[6]=seg[2]=seg[3]=seg[4]=true; break;
    case 7: seg[0]=seg[1]=seg[2]=true; break;
    case 8: for (int i = 0; i < 7; i++) seg[i] = true; break;
    case 9: seg[0]=seg[1]=seg[2]=seg[3]=seg[5]=seg[6]=true; break;
  }
  int wl = w - 2 * t;
  int midy = y + (h - t) / 2;
  int hw = h - 2 * t;
  if (seg[0]) r.fillRect(x + t, y, wl, t, black);
  if (seg[3]) r.fillRect(x + t, y + h - t, wl, t, black);
  if (seg[6]) r.fillRect(x + t, midy, wl, t, black);
  if (seg[1]) r.fillRect(x + w - t, y + t, t, (hw - t) / 2, black);
  if (seg[2]) r.fillRect(x + w - t, midy, t, (hw - t) / 2 + t, black);
  if (seg[5]) r.fillRect(x, y + t, t, (hw - t) / 2, black);
  if (seg[4]) r.fillRect(x, midy, t, (hw - t) / 2 + t, black);
}

static void drawBigTemp(GfxRenderer& r, int cx, int bottomY, int value, int digitHeight) {
  int h = digitHeight;
  int w = h * 55 / 100;
  int gap = h / 8;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", value);
  int n = (int)strlen(buf);
  if (n == 0) return;

  const int unitFont = weatherFontMedium(g_settings.fontFamily);
  char unit[8];
  snprintf(unit, sizeof(unit), "\xc2\xb0%s", tempUnit(g_settings));
  int unitW = r.getTextWidth(unitFont, unit);
  int totalW = n * w + (n - 1) * gap + gap + unitW + 6;
  int x = cx - totalW / 2;
  int y = bottomY - h;

  for (int i = 0; i < n; i++) {
    char c = buf[i];
    if (c == '-') {
      r.fillRect(x, y + (h - 3) / 2, w, 3, true);
    } else if (c >= '0' && c <= '9') {
      segDigit(r, x, y, w, h, c - '0', true);
    }
    x += w + gap;
  }

  x += gap;
  int uh = r.getFontAscenderSize(unitFont);
  r.drawText(unitFont, x, bottomY - uh, unit, true, EpdFontFamily::BOLD);
}

// ---------------------------------------------------------------------------
// Stat cells
// ---------------------------------------------------------------------------

static const char* uvCategory(float uv) {
  if (uv < 3) return "Low";
  if (uv < 6) return "Moderate";
  if (uv < 8) return "High";
  if (uv < 11) return "V. High";
  return "Extreme";
}

static const char* aqiCategory(float aqi) {
  if (aqi <= 20) return "Good";
  if (aqi <= 40) return "Fair";
  if (aqi <= 60) return "Moderate";
  if (aqi <= 80) return "Poor";
  if (aqi <= 100) return "V. Poor";
  return "Extreme";
}

static const char* compassDir(float deg) {
  static const char* dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  int idx = ((int)((deg + 22.5f) / 45.0f)) % 8;
  return dirs[idx];
}

// Each text line owns its own band: a baseline must be >= previous baseline +
// previous descender + lineGap + next ascender, so glyph tops never reach the
// previous line's descenders.
static void drawStatCell(GfxRenderer& r, int x, int y, int w, int h, const char* label,
                         const char* value, const char* sub, int sAsc, int sDesc, int mAsc,
                         int mDesc, bool showSub) {
  const bool hasSub = showSub && sub && sub[0];
  const int smallFont = weatherFontSmall(g_settings.fontFamily);
  const int mediumFont = weatherFontMedium(g_settings.fontFamily);
  const int lineGap = 4;
  const int pad = 4;  // min gap between the text group and the box border

  // Vertical span of the label+value(+sub) group when stacked with real gaps.
  int contentH = sAsc + sDesc + lineGap + mAsc + mDesc;
  if (hasSub) contentH += lineGap + sAsc + sDesc;
  const int availH = h - 2 * pad;
  if (contentH > availH) contentH = availH;

  const int labelBase = y + pad + (availH - contentH) / 2 + sAsc;  // label baseline
  drawClippedText(r, smallFont, x + 8, labelBase - sAsc, label, w - 16);
  const int valueBase = labelBase + sDesc + mAsc + lineGap;        // value baseline
  drawClippedText(r, mediumFont, x + 8, valueBase - mAsc, value, w - 16, true, EpdFontFamily::BOLD);
  if (hasSub) {
    const int subBase = valueBase + mDesc + sAsc + lineGap;        // sub baseline
    drawClippedText(r, smallFont, x + 8, subBase - sAsc, sub, w - 16);
  }
}

// Metric-aware height of the forecast strip (measured from its top edge).
// Day label, icon, hi and lo each occupy their own band; the hi temp sits
// above the lo temp. `compact` shrinks the icon and gaps for landscape.
static int weatherForecastHeight(int sAsc, int sDesc, bool compact) {
  const int bandGap = compact ? 5 : 6;
  const int iconHalf = compact ? 15 : 17;  // 30px vs 34px icon box
  const int dayBaseline = 8 + 4 + sAsc;
  const int iconCy = dayBaseline + sDesc + bandGap + iconHalf;
  const int hiBaseline = iconCy + iconHalf + bandGap + sAsc;
  const int loBaseline = hiBaseline + sDesc + bandGap + sAsc;
  return loBaseline + sDesc + 4;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void WeatherActivity::onEnter() {
  Activity::onEnter();
  if (!settingsLoaded) {
    settingsLoad();
    settingsLoaded = true;
  }
  wifiWasConnectedOnEnter = (WiFi.status() == WL_CONNECTED);

  autoFetchTried = false;
  fetchStarted = false;
  autoRefreshBlocked = false;
  autoDueMask = 0;
  autoRefreshBlockedUntilMs = 0;  // fresh entry may auto-refresh immediately
  state = State::SHOWING;
  menuSelection = 0;
  requestUpdate();
}

void WeatherActivity::onExit() {
  Activity::onExit();
  if (wifiTornDownOnExit && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_OFF);
  }
}

bool WeatherActivity::preventAutoSleep() {
  return true;  // weather app keeps the device awake while it is open
}

void WeatherActivity::launchWifiSelection() {
  wifiTornDownOnExit = true;
  startActivityForResult(std::make_unique<WifiSelectionActivity>(
                             renderer, mappedInput, true, false, toWeatherRendererOrientation(g_settings.orientation),
                             true),
                         [this](const ActivityResult& result) {
                           onWifiSelectionComplete(!result.isCancelled);
                         });
}

void WeatherActivity::onWifiSelectionComplete(bool connected) {
  if (!connected) {
    autoRefreshBlocked = true;  // don't nag with repeated WiFi prompts
    requestUpdate();
    return;
  }
  autoRefreshBlocked = false;
  uint32_t mask = autoDueMask ? autoDueMask : allShownBlockMask();
  autoDueMask = 0;
  startFetch(g_weather.valid ? mask : allShownBlockMask());
}

void WeatherActivity::startFetch(uint32_t mask) {
  pendingMask = mask;
  fetchStarted = false;
  state = State::FETCHING;
  requestUpdate();
}

void WeatherActivity::doFetch() {
  // Bound the whole fetch and let the Back button abort it even though loop()
  // is blocked. HttpDownloader polls this between requests and during the body
  // read; each request is also capped at 8s in WeatherApi. 20 s fits 5 blocks
  // (2 main + 3 forecast) worst-case and keeps the Loading screen responsive.
  bool cancelRequested = false;
  const uint32_t deadline = millis() + 15000;
  const auto shouldCancel = [this, &cancelRequested, deadline]() {
    if (millis() > deadline) {
      cancelRequested = true;
      return true;
    }
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      cancelRequested = true;
      return true;
    }
    return false;
  };

  // The Loading screen was flushed by requestUpdateAndWait() before doFetch().
  // The framebuffer (~52 KB) is idle while the panel holds that image — lend
  // it back to the heap so the TLS handshake has contiguous memory to work
  // with (observed MBEDTLS_ERR_X509_ALLOC_FAILED at ~51 KB max-alloc). Hold
  // the RenderLock for the whole release/fetch/realloc window: a previously
  // queued update notification would otherwise let the render task paint into
  // the freed framebuffer the instant we free it (Load-access-fault panic).
  {
    RenderLock lock(*this);
    // Free SD card font registry + loaded fonts before the fetch. The font
    // catalog stays in heap across activities; without releasing it the ~52 KB
    // framebuffer often cannot be reallocated after the HTTP buffers fragment
    // the heap (observed when .fonts/ is present on the SD card).
    sdFontSystem.releaseForNetwork(renderer);
    renderer.releaseFramebuffers();
    bool ok = weatherFetch(pendingMask, shouldCancel);
    if (!reallocFramebufferWithRecovery(true)) {
      LOG_ERR("MEM", "framebuffer realloc FAILED after fetch (free=%u maxAlloc=%u); screen deferred",
              ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      framebufferMissing = true;
      framebufferMissingSince = millis();
    }
    if (cancelRequested) {
      state = State::SHOWING;
    } else if (ok) {
      g_weather.updatedAt = time(nullptr);
      state = State::SHOWING;
      markAllFetched();
    } else {
      state = State::FETCH_FAILED;
    }
    // A failed/cancelled fetch leaves the due blocks un-fetched, which would
    // otherwise make the auto-refresh re-launch it immediately and trap the
    // user on the error screen. Gate auto-refresh for a cooldown window.
    if (!ok || cancelRequested) {
      autoRefreshBlockedUntilMs = millis() + kFailedFetchCooldownMs;
    }
  }
  // Always request a render so the UI shows the fetch result (or error) even
  // if framebuffer recovery is deferred to the next loop() iteration.
  requestUpdate();
}

bool WeatherActivity::reallocFramebufferWithRecovery(bool allowRadioCycle) {
  // The fetch's TLS/HTTP allocations can leave the heap fragmented so that no
  // single block as large as the framebuffer is available right after the
  // fetch. Give it a short grace period, then power-cycle the WiFi radio to
  // force lwIP to release its connection buffers (which pinned ~2 KB inside
  // the freed framebuffer block, leaving maxAlloc just under the buffer size).
  const uint32_t deadline = millis() + 2000;
  while (!renderer.reallocFramebuffers() && millis() < deadline) {
    delay(25);
  }
  if (renderer.getFrameBuffer() != nullptr) {
    return true;
  }

  if (!allowRadioCycle || (millis() - lastRadioCycleMs) < kRadioCycleIntervalMs) {
    return false;
  }
  lastRadioCycleMs = millis();

  LOG_ERR("MEM", "realloc still failing; tearing down WiFi radio to release lwIP buffers (free=%u maxAlloc=%u)",
          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  WiFi.disconnect(false);
  delay(30);
  WiFi.mode(WIFI_OFF);
  delay(80);

  // Keep the radio OFF while the freed lwIP/TCP blocks coalesce. Re-enabling
  // STA too early lets the auto-reconnect allocations land inside the very
  // block the framebuffer needs, fragmenting it again (observed maxAlloc drop
  // to ~27 KB). Reallocate first, reconnect only after the buffer is back.
  const uint32_t offDeadline = millis() + 30000;
  while (millis() < offDeadline) {
    if (renderer.reallocFramebuffers()) {
      LOG_ERR("MEM", "framebuffer realloc succeeded while radio off (free=%u maxAlloc=%u)", ESP.getFreeHeap(),
              ESP.getMaxAllocHeap());
      WiFi.mode(WIFI_STA);
      return true;
    }
    delay(200);
  }

  WiFi.mode(WIFI_STA);
  LOG_ERR("MEM", "realloc still failing after radio teardown (free=%u maxAlloc=%u)", ESP.getFreeHeap(),
          ESP.getMaxAllocHeap());
  return false;
}

void WeatherActivity::openSettings() {
  startActivityForResult(std::make_unique<WeatherSettingsActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void WeatherActivity::dispatchMenuAction(int index) {
  switch (index) {
    case 0:  // Update now
      autoRefreshBlocked = false;
      autoRefreshBlockedUntilMs = 0;
      markAllFetched();
      if (g_settings.latitude == 0.0f && g_settings.longitude == 0.0f) {
        state = State::SHOWING;
        requestUpdate();
        return;  // no location; user should open settings
      }
      if (WiFi.status() == WL_CONNECTED) {
        startFetch(allShownBlockMask());
      } else {
        launchWifiSelection();
      }
      break;
    case 1:  // Weather settings
      openSettings();
      break;
    case 2:  // Launch CrossInk
      launchCrossInk();
      break;
    case 3:  // About
      state = State::ABOUT;
      requestUpdate();
      break;
    default:
      break;
  }
}

void WeatherActivity::triggerManualRefresh() {
  autoRefreshBlocked = false;
  autoRefreshBlockedUntilMs = 0;
  markAllFetched();
  if (g_settings.latitude == 0.0f && g_settings.longitude == 0.0f) {
    state = State::SHOWING;
    requestUpdate();
    return;  // no location; user should open settings
  }
  if (WiFi.status() == WL_CONNECTED) {
    startFetch(allShownBlockMask());
  } else {
    launchWifiSelection();
  }
}

void WeatherActivity::launchCrossInk() {
  GUI.drawPopup(renderer, tr(STR_WEATHER_SWITCHING_CROSSINK));
  delay(200);  // let the popup register before the reboot
  bootToCrossInk();
}

void WeatherActivity::loop() {
  // The framebuffer is still missing after a fetch. Retry the realloc (the
  // radio cycle in doFetch may not have been enough); once the buffer is back,
  // repaint. Keep processing input so the user can exit even while the buffer
  // is missing — the render() path skips drawing when it is null.
  if (framebufferMissing) {
    bool recovered = false;
    {
      RenderLock lock(*this);
      recovered = reallocFramebufferWithRecovery(true);
    }
    if (recovered) {
      framebufferMissing = false;
      requestUpdate();
    }
  }

  if (state == State::FETCHING) {
    if (!fetchStarted) {
      fetchStarted = true;
      requestUpdateAndWait();
      doFetch();
    }
    return;
  }

  // The power button is this partition's manual refresh control (sleep is
  // disabled here). Both the short tap and the long press can be assigned an
  // action in settings (refresh, boot to CrossInk, or nothing). While the menu
  // is open the button is intentionally ignored.
  if (mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    const uint8_t action = mappedInput.getHeldTime() >= CrossPointSettings::POWER_BUTTON_LONG_PRESS_MS
                               ? g_settings.powerHoldAction
                               : g_settings.powerTapAction;
    if (state == State::SHOWING || state == State::FETCH_FAILED) {
      switch (action) {
        case WX_POWER_REFRESH:
          triggerManualRefresh();
          break;
        case WX_POWER_CROSSINK:
          launchCrossInk();
          break;
        default:
          break;
      }
    }
    return;
  }

  if (state == State::MENU) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = State::SHOWING;
      requestUpdate();
      return;
    }
    buttonNavigator.onPrevious([this] {
      menuSelection = menuSelection > 0 ? menuSelection - 1 : 3;
      requestUpdate();
    });
    buttonNavigator.onNext([this] {
      menuSelection = menuSelection < 3 ? menuSelection + 1 : 0;
      requestUpdate();
    });
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      dispatchMenuAction(menuSelection);
    }
    return;
  }

  if (state == State::FETCH_FAILED) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      state = State::SHOWING;
      requestUpdate();
    }
    return;
  }

  if (state == State::ABOUT) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      state = State::MENU;
      requestUpdate();
    }
    return;
  }

  // SHOWING
  // Back/X never exits the weather app: there is no activity beneath the
  // weather root (this partition does not run the reader). X only cancels
  // in-flight work (handled in doFetch/shouldCancel and the wifi picker). On
  // the idle home screen the back button is the refresh hint: it performs the
  // same manual refresh as the power-button tap and the menu's "Update now".
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    triggerManualRefresh();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    state = State::MENU;
    menuSelection = 0;
    requestUpdate();
    return;
  }

  // Keep the header clock fresh on its own interval, independent of the weather
  // blocks (BLK_CLOCK is render-only and never part of dueBlockMask), so the
  // displayed time and date never freeze between weather refreshes.
  if (g_settings.showClock && blockDue(BLK_CLOCK)) {
    markBlockFetched(BLK_CLOCK);
    requestUpdate();
  }

  // Auto-refresh: when any shown block's interval has elapsed, fetch ALL
  // shown blocks in one go and refresh the screen once. This prevents the
  // distracting staggered re-fetches that happened when each block had its
  // own independent timer.
  if (!framebufferMissing && !autoRefreshBlocked && millis() >= autoRefreshBlockedUntilMs &&
      g_settings.latitude != 0.0f && g_settings.longitude != 0.0f) {
    uint32_t due = dueBlockMask();
    if (due != 0) {
      autoDueMask = due;
      if (WiFi.status() == WL_CONNECTED) {
        startFetch(allShownBlockMask());
      } else {
        autoRefreshBlocked = true;
        launchWifiSelection();
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void WeatherActivity::renderContent() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight;
  const int chipsH = metrics.buttonHintsHeight;
  int y = contentTop + 4;

  const int bottomLimit = sh - chipsH;

  const bool landscape = sw >= 640;

  // In landscape (LandscapeClockwise) the front-button hint bar occupies the
  // left buttonHintsHeight strip (see UITheme::getScreenSafeArea). Keep all
  // content out of it.
  const int contentLeft = landscape ? chipsH : 0;
  const int usableW = sw - contentLeft;

  // Divider rule between the city/date header and the current-weather block.
  // The lean build's default theme draws no header underline, so the weather
  // draws its own line just above the hero.
  renderer.drawLine(contentLeft + 10, contentTop + 2, sw - 10, contentTop + 2, 1, true);

  // Font metrics for the active family (drawText y is the line top).
  const int sFont = weatherFontSmall(g_settings.fontFamily);
  const int mFont = weatherFontMedium(g_settings.fontFamily);
  const int sAsc = renderer.getFontAscenderSize(sFont);
  const int sDesc = renderer.getFontDescenderSize(sFont);
  const int mAsc = renderer.getFontAscenderSize(mFont);
  const int mDesc = renderer.getFontDescenderSize(mFont);

  // --- Hero: condition icon + big temp + condition text ---
  const bool heroTemp = blockShown(BLK_TEMP);
  const bool heroCond = blockShown(BLK_COND);

  if (heroTemp || heroCond) {
    const int heroTop = y;
    const int iconSize = 84;

    char condBuf[40];
    const char* cond = wmoConditionText(g_weather.weatherCode);
    strncpy(condBuf, cond && cond[0] ? cond : "Unknown", sizeof(condBuf) - 1);
    condBuf[sizeof(condBuf) - 1] = '\0';

    if (landscape && heroTemp) {
      // Horizontal hero: icon left, big temp center, condition right. Each
      // element sits at its own x so no band overlaps.
      const int heroCy = heroTop + 46;
      wxIcon(renderer, contentLeft + 70, heroCy, 54, g_weather.weatherCode, g_weather.isDay, true);
      drawBigTemp(renderer, contentLeft + usableW / 2, heroCy + 38,
                  (int)roundf(tempValue(g_settings, g_weather.tempC)), 72);
      if (heroCond) {
        const int condW = renderer.getTextWidth(mFont, condBuf);
        if (condW > usableW - 20) {
          drawClippedText(renderer, mFont, contentLeft + 10, heroCy + 4 - mAsc, condBuf, usableW - 20, true,
                          EpdFontFamily::BOLD);
        } else {
          drawRightText(renderer, mFont, sw - 10, heroCy + 4 - mAsc, condBuf, true,
                        EpdFontFamily::BOLD);
        }
      }
      y = heroCy + 27 + 8;
    } else if (heroTemp) {
      // Vertical hero: icon left of the temp digits, condition text below the
      // digits. The digits' bottom stroke and the condition top never meet.
      wxIcon(renderer, 70, heroTop + 70, 54, g_weather.weatherCode, g_weather.isDay, true);
      drawBigTemp(renderer, sw / 2, heroTop + 96,
                  (int)roundf(tempValue(g_settings, g_weather.tempC)), 96);
      if (heroCond) {
        const int condTop = heroTop + 96 + 4;
        const int condW = renderer.getTextWidth(mFont, condBuf);
        drawClippedText(renderer, mFont, (sw - condW) / 2, condTop, condBuf, sw - 20, true,
                        EpdFontFamily::BOLD);
        y = condTop + mAsc + mDesc + 8;
      } else {
        y = heroTop + 96 + 8;
      }
    } else {
      // Condition only: large centered icon, condition text below with a gap.
      wxIcon(renderer, sw / 2, heroTop + 60, iconSize, g_weather.weatherCode, g_weather.isDay, true);
      const int condTop = heroTop + 60 + 42 + 8;
      const int condW = renderer.getTextWidth(mFont, condBuf);
      drawClippedText(renderer, mFont, (sw - condW) / 2, condTop, condBuf, sw - 20, true,
                      EpdFontFamily::BOLD);
      y = condTop + mAsc + mDesc + 8;
    }
  }

  // --- Stat grid (dynamic: only enabled blocks, flows into columns) ---
  struct CellSpec { BlockId id; const char* label; };
  static const CellSpec cells[] = {
    {BLK_FEELS, "Feels like"},
    {BLK_HUM, "Humidity"},
    {BLK_WIND, "Wind"},
    {BLK_UV, "UV index"},
    {BLK_AQI, "Air quality"},
    {BLK_PRES, "Pressure"},
    {BLK_SUN, "Sunrise/Sunset"},
    {BLK_EXTRA, nullptr},  // label comes from the selected metric
  };
  const int kCells = sizeof(cells) / sizeof(cells[0]);

  const int cols = landscape ? 4 : 2;
  const int cellGap = 8;
  const int margin = 10;
  int cellW = (usableW - margin * 2 - (cols - 1) * cellGap) / cols;

  int shownCount = 0;
  for (int i = 0; i < kCells; i++) {
    if (blockShown(cells[i].id)) shownCount++;
  }
  int rows = (shownCount + cols - 1) / cols;

  // Stacked group heights with real line gaps (see drawStatCell).
  const int lineGap = 4;
  const int cellPad = 4;  // keep the text group off the box border
  const int minCellH2 = cellPad * 2 + sAsc + sDesc + lineGap + mAsc + mDesc;
  const int minCellH3 = minCellH2 + lineGap + sAsc + sDesc;
  const int minCellH4 = minCellH3 + lineGap + mAsc + mDesc;  // label + 2 values + sub

  const int fcH = (blockShown(BLK_FC) ? weatherForecastHeight(sAsc, sDesc, landscape) : 0);
  const int avail = bottomLimit - y - fcH - cellGap;

  int cellH = 80;
  if (rows > 0) {
    const int availH = avail - cellGap * (rows - 1);
    const int target = availH / rows;
    cellH = target < minCellH2 ? minCellH2 : target;
    if (cellH > 100) cellH = 100;
  }
  // In landscape, sunrise/sunset need enough vertical space to stack
  // (label + 2 value lines = 3-line cell, same as minCellH3).
  if (landscape && blockShown(BLK_SUN) && cellH < minCellH3) cellH = minCellH3;
  const bool showSub = cellH >= minCellH3;

  int idx = 0;
  for (int i = 0; i < kCells; i++) {
    if (!blockShown(cells[i].id)) continue;
    const int col = idx % cols;
    const int row = idx / cols;
    const int cx = contentLeft + margin + col * (cellW + cellGap);
    const int cy = y + row * (cellH + cellGap);

    char value[40], sub[32];
    value[0] = '\0'; sub[0] = '\0';
    const char* label = cells[i].label;
    if (!label) label = extraMetricName(g_settings.extData);

    bool useCustomDraw = false;

    switch (cells[i].id) {
      case BLK_FEELS:
        formatTemp(value, sizeof(value), g_weather.feelsLikeC, !g_settings.useCelsius, true);
        break;
      case BLK_HUM:
        snprintf(value, sizeof(value), "%d%%", (int)roundf(g_weather.humidityPct));
        break;
      case BLK_WIND:
        snprintf(value, sizeof(value), "%d %s",
                 (int)roundf(windSpeedValue(g_settings, g_weather.windKph)), windUnit(g_settings));
        break;
      case BLK_UV:
        snprintf(value, sizeof(value), "%.1f", g_weather.uvIndex);
        break;
      case BLK_AQI:
        snprintf(value, sizeof(value), "%d", (int)roundf(g_weather.aqi));
        break;
      case BLK_PRES:
        snprintf(value, sizeof(value), "%d hPa", (int)roundf(g_weather.pressureHpa));
        break;
      case BLK_SUN: {
        char sr[10], ss[10];
        renderLocalTime(sr, sizeof(sr), g_weather.sunrise, g_settings.use24h);
        renderLocalTime(ss, sizeof(ss), g_weather.sunset, g_settings.use24h);
        if (landscape) {
          // Stacked layout in landscape: sunrise on its own line, sunset below it.
          // No daylight duration — keeps the cell shorter so the forecast isn't pushed down.
          const int sFont = weatherFontSmall(g_settings.fontFamily);
          const int mFont = weatherFontMedium(g_settings.fontFamily);
          const int contentH = sAsc + sDesc + lineGap + mAsc + mDesc + lineGap + mAsc + mDesc;
          int base = cy + cellPad + (cellH - 2 * cellPad - contentH) / 2 + sAsc;
          drawClippedText(renderer, sFont, cx + 8, base - sAsc, label, cellW - 16);
          base += sDesc + lineGap + mAsc;
          drawClippedText(renderer, mFont, cx + 8, base - mAsc, sr, cellW - 16, true, EpdFontFamily::BOLD);
          base += mDesc + lineGap + mAsc;
          drawClippedText(renderer, mFont, cx + 8, base - mAsc, ss, cellW - 16, true, EpdFontFamily::BOLD);
          useCustomDraw = true;
        } else {
          snprintf(value, sizeof(value), "%s  %s", sr, ss);
        }
        break;
      }
      case BLK_EXTRA:
        switch (g_settings.extData) {
          case EXTRA_CLOUD:
            snprintf(value, sizeof(value), "%d%%", (int)roundf(g_weather.cloudPct));
            break;
          case EXTRA_VISIBILITY:
            snprintf(value, sizeof(value), "%.1f km", g_weather.visibilityKm);
            break;
          case EXTRA_GUST:
            snprintf(value, sizeof(value), "%d %s",
                     (int)roundf(windSpeedValue(g_settings, g_weather.gustKph)),
                     windUnit(g_settings));
            break;
          case EXTRA_PRECIP:
            snprintf(value, sizeof(value), "%.1f mm", g_weather.precipMm);
            break;
          case EXTRA_DEWPOINT:
          default:
            formatTemp(value, sizeof(value), g_weather.dewPointC, !g_settings.useCelsius, true);
            break;
        }
        break;
      default: break;
    }

    if (!useCustomDraw) {
      drawStatCell(renderer, cx, cy, cellW, cellH, label, value, sub, sAsc, sDesc, mAsc,
                   mDesc, showSub);
    }
    idx++;
  }
  y += rows * (cellH + cellGap);

  // --- Forecast strip (day label, icon, hi and lo each in separate bands) ---
  if (blockShown(BLK_FC)) {
    // In landscape the sunrise/sunset cell is taller; tighten the gap so the
    // dividing line sits right below the last cell instead of floating far under.
    const int fcTop = landscape ? y - cellGap + 4 : y;
    const bool compact = landscape;
    const int bandGap = compact ? 5 : 6;
    const int iconHalf = compact ? 15 : 17;  // 30px vs 34px icon box
    const int iconSize = iconHalf * 2;
    renderer.drawLine(contentLeft + 10, fcTop + 4, sw - 10, fcTop + 4, 1, true);
    const int dayBaseline = fcTop + 8 + 4 + sAsc;
    const int iconCy = dayBaseline + sDesc + bandGap + iconHalf;
    const int hiBaseline = iconCy + iconHalf + bandGap + sAsc;
    const int loBaseline = hiBaseline + sDesc + bandGap + sAsc;
    const int n = 5;
    const int cw = usableW / n;
    time_t base = time(nullptr) + g_tzOffsetSeconds;
    struct tm tm0;
    gmtime_r(&base, &tm0);

    for (int i = 0; i < n; i++) {
      int x0 = contentLeft + i * cw;
      if (!g_weather.fcValid[i]) continue;
      char day[8];
      if (i == 0) snprintf(day, sizeof(day), "Today");
      else {
        time_t d = base + (time_t)i * 86400;
        struct tm tmd;
        gmtime_r(&d, &tmd);
        snprintf(day, sizeof(day), "%s", dayName(tmd.tm_wday));
      }
      int dw = renderer.getTextWidth(sFont, day);
      drawClippedText(renderer, sFont, x0 + (cw - dw) / 2, dayBaseline - sAsc, day, cw - 4);

      wxIconSmall(renderer, x0 + cw / 2, iconCy, iconSize, g_weather.fcCode[i], true);

      char hi[12], lo[12];
      formatTemp(hi, sizeof(hi), g_weather.fcMaxC[i], !g_settings.useCelsius, true);
      formatTemp(lo, sizeof(lo), g_weather.fcMinC[i], !g_settings.useCelsius, true);
      const int hw = renderer.getTextWidth(sFont, hi);
      const int lw = renderer.getTextWidth(sFont, lo);
      drawClippedText(renderer, sFont, x0 + (cw - hw) / 2, hiBaseline - sAsc, hi, cw - 4, true,
                      EpdFontFamily::BOLD);
      drawClippedText(renderer, sFont, x0 + (cw - lw) / 2, loBaseline - sAsc, lo, cw - 4);
    }
    y = fcTop + weatherForecastHeight(sAsc, sDesc, compact);
  }
}

void WeatherActivity::renderScene() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight;
  const int contentBottom = sh - metrics.buttonHintsHeight;

  // In landscape the front-button hint bar occupies the left buttonHintsHeight
  // strip (see UITheme::getScreenSafeArea). Keep the header title clear of it.
  const bool landscape = sw >= 640;
  const int contentLeft = landscape ? metrics.buttonHintsHeight : 0;
  const int usableW = sw - contentLeft;

  const char* title = g_settings.locationName[0] ? g_settings.locationName : tr(STR_WEATHER);
  // The theme clock is suppressed (drawClock=false): the weather app renders its
  // own time plus the date and the "Updating..." indicator, see
  // drawWeatherHeaderExtras. Title stays left-aligned (city name top-left).
  GUI.drawHeader(renderer, Rect{contentLeft, metrics.topPadding, usableW, metrics.headerHeight}, title, nullptr,
                 false, false);
  drawWeatherHeaderExtras(renderer, Rect{contentLeft, metrics.topPadding, usableW, metrics.headerHeight},
                          (state == State::FETCHING && g_weather.valid));

  const int midY = (contentTop + contentBottom) / 2;

  switch (state) {
    case State::FETCHING:
      // Refresh in the background of the last good frame instead of a blank
      // Loading screen (only when we have something to show).
      if (g_weather.valid) {
        renderContent();
      } else {
        renderer.drawCenteredText(UI_12_FONT_ID, midY, tr(STR_LOADING));
      }
      break;
    case State::FETCH_FAILED: {
      const char* reason = weatherLastError();
      if (reason && reason[0]) {
        // Actionable error (e.g. missing API key) — no serial output needed.
        renderer.drawCenteredText(UI_12_FONT_ID, midY - 30, tr(STR_WEATHER_UPDATE_FAILED), true,
                                  EpdFontFamily::BOLD);
        renderer.drawCenteredText(UI_10_FONT_ID, midY + 2, reason);
        renderer.drawCenteredText(UI_10_FONT_ID, midY + 20, tr(STR_CHECK_SERIAL_OUTPUT), true);
      } else {
        renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_WEATHER_UPDATE_FAILED), true,
                                  EpdFontFamily::BOLD);
        renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, tr(STR_CHECK_SERIAL_OUTPUT));
      }
      break;
    }
    case State::ABOUT: {
      // The InkStorm logo bitmap is a portrait glyph and drawImage does not
      // rotate its bits, so in landscape it would appear rotated 90°. Render
      // the whole About scene in the portrait frame — the renderer rotates the
      // frame for landscape, leaving the logo and text upright.
      const GfxRenderer::Orientation savedOrientation = renderer.getOrientation();
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      const int pw = renderer.getScreenWidth();
      const int ph = renderer.getScreenHeight();
      const int logoSize = 120;
      const int logoY = ph / 2 - 110;
      renderer.drawImage(Logo120, (pw - logoSize) / 2, logoY, logoSize, logoSize);
      int textY = logoY + logoSize + 22;
      renderer.drawCenteredText(UI_12_FONT_ID, textY, "InkStorm", true, EpdFontFamily::BOLD);
      textY += 36;
      renderer.drawCenteredText(UI_10_FONT_ID, textY, "Version 1.0.0");
      textY += 32;
      renderer.drawCenteredText(UI_10_FONT_ID, textY, "By SkyWalker541");
      textY += 30;
      char powerInfo[64];
      snprintf(powerInfo, sizeof(powerInfo), "Power tap=%s hold=%s", weatherPowerActionName(g_settings.powerTapAction),
               weatherPowerActionName(g_settings.powerHoldAction));
      renderer.drawCenteredText(UI_10_FONT_ID, textY, powerInfo);
      renderer.setOrientation(savedOrientation);
      break;
    }
    case State::MENU: {
      Rect listRect{contentLeft, contentTop, usableW, contentBottom - contentTop};
      const auto rowTitle = [this](int index) -> std::string {
        switch (index) {
          case 0: return tr(STR_WEATHER_UPDATE);
          case 1: return tr(STR_WEATHER_SETTINGS);
          case 2: return "Launch CrossInk";
          default: return tr(STR_ABOUT);
        }
      };
      GUI.drawList(renderer, listRect, 4, menuSelection, rowTitle);
      break;
    }
    case State::SHOWING:
    default:
      if (!g_weather.valid) {
        renderer.drawCenteredText(UI_12_FONT_ID, midY - 30, tr(STR_WEATHER_NO_DATA), true,
                                  EpdFontFamily::BOLD);
        if (g_settings.latitude == 0.0f && g_settings.longitude == 0.0f) {
          renderer.drawCenteredText(UI_10_FONT_ID, midY, tr(STR_WEATHER_NO_LOCATION));
        }
        renderer.drawCenteredText(UI_10_FONT_ID, midY + 16, tr(STR_WEATHER_UPDATE));
      } else {
        renderContent();
      }
      break;
  }

  switch (state) {
    case State::SHOWING: {
      drawWeatherSymbolHints(
          renderer, mappedInput.mapSymbols(ButtonHintSymbol::Refresh, ButtonHintSymbol::Menu, ButtonHintSymbol::None,
                                           ButtonHintSymbol::None));
      break;
    }
    case State::MENU: {
      drawWeatherSymbolHints(
          renderer, mappedInput.mapSymbols(ButtonHintSymbol::Back, ButtonHintSymbol::Select, ButtonHintSymbol::Up,
                                           ButtonHintSymbol::Down));
      break;
    }
    case State::FETCH_FAILED:
    case State::ABOUT: {
      drawWeatherSymbolHints(
          renderer, mappedInput.mapSymbols(ButtonHintSymbol::Back, ButtonHintSymbol::None, ButtonHintSymbol::None,
                                           ButtonHintSymbol::None));
      break;
    }
    case State::FETCHING:
      break;
  }
}

void WeatherActivity::render(RenderLock&&) {
  // Weather has its own orientation setting, independent of the global reader
  // orientation. Force it for this frame and restore the renderer's previous
  // value afterwards (the renderer is shared with other activities).
  const GfxRenderer::Orientation savedOrientation = renderer.getOrientation();
  renderer.setOrientation(toWeatherRendererOrientation(g_settings.orientation));

  // Framebuffer may still be missing after a fetch (heap fragmentation after
  // the reader). Skip drawing to avoid null-pointer crashes; loop() will retry
  // recovery and request a new render once the buffer is back.
  if (!renderer.hasFrameBuffer()) {
    renderer.setOrientation(savedOrientation);
    return;
  }

  renderer.clearScreen();
  renderScene();

  // Only the weather content (SHOWING) uses the selected weather font family.
  // MENU / ABOUT / FETCH_FAILED render with the plain UI fonts, so skip the
  // multi-pass grayscale render there: navigating the menu (up/down) then uses
  // a single fast refresh instead of a full flashing refresh on every move,
  // matching the CrossInk menu behavior.
  const bool grayscale = (state == State::SHOWING || (state == State::FETCHING && g_weather.valid)) &&
                         g_settings.fontFamily != WX_FONT_UI;
  if (grayscale) {
    // 2-bit family fonts carry 4-level anti-aliasing. Push a clean BW base,
    // then re-render once per grayscale plane so the gray glyph pixels survive.
    renderer.displayGrayscaleBase(HalDisplay::FULL_REFRESH);

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderScene();
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderScene();
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  } else {
    renderer.displayBuffer();
  }

  renderer.setOrientation(savedOrientation);
}
