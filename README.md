# PagerCall ESP32

Firmware for the [Heltec WiFi LoRa 32 V3](https://heltec.org/project/wifi-lora-32-v3/) (ESP32-S3 + SX1262) that calls a [Retekess TD157](https://www.retekess.com/restaurant-pager-system/TD157.html) restaurant pager over 433.92 MHz OOK via a simple REST API.

This project is a hardware variant of [pagercall](https://github.com/minlux/pagercall), which does the same thing on different hardware. The advantage here is that the Heltec board is inexpensive, widely available, and can be bought ready-to-use — including from AliExpress with a compact housing included.

## Hardware

- [Heltec WiFi LoRa 32 V3](https://heltec.org/project/wifi-lora-32-v3/) — ESP32-S3 with onboard SX1262 LoRa transceiver and 0.96" OLED display

## Hardware modification

The SX1262 does not support OOK natively (see `doc/findings.md` for a detailed investigation of what is and is not possible on this chip). The approach used here is:

1. Put the SX1262 into continuous-wave (CW) TX mode at the start of a call — this keeps the carrier running at full power for the duration of the transmission.
2. Control the antenna switch with a GPIO pin of the ESP32 to turn the RF output on and off, producing the OOK pattern. The ESP32's UART TX signal (inverted, 4800 baud, 6N1) is used for this — no bit-banging required. The UART hardware generates the exact per-bit timing, and the start/stop framing bits align naturally with the OOK symbol boundaries.

The SX1262 has an onboard antenna switch control line (`ANT_SW_CTRL`) that is normally driven by the chip itself via DIO2. To take over control from the ESP32:

- **Connect ESP32 GPIO 7 to SX1262 DIO2.** This feeds the UART TX signal directly onto the antenna switch control line.
- The firmware disables the SX1262's DIO2 output driver so that the chip and the ESP32 do not fight each other on that line. The `SetDio2AsRfSwitchCtrl` feature is left disabled for the same reason.

In summary: solder a single wire from **GPIO 7** to **DIO2**.

## Usage

Once the device is configured and connected to Wi-Fi, trigger a pager call with a single HTTP request:

```bash
curl http://<device-ip>/pagercall/rtd157-<keyboard>.<pager>
# Example: call pager 1 on keyboard 274
curl http://<device-ip>/pagercall/rtd157-274.1
```

The device repeats the RF telegram 32 times and returns `200 OK` immediately (the transmission runs in the background).

## First-time Wi-Fi setup

On first boot (no credentials stored), the device opens a captive portal access point named `PagerCall-Setup`. Connect to it, enter your Wi-Fi credentials, and the device reboots and joins your network. Hold the button for 3 seconds at startup to re-enter provisioning mode.

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
pio run --target upload --upload-port /dev/ttyUSB1
```

The Heltec WiFi LoRa 32 V3 uses the ESP32-S3's built-in USB CDC and typically appears as `/dev/ttyUSB0` on Linux. If the device is not found, check `dmesg` after plugging in:

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
pio device monitor --baud 115200 --port /dev/ttyUSB1
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
