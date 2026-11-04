// Host test of creds_patch() (PUT /credentials), see run.sh
// Same cases as checked for Candela and the SGW, so that all three behave the same.
#include <stdio.h>
#include <string.h>
#include <string>
#include "creds_patch.h"

static int failures = 0;

static creds_t make(const char *ssid, const char *password, const char *token)
{
    creds_t c;
    snprintf(c.ssid, sizeof(c.ssid), "%s", ssid);
    snprintf(c.password, sizeof(c.password), "%s", password);
    snprintf(c.ota_token, sizeof(c.ota_token), "%s", token);
    return c;
}

// apply body to the state, check the result (error == NULL: success expected)
static void check(creds_t *state, const std::string &body, const char *error, const char *ssid, const char *password,
                  const char *token)
{
    const creds_t before = *state;
    creds_changes_t changes;
    const char *result = creds_patch(state, body.data(), body.size(), &changes);
    bool ok = (error == NULL) ? (result == NULL) : ((result != NULL) && (strcmp(result, error) == 0));
    ok = ok && (strcmp(state->ssid, ssid) == 0) && (strcmp(state->password, password) == 0) &&
         (strcmp(state->ota_token, token) == 0);
    if ((result != NULL) && (memcmp(&before, state, sizeof(before)) != 0)) {
        ok = false;  // nothing may change on an error
    }
    printf("%s  %-62.62s -> %s | ssid=\"%s\" password=\"%s\" token=\"%.12s\"\n", ok ? "OK  " : "FAIL", body.c_str(),
           result ? result : "200", state->ssid, state->password, state->ota_token);
    if (!ok) {
        ++failures;
        printf("      expected: %s | ssid=\"%s\" password=\"%s\" token=\"%.12s\"\n", error ? error : "200", ssid, password, token);
    }
}

int main(void)
{
    creds_t c = make("Vereinsheim", "altes-passwort", "");
    const char *TYPE = "ssid, password and ota_token must be strings or null";

    check(&c, "", NULL, "Vereinsheim", "altes-passwort", "");                       // empty body: nothing changes
    check(&c, " \r\n", NULL, "Vereinsheim", "altes-passwort", "");
    check(&c, "{}", NULL, "Vereinsheim", "altes-passwort", "");
    check(&c, "{\"ota_token\":\"eyJabc\"}", NULL, "Vereinsheim", "altes-passwort", "eyJabc");  // token only, WLAN stays
    check(&c, "{\"password\":\"neuespasswort\"}", NULL, "Vereinsheim", "neuespasswort", "eyJabc"); // password only
    check(&c, "{\"ssid\":\"Anderes\"}", NULL, "Anderes", "", "eyJabc");             // ssid without password: open WLAN
    check(&c, "{\"ssid\":\"Drittes\",\"password\":\"geheim123\"}", NULL, "Drittes", "geheim123", "eyJabc");
    check(&c, "{\"password\":null}", NULL, "Drittes", "", "eyJabc");
    check(&c, "{\"ota_token\":null}", NULL, "Drittes", "", "");
    check(&c, "{\"ota_token\":\"eyJabc\"}", NULL, "Drittes", "", "eyJabc");
    check(&c, "{\"ota_token\":\"\"}", NULL, "Drittes", "", "");                     // "" like null
    check(&c, "{\"unknown\":5}", NULL, "Drittes", "", "");                          // unknown fields are ignored
    check(&c, "{\"ssid\":5}", TYPE, "Drittes", "", "");
    check(&c, "{\"ota_token\":true}", TYPE, "Drittes", "", "");
    check(&c, "[1]", "Body must be a JSON object", "Drittes", "", "");
    check(&c, "{\"ssid\":", "Invalid JSON", "Drittes", "", "");
    check(&c, "{\"ssid\":\"x\",\"password\":\"kurz\"}", "password must have 8 to 64 characters (or be empty)", "Drittes", "", "");
    check(&c, "{\"password\":\"" + std::string(65, 'p') + "\"}", "password must have 8 to 64 characters (or be empty)", "Drittes", "", "");
    check(&c, "{\"ssid\":\"x\",\"password\":\"" + std::string(64, 'p') + "\"}", NULL, "x", std::string(64, 'p').c_str(), "");
    check(&c, "{\"ota_token\":\"mit leerzeichen\"}", "ota_token must have at most 400 printable characters", "x", std::string(64, 'p').c_str(), "");
    check(&c, "{\"ota_token\":\"" + std::string(401, 'e') + "\"}", "ota_token must have at most 400 printable characters", "x", std::string(64, 'p').c_str(), "");
    check(&c, "{\"ota_token\":\"" + std::string(400, 'e') + "\"}", NULL, "x", std::string(64, 'p').c_str(), std::string(400, 'e').c_str());
    check(&c, "{\"ssid\":\"012345678901234567890123456789012\"}", "ssid must have at most 32 bytes", "x", std::string(64, 'p').c_str(), std::string(400, 'e').c_str());
    check(&c, "{\"ssid\":\"01234567890123456789012345678901\",\"ota_token\":null}", NULL, "01234567890123456789012345678901", "", "");
    check(&c, "{\"ssid\":\"Sch\\u00fctzenhaus \\\"Gast\\\"\",\"password\":\"geheim123\"}", NULL, "Sch\xc3\xbctzenhaus \"Gast\"", "geheim123", "");
    check(&c, "{\"ssid\":null}", NULL, "", "", "");                                 // delete the WLAN (password too)
    check(&c, "{\"password\":\"geheim123\"}", "password without ssid (no WLAN stored)", "", "", "");
    check(&c, "{\"password\":\"\"}", NULL, "", "", "");
    check(&c, "{\"ssid\":\"Wieder\",\"password\":\"geheim123\",\"ota_token\":\"eyJx\"}", NULL, "Wieder", "geheim123", "eyJx");
    check(&c, "{\"ssid\":\"\",\"ota_token\":\"eyJy\"}", NULL, "", "", "eyJy");      // ssid "" like null

    // the body need not be zero terminated (the server passes a buffer)
    const char raw[] = "{\"ota_token\":\"eyJz\"}XXXX";
    creds_changes_t changes;
    const char *r = creds_patch(&c, raw, strlen(raw) - 4, &changes);
    const bool ok = (r == NULL) && (strcmp(c.ota_token, "eyJz") == 0) && changes.token && !changes.wlan;
    printf("%s  body without zero termination, changes.token/wlan\n", ok ? "OK  " : "FAIL");
    failures += ok ? 0 : 1;

    printf("\ncreds_patch: %s (%d failures)\n", failures ? "FAILED" : "all OK", failures);
    return failures ? 1 : 0;
}
