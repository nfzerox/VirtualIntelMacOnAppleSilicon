/* Metadata implementation controls, NOT a GPU/desktop performance test. */
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#ifndef VIM_HANDOFF_DISABLED_CONTROL
static uint64_t fake_now=1000000000,reads;
static unsigned slow_logs,summaries;
static void clock_get_uptime(uint64_t* value){++reads;*value=fake_now;fake_now+=10;}
static void absolutetime_to_nanoseconds(uint64_t value,uint64_t* result){*result=value;}
static void IOLog(const char* format,...){
    if(strstr(format,"summary"))++summaries;else ++slow_logs;
}
#define VIM_OPENGL_HANDOFF_PROFILE 1
#define VIM_HANDOFF_PROFILE_TEST 1
#endif
#include "../vendor/source/VMsvga2/vminclude/vim_handoff_profile.h"
int main(){
#ifdef VIM_HANDOFF_DISABLED_CONTROL
    unsigned side_effect=0;
    VIM_HANDOFF_SPAN_IF(span,"disabled",++side_effect);
    VIM_HANDOFF_MARK(span,++side_effect);
    assert(!side_effect);
    puts("PASS default-disabled build needs no clock/log API and evaluates no diagnostic arguments");
#else
    VIMHandoffProfileState state={0,0,0,0,0};
    {VIMHandoffProfileSpan span(&state,"refused",false);span.mark(1);}
    assert(!reads && !state.admitted && !state.completed);
    for(unsigned i=0;i<5000;++i){
        VIMHandoffProfileSpan span(&state,"control",true);
        span.mark(1);fake_now+=i<40?30000000:0;span.mark(2);
    }
    assert(state.admitted==4096 && state.completed==4096 && state.slow==40);
    assert(slow_logs==32 && summaries==16 && state.maximum==30000030);
    assert(reads==4096*4);
    assert(state.total==uint64_t(40)*30000000+4096*30);
    puts("PASS finite4096 source-clock admissions, positive stage marks,32 slow records,16 summaries, no disabled/expired clocks");
#endif
    return 0;
}
