#pragma once

#include <Preferences.h>

#include <cstdint>

// ---------------------------------------------------------------------------
// Weather display blocks. Each block has its own show-toggle AND its own
// update interval (0 = manual only). Blocks are the atomic unit of fetching.
// ---------------------------------------------------------------------------
enum BlockId : uint8_t {
  BLK_TEMP = 0,  // Big temperature readout
  BLK_FEELS,     // Feels-like
  BLK_COND,      // Condition icon + text (+ day/night)
  BLK_HUM,       // Humidity
  BLK_WIND,      // Wind speed + direction
  BLK_UV,        // UV / solar index
  BLK_AQI,       // Air quality (AQI + PM2.5)
  BLK_PRES,      // Pressure
  BLK_SUN,       // Sunrise / sunset
  BLK_FC,        // Daily forecast strip
  BLK_EXTRA,     // User-selectable extra metric (see ExtraMetric)
  BLK_CLOCK,     // Clock in header (re-render interval, no network)
  BLK_COUNT
};

static constexpr int kBlockCount = BLK_COUNT;

// Metrics the extra (8th) grid cell can show. The user picks one in settings;
// the block keeps its own show-toggle and refresh interval.
enum ExtraMetric : uint8_t {
  EXTRA_DEWPOINT = 0,
  EXTRA_CLOUD,
  EXTRA_VISIBILITY,
  EXTRA_GUST,
  EXTRA_PRECIP,
  EXTRA_OFF,  // block hidden
  EXTRA_COUNT
};

const char* extraMetricName(uint8_t m);

// Interval options in seconds. Index 0 = manual only.
static const uint32_t kIntervalSeconds[] = {0, 30, 60, 300, 900, 1800, 3600, 10800, 21600, 43200, 86400};
static constexpr int kIntervalCount = sizeof(kIntervalSeconds) / sizeof(kIntervalSeconds[0]);

const char* blockIntervalLabel(uint8_t index);
const char* blockName(BlockId b);

// Weather display font family (index into the built-in CrossInk font families).
enum WeatherFontFamily : uint8_t {
  WX_FONT_UI = 0,  // default CrossInk UI fonts
  WX_FONT_LEXENDDECA,
  WX_FONT_BITTER,
  WX_FONT_CHAREINK,
  WX_FONT_COUNT
};

// Weather screen orientation (independent of the global reader orientation).
enum WeatherOrientation : uint8_t { WX_ORIENTATION_PORTRAIT = 0, WX_ORIENTATION_LANDSCAPE = 1, WX_ORIENTATION_COUNT };

// What the hardware power button does on this partition (sleep is disabled,
// so the button is a free control). A short tap and a long press can each be
// assigned an action independently.
enum WeatherPowerAction : uint8_t {
  WX_POWER_REFRESH = 0,   // manual weather refresh
  WX_POWER_CROSSINK = 1,  // boot into CrossInk (the reader partition)
  WX_POWER_NONE = 2,      // do nothing
  WX_POWER_COUNT
};

const char* weatherPowerActionName(uint8_t action);

struct WxSettings {
  bool useCelsius = true;
  bool use24h = true;
  bool useMph = false;  // wind speed unit: km/h (false) or mph (true)

  // Show toggles
  bool showTemp = true;
  bool showFeelsLike = true;
  bool showCondition = true;
  bool showHumidity = true;
  bool showWind = true;
  bool showUv = true;
  bool showAirQuality = true;
  bool showPressure = true;
  bool showSun = true;
  bool showForecast = true;
  bool showExtra = true;

  // Per-block update interval indices into kIntervalSeconds
  uint8_t intTemp = 6;   // 1h
  uint8_t intFeels = 6;  // 1h
  uint8_t intCond = 6;   // 1h
  uint8_t intHum = 6;    // 1h
  uint8_t intWind = 6;   // 1h
  uint8_t intUv = 6;     // 1h
  uint8_t intAqi = 8;    // 6h
  uint8_t intPres = 8;   // 6h
  uint8_t intSun = 10;   // 24h
  uint8_t intFc = 7;     // 3h
  uint8_t intExtra = 6;  // 1h
  uint8_t intClock = 5;  // 30m (re-render only)

  // Which metric the extra (8th) cell shows (see ExtraMetric).
  uint8_t extData = EXTRA_DEWPOINT;

  // Location
  float latitude = 0.0f;
  float longitude = 0.0f;
  char locationName[48] = "";

