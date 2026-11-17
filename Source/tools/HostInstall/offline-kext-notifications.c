/*
 * Sierra's period kextcache records every cache entry with the retired libdz
 * host service. Current macOS no longer supplies it, and the shipped tool calls
 * its weak notification import without testing for NULL. Offline construction
 * must not report guest extensions to the host's security service anyway.
 *
 * Only the private host-executed copy of kextcache loads this library. Neither
 * guest files nor host libraries/policy are changed. The absent-service policy
 * is allow, as in Apple's dzAllowKextLoad source; the shipped binary omits the
 * NULL guard for this import too. Cache construction already uses the tool's
 * -no-authentication mode. These functions never load an extension on the host.
 */
void dz_notify_kextcache_update(const char *path) {
    (void)path;
}

#include <stdint.h>
uint64_t dz_check_policy_kext_load(const char *path) {
    (void)path;
    return 0; /* DZ_CHECK_POLICY_STATUS_ALLOW */
}
