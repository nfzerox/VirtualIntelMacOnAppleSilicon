/* Actual shared monitor classifier: native, finite, no HV/guest execution. */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "x87_helper_profile.h"
static EngineCtx states[2];
_Static_assert(offsetof(EngineCtx,profile_x87_helpers)==offsetof(EngineCtx,tsc_counter_offset)+sizeof(uint64_t),"append only");
int main(void)
{
    EngineCtx *c=&states[0];
    for(unsigned op=0;op<65536;++op)engine_profile_x87_helper(c,op);
    EngineCtx zero={0};assert(!memcmp(c,&zero,sizeof(zero)));
    unsigned char prefix[offsetof(EngineCtx,profile_x87_helpers)];
    memset(c,0xa5,sizeof(prefix));memcpy(prefix,c,sizeof(prefix));
    c->profile_x87_helpers=1;
    for(unsigned op=0;op<0x800;++op)engine_profile_x87_helper(c,op);
    for(unsigned i=0;i<64;++i)assert(c->stat_x87_helpers[i]==24);
    for(unsigned i=64;i<576;++i)assert(c->stat_x87_helpers[i]==1);
    engine_profile_x87_helper(c,0xffff);assert(c->stat_x87_helpers[576]==1);
    engine_profile_x87_helper(c,0x800);engine_profile_x87_helper(c,0x12345678);
    assert(c->stat_x87_helpers_unclassified==2);
    assert(!memcmp(prefix,c,sizeof(prefix))); // All architectural/previous state untouched.
    assert(!memcmp(&states[1],&zero,sizeof(zero)));
    states[1].profile_x87_helpers=1;engine_profile_x87_helper(&states[1],0x1c9);
    assert(states[1].stat_x87_helpers[64+64+9]==1);
    assert(c->stat_x87_helpers[64+64+9]==1);
    puts("PASS x87 helper census: disabled untouched, all2048 encodings, FWAIT, invalid and separate contexts");
    printf("EngineCtx bytes=%zu slot=%u (no existing field moves)\n",sizeof(EngineCtx),(unsigned)CTX_SLOT);
    return 0;
}
