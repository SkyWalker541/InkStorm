#include "WeatherApi.h"

#include <ArduinoJson.h>
#include <Logging.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "network/HttpDownloader.h"
#include "weather/WxSettings.h"

namespace {

// Hard per-request cap. Keeps the "Loading" screen responsive and stops a
// wedged socket from pinning the fetch forever; the caller also enforces a
// global deadline and Back-button cancellation.
constexpr uint32_t kRequestTimeoutMs = 8000;

// Safety ceiling for a buffered response body. Real responses are 0.5-2 KB;
// the cap just bounds the std::string before it is handed to the JSON parser.
constexpr size_t kMaxResponseBytes = 32768;

// Open-Meteo serves the same API over plain HTTP ("HTTPS is optional"), which
// is the transport that fits the ESP32-C3: TLS 1.3 + the CA bundle need
// ~40-50 KB of contiguous heap for the handshake and previously starved the
// ~51 KB max-alloc, aborting the device mid-fetch.
const char kOpenMeteoForecast[] = "http://api.open-meteo.com/v1/forecast";
const char kOpenMeteoAirQuality[] = "http://air-quality-api.open-meteo.com/v1/air-quality";
const char kOpenMeteoGeocoding[] = "http://geocoding-api.open-meteo.com/v1/search";

char s_error[128] = "";

void setError(const char* message) {
  snprintf(s_error, sizeof(s_error), "%s", message ? message : "");
}

bool blockInMask(uint32_t mask, BlockId b) {
  return (mask & (1u << static_cast<unsigned>(b))) != 0;
}

// Percent-encodes a free-text query (city names contain spaces).
std::string urlEncode(const std::string& in) {
  const char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(in.size());
  for (unsigned char c : in) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0x0F];
    }
  }
  return out;
}

// Buffered GET that returns false on transport failure or an oversized body.
// Error text is left to the caller so each provider reports its own reason.
bool httpGetJson(const std::string& url, const std::function<bool()>& shouldCancel, std::string& body) {
  body.clear();
  if (!HttpDownloader::fetchUrl(url, body, "", "", shouldCancel, kRequestTimeoutMs)) return false;
  return body.size() <= kMaxResponseBytes;
}

// A successful forecast call refreshes every forecast-backed block, so mark
// them all fetched regardless of which subset triggered the request. The AQI
// block is its own endpoint and is marked separately.
void markForecastBlocksFetched() {
  const BlockId kForecastBlocks[] = {BLK_TEMP, BLK_FEELS, BLK_COND, BLK_HUM,  BLK_WIND,
                                     BLK_UV,   BLK_PRES,  BLK_SUN,  BLK_FC,   BLK_EXTRA};
  for (BlockId b : kForecastBlocks) {
    markBlockFetched(b);
  }
}

// --- Open-Meteo -----------------------------------------------------------

std::string buildOpenMeteoForecastUrl() {
  char url[640];
  snprintf(url, sizeof(url),
           "%s?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,"
           "precipitation,weather_code,cloud_cover,pressure_msl,surface_pressure,"
           "wind_speed_10m,wind_direction_10m,wind_gusts_10m,visibility,dew_point_2m,"
           "uv_index,shortwave_radiation"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset"
           "&timezone=auto&timeformat=unixtime&forecast_days=7",
           kOpenMeteoForecast, g_settings.latitude, g_settings.longitude);
  return url;
}

std::string buildOpenMeteoAirQualityUrl() {
  char url[320];
  snprintf(url, sizeof(url),
           "%s?latitude=%.4f&longitude=%.4f"
           "&current=us_aqi,european_aqi,pm10,pm2_5,ozone,nitrogen_dioxide,sulphur_dioxide,carbon_monoxide",
           kOpenMeteoAirQuality, g_settings.latitude, g_settings.longitude);
  return url;
}

