// Native diagnostic-storage test; no guest, process attachment or timing claim.
#include "latency.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>

static void *writer(void *opaque) {
    uint32_t cpu=(uint32_t)(uintptr_t)opaque;
    for(uint64_t i=1;i<=10000;i++)
        engine_latency_record(cpu,ENGINE_LATENCY_HOST_CALL,cpu,i*100,7,3,i,i+1,cpu+1,3);
    return NULL;
}
int main(void) {
    pthread_t threads[ENGINE_LATENCY_CPUS];
    for(unsigned i=0;i<ENGINE_LATENCY_CPUS;i++)assert(!pthread_create(&threads[i],NULL,writer,(void *)(uintptr_t)i));
    for(unsigned i=0;i<ENGINE_LATENCY_CPUS;i++)assert(!pthread_join(threads[i],NULL));
    engine_latency_record(ENGINE_LATENCY_CPUS,1,1,1,1,1,1,1,1,1);
    for(unsigned cpu=0;cpu<ENGINE_LATENCY_CPUS;cpu++) {
        EngineLatencyRing *ring=&engine_latency_rings[cpu];
        assert(ring->total==10000);
        for(uint64_t n=10000-ENGINE_LATENCY_SAMPLES+1;n<=10000;n++) {
            EngineLatencySample *s=&ring->samples[(n-1)%ENGINE_LATENCY_SAMPLES];
            assert(s->sequence==n && s->code==cpu && s->view==cpu+1 && s->cpl==3);
            assert(s->start_ticks==n*100 && s->duration_ticks==7 && s->wait_ticks==3);
            assert(s->detail[0]==n && s->detail[1]==n+1);
        }
    }
    puts("PASS bounded latency rings: 8 independent writers, 80000 records, exact newest 128, wrap and CPU bound");
    return 0;
}
