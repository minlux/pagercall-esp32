#pragma once
#include <stddef.h>
#include "creds_patch.h"

/* ---------------------------------------------------------------------------
 * Credentials in the NVS (namespace "pagercall"): ssid, password, ota_token.
 * The NVS is not touched by an OTA update.
 *
 * REST (in the setup portal and in normal operation), same as Candela and SGW,
 * see creds_patch.h for the rules:
 *
 *   curl -X PUT -d '{ "ssid": "Gammablitz", "password": "geheim123" }' http://<ip>/credentials
 *   curl -X PUT -d '{ "ota_token": "eyJ..." }' http://<ip>/credentials    # token only
 *   curl -X PUT -d '{ "ota_token": null }' http://<ip>/credentials        # delete the token
 *   curl -X DELETE http://<ip>/credentials                                # delete everything
 *
 * WLAN credentials take effect after the next restart, the token immediately.
 * --------------------------------------------------------------------------*/

class WebServer;

void creds_begin(void);
void creds_set(const char *key, const char *value);   // "" deletes the key
void creds_get(const char *key, char *buf, size_t len); // "" if not stored

void creds_load(creds_t *creds);

// apply a PUT /credentials body (see creds_patch.h), store the changes.
// returns NULL on success, otherwise the reason (nothing is stored then)
const char *creds_apply(const char *body, size_t len);

void creds_erase(void);                                // DELETE /credentials

// PUT /credentials and DELETE /credentials on the given server
void creds_register(WebServer &server);

static inline void creds_set_ssid    (const char *val)              { creds_set("ssid",     val); }
static inline void creds_set_password(const char *val)              { creds_set("password", val); }

static inline void creds_get_ssid    (char *buf, size_t len)        { creds_get("ssid",     buf, len); }
static inline void creds_get_password(char *buf, size_t len)        { creds_get("password", buf, len); }
static inline void creds_get_ota_token(char *buf, size_t len)       { creds_get("ota_token", buf, len); }
