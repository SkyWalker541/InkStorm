#include "WxSettings.h"

#include <time.h>

WxSettings g_settings;
WxData g_weather;

int32_t g_tzOffsetSeconds = 0;

static Preferences prefs;

// Persisted last-fetch epochs (UTC) per block, survives deep sleep.
static time_t s_lastFetch[kBlockCount] = {0};

const char* blockIntervalLabel(uint8_t index) {
  if (index >= kIntervalCount) index = 0;
  switch (index) {
    case 0:
      return "Manual only";
    case 1:
      return "30 sec";
    case 2:
      return "1 min";
    case 3:
      return "5 min";
    case 4:
      return "15 min";
    case 5:
      return "30 min";
    case 6:
      return "1 hour";
    case 7:
      return "3 hours";
    case 8:
      return "6 hours";
    case 9:
      return "12 hours";
    default:
      return "24 hours";
  }
}

const char* blockName(BlockId b) {
  switch (b) {
    case BLK_TEMP:
      return "Temperature";
    case BLK_FEELS:
      return "Feels like";
    case BLK_COND:
      return "Conditions";
    case BLK_HUM:
      return "Humidity";
    case BLK_WIND:
      return "Wind";
    case BLK_UV:
      return "UV / Solar";
    case BLK_AQI:
      return "Air quality";
    case BLK_PRES:
      return "Pressure";
    case BLK_SUN:
      return "Sunrise/Sunset";
    case BLK_FC:
      return "Forecast";
    case BLK_EXTRA:
      return "More data";
    default:
      return "Clock";
  }
}

const char* extraMetricName(uint8_t m) {
  switch (m) {
    case EXTRA_CLOUD:
      return "Cloud cover";
    case EXTRA_VISIBILITY:
      return "Visibility";
    case EXTRA_GUST:
      return "Wind gust";
    case EXTRA_PRECIP:
      return "Precipitation";
    case EXTRA_DEWPOINT:
    default:
      return "Dew point";
  }
}

const char* weatherPowerActionName(uint8_t action) {
  switch (action) {
    case WX_POWER_CROSSINK:
      return "CrossInk";
    case WX_POWER_NONE:
      return "Nothing";
    case WX_POWER_REFRESH:
    default:
      return "Refresh";
  }
}

static const char* kShortMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char* kFullMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                                    "July",    "August",   "September", "October", "November", "December"};

void formatWeatherDate(char* buf, size_t n, uint8_t format, time_t now) {
  if (buf == nullptr || n == 0) return;
  if (format >= kWeatherDateFormatCount) format = 0;
  time_t t = now + g_tzOffsetSeconds;
  struct tm tm;
  gmtime_r(&t, &tm);
  const int y = tm.tm_year + 1900;
  const int mon = tm.tm_mon + 1;
  switch (format) {
    case 1:
      snprintf(buf, n, "%02d/%02d/%d", tm.tm_mday, mon, y);
      break;
    case 2:
      snprintf(buf, n, "%d-%02d-%02d", y, mon, tm.tm_mday);
      break;
    case 3:
      snprintf(buf, n, "%s %d, %d", kShortMonths[tm.tm_mon], tm.tm_mday, y);
      break;
    case 4:
      snprintf(buf, n, "%s %d, %d", kFullMonths[tm.tm_mon], tm.tm_mday, y);
      break;
    case 5:
      snprintf(buf, n, "%d %s %d", tm.tm_mday, kShortMonths[tm.tm_mon], y);
      break;
    case 6:
      snprintf(buf, n, "%d %s %d", tm.tm_mday, kFullMonths[tm.tm_mon], y);
      break;
    default:
      snprintf(buf, n, "%02d/%02d/%d", mon, tm.tm_mday, y);
      break;
  }
}

bool blockShown(BlockId b) {
  switch (b) {
    case BLK_TEMP:
      return g_settings.showTemp;
    case BLK_FEELS:
      return g_settings.showFeelsLike;
    case BLK_COND:
      return g_settings.showCondition;
    case BLK_HUM:
      return g_settings.showHumidity;
    case BLK_WIND:
      return g_settings.showWind;
    case BLK_UV:
      return g_settings.showUv;
    case BLK_AQI:
      return g_settings.showAirQuality;
    case BLK_PRES:
      return g_settings.showPressure;
    case BLK_SUN:
      return g_settings.showSun;
    case BLK_FC:
      return g_settings.showForecast;
    case BLK_EXTRA:
      return g_settings.showExtra && g_settings.extData != EXTRA_OFF;
    default:
      return true;
  }
}