bool parseOpenMeteoForecast(const std::string& body) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    setError("Open-Meteo returned invalid data");
    return false;
  }
  if (doc["error"].is<bool>() && doc["error"].as<bool>()) {
    setError("Open-Meteo rejected the request");
    return false;
  }

  const JsonObject current = doc["current"];
  if (current.isNull()) {
    setError("Open-Meteo returned invalid data");
    return false;
  }

  g_tzOffsetSeconds = doc["utc_offset_seconds"] | 0;

  g_weather.tempC = current["temperature_2m"] | 0.0f;
  g_weather.feelsLikeC = current["apparent_temperature"] | 0.0f;
  g_weather.humidityPct = current["relative_humidity_2m"] | 0.0f;
  g_weather.windKph = current["wind_speed_10m"] | 0.0f;
  g_weather.windDeg = current["wind_direction_10m"] | 0.0f;
  g_weather.gustKph = current["wind_gusts_10m"] | 0.0f;
  g_weather.pressureHpa = current["pressure_msl"] | 0.0f;
  g_weather.uvIndex = current["uv_index"] | 0.0f;
  g_weather.solarWm2 = current["shortwave_radiation"] | 0.0f;
  g_weather.dewPointC = current["dew_point_2m"] | 0.0f;
  g_weather.cloudPct = current["cloud_cover"] | 0.0f;
  // Open-Meteo reports visibility in metres; the model stores kilometres.
  g_weather.visibilityKm = (current["visibility"] | 0.0f) / 1000.0f;
  g_weather.precipMm = current["precipitation"] | 0.0f;
  g_weather.weatherCode = current["weather_code"] | 0;
  g_weather.isDay = (current["is_day"] | 1) != 0;

  const JsonObject daily = doc["daily"];
  if (!daily.isNull()) {
    const JsonArray maxT = daily["temperature_2m_max"];
    const JsonArray minT = daily["temperature_2m_min"];
    const JsonArray codes = daily["weather_code"];
    for (int i = 0; i < WxData::FORECAST_DAYS; ++i) {
      if (i < static_cast<int>(maxT.size()) && i < static_cast<int>(minT.size()) &&
          i < static_cast<int>(codes.size())) {
        g_weather.fcMaxC[i] = maxT[i] | 0.0f;
        g_weather.fcMinC[i] = minT[i] | 0.0f;
        g_weather.fcCode[i] = codes[i] | 0;
        g_weather.fcValid[i] = true;
      } else {
        g_weather.fcValid[i] = false;
      }
    }
    const JsonArray sunrise = daily["sunrise"];
    const JsonArray sunset = daily["sunset"];
    if (sunrise.size() > 0) g_weather.sunrise = static_cast<time_t>(sunrise[0].as<int64_t>());
    if (sunset.size() > 0) g_weather.sunset = static_cast<time_t>(sunset[0].as<int64_t>());
  }

  g_weather.valid = true;
  return true;
}

bool parseOpenMeteoAirQuality(const std::string& body) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    setError("Open-Meteo returned invalid data");
    return false;
  }
  const JsonObject current = doc["current"];
  if (current.isNull()) {
    setError("Open-Meteo returned invalid data");
    return false;
  }
  g_weather.aqi = current["european_aqi"] | 0.0f;
  g_weather.usAqi = current["us_aqi"] | 0.0f;
  g_weather.pm25 = current["pm2_5"] | 0.0f;
  g_weather.pm10 = current["pm10"] | 0.0f;
  g_weather.o3 = current["ozone"] | 0.0f;
  g_weather.no2 = current["nitrogen_dioxide"] | 0.0f;
  g_weather.so2 = current["sulphur_dioxide"] | 0.0f;
  g_weather.co = current["carbon_monoxide"] | 0.0f;
  return true;
}

