/* Compiled ONLY into explicitly instrumented research executors. One bounded
 * arm/stop/read interval, fixed storage, no payload logging/polling/worker.
 * Completed command spans include native submission/waiting, NOT GPU time.
 * Batch and command totals are nested and must never be added together.
 * Mutex/generation guards prevent reset/stop racing other endpoint writers.
 * Crossing-boundary commands are counted as excluded, never as fast zeros.
 */
#include <mach/mach_time.h>
#include "direct-command-profile.h"
struct DGCommandProfileStamp {uint64_t generation,at;};
static pthread_mutex_t dg_profile_lock=PTHREAD_MUTEX_INITIALIZER;
static struct DGCommandProfileReport dg_profile_report;
static uint64_t dg_profile_active,dg_profile_serial,dg_profile_deadline;
static void DGSnapshotImportPolicy(uint32_t,struct DGSnapshotImportPolicyReport *);
static void DGSnapshotRecyclePolicy(uint32_t,struct DGSnapshotRecyclePolicyReport *);
static struct DGCommandProfileStamp DGCommandProfileBegin(void)
{
    struct DGCommandProfileStamp s={__atomic_load_n(&dg_profile_active,__ATOMIC_ACQUIRE),0};
    if(s.generation)s.at=mach_absolute_time();return s;
}
static void DGCommandProfileEnd(struct DGCommandProfileStamp stamp,uint32_t op,uint32_t bytes,int kind)
{
    if(!stamp.generation)return;
    uint64_t now=mach_absolute_time();pthread_mutex_lock(&dg_profile_lock);
    if(stamp.generation==dg_profile_report.generation){
        if(stamp.generation==dg_profile_active && stamp.at>=dg_profile_report.started && now<=dg_profile_deadline){
            struct DGCommandProfileRow *r=kind==2?&dg_profile_report.fast_reads:kind==1?&dg_profile_report.batches:
                &dg_profile_report.commands[DGCommandProfileIndex(op)];
            // Kind3 is an accepted compact ENVELOPE count/byte witness.
            // Its decoded native commands are profiled separately: charging
            // the outer span too would double-count actual GL execution.
            uint64_t ticks=kind==3?0:now-stamp.at;++r->calls;r->ticks+=ticks;r->bytes+=bytes;
            if(ticks>r->maximum)r->maximum=ticks;
        }else ++dg_profile_report.excluded;
        if(dg_profile_active && now>dg_profile_deadline){
            __atomic_store_n(&dg_profile_active,0,__ATOMIC_RELEASE);
            dg_profile_report.ended=dg_profile_deadline;
        }
    }
    pthread_mutex_unlock(&dg_profile_lock);
}
static int DGCommandProfileControl(struct DGMessage *m,struct DGReply *reply)
{
    if(m->context || m->args[0]<1 || m->args[0]>5)return -1;
    int policy=m->args[0]>=4,recycling=m->args[0]==5;
    for(unsigned i=m->args[0]==1 || policy?2:1;i<16;++i)if(m->args[i])return -1;
    int reading=m->args[0]==3;
    if(m->flags!=(DG_REPLY|((reading || policy)?DG_OUTPUT_ONLY:0)) ||
       m->bytes!=(reading?sizeof(struct DGCommandProfileReport):recycling?sizeof(struct DGSnapshotRecyclePolicyReport):policy?sizeof(struct DGSnapshotImportPolicyReport):0) ||
       (m->args[0]==1 && (!m->args[1] || m->args[1]>60000)))return -1;
    pthread_mutex_lock(&dg_profile_lock);uint64_t now=mach_absolute_time();
    if(policy){
        if(m->args[1]>(recycling?1u:3u) && m->args[1]!=UINT32_MAX)reply->error=GL_INVALID_VALUE;
        else if(dg_profile_active && now<=dg_profile_deadline)reply->error=GL_INVALID_OPERATION;
        else if(recycling)DGSnapshotRecyclePolicy((uint32_t)m->args[1],(void *)(m+1));
        else DGSnapshotImportPolicy((uint32_t)m->args[1],(void *)(m+1));
    }else if(m->args[0]==1){
        if(dg_profile_active && now<=dg_profile_deadline)reply->error=GL_INVALID_OPERATION;
        else if(dg_profile_serial==UINT64_MAX)reply->error=GL_OUT_OF_MEMORY;
        else{
            __atomic_store_n(&dg_profile_active,0,__ATOMIC_RELEASE);
            memset(&dg_profile_report,0,sizeof(dg_profile_report));
            mach_timebase_info_data_t t;mach_timebase_info(&t);
            dg_profile_report.version=1;dg_profile_report.numer=t.numer;dg_profile_report.denom=t.denom;
            dg_profile_report.generation=++dg_profile_serial;dg_profile_report.started=now;
            uint64_t delta=m->args[1]*UINT64_C(1000000)*t.denom/t.numer;
            dg_profile_deadline=now+delta;
            __atomic_store_n(&dg_profile_active,dg_profile_serial,__ATOMIC_RELEASE);
        }
    }else if(m->args[0]==2){
        if(dg_profile_active){
            __atomic_store_n(&dg_profile_active,0,__ATOMIC_RELEASE);
            dg_profile_report.ended=now<dg_profile_deadline?now:dg_profile_deadline;
        }
    }else{
        if(dg_profile_active && now>dg_profile_deadline){
            __atomic_store_n(&dg_profile_active,0,__ATOMIC_RELEASE);dg_profile_report.ended=dg_profile_deadline;
        }
        if(dg_profile_active)reply->error=GL_INVALID_OPERATION;
        else memcpy(m+1,&dg_profile_report,sizeof(dg_profile_report));
    }
    reply->result=dg_profile_report.generation;pthread_mutex_unlock(&dg_profile_lock);return 0;
}
