#include "credentials.h"
#include <Preferences.h>
#include <stdio.h>
#include <string.h>


static Preferences prefs;

void creds_begin(void)
{
    prefs.begin("pagercall", false);
}

void creds_set(const char *key, const char *value)
{
    prefs.putString(key, value);
}

void creds_get(const char *key, char *buf, size_t len)
{
    prefs.getString(key, buf, len);
}