bool fetchOpenMeteo(uint32_t blockMask, const std::function<bool()>& shouldCancel) {
  const bool needForecast = blockInMask(blockMask, BLK_TEMP) || blockInMask(blockMask, BLK_FEELS) ||
                            blockInMask(blockMask, BLK_COND) || blockInMask(blockMask, BLK_HUM) ||
                            blockInMask(blockMask, BLK_WIND) || blockInMask(blockMask, BLK_UV) ||
                            blockInMask(blockMask, BLK_PRES) || blockInMask(blockMask, BLK_SUN) ||
                            blockInMask(blockMask, BLK_FC) || blockInMask(blockMask, BLK_EXTRA);
  const bool needAqi = blockInMask(blockMask, BLK_AQI);

  bool ok = true;
  if (needForecast) {
    std::string body;
    if (!httpGetJson(buildOpenMeteoForecastUrl(), shouldCancel, body)) {
      setError("Open-Meteo request failed");
      ok = false;
    } else if (!parseOpenMeteoForecast(body)) {
      ok = false;
    } else {
      markForecastBlocksFetched();
    }
  }

  if (ok && needAqi) {
    std::string body;
    if (!httpGetJson(buildOpenMeteoAirQualityUrl(), shouldCancel, body)) {
      setError("Open-Meteo request failed");
      ok = false;
    } else if (!parseOpenMeteoAirQuality(body)) {
      ok = false;
    } else {
      markBlockFetched(BLK_AQI);
    }
  }

  return ok;
}

}  // namespace

// --- Public API ------------------------------------------------------------

bool weatherFetch(uint32_t blockMask, const std::function<bool()>& shouldCancel) {
  setError("");
  if (g_settings.latitude == 0.0f && g_settings.longitude == 0.0f) {
    setError("No location set");
    return false;
  }
  return fetchOpenMeteo(blockMask, shouldCancel);
}

const char* weatherLastError() { return s_error; }

int geoSearch(const char* query, GeoResult* results, int maxResults) {
  if (query == nullptr || results == nullptr || maxResults <= 0) return -1;

  char url[512];
  snprintf(url, sizeof(url), "%s?name=%s&count=%d&language=en&format=json", kOpenMeteoGeocoding,
           urlEncode(query).c_str(), maxResults);

  std::string body;
  if (!httpGetJson(url, nullptr, body)) {
    setError("Geocoding request failed");
    return -1;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) return -1;

  const JsonArray items = doc["results"];
  if (items.isNull()) return 0;

  int count = 0;
  for (JsonVariantConst item : items) {
    if (count >= maxResults) break;
    GeoResult& out = results[count];
    out.latitude = item["latitude"] | 0.0f;
    out.longitude = item["longitude"] | 0.0f;
    const char* name = item["name"] | "";
    snprintf(out.name, sizeof(out.name), "%s", name ? name : "");
    const char* admin1 = item["admin1"] | "";
    const char* country = item["country"] | "";
    if (admin1 != nullptr && admin1[0] != '\0' && country != nullptr && country[0] != '\0') {
      snprintf(out.region, sizeof(out.region), "%s, %s", admin1, country);
    } else if (country != nullptr && country[0] != '\0') {
      snprintf(out.region, sizeof(out.region), "%s", country);
    } else {
      out.region[0] = '\0';
    }
    count++;
  }
  return count;
}

const char* wmoConditionText(int code) {
  switch (code) {
    case 0: return "Clear";
    case 1: return "Mainly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: return "Fog";
    case 48: return "Rime fog";
    case 51: return "Light drizzle";
    case 53: return "Drizzle";
    case 55: return "Dense drizzle";
    case 56: return "Freezing drizzle";
    case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: return "Freezing rain";
    case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light showers";
    case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: return "Snow showers";
    case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: return "Thunderstorm, hail";
    case 99: return "Thunderstorm, hail";
    default: return "Unknown";
  }
}

void formatTemp(char* buf, size_t n, float celsius, bool useFahrenheit, bool roundValue) {
  if (buf == nullptr || n == 0) return;
  const float v = useFahrenheit ? cToF(celsius) : celsius;
  if (roundValue) {
    snprintf(buf, n, "%d\xc2\xb0", static_cast<int>(lroundf(v)));
  } else {
    snprintf(buf, n, "%.1f\xc2\xb0", v);
  }
}