  // Header clock + date (shown on the weather home screen, opposite the
  // battery and the city name respectively). The clock re-renders on its own
  // interval (intClock) so the displayed time stays current while the app is
  // open.
  bool showClock = true;
  bool showDate = true;
  uint8_t dateFormat = 0;  // index into kWeatherDateFormat[]

  // Display font family for the weather screen.
  uint8_t fontFamily = WX_FONT_UI;

  // Screen orientation: WX_ORIENTATION_PORTRAIT or WX_ORIENTATION_LANDSCAPE.
  uint8_t orientation = WX_ORIENTATION_PORTRAIT;

  // Power-button behavior (see WeatherPowerAction): a short tap and a long
  // press are assigned independently.
  uint8_t powerTapAction = WX_POWER_REFRESH;
  uint8_t powerHoldAction = WX_POWER_NONE;
};

extern WxSettings g_settings;

// ---------------------------------------------------------------------------
// Weather data model
// ---------------------------------------------------------------------------
struct WxData {
  bool valid = false;
  bool isDay = true;
  time_t updatedAt = 0;

  float tempC = 0.0f;
  float feelsLikeC = 0.0f;
  float humidityPct = 0.0f;
  float windKph = 0.0f;
  float windDeg = 0.0f;
  float pressureHpa = 0.0f;
  float uvIndex = 0.0f;
  float solarWm2 = 0.0f;

  float aqi = 0.0f;    // European AQI (0-100 scale, primary display)
  float usAqi = 0.0f;  // US AQI (0-500 scale)
  float pm25 = 0.0f;
  float pm10 = 0.0f;
  float o3 = 0.0f;   // ozone (µg/m³)
  float no2 = 0.0f;  // nitrogen dioxide (µg/m³)
  float so2 = 0.0f;  // sulphur dioxide (µg/m³)
  float co = 0.0f;   // carbon monoxide (µg/m³)

  // Extra (8th cell) metric values — only the selected one is populated.
  float dewPointC = 0.0f;
  float cloudPct = 0.0f;
  float visibilityKm = 0.0f;  // API gives metres; stored as km
  float gustKph = 0.0f;
  float precipMm = 0.0f;

  int weatherCode = 0;

  time_t sunrise = 0;  // epoch (UTC)
  time_t sunset = 0;

  static constexpr int FORECAST_DAYS = 6;
  int fcCode[FORECAST_DAYS] = {0};
  float fcMinC[FORECAST_DAYS] = {0};
  float fcMaxC[FORECAST_DAYS] = {0};
  bool fcValid[FORECAST_DAYS] = {false};
};

extern WxData g_weather;

// Conversions
inline float cToF(float c) { return c * 9.0f / 5.0f + 32.0f; }
inline const char* tempUnit(const WxSettings& s) { return s.useCelsius ? "C" : "F"; }
inline float tempValue(const WxSettings& s, float c) { return s.useCelsius ? c : cToF(c); }
inline float kphToMph(float kph) { return kph * 0.621371f; }
inline float windSpeedValue(const WxSettings& s, float kph) { return s.useMph ? kphToMph(kph) : kph; }
inline const char* windUnit(const WxSettings& s) { return s.useMph ? "mph" : "km/h"; }

// ---------------------------------------------------------------------------
// Settings + scheduling API
// ---------------------------------------------------------------------------
void settingsLoad();
void settingsSave();
bool blockShown(BlockId b);
uint8_t blockIntervalIndex(BlockId b);
uint32_t blockIntervalSeconds(BlockId b);
void setBlockIntervalIndex(BlockId b, uint8_t index);

// Per-block due tracking. lastFetch is persisted across deep sleep.
bool blockDue(BlockId b);
time_t blockLastFetch(BlockId b);
void markBlockFetched(BlockId b);  // sets lastFetch=now for one block
void markAllFetched();             // after a manual "Update now"

// Bitmask of blocks (1u << BlockId)
uint32_t dueBlockMask();       // shown + interval elapsed
uint32_t allShownBlockMask();  // all shown blocks (for manual refresh)

// With Open-Meteo as the only provider every block and extra metric is
// always available (visibility is included in the current-weather response).
// The helpers remain so future providers can re-introduce capability limits.
bool blockAvailableForProvider(BlockId b);
bool extraMetricAvailableForProvider(uint8_t metric);

// Header date formatting. kWeatherDateFormatCount formats are supported;
// the date is rendered in the location's local time (g_tzOffsetSeconds).
static constexpr int kWeatherDateFormatCount = 7;
void formatWeatherDate(char* buf, size_t n, uint8_t format, time_t now);

// Time-zone offset (seconds, from Open-Meteo "utc_offset_seconds").
extern int32_t g_tzOffsetSeconds;
