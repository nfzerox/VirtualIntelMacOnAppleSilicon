#include "../Sources/CEngine/include/compat_segments.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const unsigned es=0,ds=3,fs=4,gs=5;
    for(unsigned dpl=0;dpl<4;dpl++)for(unsigned rpl=0;rpl<4;rpl++) {
        uint32_t flags=0x00cf9300|(dpl<<13);
        uint32_t bad=engine_compat_segment_bad(ds,0x10|rpl,flags,0,UINT32_MAX);
        assert(!!engine_compat_segments_bad(bad,0)==(dpl<rpl));
        assert(!!engine_compat_segments_bad(bad,3)==(dpl!=3));
    }
    for(unsigned segment=0;segment<6;segment++) {
        if(segment==1||segment==2)continue;
        uint32_t bad=engine_compat_segment_bad(segment,0,0,0,0);
        assert(!!engine_compat_segments_bad(bad,0)==(segment<4));
        assert(!!engine_compat_segments_bad(bad,3)==(segment<4));
    }
    uint32_t kernel=engine_compat_segment_bad(ds,0x10,0x00cf9300,0,UINT32_MAX);
    assert(!engine_compat_segments_bad(kernel,0)&&engine_compat_segments_bad(kernel,3));
    uint32_t user=engine_compat_segment_bad(ds,0x1f,0x00cff300,0,UINT32_MAX);
    assert(!engine_compat_segments_bad(user,0)&&!engine_compat_segments_bad(user,3));
    for(unsigned segment=es;segment<=gs;segment++) {
        if(segment==1||segment==2)continue;
        uint32_t bad=engine_compat_segment_bad(segment,0x10,0x00cf9300,0x123000,0x5ff);
        assert(!!engine_compat_segments_bad(bad,0)==(segment<4));
        assert(engine_compat_segments_bad(bad,3));
    }
    assert(engine_compat_segments_bad(engine_compat_segment_bad(fs,0x10,0x00cf1300,0,UINT32_MAX),0)); // absent
    assert(engine_compat_segments_bad(engine_compat_segment_bad(gs,0x10,0x00cf9b00,0,UINT32_MAX),0)); // code
    assert(engine_compat_segments_bad(engine_compat_segment_bad(ds,0x10,0x00cf9700,0,UINT32_MAX),0)); // expand-down
    puts("PASS: separate supervisor/user segment policies, RPL/DPL, null and per-CPU GS selectors");
}
