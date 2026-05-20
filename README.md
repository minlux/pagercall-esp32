# PagerCall ESP32

## Prerequisites

- [PlatformIO](https://platformio.org/install/cli) installed (installs into `~/.platformio/`)
- Python 3.x

## Building from the command line

PlatformIO's `pio` binary lives inside its own virtual environment and is not on `PATH` by default. Activate it once per shell session:

```bash
source ~/.platformio/penv/bin/activate
```

Then build:

```bash
pio run
```

To build **and** flash to the connected board:

```bash
pio run --target upload
```

PlatformIO auto-detects the port. If detection fails or you have multiple devices connected, specify the port explicitly:

```bash
pio run --target upload --upload-port /dev/ttyACM0
```

The Heltec WiFi LoRa 32 V3 uses the ESP32-S3's built-in USB CDC and typically appears as `/dev/ttyACM0` on Linux. If the device is not found, check `dmesg` after plugging in:

```bash
dmesg | tail -5
```

On Linux you may need to add your user to the `dialout` group to access the port without `sudo`:

```bash
sudo usermod -aG dialout $USER   # log out and back in to take effect
```

To open the serial monitor after flashing:

```bash
pio device monitor
```

The default baud rate is 115200. To specify a different baud rate explicitly:

```bash
pio device monitor --baud 115200
```

The monitor can also be combined with upload in one step:

```bash
pio run --target upload && pio device monitor
```

## Flashing a pre-built firmware.bin

To flash an existing `firmware.bin` without rebuilding, use `esptool.py` directly:

```bash
~/.platformio/packages/tool-esptoolpy/esptool.py \
    --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
    write_flash 0x10000 firmware.bin
```

The bootloader and partition table are already on the device, so flashing at offset `0x10000` (the app partition) is sufficient.

To flash the complete bundle (bootloader + partition table + firmware) — e.g. on a blank chip:

```bash
~/.platformio/packages/tool-esptoolpy/esptool.py \
    --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
    write_flash \
    --flash_mode qio --flash_freq 80m --flash_size 8MB \
    0x0      bootloader.bin \
    0x8000   partitions.bin \
    0x10000  firmware.bin
```

How the offsets and flash parameters were determined:

- **`0x0` (bootloader)** — fixed by the ESP32-S3 ROM; the boot ROM always loads the bootloader from the start of flash.
- **`0x8000` (partition table)** — ESP32 convention; the IDF and Arduino core always place the partition table at this address.
- **`0x10000` (firmware)** — read from `partitions.csv` in this repo: the `app0` entry has `Offset = 0x10000`.
- **`--flash_mode qio`, `--flash_freq 80m`** — read from `.platformio/platforms/espressif32/boards/heltec_wifi_lora_32_V3.json` (`flash_mode` and `f_flash` fields).

## Build output

After `pio run` the compiled firmware is placed in:

```
.pio/build/heltec_wifi_lora_32_V3/firmware.bin
```

This file is used for over-the-air updates via the `PUT /firmware` endpoint:

```bash
curl -X PUT http://<device-ip>/firmware \
     -H "Content-Type: application/octet-stream" \
     --data-binary @.pio/build/heltec_wifi_lora_32_V3/firmware.bin
```

## First-time setup

The first build will automatically download all declared `lib_deps` (including the SX1262 driver) into `.pio/libdeps/`. No manual library installation is needed.
