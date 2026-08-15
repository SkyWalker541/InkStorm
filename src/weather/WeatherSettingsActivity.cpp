#include "WeatherSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "weather/WeatherMultiPopup.h"
#include "weather/WeatherRenderUtils.h"
#include "weather/WxSettings.h"
#include "weather/WeatherApi.h"

// ---------------------------------------------------------------------------
// Block rows: Off -> Manual -> 30s -> 1m -> 5m -> ... -> 24h
// ---------------------------------------------------------------------------

static constexpr int kBlockRows = 10;  // TEMP..FC (EXTRA handled by the combined row below; CLOCK by header)
static constexpr int kListHeader = 8;  // Location, Units, Wind, Time, Date, Font, Power tap, Power hold
static const BlockId kBlockIds[kBlockRows] = {
    BLK_TEMP, BLK_FEELS, BLK_COND, BLK_HUM, BLK_WIND,
    BLK_UV, BLK_AQI, BLK_PRES, BLK_SUN, BLK_FC};

static int blockCycleState(BlockId b) {
  if (!blockShown(b)) return 0;
  return 1 + blockIntervalIndex(b);
}

static const char* blockStateLabel(int state) {
  if (state == 0) return tr(STR_WEATHER_OFF);
  if (state == 1) return tr(STR_WEATHER_MANUAL);
  return blockIntervalLabel(state - 1);
}

static void setBlockCycleState(BlockId b, int state) {
  if (state < 0) state = 0;
  switch (b) {
    case BLK_TEMP:  g_settings.showTemp = state > 0; break;
    case BLK_FEELS: g_settings.showFeelsLike = state > 0; break;
    case BLK_COND:  g_settings.showCondition = state > 0; break;
    case BLK_HUM:   g_settings.showHumidity = state > 0; break;
    case BLK_WIND:  g_settings.showWind = state > 0; break;
    case BLK_UV:    g_settings.showUv = state > 0; break;
    case BLK_AQI:   g_settings.showAirQuality = state > 0; break;
    case BLK_PRES:  g_settings.showPressure = state > 0; break;
    case BLK_SUN:   g_settings.showSun = state > 0; break;
    case BLK_FC:    g_settings.showForecast = state > 0; break;
    case BLK_EXTRA: g_settings.showExtra = state > 0; break;
    default: break;
  }
  if (state > 0) {
    setBlockIntervalIndex(b, static_cast<uint8_t>(state - 1));
  } else {
    setBlockIntervalIndex(b, 0);
  }
  settingsSave();
}

static const char* weatherFontName(uint8_t family) {
  switch (family) {
    case WX_FONT_LEXENDDECA: return "LexendDeca";
    case WX_FONT_BITTER: return "Bitter";
    case WX_FONT_CHAREINK: return "ChareInk";
    default: return tr(STR_WEATHER_FONT_DEFAULT);
  }
}

static const char* dateFormatLabel(uint8_t format) {
  switch (format) {
    case 0: return "MM/DD/YYYY";
    case 1: return "DD/MM/YYYY";
    case 2: return "YYYY-MM-DD";
    case 3: return "Jan 1, 2024";
    case 4: return "January 1, 2024";
    case 5: return "1 Jan 2024";
    case 6: return "1 January 2024";
    default: return "";
  }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void WeatherSettingsActivity::onEnter() {
  Activity::onEnter();
  settingsLoad();
  state = State::LIST;
  selection = 0;
  requestUpdate();
}

void WeatherSettingsActivity::onExit() {
  Activity::onExit();
  if (wifiTornDownOnExit && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_OFF);
  }
}

bool WeatherSettingsActivity::preventAutoSleep() {
  return true;  // weather app (incl. settings) keeps the device awake
}

void WeatherSettingsActivity::startCitySearch() {
  auto handler = [this](const ActivityResult& result) {
    if (!result.isCancelled) {
      const auto& kb = std::get<KeyboardResult>(result.data);
      onKeyboardResult(kb.text);
    }
  };
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput,
                                                                  tr(STR_WEATHER_SEARCH_TITLE), "", 63,
                                                                  InputType::Text, 0,
                                                                  GfxRenderer::Orientation::Portrait),
                          handler);
}

void WeatherSettingsActivity::onKeyboardResult(const std::string& text) {
  if (text.empty()) return;
  if (WiFi.status() == WL_CONNECTED) {
    startSearch(text);
  } else {
    wifiTornDownOnExit = true;
    launchWifiSelectionForSearch(text);
  }
}

void WeatherSettingsActivity::launchWifiSelectionForSearch(const std::string& query) {
  searchQuery = query;
  startActivityForResult(std::make_unique<WifiSelectionActivity>(
                             renderer, mappedInput, true, false, GfxRenderer::Orientation::Portrait,
                             true),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             startSearch(searchQuery);
                           } else {
                             requestUpdate();
                           }
                         });
}

