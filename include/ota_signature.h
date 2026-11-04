#pragma once
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Signature check of OTA images (PUT /firmware and update server Souper).
 *
 * Image layout (built by sign_firmware.py as firmware.signed.bin):
 *
 *    image | signature | length of the signature (uint32, little-endian)
 *
 * The signature is made over the SHA256 of the image (without the trailer)
 * with the private key matching ota_key_der[] (ota_key.h). The algorithm follows
 * from the key: ECDSA P-256 (signature as DER, 70..72 bytes) is used for
 * PagerCall; RSA 2048 (PKCS#1 v1.5, 256 bytes) would work the same way.
 *
 * Only mbedtls is used (no flash access), so this can be tested on the PC.
 * --------------------------------------------------------------------------*/

#define OTA_SIGNATURE_SIZE_MAX  (512u)  // longest accepted signature (RSA 4096)

// read len bytes at offset of the image. returns false on error
typedef bool (*ota_read_fn)(void *ctx, size_t offset, void *buf, size_t len);

// check the signature of an image of total_size bytes (including the trailer).
// returns true, if it is valid. otherwise false, and the reason in err
bool ota_signature_verify(ota_read_fn read, void *ctx, size_t total_size, char *err, size_t err_size);

// size of the image without the trailer (0, if the trailer is invalid)
size_t ota_signature_image_size(ota_read_fn read, void *ctx, size_t total_size);
