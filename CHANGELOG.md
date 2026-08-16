# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [v1.0.0] - 2026-08-11

Initial release.

CrossInk v1.5.0 / InkStorm v1.0.0 Dual Boot — a single universal firmware that
installs to both the CrossInk (reader) and InkStorm (weather) partitions of one
device, with partition-switching boot.

- **Dual boot** — CrossInk (reader, default) on `app0`; InkStorm (weather) on `app1`.
- **Weather app** — current conditions, a 5-day forecast strip, air quality,
  and per-block refresh intervals, powered by Open-Meteo.
- **Partition switching** — the InkStorm home menu can launch CrossInk, and the
  reader home menu can launch the weather app; sleep always returns to CrossInk.
- **No sleep in weather** — deep sleep is fully disabled on the weather partition;
  the power button is a configurable control (short tap and long press can each
  refresh the weather, launch CrossInk, or do nothing).
- **New InkStorm logo** and per-partition boot splash.
- **Universal firmware** — one binary auto-detects Xteink X3/X4 at boot.

Upstream: built on CrossInk v1.5.0 (by uxjulia) and CrossPoint Reader (by
Alistair Shepherd).
