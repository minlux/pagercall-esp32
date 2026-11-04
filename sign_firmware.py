# PlatformIO extra script: signed image for OTA updates (PUT /firmware, update server Souper)
#
# After firmware.bin is built, firmware.signed.bin is written next to it: the image, followed
# by the signature (SHA256; ECDSA P-256 as DER, or RSA PKCS#1 v1.5, depending on the key) and its
# length as uint32 little-endian. The firmware checks this in ota_signature.cpp.
#
# Private key: $PAGERCALL_SIGNING_KEY, otherwise signing/private.key (not in git).
# Without it only firmware.bin is built (flashable by cable, but not via OTA).
# After signing, the signature is checked with the public key from include/ota_key.h. So it shows
# up, if the private key and the firmware do not match.
#
# Requires: openssl
import os
import re
import struct
import subprocess
import tempfile

Import("env")

PROJECT_DIR = env["PROJECT_DIR"]
KEY = os.environ.get("PAGERCALL_SIGNING_KEY") or os.path.join(PROJECT_DIR, "signing", "private.key")


def public_key_der():
    """ota_key_der[] from include/ota_key.h"""
    with open(os.path.join(PROJECT_DIR, "include", "ota_key.h")) as f:
        text = f.read()
    body = re.search(r"ota_key_der\[\]\s*=\s*\{(.*?)\}", text, re.S).group(1)
    return bytes(int(h, 16) for h in re.findall(r"0x([0-9a-fA-F]{2})", body))


def openssl(*args, data=None):
    return subprocess.run(["openssl"] + list(args), input=data, capture_output=True)


def sign(source, target, env):
    bin_path = str(target[0])
    signed_path = os.path.splitext(bin_path)[0] + ".signed.bin"
    if os.path.exists(signed_path):
        os.remove(signed_path)  # no stale image of an earlier build
    if not os.path.isfile(KEY):
        print("PagerCall: no private key (%s), firmware.signed.bin is not built" % KEY)
        return

    with open(bin_path, "rb") as f:
        image = f.read()
    result = openssl("dgst", "-sha256", "-sign", KEY, data=image)
    if result.returncode != 0:
        env.Exit("PagerCall: signing failed: %s" % result.stderr.decode(errors="replace"))
    signature = result.stdout

    # check against the public key compiled into the firmware
    with tempfile.TemporaryDirectory() as tmp:
        files = {}
        for name, data in (("image", image), ("signature", signature), ("key.der", public_key_der())):
            files[name] = os.path.join(tmp, name)
            with open(files[name], "wb") as f:
                f.write(data)
        result = openssl("dgst", "-sha256", "-verify", files["key.der"], "-keyform", "DER",
                         "-signature", files["signature"], files["image"])
    if result.returncode != 0:
        env.Exit("PagerCall: signature does not match the key in include/ota_key.h (%s)" % KEY)

    with open(signed_path, "wb") as f:
        f.write(image + signature + struct.pack("<I", len(signature)))
    print("PagerCall: signed %s (%d bytes, signature %d bytes)"
          % (os.path.relpath(signed_path, PROJECT_DIR), len(image) + len(signature) + 4, len(signature)))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", sign)
