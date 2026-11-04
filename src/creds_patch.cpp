#include "creds_patch.h"

#include <string.h>
#include <string>
#include <cJSON.h>

// field of the patch: missing, null (also ""), or a string
enum class Field { MISSING, NUL, STRING };

static const char *field(const cJSON *json, const char *key, Field *kind, const char **value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    *value = "";
    if (!item) {
        *kind = Field::MISSING;
    } else if (cJSON_IsNull(item)) {
        *kind = Field::NUL;
    } else if (cJSON_IsString(item)) {
        *value = item->valuestring;
        *kind  = (*value[0] == 0) ? Field::NUL : Field::STRING;
    } else {
        return "ssid, password and ota_token must be strings or null";
    }
    return NULL;
}

const char *creds_patch(creds_t *io, const char *body, size_t len, creds_changes_t *changes)
{
    *changes = {};

    // empty body: nothing to change
    size_t i = 0;
    while ((i < len) && ((body[i] == ' ') || (body[i] == '\t') || (body[i] == '\r') || (body[i] == '\n'))) {
        ++i;
    }
    if (i == len) {
        return NULL;
    }
    if (body[i] != '{') {
        return "Body must be a JSON object";
    }

    std::string text(body + i, len - i);  // zero terminated copy
    cJSON *json = cJSON_Parse(text.c_str());
    if (!cJSON_IsObject(json)) {
        cJSON_Delete(json);
        return "Invalid JSON";
    }

    Field ssid_kind, password_kind, token_kind;
    const char *ssid, *password, *token;
    const char *error = field(json, "ssid", &ssid_kind, &ssid);
    if (!error) error = field(json, "password", &password_kind, &password);
    if (!error) error = field(json, "ota_token", &token_kind, &token);

    if (!error && (strlen(ssid) > CREDS_SSID_SIZE_MAX)) {
        error = "ssid must have at most 32 bytes";
    }
    const size_t password_len = strlen(password);
    if (!error && ((password_len > CREDS_PASSWORD_SIZE_MAX) || ((password_len > 0) && (password_len < 8)))) {
        error = "password must have 8 to 64 characters (or be empty)";
    }
    if (!error) {
        // the token goes into a header line: printable ASCII only
        bool valid = (strlen(token) <= CREDS_TOKEN_SIZE_MAX);
        for (const char *c = token; valid && *c; ++c) {
            valid = (*c > ' ') && (*c <= '~');
        }
        if (!valid) {
            error = "ota_token must have at most 400 printable characters";
        }
    }

    // resulting WLAN credentials. ssid and password are a pair: ssid without password is an open WLAN
    creds_t result = *io;
    if (!error && ((ssid_kind != Field::MISSING) || (password_kind != Field::MISSING))) {
        if (ssid_kind != Field::MISSING) {
            strcpy(result.ssid, ssid);  // "" for null
        }
        strcpy(result.password, password);  // "" for null or missing
        if ((result.ssid[0] == 0) && (result.password[0] != 0)) {
            error = "password without ssid (no WLAN stored)";
        }
        changes->wlan = true;
    }
    if (!error && (token_kind != Field::MISSING)) {
        strcpy(result.ota_token, token);  // "" for null
        changes->token = true;
    }
    cJSON_Delete(json);

    if (error) {
        *changes = {};
        return error;
    }
    *io = result;
    return NULL;
}
