/* Compare the clock feeding unmodified ARM TCG ticks with Darwin's raw clock.
 * Finite, read-only host diagnostic; no guest, privileges or clock changes.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static uint64_t monotonic_ns(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + value.tv_nsec;
}

static uint64_t uptime_raw_ns(void)
{
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

static void measure(const char *name, uint64_t (*read_clock)(void))
{
    const unsigned samples = 1000000;
    uint64_t minimum = UINT64_MAX, maximum = 0, zeros = 0, backwards = 0;
    uint64_t non_microsecond = 0, first = read_clock(), previous = first;
    for (unsigned index = 0; index < samples; ++index) {
        uint64_t now = read_clock();
        if (now % 1000) ++non_microsecond;
        if (now < previous) ++backwards;
        else if (now == previous) ++zeros;
        else {
            uint64_t delta = now - previous;
            if (delta < minimum) minimum = delta;
            if (delta > maximum) maximum = delta;
        }
        previous = now;
    }
    printf("%s samples=%u min_positive_ns=%" PRIu64
           " max_ns=%" PRIu64 " equal=%" PRIu64 " backwards=%" PRIu64
           " non_microsecond=%" PRIu64 " elapsed_ns=%" PRIu64 "\n",
           name, samples, minimum, maximum, zeros, backwards,
           non_microsecond, previous - first);
}

int main(void)
{
    measure("CLOCK_MONOTONIC", monotonic_ns);
    measure("CLOCK_UPTIME_RAW", uptime_raw_ns);
    return 0;
}
