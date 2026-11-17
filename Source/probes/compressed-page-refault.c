/* Finite diagnostic for the mixed-validity APFS verification path.
 * compressed-page-refault FILE ALIGNED_OFFSET EXPECTED_SHA256
 * Read-only MAP_SHARED; invalidate one clean mapped page at a time through
 * native msync, then hash the complete 16KiB window after refaulting it.
 * No system-file writes, cache deletion, signature bypass or recovery action.
 * SIGBUS is deliberately not handled: retain the native report if it happens.
 */
#include <CommonCrypto/CommonDigest.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int check(const void *bytes, const char *expected, unsigned round,
                 unsigned page)
{
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    char hex[65];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CC_SHA256(bytes, 16384, digest);
#pragma clang diagnostic pop
    for (unsigned i = 0; i < sizeof(digest); i++)
        snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    if (!strcmp(hex, expected)) return 0;
    fprintf(stderr, "MISMATCH round=%u page=%u actual=%s expected=%s\n",
            round, page, hex, expected);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 4 || strlen(argv[3]) != 64) return 64;
    char *end;
    uint64_t offset = strtoull(argv[2], &end, 0);
    long page = getpagesize();
    if (*end || page != 4096 || offset % 16384 || offset > INT64_MAX - 16384)
        return 64;
    int fd = open(argv[1], O_RDONLY | O_NOFOLLOW);
    struct stat info;
    if (fd < 0 || fstat(fd, &info) || !S_ISREG(info.st_mode) ||
        info.st_size < (off_t)offset + 16384) {
        perror("window file");
        if (fd >= 0) close(fd);
        return 1;
    }
    unsigned char *window = mmap(NULL, 16384, PROT_READ, MAP_SHARED, fd, offset);
    close(fd);
    if (window == MAP_FAILED) { perror("mmap"); return 1; }
    if (check(window, argv[3], 0, 0)) return 2;
    for (unsigned round = 0; round < 32; round++) {
        for (unsigned index = 0; index < 4; index++) {
            if (msync(window + index * page, page, MS_SYNC | MS_INVALIDATE)) {
                perror("native single-page invalidation");
                munmap(window, 16384);
                return 3;
            }
            if (check(window, argv[3], round, index)) {
                munmap(window, 16384);
                return 2;
            }
        }
    }
    munmap(window, 16384);
    puts("128 single-page invalidate/refault checks: MATCH");
    return 0;
}
