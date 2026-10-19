/* Local diagnostic experiment; not an upstream contribution. */
#ifndef APPLE_GFX_OOP_TIMING_H
#define APPLE_GFX_OOP_TIMING_H
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct AppleGFXOOPTiming {
    uint64_t count, total, maximum, buckets[5], reported;
} AppleGFXOOPTiming;
static inline uint64_t apple_gfx_oop_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
/* Caller serializes each metric. Cumulative buckets: <100us, <1ms, <10ms,
 * <100ms, >=100ms. Disabled by default; at most one report/5s/metric. */
static inline void apple_gfx_oop_timing(AppleGFXOOPTiming *t, const char *name,
                                       uint64_t start)
{
    uint64_t now = apple_gfx_oop_now(), elapsed = now - start;
    t->count++;
    t->total += elapsed;
    if (elapsed > t->maximum) { t->maximum = elapsed; }
    unsigned bucket = elapsed < 100000 ? 0 : elapsed < 1000000 ? 1 :
                      elapsed < 10000000 ? 2 : elapsed < 100000000 ? 3 : 4;
    t->buckets[bucket]++;
    if (!t->reported) { t->reported = now; }
    if (now - t->reported < 5000000000ULL) { return; }
    t->reported = now;
    fprintf(stderr, "PVG timing %s count=%llu mean_us=%llu max_us=%llu bins=%llu,%llu,%llu,%llu,%llu\n",
        name, (unsigned long long)t->count,
        (unsigned long long)(t->total / t->count / 1000),
        (unsigned long long)(t->maximum / 1000),
        (unsigned long long)t->buckets[0], (unsigned long long)t->buckets[1],
        (unsigned long long)t->buckets[2], (unsigned long long)t->buckets[3],
        (unsigned long long)t->buckets[4]);
}
#endif