uint8_t blockIntervalIndex(BlockId b) {
  switch (b) {
    case BLK_TEMP:
      return g_settings.intTemp;
    case BLK_FEELS:
      return g_settings.intFeels;
    case BLK_COND:
      return g_settings.intCond;
    case BLK_HUM:
      return g_settings.intHum;
    case BLK_WIND:
      return g_settings.intWind;
    case BLK_UV:
      return g_settings.intUv;
    case BLK_AQI:
      return g_settings.intAqi;
    case BLK_PRES:
      return g_settings.intPres;
    case BLK_SUN:
      return g_settings.intSun;
    case BLK_FC:
      return g_settings.intFc;
    case BLK_EXTRA:
      return g_settings.intExtra;
    default:
      return g_settings.intClock;
  }
}

void setBlockIntervalIndex(BlockId b, uint8_t index) {
  if (index >= kIntervalCount) return;
  switch (b) {
    case BLK_TEMP:
      g_settings.intTemp = index;
      break;
    case BLK_FEELS:
      g_settings.intFeels = index;
      break;
    case BLK_COND:
      g_settings.intCond = index;
      break;
    case BLK_HUM:
      g_settings.intHum = index;
      break;
    case BLK_WIND:
      g_settings.intWind = index;
      break;
    case BLK_UV:
      g_settings.intUv = index;
      break;
    case BLK_AQI:
      g_settings.intAqi = index;
      break;
    case BLK_PRES:
      g_settings.intPres = index;
      break;
    case BLK_SUN:
      g_settings.intSun = index;
      break;
    case BLK_FC:
      g_settings.intFc = index;
      break;
    case BLK_EXTRA:
      g_settings.intExtra = index;
      break;
    default:
      g_settings.intClock = index;
      break;
  }
  settingsSave();
}

uint32_t blockIntervalSeconds(BlockId b) { return kIntervalSeconds[blockIntervalIndex(b)]; }

time_t blockLastFetch(BlockId b) { return s_lastFetch[b]; }

void markBlockFetched(BlockId b) {
  if (b >= kBlockCount) return;
  s_lastFetch[b] = time(nullptr);
  prefs.begin("wx_prefs", false);
  prefs.putBytes("lastFetch", s_lastFetch, sizeof(s_lastFetch));
  prefs.end();
}

void markAllFetched() {
  for (int i = 0; i < kBlockCount; i++) s_lastFetch[i] = time(nullptr);
  prefs.begin("wx_prefs", false);
  prefs.putBytes("lastFetch", s_lastFetch, sizeof(s_lastFetch));
  prefs.end();
}

bool blockDue(BlockId b) {
  if (!blockShown(b)) return false;
  uint32_t interval = blockIntervalSeconds(b);
  if (interval == 0) return false;
  if (s_lastFetch[b] == 0) return true;  // never fetched
  time_t now = time(nullptr);
  return (now - s_lastFetch[b]) >= (time_t)interval;
}

uint32_t dueBlockMask() {
  uint32_t mask = 0;
  for (int i = 0; i < kBlockCount; i++) {
    if (i == BLK_CLOCK) continue;  // render-only
    if (blockDue(static_cast<BlockId>(i))) mask |= (1u << i);
  }
  return mask;
}

uint32_t allShownBlockMask() {
  uint32_t mask = 0;
  for (int i = 0; i < kBlockCount; i++) {
    if (i == BLK_CLOCK) continue;  // render-only
    if (blockShown(static_cast<BlockId>(i))) mask |= (1u << i);
  }
  return mask;
}

bool blockAvailableForProvider(BlockId b) {
  (void)b;
  // Open-Meteo (the only provider) supplies every block, including UV/solar.
  return true;
}

bool extraMetricAvailableForProvider(uint8_t metric) {
  (void)metric;
  // Open-Meteo reports dew point, cloud cover, visibility, gusts and
  // precipitation in its current-weather response.
  return true;
}

// ---------------------------------------------------------------------------

