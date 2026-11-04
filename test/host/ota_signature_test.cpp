// Host test of ota_signature_verify(), see run.sh
//
//   ota_signature_test <expect> <image>    expect: valid | invalid
//
// The image is read from a file with the same layout as on the flash (firmware.signed.bin).
// ota_key.h is chosen by the include path (run.sh builds one binary per test key).
#include <stdio.h>
#include <string.h>
#include <vector>
#include "ota_signature.h"

static bool read_file(void *ctx, size_t offset, void *buf, size_t len)
{
    const std::vector<unsigned char> &data = *(const std::vector<unsigned char> *)ctx;
    if (offset + len > data.size()) {
        return false;
    }
    memcpy(buf, data.data() + offset, len);
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s valid|invalid <image>\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[2], "rb");
    if (!f) {
        perror(argv[2]);
        return 2;
    }
    std::vector<unsigned char> data;
    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        data.insert(data.end(), buf, buf + n);
    }
    fclose(f);

    char err[128] = "";
    const bool valid = ota_signature_verify(read_file, &data, data.size(), err, sizeof(err));
    const bool expected = (strcmp(argv[1], "valid") == 0);
    printf("%s  %-7s %s (%u bytes)%s%s\n", (valid == expected) ? "OK  " : "FAIL", valid ? "valid" : "invalid", argv[2],
           (unsigned)data.size(), err[0] ? ": " : "", err);
    return (valid == expected) ? 0 : 1;
}
