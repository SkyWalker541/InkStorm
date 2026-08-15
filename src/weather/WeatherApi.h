#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// Maximum number of city results returned by geoSearch().
constexpr int MAX_GEO_RESULTS = 6;

// One candidate city from the geocoding provider.
struct GeoResult {
  float latitude = 0.0f;
  float longitude = 0.0f;
  char name[48] = "";
  char region[64] = "";  // e.g. "Oregon, United States"
};

// Fetches the weather data backing every block set in blockMask (a bitmask of
// BlockId). The configured provider supplies the fields; the mask only decides
// which network calls run (air-quality is a separate endpoint) and which blocks
// get their fetch timestamps updated. Returns true when all requested data was
// refreshed; on failure weatherLastError() describes the problem.
//
// shouldCancel is polled during network I/O so the caller can abort (Back
// button / deadline). Each HTTP request is independently time-capped.
bool weatherFetch(uint32_t blockMask, const std::function<bool()>& shouldCancel);

// Human-readable reason for the most recent weatherFetch() failure. Returns an
// empty string when no error is set. The buffer is static and overwritten on
// each fetch attempt.
const char* weatherLastError();

// City search via the geocoding provider. Fills up to maxResults entries and
// returns the count written, 0 when nothing matched, or -1 on transport
// failure.
int geoSearch(const char* query, GeoResult* results, int maxResults);

// Maps a WMO weather code to a short human-readable condition string
// ("Clear", "Moderate rain", ...). Never returns null.
const char* wmoConditionText(int code);

// Formats a temperature into buf. celsius is always the input; useFahrenheit
// converts first. When roundValue is true a whole degree ("23\xc2\xb0") is
// produced, otherwise one decimal ("23.4\xc2\xb0").
void formatTemp(char* buf, size_t n, float celsius, bool useFahrenheit, bool roundValue);