void WeatherSettingsActivity::startSearch(const std::string& query) {
  searchQuery = query;
  searchStarted = false;
  state = State::SEARCHING;
  requestUpdate();
}

void WeatherSettingsActivity::doSearch() {
  geoCount = geoSearch(searchQuery.c_str(), geoResults, MAX_GEO_RESULTS);
  state = State::RESULTS;
  selection = 0;
  requestUpdate();
}

void WeatherSettingsActivity::dispatchListAction(int index) {
  if (index == 0) {
    startCitySearch();
    return;
  }
  if (index == 1) {  // Units (temperature)
    std::vector<std::string> options = {"\xc2\xb0" "C", "\xc2\xb0" "F"};
    optionPopup.show(StrId::STR_WEATHER_UNITS, options, g_settings.useCelsius ? 0 : 1, [this](int i) {
      g_settings.useCelsius = i == 0;
      settingsSave();
      requestUpdate();
    });
    requestUpdate();
    return;
  }
  if (index == 2) {  // Wind speed unit
    std::vector<std::string> options = {"km/h", "mph"};
    optionPopup.show(StrId::STR_WEATHER_WIND_SPEED, options, g_settings.useMph ? 1 : 0, [this](int i) {
      g_settings.useMph = i == 1;
      settingsSave();
      requestUpdate();
    });
    requestUpdate();
    return;
  }
  if (index == 3) {  // Time (show/hide + format + refresh interval)
    std::vector<WeatherMultiPopup::Group> groups;
    groups.push_back({nullptr, {"Show", "Hide"}, g_settings.showClock ? 0 : 1});
    groups.push_back({"Format", {"24h", "12h"}, g_settings.use24h ? 0 : 1});
    std::vector<std::string> refreshOptions;
    for (int i = 0; i < kIntervalCount; i++) refreshOptions.emplace_back(blockIntervalLabel(i));
    groups.push_back({"Refresh", refreshOptions, g_settings.intClock});
    multiPopup.show("Time", groups, [this](int group, int option) {
      switch (group) {
        case 0: g_settings.showClock = (option == 0); break;
        case 1: g_settings.use24h = (option == 0); break;
        case 2: setBlockIntervalIndex(BLK_CLOCK, static_cast<uint8_t>(option)); break;
      }
      settingsSave();
    });
    requestUpdate();
    return;
  }
  if (index == 4) {  // Date (show/hide + format)
    std::vector<WeatherMultiPopup::Group> groups;
    groups.push_back({nullptr, {"Show", "Hide"}, g_settings.showDate ? 0 : 1});
    std::vector<std::string> formatOptions;
    for (int i = 0; i < kWeatherDateFormatCount; i++) formatOptions.emplace_back(dateFormatLabel(i));
    groups.push_back({"Format", formatOptions, g_settings.dateFormat});
    multiPopup.show("Date", groups, [this](int group, int option) {
      switch (group) {
        case 0: g_settings.showDate = (option == 0); break;
        case 1: g_settings.dateFormat = static_cast<uint8_t>(option); break;
      }
      settingsSave();
    });
    requestUpdate();
    return;
  }
  if (index == 5) {  // Font family
    std::vector<std::string> options;
    for (uint8_t f = 0; f < WX_FONT_COUNT; f++) options.push_back(weatherFontName(f));
    optionPopup.show(StrId::STR_WEATHER_FONT, options, g_settings.fontFamily, [this](int i) {
      g_settings.fontFamily = static_cast<uint8_t>(i);
      settingsSave();
      requestUpdate();
    });
    requestUpdate();
    return;
  }
  if (index == 6) {  // Power button: short tap
    std::vector<std::string> options;
    for (uint8_t a = 0; a < WX_POWER_COUNT; a++) options.push_back(weatherPowerActionName(a));
    optionPopup.show(StrId::STR_WEATHER_POWER_TAP, options, g_settings.powerTapAction, [this](int i) {
      g_settings.powerTapAction = static_cast<uint8_t>(i);
      settingsSave();
      requestUpdate();
    });
    requestUpdate();
    return;
  }
  if (index == 7) {  // Power button: long press
    std::vector<std::string> options;
    for (uint8_t a = 0; a < WX_POWER_COUNT; a++) options.push_back(weatherPowerActionName(a));
    optionPopup.show(StrId::STR_WEATHER_POWER_HOLD, options, g_settings.powerHoldAction, [this](int i) {
      g_settings.powerHoldAction = static_cast<uint8_t>(i);
      settingsSave();
      requestUpdate();
    });
    requestUpdate();
    return;
  }

  if (index == kListHeader + kBlockRows) {  // Extra data (metric + refresh interval)
    std::vector<WeatherMultiPopup::Group> groups;
    // Metric section
    std::vector<std::string> metricOptions;
    for (uint8_t m = EXTRA_DEWPOINT; m < EXTRA_OFF; m++) {
      if (!extraMetricAvailableForProvider(m)) continue;
      metricOptions.emplace_back(extraMetricName(m));
    }
    if (g_settings.extData >= EXTRA_OFF) g_settings.extData = EXTRA_DEWPOINT;
    groups.push_back({"Metric", metricOptions, g_settings.extData});
    // Refresh section (same options as a block picker)
    std::vector<std::string> refreshOptions;
    refreshOptions.emplace_back(tr(STR_WEATHER_OFF));
    refreshOptions.emplace_back(tr(STR_WEATHER_MANUAL));
    for (int i = 1; i < kIntervalCount; i++) refreshOptions.emplace_back(blockIntervalLabel(i));
    int refreshCur = g_settings.showExtra ? (1 + g_settings.intExtra) : 0;
    groups.push_back({"Refresh", refreshOptions, refreshCur});
    multiPopup.show(tr(STR_WEATHER_EXTRA_DATA), groups, [this](int group, int option) {
      if (group == 0) {
        g_settings.extData = static_cast<uint8_t>(option);
      } else if (group == 1) {
        if (option == 0) {
          g_settings.showExtra = false;
        } else {
          g_settings.showExtra = true;
          setBlockIntervalIndex(BLK_EXTRA, static_cast<uint8_t>(option - 1));
        }
      }
      settingsSave();
    });
    requestUpdate();
    return;
  }
  if (index >= kListHeader && index < kListHeader + kBlockRows) {
    const BlockId b = kBlockIds[index - kListHeader];
    // Provider cannot supply this block (e.g. UV under OpenWeatherMap): the
    // row is labelled "Unavailable" and cannot be edited.
    if (!blockAvailableForProvider(b)) return;
    openBlockIntervalPicker(b);
  }
}

