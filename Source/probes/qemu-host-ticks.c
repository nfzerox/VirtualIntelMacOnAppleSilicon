/* Finite native probe of the actual QEMU host-ticks inline implementation.
 * Compile with the selected QEMU build's config/include paths.  No VM disk,
 * guest services, or system settings are used.
 */
#include "qemu/osdep.h"
#include "qemu/timer.h"

int main(void)
{
    int64_t previous = cpu_get_host_ticks();
    int64_t minimum = INT64_MAX;
    uint64_t changes = 0, backwards = 0;

    for (unsigned i = 0; i < 1000000; i++) {
        int64_t current = cpu_get_host_ticks();
        int64_t delta = current - previous;
        if (delta < 0) {
            backwards++;
        } else if (delta > 0) {
            changes++;
            minimum = MIN(minimum, delta);
        }
        previous = current;
    }
    printf("host ticks: changes=%" PRIu64 " backwards=%" PRIu64
           " minimum_delta=%" PRId64 "\n", changes, backwards, minimum);
    if (backwards || !changes) {
        return 1;
    }
#if defined(__APPLE__) && defined(__aarch64__)
    /* The ARM Darwin tick unit is nanoseconds; reject the former 1-us clock.
     * A minimum (rather than an average) is insensitive to scheduling stalls.
     */
    if (minimum >= 1000) {
        return 1;
    }
#endif
    return 0;
}
