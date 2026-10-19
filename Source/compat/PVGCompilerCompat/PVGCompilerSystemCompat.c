#include <stdbool.h>

// Added in Big Sur's dyld/libSystem. The 31001 MTL compiler uses this only to
// choose between loading a compiler plug-in directly from the shared cache and
// validating/loading a file-system bundle. Catalina's PVG plug-in is installed
// in /System/Library/Extensions, so it must take the file-system branch.
bool _dyld_shared_cache_contains_path(const char *path) {
    (void)path;
    return false;
}
