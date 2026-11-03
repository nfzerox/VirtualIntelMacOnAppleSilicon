/* Explicit laboratory build only. Fixed-budget source clocks around our own
 * ownership handoff, not FBT, kernel patching or a renderer protocol change.
 * 4096 calls/site, at most32 slow records/site; no timer, allocation, sleeping,
 * stack capture, user data or pointer output. Disabled builds have NO clock,
 * counter or log operation. Raw Mach ticks share the client profiler's clock.
 */
#ifndef VIM_HANDOFF_PROFILE_H
#define VIM_HANDOFF_PROFILE_H
#ifdef VIM_OPENGL_HANDOFF_PROFILE
#ifndef VIM_HANDOFF_PROFILE_TEST
#include <kern/clock.h>
#include <libkern/libkern.h>
#endif
struct VIMHandoffProfileState {
    volatile unsigned admitted,completed,slow;
    volatile uint64_t total,maximum;
};
class VIMHandoffProfileSpan {
    VIMHandoffProfileState* state;
    const char* tag;
    unsigned serial;
    uint64_t times[8];
    VIMHandoffProfileSpan(VIMHandoffProfileSpan const&);
    VIMHandoffProfileSpan& operator=(VIMHandoffProfileSpan const&);
public:
    VIMHandoffProfileSpan(VIMHandoffProfileState* value,const char* name,bool enabled):
        state(0),tag(name),serial(0) {
        if(!enabled)return;
        unsigned count=value->admitted;
        while(count<4096){
            unsigned prior=__sync_val_compare_and_swap(&value->admitted,count,count+1);
            if(prior==count){state=value;serial=count+1;break;}
            count=prior;
        }
        if(state){for(unsigned i=0;i<8;++i)times[i]=0;clock_get_uptime(&times[0]);}
    }
    void mark(unsigned index){if(state && index && index<7)clock_get_uptime(&times[index]);}
    ~VIMHandoffProfileSpan(){
        if(!state)return;
        clock_get_uptime(&times[7]);
        uint64_t elapsed=times[7]-times[0],ns=0;
        __sync_fetch_and_add(&state->total,elapsed);
        uint64_t maximum=state->maximum;
        while(elapsed>maximum){
            uint64_t prior=__sync_val_compare_and_swap(&state->maximum,maximum,elapsed);
            if(prior==maximum)break;maximum=prior;
        }
        unsigned done=__sync_add_and_fetch(&state->completed,1);
        absolutetime_to_nanoseconds(elapsed,&ns);
        if(ns>=20000000 && __sync_fetch_and_add(&state->slow,1)<32){
            IOLog("VIM-HANDOFF v=1 tag=%s serial=%u begin=%llu end=%llu marks=%llu,%llu,%llu,%llu,%llu,%llu\n",
                tag,serial,times[0],times[7],times[1],times[2],times[3],times[4],times[5],times[6]);
        }
        // Positive coverage, bounded16 summaries/site. Concurrent aggregate
        // reads are diagnostic snapshots, NOT a transactional critical path.
        if(!(done&255))IOLog("VIM-HANDOFF summary v=1 tag=%s completed=%u total_ticks=%llu maximum_ticks=%llu admitted=%u\n",
            tag,done,state->total,state->maximum,state->admitted);
    }
};
#define VIM_HANDOFF_SPAN_IF(name,tag,condition) \
    static VIMHandoffProfileState name##_state={0,0,0,0,0}; \
    VIMHandoffProfileSpan name(&name##_state,tag,condition)
#define VIM_HANDOFF_SPAN(name,tag) VIM_HANDOFF_SPAN_IF(name,tag,true)
#define VIM_HANDOFF_MARK(name,index) name.mark(index)
#else
#define VIM_HANDOFF_SPAN_IF(name,tag,condition) do {} while(0)
#define VIM_HANDOFF_SPAN(name,tag) do {} while(0)
#define VIM_HANDOFF_MARK(name,index) do {} while(0)
#endif
#endif
