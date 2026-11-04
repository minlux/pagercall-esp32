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

On first boot (no credentials stored), the device opens a captive portal access point named `PagerCall-Setup`. Connect to it, enter your Wi-Fi credentials (and, optionally, the token for the [update server](#update-via-the-update-server-souper)), and the device reboots and joins your network. Hold the button for 3 seconds at startup to re-enter provisioning mode.

The credentials can also be set via REST, in the portal (`http://192.168.4.1`) and in normal operation, see [Credentials](#credentials-put-credentials). After `DELETE /credentials` and a restart, the device opens the portal again.

## HTTP API

| Request | Description |
|---|---|
| `GET /pagercall/rtd157-<keyboard>.<pager>` | call a pager (see [Usage](#usage)). `503` while a restart into a new firmware is pending |
| `GET /version` | firmware version, e.g. `{"version":"1.000","git":"v1.000-2-g1a2b3c4","build":"2026-11-04 12:00"}` |
| `PUT /credentials` | set Wi-Fi credentials and/or the update token, see [Credentials](#credentials-put-credentials) |
| `DELETE /credentials` | delete Wi-Fi credentials and update token |
| `PUT /firmware` | firmware update, signed image as body, see [Update via Wi-Fi](#update-via-wi-fi-put-firmware) |
| `GET /ota` | state of the update server, e.g. `{"token":true,"url":"http://minlux.de/souper","name":"PagerCall_48-ca-43-01-02-03","last_poll":120,"next_poll":3480,"result":"ok","pending":[],"state":"idle"}`. Times in seconds relative to now; `state` is `idle`, `update`, `download` (then with `progress` in %), `report` or `restart`. The token itself is never returned |
| `POST /ota/poll` | ask the update server now |
| `GET /reset` | restart (Wi-Fi credentials take effect after a restart) |

`PUT`/`DELETE /credentials` and `GET /reset` are also available in the setup portal.

## Credentials (`PUT /credentials`)

Same behaviour as [Candela](https://github.com/minlux/candela) and the SGW ([pvmonitor](https://github.com/minlux/pvmonitor)):

- The body is a JSON object with `ssid`, `password` and `ota_token`. The `Content-Type` is not checked. An empty body is allowed and changes nothing.
- Each field is optional, like a JSON merge patch: missing keeps the stored value, a string sets it, `null` (or `""`) deletes it.
- Except the password: `ssid` without `password` deletes the password (open Wi-Fi). `password` without `ssid` changes the password only.
- `ssid` at most 32 bytes, `password` empty or 8–64 characters, `ota_token` at most 400 printable ASCII characters. Otherwise `400` with the reason as text, and nothing is stored.
- Wi-Fi credentials take effect after the next restart (`GET /reset`), the token immediately.

```bash
curl -X PUT -d '{ "ssid": "MyWiFi", "password": "secret123" }' http://<device-ip>/credentials
curl -X PUT -d '{ "ota_token": "eyJ..." }' http://<device-ip>/credentials    # token only, Wi-Fi unchanged
curl -X PUT -d '{ "ota_token": null }' http://<device-ip>/credentials        # delete the token
curl -X DELETE http://<device-ip>/credentials                                # delete everything
curl http://<device-ip>/reset
```

The credentials are stored in the NVS; a firmware update does not touch them.

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
.pio/build/heltec_wifi_lora_32_V3/firmware.bin          # for flashing by cable
.pio/build/heltec_wifi_lora_32_V3/firmware.signed.bin   # for updates via Wi-Fi and the update server
```

## Versioning

The version number is maintained by hand: `PAGERCALL_VERSION_STR` in `include/version.h` (e.g. `1.000`), increment it for every release. `git_version.py` adds the git revision (`git describe --tags --always --dirty`) and the build time. All three are reported by `GET /version`, printed on the serial console at startup and sent to the update server.

## Signed images

OTA updates (via Wi-Fi and via the update server) only accept signed images. After building `firmware.bin`, `sign_firmware.py` signs it with the private key and writes `firmware.signed.bin` next to it:

```
image | signature | length of the signature (uint32, little-endian)
```

The signature is ECDSA P-256 over the SHA256 of the image (DER, 70–72 bytes). Before activating a new image, the device checks it with the public key compiled into the running firmware (`include/ota_key.h`) using mbedtls (`src/ota_signature.cpp`). The same code would also check RSA signatures: the algorithm follows from the key. ECDSA P-256 is used, because on the ESP32 mbedtls includes it anyway (it costs no extra flash) and it is more modern than RSA 2048; the ESP8266 projects (Candela, SGW) use RSA 2048.

- Private key: `signing/private.key` (**not in git, keep a backup!**) or the path in `$PAGERCALL_SIGNING_KEY`. Without it, `pio run` builds only `firmware.bin`: flashable by cable, but not via OTA. If the private key does not match `include/ota_key.h`, the build fails.
- New key pair: see the comment at the top of `include/ota_key.h`. Devices only accept images signed with the key of their running firmware, so install a key change signed with the old key, or flash it by cable.
- Requires `openssl` on the build machine.

## Update via Wi-Fi (`PUT /firmware`)

The partition table (`partitions.csv`) has two app partitions (`app0`, `app1`) for OTA, so no change by cable is needed for that.

```bash
pio run
curl -X PUT -H "Content-Type: application/octet-stream" \
     --data-binary @.pio/build/heltec_wifi_lora_32_V3/firmware.signed.bin http://<device-ip>/firmware
curl http://<device-ip>/version   # after the restart: is the new version running?
```

- On success the device answers `200 OK, firmware installed (...), restarting` and restarts after about 1 s, as soon as all pending pager calls have been sent. Until then new calls are answered with `503`.
- On an error the old firmware keeps running, and the response body gives the reason: `400` (no valid ESP32 image), `403` (no or invalid signature, e.g. `firmware.bin` instead of `firmware.signed.bin`), `409` (an update is already running, also by the update server), `411` (`Content-Length` missing), `413` (image larger than the OTA partition), `415` (`Content-Type` is not `application/octet-stream`), `500` (other error).
- While the image is uploaded, no pager calls are sent (they are queued, up to 16).
- Nothing protects the endpoint: anybody in the Wi-Fi can start an update, but only with an image signed with the private key.
- Before each OTA update, test the same version once by cable: a firmware that starts but has no working Wi-Fi or OTA can only be replaced by cable (there is no automatic rollback).

## Update via the update server (Souper)

If a token is stored, PagerCall regularly asks the update server **Souper** whether a firmware is pending for it, and installs it by itself. Without a token nothing changes.

1. **Create the target in Souper and get its token.** Put **only the token of exactly this target** on the device, never the universal token (target 0): the token goes over the network unencrypted (HTTP); a universal token would allow to create all targets and report their updates. Name of the target: `PagerCall_xx-xx-xx-xx-xx-xx` (MAC, see `name` in `GET /ota`).
2. **Store the token** in the setup portal (field "Update token") or via REST:

   ```bash
   curl -X PUT -d '{ "ota_token": "eyJ..." }' http://<device-ip>/credentials
   ```

3. **Schedule the update:** upload `firmware.signed.bin` to Souper (not `firmware.bin`) with `dst = firmware` and assign it to the target. `dst = firmware.dryrun` only downloads and checks the image, but does not activate it.

How it works:

- Requests (all with `Authorization: Bearer <token>`): `PATCH /target/<name>` with system info (version, git, build, chip, flash, heap, Wi-Fi, uptime) → `{ "poll": 3600, "updates": [ 17 ] }`; then for each pending update `GET /update/<id>`, download of `file.src`, and `PATCH /update/<id>` with the status: `2` installed, `1` failed (retry with the next poll), `-2` failed finally (after the 3rd attempt, or if a retry cannot help).
- First poll 30 s after the Wi-Fi connection, then in the interval given by the server (5 min to 24 h, default 1 h). After errors: retry after 5 min, doubled up to the interval. Token invalid (`401`/`403`): only once a day. After setting the token: poll after 5 s. `POST /ota/poll` polls at once.
- Runs in its own task, so the web server and pager calls keep working during the download. The display shows the progress. The restart into the new firmware waits, until all pending pager calls have been sent.
- The server URL is fixed at build time (`SOUPER_URL` in `platformio.ini`: `http://minlux.de/souper`) and cannot be changed on the device, so nobody in the Wi-Fi can redirect the device (and its token) to another server.
- For tests against a server in the LAN there is the build variant `heltec_wifi_lora_32_V3_test` (`pio run -e heltec_wifi_lora_32_V3_test`, URL in `platformio.ini`, image in `.pio/build/heltec_wifi_lora_32_V3_test/`). `test/souper_mock.py` is a mock of Souper for that (see its header).

## Tests without hardware

`test/host/run.sh` builds and runs host tests on the PC:

- `creds_patch` — the `PUT /credentials` rules (the same cases as for Candela and the SGW),
- `ota_signature` — the signature check with test keys (ECDSA P-256 and RSA 2048: valid, unsigned, other key, other algorithm, changed image or signature, broken trailer) and with the `firmware.signed.bin` of the last `pio run`.

It downloads cJSON 1.7.19 and mbedtls 3.6.5 (the versions of the firmware) once into `test/host/.deps/` (not in git).

## First-time setup

The first build will automatically download all declared `lib_deps` (including the SX1262 driver) into `.pio/libdeps/`. No manual library installation is needed.
