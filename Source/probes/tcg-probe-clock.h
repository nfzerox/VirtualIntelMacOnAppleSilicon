/* Monotonic intervals for the CPU probes. Darwin's Mach clock also works
 * on pre-Sierra guests, where libSystem does not export clock_gettime. */
#ifndef VIM_TCG_PROBE_CLOCK_H
#define VIM_TCG_PROBE_CLOCK_H

#include <stdint.h>
#ifdef __APPLE__
#include <mach/mach_time.h>

static mach_timebase_info_data_t tcg_probe_timebase;

static inline int tcg_probe_clock_init(void)
{
    return mach_timebase_info(&tcg_probe_timebase) == KERN_SUCCESS &&
           tcg_probe_timebase.numer && tcg_probe_timebase.denom ? 0 : -1;
}

static inline uint64_t tcg_probe_ticks(void)
{
    return mach_absolute_time();
}

static inline double tcg_probe_seconds(uint64_t start, uint64_t end)
{
    return (end - start) * (1e-9 * tcg_probe_timebase.numer /
                           tcg_probe_timebase.denom);
}
#else
#include <time.h>

static inline int tcg_probe_clock_init(void)
{
    struct timespec now;
    return clock_gettime(CLOCK_MONOTONIC, &now);
}

static inline uint64_t tcg_probe_ticks(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + now.tv_nsec;
}

static inline double tcg_probe_seconds(uint64_t start, uint64_t end)
{
    return (end - start) * 1e-9;
}
#endif
#endif
