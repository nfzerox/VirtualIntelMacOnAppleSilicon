// Development-only guest probe. --frameworks additionally proves that core
// framework image headers, not merely the cache UUID, use the mapped cache.
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <uuid/uuid.h>

static int checkFrameworks(const void *base, size_t size)
{
    const char *paths[] = {
        "/usr/lib/libSystem.B.dylib",
        "/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation",
        "/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation",
        "/System/Library/PrivateFrameworks/SkyLight.framework/Versions/A/SkyLight"
    };
    if (!base || !size) {
        fputs("Cannot inspect a live shared-cache range\n", stderr);
        return 1;
    }
    int failed = 0;
    for (size_t p = 0; p < sizeof(paths) / sizeof(paths[0]); ++p) {
        // Retain the handles until process exit so all inspected images remain
        // loaded. Framework initializers still run as with a normal client;
        // this probe must be executed on the guest, not used as a host test.
        printf("Checking image: %s\n", paths[p]);
        fflush(stdout);
        if (!dlopen(paths[p], RTLD_LAZY | RTLD_LOCAL)) {
            fprintf(stderr, "Cannot load %s: %s\n", paths[p], dlerror());
            failed = 1;
            continue;
        }
        bool found = false;
        for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
            const char *name = _dyld_get_image_name(i);
            if (!name || strcmp(name, paths[p]))
                continue;
            uintptr_t address = (uintptr_t)_dyld_get_image_header(i);
            bool cached = address >= (uintptr_t)base && address - (uintptr_t)base < size;
            printf("%s: %s header=%p\n", cached ? "Cached image" : "Loose image", name, (void *)address);
            failed |= !cached;
            found = true;
            break;
        }
        if (!found) {
            fprintf(stderr, "Cannot locate loaded image %s\n", paths[p]);
            failed = 1;
        }
    }
    return failed;
}

int main(int argc, char **argv)
{
    bool frameworks = argc == 2 && !strcmp(argv[1], "--frameworks");
    if (argc > 1 && !frameworks)
        return 64;
    bool (*getUUID)(uuid_t) = dlsym(RTLD_DEFAULT, "_dyld_get_shared_cache_uuid");
    const void *(*getRange)(size_t *) = dlsym(RTLD_DEFAULT, "_dyld_get_shared_cache_range");
    uuid_t uuid;
    if (!getUUID) {
        if (frameworks) {
            fputs("Framework range inspection requires the shared-cache query APIs\n", stderr);
            return 1;
        }
        // Yosemite and earlier do not export the newer UUID query. Apple's
        // period MachOBinder sets bit31 on the images placed in its cache.
        // Inspect a live core image, not the existence of a cache file or a
        // presumed shared-region address. This also proves actual dylib use.
        for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
            const char *name = _dyld_get_image_name(i);
            if (!name || strcmp(name, "/usr/lib/libSystem.B.dylib"))
                continue;
            const struct mach_header *header = _dyld_get_image_header(i);
            if (header->flags & UINT32_C(0x80000000)) {
                printf("Live dyld shared cache: libSystem=%p cache-header-flag (UUID API unavailable)\n", (const void *)header);
                return 0;
            }
            fputs("libSystem is a loose image (UUID API unavailable)\n", stderr);
            return 1;
        }
        fputs("Cannot locate live libSystem (UUID API unavailable)\n", stderr);
        return 1;
    }
    if (!getUUID(uuid)) {
        fputs("No live dyld shared cache\n", stderr);
        return 1;
    }
    char text[37];
    uuid_unparse_upper(uuid, text);
    size_t size = 0;
    const void *base = getRange ? getRange(&size) : NULL;
    printf("Live dyld shared cache: %s base=%p size=%zu\n", text, base, size);
    return frameworks ? checkFrameworks(base, size) : 0;
}