void WeatherSettingsActivity::openBlockIntervalPicker(BlockId b) {
  std::vector<std::string> options;
  options.push_back(tr(STR_WEATHER_OFF));
  options.push_back(tr(STR_WEATHER_MANUAL));
  // blockIntervalLabel(0) is "Manual only", which the Manual option above
  // already covers. Skip it so option index == blockCycleState (0 Off, 1
  // Manual, 2 "30 sec", 3 "1 min", ...) and the highlighted row matches the
  // block's actual interval instead of the one below it.
  for (int i = 1; i < kIntervalCount; i++) options.push_back(blockIntervalLabel(i));
  optionPopup.show(blockName(b), options, blockCycleState(b), [this, b](int i) {
    setBlockCycleState(b, i);
    requestUpdate();
  });
  requestUpdate();
}

void WeatherSettingsActivity::loop() {
  if (multiPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  if (state == State::SEARCHING) {
    if (!searchStarted) {
      searchStarted = true;
      requestUpdateAndWait();
      doSearch();
    }
    return;
  }

  if (state == State::RESULTS) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = State::LIST;
      requestUpdate();
      return;
    }
    if (geoCount > 0) {
      buttonNavigator.onPrevious([this] {
        selection = selection > 0 ? selection - 1 : geoCount - 1;
        requestUpdate();
      });
      buttonNavigator.onNext([this] {
        selection = selection < geoCount - 1 ? selection + 1 : 0;
        requestUpdate();
      });
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        if (selection >= 0 && selection < geoCount) {
          g_settings.latitude = geoResults[selection].latitude;
          g_settings.longitude = geoResults[selection].longitude;
          strncpy(g_settings.locationName, geoResults[selection].name, sizeof(g_settings.locationName) - 1);
          g_settings.locationName[sizeof(g_settings.locationName) - 1] = '\0';
          settingsSave();
          state = State::LIST;
          requestUpdate();
        }
      }
    }
    return;
  }

  // LIST
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  const int itemCount = kListHeader + kBlockRows + 1;
  buttonNavigator.onPrevious([this, itemCount] {
    selection = selection > 0 ? selection - 1 : itemCount - 1;
    requestUpdate();
  });
  buttonNavigator.onNext([this, itemCount] {
    selection = selection < itemCount - 1 ? selection + 1 : 0;
    requestUpdate();
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    dispatchListAction(selection);
  }
}

