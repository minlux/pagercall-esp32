#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

void creds_begin(void);
void creds_set(const char *key, const char *value);
void creds_get(const char *key, char *buf, size_t len);

#ifdef __cplusplus
}
#endif

static inline void creds_set_ssid    (const char *val)              { creds_set("ssid",     val); }
static inline void creds_set_password(const char *val)              { creds_set("password", val); }

static inline void creds_get_ssid    (char *buf, size_t len)        { creds_get("ssid",     buf, len); }
static inline void creds_get_password(char *buf, size_t len)        { creds_get("password", buf, len); }
