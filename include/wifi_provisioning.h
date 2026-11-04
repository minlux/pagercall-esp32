#pragma once

/**
 * Captive-portal Wi-Fi provisioning.
 *
 * Call wifi_provisioning_begin() once in setup().
 *   - If credentials are already stored the device connects as a Wi-Fi station.
 *   - If no SSID is stored the device opens a SoftAP named "PagerCall-Setup"
 *     and runs a captive portal that lets the user supply SSID / password and,
 *     optionally, the token of the update server. Credentials are persisted via
 *     creds_apply() (same rules as PUT /credentials) and the device restarts.
 *     PUT/DELETE /credentials and GET /reset are available in the portal, too.
 *
 * Call wifi_provisioning_loop() every iteration of loop().  It is a no-op once
 * the device is running in station mode.
 */

void wifi_provisioning_begin(void);
bool wifi_provisioning_active(void);
void wifi_provisioning_loop(void);