void settingsLoad() {
  prefs.begin("wx_prefs", false);

  g_settings.useCelsius = prefs.getBool("celsius", true);
  g_settings.use24h = prefs.getBool("use24h", true);
  g_settings.useMph = prefs.getBool("useMph", false);

  g_settings.showTemp = prefs.getBool("s_temp", true);
  g_settings.showFeelsLike = prefs.getBool("s_feels", true);
  g_settings.showCondition = prefs.getBool("s_cond", true);
  g_settings.showHumidity = prefs.getBool("s_hum", true);
  g_settings.showWind = prefs.getBool("s_wind", true);
  g_settings.showUv = prefs.getBool("s_uv", true);
  g_settings.showAirQuality = prefs.getBool("s_aqi", true);
  g_settings.showPressure = prefs.getBool("s_pres", true);
  g_settings.showSun = prefs.getBool("s_sun", true);
  g_settings.showForecast = prefs.getBool("s_fc", true);
  g_settings.showExtra = prefs.getBool("s_extra", true);

  g_settings.intTemp = prefs.getUChar("i_temp", 6);
  g_settings.intFeels = prefs.getUChar("i_feels", 6);
  g_settings.intCond = prefs.getUChar("i_cond", 6);
  g_settings.intHum = prefs.getUChar("i_hum", 6);
  g_settings.intWind = prefs.getUChar("i_wind", 6);
  g_settings.intUv = prefs.getUChar("i_uv", 6);
  g_settings.intAqi = prefs.getUChar("i_aqi", 8);
  g_settings.intPres = prefs.getUChar("i_pres", 8);
  g_settings.intSun = prefs.getUChar("i_sun", 10);
  g_settings.intFc = prefs.getUChar("i_fc", 7);
  g_settings.intExtra = prefs.getUChar("i_extra", 6);
  g_settings.intClock = prefs.getUChar("i_clock", 5);

  g_settings.extData = prefs.getUChar("e_data", EXTRA_DEWPOINT);
  if (g_settings.extData >= EXTRA_COUNT) g_settings.extData = EXTRA_DEWPOINT;

  g_settings.latitude = prefs.getFloat("lat", 0.0f);
  g_settings.longitude = prefs.getFloat("lon", 0.0f);
  prefs.getString("locName", g_settings.locationName, sizeof(g_settings.locationName));

  g_settings.showClock = prefs.getBool("showClock", true);
  g_settings.showDate = prefs.getBool("showDate", true);
  g_settings.dateFormat = prefs.getUChar("dateFmt", 0);
  if (g_settings.dateFormat >= kWeatherDateFormatCount) g_settings.dateFormat = 0;

  g_settings.fontFamily = prefs.getUChar("font", WX_FONT_UI);
  if (g_settings.fontFamily >= WX_FONT_COUNT) g_settings.fontFamily = WX_FONT_UI;

  g_settings.orientation = prefs.getUChar("orient", WX_ORIENTATION_PORTRAIT);
  if (g_settings.orientation >= WX_ORIENTATION_COUNT) g_settings.orientation = WX_ORIENTATION_PORTRAIT;

  g_settings.powerTapAction = prefs.getUChar("pwrTap", WX_POWER_REFRESH);
  if (g_settings.powerTapAction >= WX_POWER_COUNT) g_settings.powerTapAction = WX_POWER_REFRESH;
  g_settings.powerHoldAction = prefs.getUChar("pwrHold", WX_POWER_NONE);
  if (g_settings.powerHoldAction >= WX_POWER_COUNT) g_settings.powerHoldAction = WX_POWER_NONE;

  prefs.getBytes("lastFetch", s_lastFetch, sizeof(s_lastFetch));

  prefs.end();
}

void settingsSave() {
  prefs.begin("wx_prefs", false);

  prefs.putBool("celsius", g_settings.useCelsius);
  prefs.putBool("use24h", g_settings.use24h);
  prefs.putBool("useMph", g_settings.useMph);

  prefs.putBool("s_temp", g_settings.showTemp);
  prefs.putBool("s_feels", g_settings.showFeelsLike);
  prefs.putBool("s_cond", g_settings.showCondition);
  prefs.putBool("s_hum", g_settings.showHumidity);
  prefs.putBool("s_wind", g_settings.showWind);
  prefs.putBool("s_uv", g_settings.showUv);
  prefs.putBool("s_aqi", g_settings.showAirQuality);
  prefs.putBool("s_pres", g_settings.showPressure);
  prefs.putBool("s_sun", g_settings.showSun);
  prefs.putBool("s_fc", g_settings.showForecast);
  prefs.putBool("s_extra", g_settings.showExtra);

  prefs.putUChar("i_temp", g_settings.intTemp);
  prefs.putUChar("i_feels", g_settings.intFeels);
  prefs.putUChar("i_cond", g_settings.intCond);
  prefs.putUChar("i_hum", g_settings.intHum);
  prefs.putUChar("i_wind", g_settings.intWind);
  prefs.putUChar("i_uv", g_settings.intUv);
  prefs.putUChar("i_aqi", g_settings.intAqi);
  prefs.putUChar("i_pres", g_settings.intPres);
  prefs.putUChar("i_sun", g_settings.intSun);
  prefs.putUChar("i_fc", g_settings.intFc);
  prefs.putUChar("i_extra", g_settings.intExtra);
  prefs.putUChar("i_clock", g_settings.intClock);

  prefs.putUChar("e_data", g_settings.extData);

  prefs.putFloat("lat", g_settings.latitude);
  prefs.putFloat("lon", g_settings.longitude);
  prefs.putString("locName", g_settings.locationName);

  prefs.putBool("showClock", g_settings.showClock);
  prefs.putBool("showDate", g_settings.showDate);
  prefs.putUChar("dateFmt", g_settings.dateFormat);

  prefs.putUChar("font", g_settings.fontFamily);
  prefs.putUChar("orient", g_settings.orientation);

  prefs.putUChar("pwrTap", g_settings.powerTapAction);
  prefs.putUChar("pwrHold", g_settings.powerHoldAction);

  prefs.putBytes("lastFetch", s_lastFetch, sizeof(s_lastFetch));

  prefs.end();
}
