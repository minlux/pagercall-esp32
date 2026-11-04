#pragma once

/* ---------------------------------------------------------------------------
 * Firmware version.
 *
 * PAGERCALL_VERSION_STR is maintained by hand: increment it for every release.
 * It is reported by GET /version, in the User-Agent of requests to the update
 * server (PagerCall/<version>) and as "version" in the system info for Souper.
 *
 * The git revision and the build time are set by git_version.py at build time
 * (only for version.cpp, so only that file is recompiled on each build).
 *
 *   1.000   04.11.2026   OTA update: PUT /firmware (signed images, ECDSA P-256),
 *                        update server Souper, PUT/DELETE /credentials, GET /version
 * --------------------------------------------------------------------------*/

#define PAGERCALL_VERSION_STR  "1.000"

extern const char pagercall_git_rev[];     // e.g. "958c9ee", "v1.000-2-g1a2b3c4-dirty"
extern const char pagercall_build_time[];  // e.g. "2026-11-04 12:00"