void WeatherSettingsActivity::render(RenderLock&&) {
  // Portrait-only: the weather app no longer supports landscape mode.
  const GfxRenderer::Orientation savedOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  if (multiPopup.processRenderSymbols(renderer, mappedInput)) {
    renderer.setOrientation(savedOrientation);
    return;
  }
  if (optionPopup.processRenderSymbols(renderer, mappedInput)) {
    renderer.setOrientation(savedOrientation);
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  // In landscape the front-button hint bar occupies the left strip (see
  // UITheme::getScreenSafeArea); keep the header title and list content clear
  // of it — same rule as WeatherActivity. safeArea.height already excludes the
  // hint bar in portrait and is the full height in landscape.
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true);
  const int contentTop = metrics.topPadding + metrics.headerHeight;
  const int contentBottom = safeArea.height;

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{safeArea.x, metrics.topPadding, safeArea.width, metrics.headerHeight},
                 tr(STR_WEATHER_SETTINGS));

  const int midY = (contentTop + contentBottom) / 2;

  switch (state) {
    case State::SEARCHING:
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 10, tr(STR_WEATHER_SEARCHING));
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 15, tr(STR_WEATHER_SEARCH_HINT));
      break;

    case State::RESULTS: {
      GUI.drawHeader(renderer, Rect{safeArea.x, metrics.topPadding, safeArea.width, metrics.headerHeight},
                     tr(STR_WEATHER_SEARCH_CITY));
      Rect listRect{safeArea.x, contentTop, safeArea.width, contentBottom - contentTop};
      if (geoCount <= 0) {
        renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_WEATHER_NO_RESULTS));
        renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, tr(STR_WEATHER_SEARCH_HINT));
      } else {
        GUI.drawList(
            renderer, listRect, geoCount, selection,
            [this](int index) -> std::string { return geoResults[index].name; },
            [this](int index) -> std::string { return geoResults[index].region; });
      }
      break;
    }

    case State::LIST:
    default: {
      const int itemCount = kListHeader + kBlockRows + 1;
      Rect listRect{safeArea.x, contentTop, safeArea.width, contentBottom - contentTop};
      GUI.drawList(
          renderer, listRect, itemCount, selection,
          [this](int index) -> std::string {
            switch (index) {
              case 0: return tr(STR_WEATHER_LOCATION);
              case 1: return tr(STR_WEATHER_UNITS);
              case 2: return tr(STR_WEATHER_WIND_SPEED);
              case 3: return tr(STR_WEATHER_TIME);
              case 4: return tr(STR_WEATHER_DATE);
              case 5: return tr(STR_WEATHER_FONT);
              case 6: return tr(STR_WEATHER_POWER_TAP);
              case 7: return tr(STR_WEATHER_POWER_HOLD);
              case kListHeader + kBlockRows: return tr(STR_WEATHER_EXTRA_DATA);
              default: return blockName(kBlockIds[index - kListHeader]);
            }
          },
          nullptr, nullptr,
          [this](int index) -> std::string {
            switch (index) {
              case 0:
                return g_settings.locationName[0] ? g_settings.locationName : tr(STR_WEATHER_NO_LOCATION);
              case 1:
                return g_settings.useCelsius ? "\xc2\xb0" "C" : "\xc2\xb0" "F";
              case 2:
                return g_settings.useMph ? "mph" : "km/h";
              case 3:
                return g_settings.showClock ? (g_settings.use24h ? "24h" : "12h") : tr(STR_WEATHER_OFF);
              case 4:
                return g_settings.showDate ? dateFormatLabel(g_settings.dateFormat) : tr(STR_WEATHER_OFF);
              case 5:
                return weatherFontName(g_settings.fontFamily);
              case 6:
                return weatherPowerActionName(g_settings.powerTapAction);
              case 7:
                return weatherPowerActionName(g_settings.powerHoldAction);
              case kListHeader + kBlockRows:
                if (!g_settings.showExtra) return tr(STR_WEATHER_OFF);
                if (!extraMetricAvailableForProvider(g_settings.extData)) return tr(STR_WEATHER_UNAVAILABLE);
                return extraMetricName(g_settings.extData);
              default: {
                const BlockId b = kBlockIds[index - kListHeader];
                if (!blockAvailableForProvider(b)) return tr(STR_WEATHER_UNAVAILABLE);
                return blockStateLabel(blockCycleState(b));
              }
            }
          });
      break;
    }
  }

  switch (state) {
    case State::LIST:
    case State::RESULTS: {
      drawWeatherSymbolHints(
          renderer, mappedInput.mapSymbols(ButtonHintSymbol::Close, ButtonHintSymbol::Select, ButtonHintSymbol::Up,
                                           ButtonHintSymbol::Down));
      break;
    }
    case State::SEARCHING:
      break;
  }

  renderer.displayBuffer();

  renderer.setOrientation(savedOrientation);
}
