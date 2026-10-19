#include <stdbool.h>

/*
 * Big Sur's MTLCompiler uses this dyld 852 SPI while deciding whether a
 * compiler-plugin path needs the ordinary on-disk stat/trust checks.  The
 * backported 31001 compiler and PVG plugin deliberately live as files outside
 * Catalina's dyld shared cache, so the conservative result is also the exact
 * result for every payload-owned path.  MTLCompiler's false branch performs
 * stat(), gpu_bundle_is_path_trusted(), and then dlopen().
 *
 * This compatibility image re-exports Catalina libSystem; build tooling
 * redirects MTLCompiler's libSystem dependency through it so all ordinary
 * libSystem binds remain two-level while adding this one missing symbol.
 */
bool _dyld_shared_cache_contains_path(const char *path) {
    (void)path;
    return false;
}
