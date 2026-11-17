#pragma once
#include <stdint.h>

// Optional native-host diagnostic, not the monitor ABI. Each vCPU is the sole
// writer of its ring; inspect only with the process stopped. No allocation,
// output, timer, recurring sampling or guest-memory access.
#define ENGINE_LATENCY_CPUS 8
#define ENGINE_LATENCY_SAMPLES 128
enum { ENGINE_LATENCY_HOST_CALL=1, ENGINE_LATENCY_FALLBACK_AWAY,
       ENGINE_LATENCY_BOUNDARY_AWAY };
typedef struct {
    uint64_t sequence, start_ticks, duration_ticks, wait_ticks, detail[2];
    uint32_t kind, code, view, cpl;
} EngineLatencySample;
typedef struct {
    uint64_t total;
    EngineLatencySample samples[ENGINE_LATENCY_SAMPLES];
} EngineLatencyRing;
#ifdef __cplusplus
extern "C" {
#endif
extern EngineLatencyRing engine_latency_rings[ENGINE_LATENCY_CPUS];
void engine_latency_record(uint32_t cpu, uint32_t kind, uint32_t code,
    uint64_t start, uint64_t duration, uint64_t wait, uint64_t detail0,
    uint64_t detail1, uint32_t view, uint32_t cpl);
#ifdef __cplusplus
}
#endif
