#!/usr/bin/env bash
#
# Host tests (on the PC, without hardware):
#
#   - creds_patch.cpp     PUT /credentials rules (same as Candela and SGW)
#   - ota_signature.cpp   signature check of OTA images, with test keys (ECDSA P-256 and RSA 2048)
#                         and, if built, with the real firmware.signed.bin and the project key
#
# Uses the same library versions as the firmware (Arduino core 3.3.7 / IDF 5.5): cJSON 1.7.19,
# mbedtls 3.6.5. They are downloaded once into test/host/.deps (not in git) and built there.
#
#   test/host/run.sh
#
# Requires: g++, gcc, make, git, curl, openssl, python3
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
DEPS="$HERE/.deps"
BUILD="$HERE/build"
CJSON="$DEPS/cjson"
MBEDTLS="$DEPS/mbedtls-3.6.5"
mkdir -p "$DEPS" "$BUILD"

# --- dependencies ----------------------------------------------------------------------------
if [ ! -f "$CJSON/cJSON.c" ]; then
   git -c advice.detachedHead=false clone -q --depth 1 --branch v1.7.19 https://github.com/DaveGamble/cJSON.git "$CJSON"
fi
if [ ! -f "$MBEDTLS/include/mbedtls/pk.h" ]; then
   curl -sSL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.5/mbedtls-3.6.5.tar.bz2 | tar xj -C "$DEPS"
fi
if [ ! -f "$MBEDTLS/library/libmbedcrypto.a" ]; then
   echo "building mbedtls (once) ..."
   make -s -C "$MBEDTLS" lib -j"$(nproc)" >/dev/null
fi

FAIL=0
run() { if "$@"; then :; else FAIL=1; fi; }

# --- creds_patch -----------------------------------------------------------------------------
echo "=== creds_patch (PUT /credentials)"
gcc -c -O1 -o "$BUILD/cJSON.o" "$CJSON/cJSON.c"
g++ -std=gnu++17 -Wall -Wextra -O1 -I"$ROOT/include" -I"$CJSON" -o "$BUILD/creds_patch_test" \
    "$HERE/creds_patch_test.cpp" "$ROOT/src/creds_patch.cpp" "$BUILD/cJSON.o"
run "$BUILD/creds_patch_test"

# --- ota_signature ---------------------------------------------------------------------------
echo
echo "=== ota_signature"

# ota_key.h with the public key of a private key (same format as include/ota_key.h)
key_header() {  # <private key> <dir>
   mkdir -p "$2"
   openssl pkey -in "$1" -pubout -outform DER | python3 -c '
import sys
d = sys.stdin.buffer.read()
print("#pragma once\n#include <stdint.h>\nstatic const uint8_t ota_key_der[] = {")
print("\n".join("    " + ", ".join("0x%02x" % b for b in d[i:i + 12]) + "," for i in range(0, len(d), 12)))
print("};")' > "$2/ota_key.h"
}

# image | signature | len (uint32 LE), as sign_firmware.py writes it
sign() {  # <private key> <image> <signed image>
   openssl dgst -sha256 -sign "$1" -out "$BUILD/sig.tmp" "$2"
   python3 -c '
import struct, sys
image, sig = open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read()
open(sys.argv[3], "wb").write(image + sig + struct.pack("<I", len(sig)))' "$2" "$BUILD/sig.tmp" "$3"
}

# copy of a file with one byte inverted at offset
flip() {  # <in> <out> <offset; negative: from the end>
   python3 -c '
import sys
d = bytearray(open(sys.argv[1], "rb").read()); i = int(sys.argv[3])
d[i] ^= 0xff
open(sys.argv[2], "wb").write(d)' "$1" "$2" "$3"
}

verifier() {  # <name> <dir with ota_key.h>
   g++ -std=gnu++17 -Wall -Wextra -O1 -I"$2" -I"$ROOT/include" -I"$MBEDTLS/include" -o "$BUILD/$1" \
       "$HERE/ota_signature_test.cpp" "$ROOT/src/ota_signature.cpp" "$MBEDTLS/library/libmbedcrypto.a"
}

K="$BUILD/keys"
mkdir -p "$K"
[ -f "$K/ec.key" ]  || openssl ecparam -name prime256v1 -genkey -noout -out "$K/ec.key"
[ -f "$K/ec2.key" ] || openssl ecparam -name prime256v1 -genkey -noout -out "$K/ec2.key"
[ -f "$K/rsa.key" ] || openssl genrsa -out "$K/rsa.key" 2048 2>/dev/null
key_header "$K/ec.key" "$K/ec"
key_header "$K/rsa.key" "$K/rsa"
verifier verify_ec "$K/ec"
verifier verify_rsa "$K/rsa"

I="$BUILD/images"
mkdir -p "$I"
head -c 300001 /dev/urandom > "$I/image.bin"
sign "$K/ec.key" "$I/image.bin" "$I/ec.bin"
sign "$K/ec2.key" "$I/image.bin" "$I/ec2.bin"
sign "$K/rsa.key" "$I/image.bin" "$I/rsa.bin"
flip "$I/ec.bin" "$I/ec_image_changed.bin" 1000
flip "$I/ec.bin" "$I/ec_signature_changed.bin" -20
head -c -1 "$I/ec.bin" > "$I/ec_truncated.bin"
python3 -c 'import sys; open(sys.argv[2], "wb").write(open(sys.argv[1], "rb").read()[:-4] + b"\xff\xff\x00\x00")' "$I/ec.bin" "$I/ec_bad_length.bin"
head -c 3 "$I/ec.bin" > "$I/tiny.bin"

echo "-- ECDSA P-256 key"
run "$BUILD/verify_ec" valid   "$I/ec.bin"
run "$BUILD/verify_ec" invalid "$I/image.bin"
run "$BUILD/verify_ec" invalid "$I/ec2.bin"
run "$BUILD/verify_ec" invalid "$I/rsa.bin"
run "$BUILD/verify_ec" invalid "$I/ec_image_changed.bin"
run "$BUILD/verify_ec" invalid "$I/ec_signature_changed.bin"
run "$BUILD/verify_ec" invalid "$I/ec_truncated.bin"
run "$BUILD/verify_ec" invalid "$I/ec_bad_length.bin"
run "$BUILD/verify_ec" invalid "$I/tiny.bin"
echo "-- RSA 2048 key (same code)"
run "$BUILD/verify_rsa" valid   "$I/rsa.bin"
run "$BUILD/verify_rsa" invalid "$I/ec.bin"

# the real image of the last build (signed by sign_firmware.py with signing/private.key)
FW="$ROOT/.pio/build/heltec_wifi_lora_32_V3"
if [ -f "$FW/firmware.signed.bin" ]; then
   echo "-- project key (include/ota_key.h), images of the last pio run"
   verifier verify_project "$ROOT/include"
   run "$BUILD/verify_project" valid   "$FW/firmware.signed.bin"
   run "$BUILD/verify_project" invalid "$FW/firmware.bin"
   flip "$FW/firmware.signed.bin" "$I/firmware_changed.bin" 4096
   run "$BUILD/verify_project" invalid "$I/firmware_changed.bin"
   run "$BUILD/verify_project" invalid "$I/ec.bin"
else
   echo "-- project key: skipped (no $FW/firmware.signed.bin, run pio run first)"
fi

echo
if [ "$FAIL" = 0 ]; then echo "ALL HOST TESTS OK"; else echo "HOST TESTS FAILED"; fi
exit "$FAIL"
