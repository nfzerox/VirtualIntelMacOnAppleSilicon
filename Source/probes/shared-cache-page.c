/* Read only the cache mapping and one page implicated by a native crash.
 * Usage: shared-cache-page CACHE_DIRECTORY RUNTIME_CACHE_BASE FAULT_ADDRESS
 * No complete-cache scan, writes, mmap, signature bypass or guest policy edits.
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <CommonCrypto/CommonDigest.h>

struct header { char magic[16]; uint32_t mapping_offset, mapping_count; };
struct mapping { uint64_t address, size, offset; uint32_t max_prot, init_prot; };

static uint32_t big32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int signed_page(int fd, uint64_t offset)
{
    uint64_t signature;
    unsigned char super[12];
    if (pread(fd, &signature, sizeof(signature), 40) != sizeof(signature) ||
        pread(fd, super, sizeof(super), signature) != sizeof(super) ||
        big32(super) != 0xfade0cc0 || big32(super + 8) > 32) return -1;
    for (unsigned index = 0; index < big32(super + 8); ++index) {
        unsigned char entry[8], directory[40];
        if (pread(fd, entry, sizeof(entry), signature + 12 + index * 8) != sizeof(entry)) return -1;
        uint64_t position = signature + big32(entry + 4);
        if (pread(fd, directory, sizeof(directory), position) != sizeof(directory)) return -1;
        if (big32(directory) != 0xfade0c02 || directory[36] != 32 || directory[37] != 2) continue;
        if (directory[39] < 12 || directory[39] > 16) return -1;
        uint64_t size = UINT64_C(1) << directory[39], page = offset / size;
        uint64_t start = page * size, limit = big32(directory + 32);
        if (page >= big32(directory + 28) || start >= limit) return -1;
        size_t count = limit - start < size ? (size_t)(limit - start) : (size_t)size;
        unsigned char bytes[65536], expected[32], actual[32];
        if (pread(fd, bytes, count, start) != (ssize_t)count ||
            pread(fd, expected, sizeof(expected), position + big32(directory + 16) + page * 32) != sizeof(expected)) return -1;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        CC_SHA256(bytes, (CC_LONG)count, actual);
#pragma clang diagnostic pop
        int matches = !memcmp(expected, actual, sizeof(actual));
        printf("Original CodeDirectory SHA256 page (%zu bytes): %s\n", count, matches ? "MATCH" : "MISMATCH");
        return matches ? 0 : 1;
    }
    return -1;
}

static int mappings(int fd, struct mapping *maps)
{
    struct header header;
    if (pread(fd, &header, sizeof(header), 0) != sizeof(header) ||
        memcmp(header.magic, "dyld_v1", 7) || !header.mapping_count ||
        header.mapping_count > 128) return -1;
    size_t bytes = header.mapping_count * sizeof(*maps);
    return pread(fd, maps, bytes, header.mapping_offset) == (ssize_t)bytes ?
        (int)header.mapping_count : -1;
}

int main(int argc, char **argv)
{
    if (argc != 4) return 64;
    uint64_t runtime_base = strtoull(argv[2], NULL, 0);
    uint64_t fault = strtoull(argv[3], NULL, 0), original_base = 0;
    for (int part = 0; part < 16; ++part) {
        char path[4096], suffix[16] = "";
        if (part) snprintf(suffix, sizeof(suffix), ".%02d", part);
        if (snprintf(path, sizeof(path), "%s/dyld_shared_cache_x86_64h%s", argv[1], suffix) >= (int)sizeof(path)) return 64;
        int fd = open(path, O_RDONLY);
        if (fd < 0) { if (!part) { perror(path); return 1; } continue; }
        struct mapping maps[128];
        int count = mappings(fd, maps);
        if (count < 0) { fprintf(stderr, "Invalid mapping header: %s\n", path); close(fd); return 1; }
        if (!part) original_base = maps[0].address;
        uint64_t address = fault - runtime_base + original_base;
        for (int index = 0; index < count; ++index) {
            struct mapping map = maps[index];
            if (address < map.address || address - map.address >= map.size) continue;
            uint64_t offset = map.offset + address - map.address;
            uint64_t page = offset & ~UINT64_C(4095);
            printf("File: %s\nPreferred address: 0x%" PRIx64 "\nFile offset: 0x%" PRIx64 "\n", path, address, offset);
            struct stat info;
            if (!fstat(fd, &info)) printf("Size: %lld; flags: 0x%x\n", (long long)info.st_size, info.st_flags);
            uint32_t compression[4];
            if (fgetxattr(fd, "com.apple.decmpfs", compression, sizeof(compression), 0, XATTR_SHOWCOMPRESSION) == sizeof(compression))
                printf("decmpfs magic: 0x%x; type: %u\n", compression[0], compression[1]);
            unsigned char bytes[4096]; errno = 0;
            ssize_t read_bytes = pread(fd, bytes, sizeof(bytes), (off_t)page);
            int saved = errno;
            printf("pread: %zd; errno: %d (%s)\n", read_bytes, saved, strerror(saved));
            if (read_bytes > (ssize_t)(offset - page)) printf("Fault byte: 0x%02x\n", bytes[offset - page]);
            int signature = read_bytes == sizeof(bytes) ? signed_page(fd, offset) : -1;
            if (signature < 0) puts("CodeDirectory page comparison unavailable");
            close(fd);
            return read_bytes == sizeof(bytes) && signature == 0 ? 0 : 1;
        }
        close(fd);
    }
    fputs("No mapping contains the fault address\n", stderr);
    return 1;
}
