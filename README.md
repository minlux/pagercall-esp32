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

To open the serial monitor after flashing:

```bash
pio device monitor
```

## First-time setup

The first build will automatically download all declared `lib_deps` (including the SX1262 driver) into `.pio/libdeps/`. No manual library installation is needed.
