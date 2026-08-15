#pragma once

#include <string>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"
#include "weather/WeatherApi.h"
#include "weather/WeatherMultiPopup.h"
#include "weather/WxSettings.h"

class WeatherSettingsActivity final : public Activity {
 public:
  explicit WeatherSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("WeatherSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override;
  void render(RenderLock&&) override;

 private:
  enum class State { LIST, SEARCHING, RESULTS };

  State state = State::LIST;
  int selection = 0;
  std::string searchQuery;
  bool searchStarted = false;
  bool wifiTornDownOnExit = false;
  int geoCount = 0;
  GeoResult geoResults[MAX_GEO_RESULTS];

  ButtonNavigator buttonNavigator;
  OptionPopup optionPopup;
  WeatherMultiPopup multiPopup;

  void dispatchListAction(int index);
  void openBlockIntervalPicker(BlockId b);
  void startCitySearch();
  void onKeyboardResult(const std::string& text);
  void launchWifiSelectionForSearch(const std::string& query);
  void startSearch(const std::string& query);
  void doSearch();
};
