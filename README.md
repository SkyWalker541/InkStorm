> # CrossInk v1.5.0 / InkStorm v1.0.0 Dual Boot
>
> **Initial release.** One firmware, two partitions: the **CrossInk** e-reader
> (default) and the **InkStorm** weather app, installed side by side on a single
> device with partition-switching boot.
>
> Built on [CrossInk V1.5.0](https://github.com/uxjulia/CrossInk) by
> [uxjulia](https://github.com/uxjulia), which itself is built on
> [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
> by Alistair Shepherd.

<p align="center">
  <img src="./docs/images/inkstorm-logo.png" alt="InkStorm Logo" width="200"/>
</p>

## What is this?

This is a **dual-boot firmware** for e-ink readers. A single universal binary is
flashed to two partitions on the device:

| Partition | Offset   | App      | What it does                     |
|-----------|----------|----------|----------------------------------|
| `app0`    | `0x10000`   | CrossInk | The full CrossInk e-reader (default) |
| `app1`    | `0x650000`  | InkStorm | The InkStorm weather app         |

Both partitions are required — **the two firmware files only work together**.
Flash both, and the device boots into CrossInk by default.

- From the **CrossInk home menu**, choose **Weather** to boot the weather app.
- From the **InkStorm home menu**, choose **Launch CrossInk** to return to the reader.
- On the weather partition, **sleep is fully disabled**; the **power button is a manual weather refresh** (short tap refreshes, long press is ignored).

### Modified for InkStorm

The CrossInk partition is **not stock upstream CrossInk** — it is modified
specifically for dual-boot use with InkStorm. The reader functionality itself is
otherwise unchanged, but the build adds:

- A **Weather** menu item on the CrossInk home menu, which switches to the
  InkStorm partition (`app1`) and boots the weather app.
- Partition-switching helpers so either app can hand off to the other
  (`app0` ⇄ `app1`).
- A dual-app partition table (`app0` = CrossInk, `app1` = InkStorm).
- The `universal` build subsets the reading fonts (omitting emoji/CJK fallbacks
  and the largest font sizes) to fit the 6.5 MB `app0` partition while keeping
  the full 10–16 pt reading range.

### Supported Devices

- Xteink X3
- Xteink X4
- Seeed Studio Sticky (universal build auto-detects at boot)

## Weather App Features

The InkStorm Weather app provides comprehensive local weather information directly on your e-ink device:

- **Dual Provider Support** — Choose between [Open-Meteo](https://open-meteo.com/) (no API key required) or [OpenWeatherMap](https://openweathermap.org/) (requires free API key).
- **Current Conditions** — Temperature, feels-like, humidity, wind speed & direction, visibility, UV index, pressure, dew point, and cloud cover.
- **Hourly Forecast** — 24-hour forecast with temperature, precipitation probability, and conditions.
- **Daily Forecast** — 7-day outlook with high/low temperatures, weather conditions, and precipitation chances.
- **Air Quality Index** — Real-time AQI with pollutant breakdowns (PM2.5, PM10, O₃, NO₂, SO₂, CO) and health category descriptions.
- **Weather Alerts** — Severe weather alerts with full descriptions and timing.
- **Auto-Refresh** — Configurable automatic refresh intervals (Off, Manual, or every 1–6 hours) with smart cooldown after failed fetches.
- **Manual Refresh** — A short tap of the **power button** refreshes the forecast at any time (long press is ignored).
- **Temperature Units** — Toggle between Celsius and Fahrenheit.
- **Location Selection** — Search for any city worldwide by name.
- **Error Handling** — Clear error messages when weather data is unavailable, with automatic retry and provider-fallback logic.

### CrossInk reader highlights (from the upstream fork)

The CrossInk partition keeps the full CrossInk V1.5.0 feature set: new reader
fonts (Lexend Deca and Bitter), Unicode emoji and symbol support, multiple
reader font sizes, strikethrough and underline support, custom themes,
bookmarks, in-reader button remapping, Bionic Reading and Guide Dots modes,
reading stats and cross-device sync, custom auto page-turn interval, and a
Recent Books grid. See [Reader Features](./docs/reader-features.md).

---

## Installation

> **Important:** USB-locked devices (devices with USB data transfer disabled,
> SD-only updates) **cannot** install this dual-boot firmware. See
> [Locked devices](#locked-devices) below.

### Option 1: Online Flasher (Recommended)

Use the **CrossPoint online flasher** — no software to install:

1. Download **both** firmware files from the
   [Releases page](https://github.com/SkyWalker541/InkStorm/releases).
2. Open the flasher: **[crosspointreader.com/#flash-tools](https://crosspointreader.com/#flash-tools)**
3. Connect your device via USB and select your device model.
4. Flash **`firmware-app0-crossink-v1.0.0.bin`** (Custom Firmware).
5. Flash **`firmware-app1-inkstorm-v1.0.0.bin`** (Custom Firmware).
6. Done — the device reboots into CrossInk.

> **Note:** After flashing, the device may briefly show a boot diagnostic
> message (e.g., "rst:" or "crash dump"). This is normal ESP32 behavior after a
> firmware update and will resolve within a few seconds.

### Option 2: Command Line (esptool)

Install `esptool`, download **both** firmware files, and flash each partition:

```sh
# app0 — CrossInk (reader)
esptool.py --chip esp32c3 --port /dev/cu.usbmodem1101 --baud 921600 \
  write_flash 0x10000 firmware-app0-crossink-v1.0.0.bin

# app1 — InkStorm (weather)
esptool.py --chip esp32c3 --port /dev/cu.usbmodem1101 --baud 921600 \
  write_flash 0x650000 firmware-app1-inkstorm-v1.0.0.bin
```

Replace the port with your device's port (`ls /dev/cu.*` on macOS,
`dmesg | grep tty` on Linux).

### Option 3: PlatformIO (USB — for developers)

```sh
git clone https://github.com/SkyWalker541/InkStorm.git
cd InkStorm
pio run -e universal
# flash app0
pio run -e universal --target upload
# flash app1
~/.platformio/packages/tool-esptoolpy/esptool.py \
  --port /dev/cu.usbmodem1101 --baud 921600 \
  write_flash 0x650000 .pio/build/universal/firmware-universal.bin
```

The `universal` environment is the default and auto-detects X3/X4 at boot.

See [Installation](./docs/installation.md) for full instructions.

## Locked Devices

Devices with **USB data transfer disabled** can only update via the SD card.
Because this firmware needs **two separate partition flashes**, it **cannot** be
installed on a USB-locked device with the standard SD-card update flow. If your
device is USB-locked, keep using the upstream
[CrossInk](https://github.com/uxjulia/CrossInk) releases instead.

---

## Documentation

- [Installation](./docs/installation.md)
- [User Guide](./docs/user-guide.md)
- [Reader Features](./docs/reader-features.md)
- [Controls](./docs/controls.md)
- [SD Card Fonts](./docs/sd-card-fonts.md)
- [Dictionary](./docs/dictionary.md)
- [Data Cache](./docs/data-cache.md)
- [Web server usage](./docs/webserver.md)
- [Common issues](./docs/troubleshooting.md)
- [Project scope](./SCOPE.md)

## Development quick start

InkStorm uses PlatformIO for building and flashing firmware.

```sh
# Build the universal (X3/X4 auto-detect) firmware
pio run -e universal
```

Nix/NixOS users can enter the development shell with either `nix develop`
(flakes) or `nix-shell nix`.

See [Getting Started](./docs/development/getting-started.md) and
[Testing and Debugging](./docs/development/testing-debugging.md) for details.

## Repository layout

- `src/` - app orchestration, settings/state, and activity implementations (home, reader, weather, settings, network, boot/sleep)
- `lib/` - supporting libraries: EPUB parsing/layout, fonts, i18n, filesystem helpers, HAL wrappers, and more
- `freeink-sdk/` - hardware SDK submodule for display, input, storage, and battery (docs: https://freeink.org/docs)
- `web/` - web portal sources; compiled by `scripts/build_web.py` into `src/network/html/*.generated.h`
- `docs/` - user and developer documentation
- `test/` - unit tests and EPUB test fixtures
- `scripts/` - build, codegen, and release tooling
- `bin/` - helper scripts for formatting and CI checks
- `nix/` - Nix/NixOS development shell definitions
- `managed_components/` - ESP-IDF managed component dependencies

## Credits

- **[CrossInk](https://github.com/uxjulia/CrossInk)** by
  [uxjulia](https://github.com/uxjulia) — The base firmware (V1.5.0) this
  project is built on. All CrossInk reader features are preserved.
- **[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)**
  by Alistair Shepherd — The original ESP32-C3 e-ink reader firmware.

This project exists because of the excellent work done by both upstream
projects. If you find it useful, please consider supporting them.

**Note:** This project was developed with AI-assisted programming under human
direction.

## Notice on Contributions

This repository does not accept pull requests. Feature requests may be opened
in [discussions](https://github.com/SkyWalker541/InkStorm/discussions), but
major features requiring ongoing support should be directed upstream to
[CrossInk](https://github.com/uxjulia/CrossInk) or
[CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader).
