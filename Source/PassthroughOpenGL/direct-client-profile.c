/* No production instrumentation: included only by the explicit client-profile
 * build. A stopped, exact-UUID debugger may arm/reset metadata without inferior
 * function calls. Unique generations and an inflight counter protect resets;
 * NEVER reset an active/inflight interval. Locks are held only to record fixed
 * metadata, never across GL, transport, another lock or a pixel operation.
 */
#include "direct-client-profile.h"
static pthread_mutex_t dg_client_profile_lock=PTHREAD_MUTEX_INITIALIZER;
static struct DGClientProfileReport dg_client_profile_report __attribute__((used));
/* Bounded source-clock evidence for the actual backing calls outside DGSubmit.
 * Separate optional debugger metadata: the existing report/wire stays intact.
 * Filled under the same metadata lock, with no extra clock or hot-path log. */
static uint64_t dg_client_backing_slow[32][4] __attribute__((used));
static uint32_t dg_client_backing_slow_count __attribute__((used));
/* A debugger is the arm/reset writer in the actual dylib. Unlike the native
 * fixture, this translation unit never stores a nonzero generation itself.
 * Keep external writes observable: otherwise global optimization can fold the
 * entire profiler away while a fixture that arms it from C still passes. */
static volatile uint64_t dg_client_profile_active __attribute__((used));
static uint64_t dg_client_profile_serial __attribute__((used)),dg_client_profile_deadline;
static uint32_t dg_client_profile_inflight __attribute__((used));
static volatile uint32_t dg_client_profile_ms __attribute__((used))=10000;
/* Explicit low-rate mode: no command/pointer/draw/transport-lock clocks or
 * metadata locks. Measure only actual backing wait/begin/commit boundaries. */
static volatile uint32_t dg_client_profile_backing_only __attribute__((used));
struct DGClientProfileStamp {uint64_t generation,at;uint32_t kind,index,bytes;};
static struct DGClientProfileRow *DGClientProfileRow(unsigned kind,unsigned index)
{
    if(kind<2 && index<DG_PROFILE_ROWS)return &dg_client_profile_report.submissions[kind][index];
    if(kind==2 && index<2)return &dg_client_profile_report.transfers[index];
    if(kind==3 && index<3)return &dg_client_profile_report.pointers[index];
    return kind==5?&dg_client_profile_report.locks:&dg_client_profile_report.draws;
}
static struct DGClientProfileStamp DGClientProfileBegin(unsigned kind,uint32_t op,size_t bytes)
{
    struct DGClientProfileStamp s={0};
    uint64_t generation=__atomic_load_n(&dg_client_profile_active,__ATOMIC_ACQUIRE);
    if(!generation)return s;
    if(dg_client_profile_backing_only && (kind!=1 ||
       (op!=DG_PROFILE_CLIENT_BACKING_WAIT && op!=DG_PROFILE_CLIENT_BACKING_BEGIN && op!=DG_PROFILE_CLIENT_BACKING_COMMIT)))return s;
    __atomic_fetch_add(&dg_client_profile_inflight,1,__ATOMIC_ACQ_REL);
    uint64_t now=mach_absolute_time();pthread_mutex_lock(&dg_client_profile_lock);
    if(generation==dg_client_profile_active){
        if(!dg_client_profile_report.started && dg_client_profile_ms && dg_client_profile_ms<=60000){
            mach_timebase_info_data_t t;
            if(mach_timebase_info(&t)==KERN_SUCCESS && t.numer && t.denom){
                dg_client_profile_report.version=1;dg_client_profile_report.numer=t.numer;dg_client_profile_report.denom=t.denom;
                dg_client_profile_report.generation=generation;dg_client_profile_report.started=now;
                dg_client_profile_deadline=now+(uint64_t)dg_client_profile_ms*1000000*t.denom/t.numer;
            }
        }
        if(dg_client_profile_report.started && now<=dg_client_profile_deadline){
            s=(struct DGClientProfileStamp){generation,now,kind,kind<2?DGCommandProfileIndex(op):op,
                (uint32_t)(bytes>UINT32_MAX?UINT32_MAX:bytes)};
            ++DGClientProfileRow(s.kind,s.index)->entered;
        }else{
            __atomic_store_n(&dg_client_profile_active,0,__ATOMIC_RELEASE);
            dg_client_profile_report.ended=dg_client_profile_deadline;
        }
    }
    pthread_mutex_unlock(&dg_client_profile_lock);
    if(!s.generation)__atomic_fetch_sub(&dg_client_profile_inflight,1,__ATOMIC_ACQ_REL);
    return s;
}
static void DGClientProfileEnd(struct DGClientProfileStamp s)
{
    if(!s.generation)return;
    uint64_t now=mach_absolute_time();pthread_mutex_lock(&dg_client_profile_lock);
    if(s.generation==dg_client_profile_report.generation){
        if(s.generation==dg_client_profile_active && now<=dg_client_profile_deadline){
            struct DGClientProfileRow *r=DGClientProfileRow(s.kind,s.index);uint64_t ticks=now-s.at;
            ++r->completed;r->ticks+=ticks;r->bytes+=s.bytes;if(ticks>r->maximum)r->maximum=ticks;
            if(s.kind==1 && (s.index==DGCommandProfileIndex(DG_PROFILE_CLIENT_BACKING_COMMIT) ||
                s.index==DGCommandProfileIndex(DG_PROFILE_CLIENT_BACKING_BEGIN) ||
                s.index==DGCommandProfileIndex(DG_PROFILE_CLIENT_BACKING_WAIT)) &&
                ticks>=(UINT64_C(20000000)*dg_client_profile_report.denom)/dg_client_profile_report.numer &&
                dg_client_backing_slow_count<32){
                uint64_t *slow=dg_client_backing_slow[dg_client_backing_slow_count++];
                slow[0]=s.generation;slow[1]=UINT64_C(0x10000)+s.index-974;
                slow[2]=s.at;slow[3]=now;
            }
            if(now>dg_client_profile_report.ended)dg_client_profile_report.ended=now;
        }else ++dg_client_profile_report.excluded;
        if(dg_client_profile_active && now>dg_client_profile_deadline){
            __atomic_store_n(&dg_client_profile_active,0,__ATOMIC_RELEASE);
            dg_client_profile_report.ended=dg_client_profile_deadline;
        }
    }
    pthread_mutex_unlock(&dg_client_profile_lock);
    __atomic_fetch_sub(&dg_client_profile_inflight,1,__ATOMIC_ACQ_REL);
}
