#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class WeatherActivity final : public Activity {
 public:
  explicit WeatherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Weather", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override;
  void render(RenderLock&&) override;

 private:
  enum class State {
    SHOWING,       // normal weather display
    MENU,          // actions menu open
    FETCHING,      // first tick renders "Updating...", then blocks on fetch
    FETCH_FAILED,  // fetch error message
    ABOUT          // about screen
  };

  State state = State::SHOWING;
  bool settingsLoaded = false;
  bool fetchStarted = false;
  uint32_t pendingMask = 0;
  bool autoFetchTried = false;
  int menuSelection = 0;
  bool wifiWasConnectedOnEnter = false;
  bool wifiTornDownOnExit = false;
  // Set when the framebuffer could not be reallocated after a fetch. While it
  // is set the activity must not request a render (the renderer would paint
  // into a null buffer); loop() retries the realloc and recovers the screen.
  bool framebufferMissing = false;
  unsigned long framebufferMissingSince = 0;
  // Last time the WiFi radio was power-cycled to defragment the heap. Cycles
  // are throttled (30 s apart) so a stubbornly missing framebuffer cannot spin
  // the radio into a reconnect storm.
  unsigned long lastRadioCycleMs = 0;

  // Auto-refresh while the weather screen stays open: set when a fetch cannot
  // run (WiFi offline); cleared when a connection is (re)established.
  bool autoRefreshBlocked = false;
  // Block mask that triggered a pending WiFi-connect fetch.
  uint32_t autoDueMask = 0;
  // Wall-clock time before which auto-refresh is suppressed. Set after a
  // failed (or user-cancelled) fetch so the screen does not trap the user in
  // a retry loop: the failed blocks stay "due", so without this gate the
  // auto-refresh would re-launch the fetch the instant the error screen is
  // dismissed, before the user can reach the menu/settings to fix it.
  unsigned long autoRefreshBlockedUntilMs = 0;

  ButtonNavigator buttonNavigator;

  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void startFetch(uint32_t mask);
  void doFetch();
  // Attempts to reallocate the lent framebuffer after a fetch. Retries for a
  // short grace period, then — if `allowRadioCycle` and at least
  // kRadioCycleIntervalMs have passed since the last one — power-cycles the
  // WiFi radio to force lwIP to release the buffers that keep the heap
  // fragmented. Returns true only when the framebuffer is back and rendering
  // is safe again.
  bool reallocFramebufferWithRecovery(bool allowRadioCycle = true);
  void openSettings();
  void dispatchMenuAction(int index);
  // Manual refresh (power-button tap, the refresh hint button, or menu
  // "Update now"): refetch all shown blocks, launching the WiFi picker first
  // when the radio is down.
  void triggerManualRefresh();
  // Shows a "Switching to CrossInk..." popup, then sets the app0 boot
  // partition and restarts. The popup stays on the e-ink while the device
  // reboots, so users get feedback during the boot delay.
  void launchCrossInk();

  void renderContent();
  // Draws the whole frame (header, body for the current state, button hints)
  // into the current framebuffer. Called once for BW and once per grayscale plane.
  void renderScene();
};
