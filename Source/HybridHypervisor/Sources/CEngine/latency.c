#include "latency.h"

EngineLatencyRing engine_latency_rings[ENGINE_LATENCY_CPUS];

void engine_latency_record(uint32_t cpu, uint32_t kind, uint32_t code,
    uint64_t start, uint64_t duration, uint64_t wait, uint64_t detail0,
    uint64_t detail1, uint32_t view, uint32_t cpl) {
    if(cpu>=ENGINE_LATENCY_CPUS)return;
    EngineLatencyRing *ring=&engine_latency_rings[cpu];
    uint64_t sequence=ring->total+1;
    EngineLatencySample *sample=&ring->samples[(sequence-1)%ENGINE_LATENCY_SAMPLES];
    sample->sequence=0;
    sample->start_ticks=start;
    sample->duration_ticks=duration;
    sample->wait_ticks=wait;
    sample->detail[0]=detail0; sample->detail[1]=detail1;
    sample->kind=kind; sample->code=code; sample->view=view; sample->cpl=cpl;
    // Publish after the record; debugger snapshots still stop ALL threads.
    __atomic_store_n(&sample->sequence,sequence,__ATOMIC_RELEASE);
    ring->total=sequence;
}
