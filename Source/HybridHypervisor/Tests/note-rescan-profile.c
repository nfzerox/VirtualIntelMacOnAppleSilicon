// Actual recorder implementation, no guest/clock/debugger or timing claim.
#include "note-rescan-profile.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    EngineNoteRescanRing rings[2]={0};
    for(unsigned i=1;i<=200;i++) {
        rings[0].active_start_ticks=i*100;
        engine_note_rescan_record(&rings[0],i*100,i+1,i+2,i+3);
        assert(!rings[0].active_start_ticks && rings[0].total==i);
    }
    assert(!rings[1].total && !rings[1].active_start_ticks);
    for(unsigned i=137;i<=200;i++) {
        EngineNoteRescanSample *s=&rings[0].samples[(i-1)%64];
        assert(s->sequence==i && s->start_ticks==i*100 &&
            s->duration_ticks==i+1 && s->code_frames==i+2 && s->table_frames==i+3);
    }
    engine_note_rescan_record(&rings[1],1,0,0,0);
    assert(rings[1].total==1 && rings[1].samples[0].sequence==1 && rings[0].total==200);
    puts("PASS note rescan recorder: every event, exact newest64, wrap, zero duration, separate CPUs");
    return 0;
}
