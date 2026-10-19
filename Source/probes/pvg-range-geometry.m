// Exercise the actual donor's packed range ABI, not a replacement implementation.
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc != 2) return 64;
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    typedef uint64_t (*Calculate)(void *, uint64_t, uint64_t);
    Calculate aligned = (Calculate)dlsym(h, "_ZNK20PGMappedRangeTracker23calculatePageRunAlignedEyy");
    Calculate unaligned = (Calculate)dlsym(h, "_ZNK20PGMappedRangeTracker25calculatePageRunUnalignedEyy");
    void (*range)(uint64_t *, uint64_t *, uint64_t *) =
        dlsym(h, "_ZN20PGMappedRangeTracker3Run8getRangeERyS1_");
    if (!aligned || !unaligned || !range) return 2;
    unsigned checked = 0;
    @try {
        for (uint64_t page = 1; page < 1030; page += 7) {
            uint64_t offset = page << 12, length = ((page % 9) + 1) << 12;
            uint64_t run = aligned(NULL, offset, length);
            if (run != (page | ((length >> 12) << 32))) return 3;
            uint64_t start = 0, size = 0;
            range(&run, &start, &size);
            if (start != offset || size != length) return 4;
            // Match the Intel donor's unaligned packing exactly, including
            // its 0xffe00000 bias (not a guessed generic ceil operation).
            for (unsigned delta = 0; delta < 4096; delta += 511) {
                uint64_t off = offset + delta;
                uint64_t packed = (((off + length - (off & UINT64_C(0xffffffff000)))
                                    << 20) + UINT64_C(0xffe00000));
                packed = (packed & UINT64_C(0xffffffff00000000)) | (uint32_t)(off >> 12);
                if (unaligned(NULL, off, length) != packed) return 5;
                checked++;
            }
        }
    } @catch (NSException *e) {
        fprintf(stderr, "unexpected: %s\n", e.reason.UTF8String); return 6;
    }
    unsigned rejected = 0;
    @try { aligned(NULL, 4097, 4096); } @catch (NSException *e) { rejected++; }
    @try { aligned(NULL, 4096, 4097); } @catch (NSException *e) { rejected++; }
    @try { aligned(NULL, 4096, 0); } @catch (NSException *e) { rejected++; }
    printf("unaligned cases=%u aligned roundtrips=147 rejected=%u/3\n", checked, rejected);
    return rejected == 3 ? 0 : 7;
}
