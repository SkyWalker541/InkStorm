---
title: Installation
nav_order: 2
---

# Installation

This firmware is a **dual boot**: the same binary is flashed to two partitions
on the device — **CrossInk** (the e-reader, default) and **InkStorm** (the
weather app). **Both partitions are required and only work together.**

| Partition | Offset   | Firmware file                  | App |
|-----------|----------|--------------------------------|-----|
| `app0`    | `0x10000`   | `firmware-app0-crossink-v1.0.0.bin`  | CrossInk e-reader (default) |
| `app1`    | `0x650000`  | `firmware-app1-inkstorm-v1.0.0.bin`  | InkStorm weather app |

## Supported Devices

- Xteink X3, X4
- Seeed Studio Sticky (universal build auto-detects at boot)

## Web Installation via USB

The web flasher flashes one partition at a time, so run it twice:

1. Navigate to [https://crosspointreader.com/#flash-tools](https://crosspointreader.com/#flash-tools) and select your device model.
2. Choose **Custom Firmware** and upload **`firmware-app0-crossink-v1.0.0.bin`** at offset **`0x10000`**.
3. Click **Flash Firmware** and wait for completion.
4. Repeat with **`firmware-app1-inkstorm-v1.0.0.bin`** at offset **`0x650000`**.
5. The device reboots into CrossInk.

> **Note:** After flashing, the device may briefly show a boot diagnostic
> message (e.g., "rst:" or "crash dump"). This is normal ESP32 behavior after a
> firmware update and will resolve within a few seconds.

## Command Line

These instructions are for macOS and Linux. Windows users should use the web
installer.

Install `esptool`:

```sh
pip3 install esptool
```

Download **both** firmware files from the
[releases page](https://github.com/SkyWalker541/InkStorm/releases), then connect
your device with USB-C.

Find the device port:

```sh
# Linux
dmesg | grep tty

# macOS
ls /dev/cu.*
```

Flash both partitions:

```sh
# app0 — CrossInk (reader)
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x10000 firmware-app0-crossink-v1.0.0.bin

# app1 — InkStorm (weather)
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x650000 firmware-app1-inkstorm-v1.0.0.bin
```

macOS example, replacing the port with your device's port:

```sh
esptool.py --chip esp32c3 --port /dev/cu.usbmodem1101 --baud 921600 \
  write_flash 0x10000 firmware-app0-crossink-v1.0.0.bin
esptool.py --chip esp32c3 --port /dev/cu.usbmodem1101 --baud 921600 \
  write_flash 0x650000 firmware-app1-inkstorm-v1.0.0.bin
```

## SD Card Firmware Update

**Not supported for this dual-boot firmware.** The SD-card update flow flashes
a single partition and cannot install the second partition this firmware
requires.

## USB Locked Devices

**Not supported.** If your device has USB data transfer disabled (SD-only
updates), this dual-boot firmware **cannot** be installed, because it requires
two separate partition flashes over USB. Keep using the upstream
[CrossInk](https://github.com/uxjulia/CrossInk) releases on locked devices.

## How to switch between partitions

Once both partitions are flashed, you don't reflash to switch:

- **CrossInk → InkStorm:** From the CrossInk home menu, open the menu and
  choose **InkStorm Weather**. The device reboots into the weather app.
- **InkStorm → CrossInk:** From the InkStorm home menu, choose
  **Launch CrossInk**. The device reboots into the reader.
- **Sleep:** Sleeping from the weather app always wakes back into CrossInk.

## Weather partition behavior

- **Sleep is disabled** on the weather partition. The device never auto-sleeps
  while the weather app is open.
- **Power button = manual refresh.** A short tap refreshes the forecast; a long
  press is ignored.
