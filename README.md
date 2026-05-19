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

The default baud rate is 115200. The monitor can also be combined with upload in one step:

```bash
pio run --target upload && pio device monitor
```

## First-time setup

The first build will automatically download all declared `lib_deps` (including the SX1262 driver) into `.pio/libdeps/`. No manual library installation is needed.
