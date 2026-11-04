#pragma once
#include <stddef.h>
#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * PUT /credentials: apply a JSON body to the stored credentials. Same behaviour
 * as Candela and the SGW (pvmonitor):
 *
 *   - The body is a JSON object with "ssid", "password" and "ota_token". The
 *     Content-Type is not checked. An empty body is allowed and changes nothing.
 *   - Each field is optional, like a JSON merge patch: missing keeps the stored
 *     value, a string sets it, null deletes it. "" is the same as null.
 *     Fields of another type: error. Unknown fields are ignored.
 *   - Except the password: if "ssid" is given (also null) without "password",
 *     the password is deleted (open WLAN). "password" without "ssid" changes the
 *     password only (error, if no ssid is stored).
 *   - ssid at most 32 bytes, password empty or 8..64 characters, ota_token at
 *     most 400 printable ASCII characters (0x21..0x7e).
 *   - On an error nothing is changed.
 *
 * No NVS or HTTP here (see credentials.cpp), so this can be tested on the PC.
 * --------------------------------------------------------------------------*/

#define CREDS_SSID_SIZE_MAX      32
#define CREDS_PASSWORD_SIZE_MAX  64
#define CREDS_TOKEN_SIZE_MAX     400

typedef struct {
    char ssid[CREDS_SSID_SIZE_MAX + 1];
    char password[CREDS_PASSWORD_SIZE_MAX + 1];
    char ota_token[CREDS_TOKEN_SIZE_MAX + 1];
} creds_t;

typedef struct {
    bool wlan;    // ssid and/or password changed
    bool token;   // ota_token changed
} creds_changes_t;

// apply body (len bytes, need not be zero terminated) to *io.
// returns NULL on success, otherwise the reason (*io is unchanged then)
const char *creds_patch(creds_t *io, const char *body, size_t len, creds_changes_t *changes);
