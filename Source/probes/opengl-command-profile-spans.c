/* Actual host metadata/control implementation with a deterministic test clock.
 * No GPU, VM, guest, debugger or timing-performance claim. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <OpenGL/gl.h>
#include <mach/mach_time.h>
#include "../PassthroughOpenGL/passthrough-opengl-wire.h"
#include "../PassthroughOpenGL/direct-command-profile.h"
static uint64_t test_now=1000000;
static uint64_t test_clock(void){return test_now;}
static kern_return_t test_timebase(mach_timebase_info_t t){t->numer=1;t->denom=1;return KERN_SUCCESS;}
#define mach_absolute_time test_clock
#define mach_timebase_info test_timebase
#include "../PassthroughOpenGL/direct-command-profile-host.c"
static void DGSnapshotImportPolicy(uint32_t flags,struct DGSnapshotImportPolicyReport *r){(void)flags;(void)r;}
static void DGSnapshotRecyclePolicy(uint32_t flags,struct DGSnapshotRecyclePolicyReport *r){(void)flags;(void)r;}
static void arm(void)
{
    struct DGMessage m={.flags=DG_REPLY,.args={1,10000}};struct DGReply r={0};
    assert(!DGCommandProfileControl(&m,&r) && !r.error && r.result);
}
static void span(unsigned kind,uint64_t duration)
{
    struct DGCommandProfileStamp s=DGCommandProfileBegin();test_now+=duration;
    DGCommandProfileEnd(s,7,123,kind);
}
int main(void)
{
    assert(!DGCommandProfileBegin().generation);arm();
    span(1,499999);assert(!dg_profile_report.span_count);
    span(2,500000);assert(dg_profile_report.span_count==1 && dg_profile_report.spans[0].kind==2);
    span(0,700000);span(3,700000);assert(dg_profile_report.span_count==1);
    for(unsigned i=0;i<DG_PROFILE_SPANS+3;++i)span(1,600000);
    uint64_t count=dg_profile_report.span_count;assert(count==DG_PROFILE_SPANS+4);
    for(uint64_t seq=count-DG_PROFILE_SPANS+1;seq<=count;++seq){
        const struct DGCommandProfileSpan *s=&dg_profile_report.spans[(seq-1)%DG_PROFILE_SPANS];
        assert(s->sequence==seq && s->kind==1 && s->ticks==600000 && s->bytes==123);
    }
    struct DGCommandProfileStamp crossing=DGCommandProfileBegin();
    test_now=dg_profile_deadline+1;DGCommandProfileEnd(crossing,8,9,1);
    assert(dg_profile_report.excluded==1 && dg_profile_report.span_count==count && !dg_profile_active);
    struct {struct DGMessage m;struct DGCommandProfileReport report;} read={0};
    read.m=(struct DGMessage){.flags=DG_REPLY|DG_OUTPUT_ONLY,.bytes=sizeof(read.report),.args={3}};
    struct DGReply reply={0};assert(!DGCommandProfileControl(&read.m,&reply) && !reply.error);
    assert(read.report.version==2 && read.report.span_count==count && sizeof(read.report)<65536);
    uint64_t prior=dg_profile_report.generation;arm();assert(dg_profile_report.generation>prior);
    assert(!dg_profile_report.span_count && !dg_profile_report.spans[0].sequence);
    DGCommandProfileEnd(crossing,8,9,1);assert(!dg_profile_report.span_count && !dg_profile_report.excluded);
    puts("PASS host span threshold, outer-only selection, ring wrap, wire read, expiry, reset and stale generations");
    return 0;
}
