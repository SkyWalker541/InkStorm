# InkStorm

> **Dual-boot firmware for e-ink readers** — the full
> [CrossInk](https://github.com/uxjulia/CrossInk) e-reader on `app0` and the
> **InkStorm** weather app on `app1`, installed side by side on one device.

<p align="center">
  <img src="./docs/images/inkstorm-logo.png" alt="InkStorm Logo" width="200"/>
</p>

## What is this?

One universal binary, flashed to two partitions:

| Partition | Offset    | App      | What it does                       |
|-----------|-----------|----------|------------------------------------|
| `app0`    | `0x10000` | CrossInk | The full CrossInk e-reader (default boot) |
| `app1`    | `0x650000` | InkStorm | The InkStorm weather app           |

Both partitions are required — **the two firmware files only work together.**

- From the **CrossInk home menu**, choose **Weather** to boot the weather app.
- From the **InkStorm home menu**, choose **Launch CrossInk** to return to the reader.
- On the weather partition, **sleep is disabled** and the **power button** is a
  free control — a short tap and a long press can each be set to refresh the
  weather, launch CrossInk, or do nothing.

### What's changed from CrossInk

The CrossInk partition is upstream CrossInk V1.5.0 with the minimal additions
needed for dual boot:

- A **Weather** menu item on the CrossInk home menu that boots `app1`.
- Partition-switching helpers so either app can hand off to the other (`app0` ⇄ `app1`).
- A dual-app partition table.
- The `universal` build subsets the reading fonts to fit the 6.5 MB `app0`
  partition (the full 10–16 pt reading range is kept).

Everything else — reader features, fonts, controls, sync, supported devices — is
**unchanged from upstream**. See the [CrossInk repo](https://github.com/uxjulia/CrossInk).

## InkStorm Weather

The InkStorm weather app shows local weather directly on your e-ink device.
Weather data comes from **[Open-Meteo](https://open-meteo.com/) — it is already
configured in this build, with **no API key needed**. Just open the weather
app's settings and enter your **city or ZIP code** to get started.

- **Live conditions** — big current temperature, condition (icon + text,
  day/night), feels-like, humidity, wind speed, UV index, pressure,
  sunrise/sunset, and air quality (AQI).
- **Daily forecast** — a 5-day strip (today + 4 days) with condition icon and
  high/low temperatures.
- **Extra readout** — pick one for the last grid cell: dew point, cloud cover,
  visibility, wind gust, or precipitation.
- **Auto-refresh** — off, manual, or every 30 seconds to 24 hours, with a
  cooldown after failures.
- **Manual refresh** — the back button, the menu's "Update now", or the power
  button.
- **Units & layout** — °C/°F, 12/24-hour clock, km/h or mph wind, portrait or
  landscape, city search.

## Installation

> **USB-locked devices** (USB data transfer disabled) cannot install dual-boot
> firmware. Keep using the upstream
> [CrossInk](https://github.com/uxjulia/CrossInk) releases instead.

### Option 1 — Online flasher (recommended)

No software to install. Open
**[InkStorm flasher](https://skywalker541.github.io/InkStorm/flash.html)** in
**Chrome or Microsoft Edge** on a desktop/laptop and follow the on-screen steps.

> **Browser support:** the flasher uses the browser's Web Serial API, which only
> Chrome and Microsoft Edge support. **Safari, Firefox, and phones/tablets will
> not work.**

The flasher downloads both firmware files, verifies them, and writes CrossInk to
`app0` and InkStorm to `app1` automatically.

### Option 2 — Command-line (esptool)

Download **both** files from the
[Releases page](https://github.com/SkyWalker541/InkStorm/releases), then flash
each partition:

```sh
esptool.py --chip esp32c3 --port /dev/cu.usbmodem1101 --baud 921600 \
  write_flash 0x10000 firmware-app0-crossink-v1.0.0.bin   # CrossInk (reader)
esptool.py --chip esp32c3 --port /dev/cu.usbmodem1101 --baud 921600 \
  write_flash 0x650000 firmware-app1-inkstorm-v1.0.0.bin  # InkStorm (weather)
```

Replace the port with your device's port (`ls /dev/cu.*` on macOS).
See [Installation](./docs/installation.md) for full instructions.

## Documentation

- [Installation](./docs/installation.md)
- [User Guide](./docs/user-guide.md)
- [Reader Features](./docs/reader-features.md)
- [Controls](./docs/controls.md)
- [Troubleshooting](./docs/troubleshooting.md)

All other documentation applies unchanged from upstream — see the
[CrossInk docs](https://github.com/uxjulia/CrossInk/tree/main/docs).

## Development

Build the universal (X3/X4 auto-detect) firmware with PlatformIO:

```sh
pio run -e universal
```

See [Getting Started](./docs/development/getting-started.md) and
[Testing & Debugging](./docs/development/testing-debugging.md).

## Credits

This project builds on the work of others:

- **[CrossInk](https://github.com/uxjulia/CrossInk)** by
  [uxjulia](https://github.com/uxjulia) — the base firmware (V1.5.0) and the
  reader you boot into.
- **[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)**
  by Alistair Shepherd and the CrossPoint community — the original ESP32-C3
  e-reader firmware that CrossInk is forked from.
- **[diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader)**
  by atomic14 — the project that inspired CrossPoint.
- **[Open-Meteo](https://open-meteo.com/)** — weather data for the
  InkStorm app.
- **[ESP-IDF](https://github.com/espressif/esp-idf)** by Espressif — the chip
  and firmware SDK.
- **[PlatformIO](https://platformio.org/)** — the build system.

If you find this useful, please consider supporting the upstream projects.

**Note:** This project was developed with AI-assisted programming under human
direction.

## Contributions

This repository does not accept pull requests. Feature requests may be opened in
[discussions](https://github.com/SkyWalker541/InkStorm/discussions); features
requiring ongoing support should be directed upstream to
[CrossInk](https://github.com/uxjulia/CrossInk) or
[CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader).
