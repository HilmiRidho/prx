#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
int main(int argc, char**argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: ldpatch INPUT_ELF OUTPUT_ELF\n");
        return 2;
    }
    FILE*f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        fprintf(stderr, "ldpatch: input file is empty\n");
        return 1;
    }
    uint8_t*buf = (uint8_t*) malloc((size_t) sz);
    if (!buf || fread(buf, 1, (size_t) sz, f) != (size_t) sz) {
        fclose(f);
        if (buf) free(buf);
        fprintf(stderr, "ldpatch: error reading %s\n", argv[1]);
        return 1;
    }
    fclose(f);
    int patched = 0;
    size_t n = (size_t) sz/4;
    for (size_t i = 0; i<n; i++) {
        uint32_t w;
        memcpy(&w, buf+i*4, 4);
        if (w != 0xD2800C68u && w != 0x52800C68u && w != 0xD28024A8u && w != 0x528024A8u && w != 0xD2803668u && w != 0x52803668u) continue;
        for (size_t j = i+1; j<n && j <= i+12; j++) {
            uint32_t o;
            memcpy(&o, buf+j*4, 4);
            if (o == 0xD4000001u) {
                uint32_t p = 0x928004A0u;
                memcpy(buf+j*4, &p, 4);
                patched++;
                break;
            }
            if ((o&0x7C000000u) == 0x14000000u || o == 0xD65F03C0u) break;
        }
    }
    f = fopen(argv[2], "wb");
    if (!f || fwrite(buf, 1, (size_t) sz, f) != (size_t) sz) {
        if (f) fclose(f);
        free(buf);
        fprintf(stderr, "ldpatch: failed writing to %s\n", argv[2]);
        return 1;
    }
    fclose(f);
    free(buf);
    chmod(argv[2], 0755);
    printf("%d\n", patched);
    return 0;
}
