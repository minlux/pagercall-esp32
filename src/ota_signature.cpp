#include "ota_signature.h"
#include "ota_key.h"

#include <stdio.h>
#include <string.h>
#include <memory>
#include "mbedtls/sha256.h"
#include "mbedtls/pk.h"

static const size_t CHUNK_SIZE = 4096;

size_t ota_signature_image_size(ota_read_fn read, void *ctx, size_t total_size)
{
    uint32_t sig_len = 0;
    if ((total_size < sizeof(sig_len)) || !read(ctx, total_size - sizeof(sig_len), &sig_len, sizeof(sig_len))) {
        return 0;
    }
    // trailer is little-endian; the ESP32 and the PC are little-endian, too
    if ((sig_len == 0) || (sig_len > OTA_SIGNATURE_SIZE_MAX) || (total_size <= sig_len + sizeof(sig_len))) {
        return 0;
    }
    return total_size - sizeof(sig_len) - sig_len;
}

bool ota_signature_verify(ota_read_fn read, void *ctx, size_t total_size, char *err, size_t err_size)
{
    const size_t image_size = ota_signature_image_size(read, ctx, total_size);
    if (image_size == 0) {
        snprintf(err, err_size, "No signature (unsigned image? use firmware.signed.bin)");
        return false;
    }
    const size_t sig_len = total_size - sizeof(uint32_t) - image_size;

    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[CHUNK_SIZE]);
    if (!buf) {
        snprintf(err, err_size, "Out of memory");
        return false;
    }

    // SHA256 of the image
    uint8_t hash[32];
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    bool ok = (mbedtls_sha256_starts(&sha, 0) == 0);
    for (size_t pos = 0; ok && (pos < image_size); pos += CHUNK_SIZE) {
        const size_t n = ((image_size - pos) < CHUNK_SIZE) ? (image_size - pos) : CHUNK_SIZE;
        ok = read(ctx, pos, buf.get(), n) && (mbedtls_sha256_update(&sha, buf.get(), n) == 0);
    }
    ok = ok && (mbedtls_sha256_finish(&sha, hash) == 0);
    mbedtls_sha256_free(&sha);
    if (!ok) {
        snprintf(err, err_size, "Reading the image failed");
        return false;
    }

    // signature, checked with the public key (ECDSA or RSA, given by the key)
    if (!read(ctx, image_size, buf.get(), sig_len)) {
        snprintf(err, err_size, "Reading the signature failed");
        return false;
    }
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int ret = mbedtls_pk_parse_public_key(&pk, ota_key_der, sizeof(ota_key_der));
    if (ret != 0) {
        snprintf(err, err_size, "Invalid public key (-0x%04x)", (unsigned)-ret);
    } else {
        ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), buf.get(), sig_len);
        if (ret != 0) {
            snprintf(err, err_size, "Invalid signature (-0x%04x)", (unsigned)-ret);
        }
    }
    mbedtls_pk_free(&pk);
    return ret == 0;
}
