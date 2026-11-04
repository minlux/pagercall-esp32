#pragma once
#include <Arduino.h>

/* ---------------------------------------------------------------------------
 * Firmware update via the update server Souper.
 *
 * If a token is stored (PUT /credentials with "ota_token"), PagerCall regularly asks
 * Souper, whether an update is pending for it, and installs it by itself:
 *
 *   PATCH <url>/assets/api/target/PagerCall_xx-xx-xx-xx-xx-xx  system info -> { "poll": 3600, "updates": [ 17 ] }
 *   GET   <url>/assets/api/update/17                         -> { "merker": 0, "file": { "src": .., "dst": .., "size": .. } }
 *   GET   <file.src>                                         the signed image (firmware.signed.bin)
 *   PATCH <url>/assets/api/update/17                         { "status": 2, "merker": 0, "log": ".." }
 *
 * <url> is fixed at build time (SOUPER_URL in platformio.ini; test server: pio run -e
 * heltec_wifi_lora_32_V3_test), so nobody in the WLAN can redirect PagerCall (and the token)
 * to another server. All requests with "Authorization: Bearer <token>". The image is only
 * activated with a valid signature, as with PUT /firmware (see ota_signature.h).
 *
 * dst must start with "firmware". "firmware.dryrun" downloads and checks the image, but does
 * not activate it.
 *
 * Status of an update: 2 installed, 1 failed (retry with the next poll, "merker" counts the
 * attempts), -2 failed finally (after the 3rd attempt, or if a retry cannot help).
 *
 * Runs in its own FreeRTOS task, so the blocking requests and the download do not stall
 * loop() (web server, pager calls). The restart into a new firmware is done in loop()
 * (ota_restart_loop()), as soon as no pager call is pending.
 * --------------------------------------------------------------------------*/

class WebServer;

void   souper_begin(void);              // start the task (normal operation only)
void   souper_register(WebServer &server); // GET /ota, POST /ota/poll
void   souper_token_changed(void);      // the token was set or deleted (poll soon)
void   souper_poll_now(void);           // POST /ota/poll

// state as JSON, for GET /ota:
//   { "token": true, "url": "http://..", "name": "PagerCall_..", "last_poll": 120 or null,
//     "next_poll": 3480 or null, "result": "ok", "pending": [ 17 ],
//     "state": "idle" | "update" | "download" | "report" | "restart", "progress": 45 (only while downloading) }
//   times in seconds, relative to now (there is no clock)
String souper_state_json(void);

// download progress in percent, -1 if no download is running (for the display)
int    souper_progress(void);
