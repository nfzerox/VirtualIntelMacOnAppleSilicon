/* Process-local source profiling, explicit research builds only. This is
 * debugger-readable fixed metadata, NOT a renderer/guest/host wire ABI. */
#ifndef VIM_DIRECT_CLIENT_PROFILE_H
#define VIM_DIRECT_CLIENT_PROFILE_H
#include "direct-command-profile.h"
/* Source-only metadata row, NOT an executable packet. This actual kernel
 * struct call is outside DGSubmit and was absent from the earlier totals. */
#define DG_PROFILE_CLIENT_BACKING_COMMIT 0x100f4u
#define DG_PROFILE_CLIENT_BACKING_WAIT 0x100f5u
#define DG_PROFILE_CLIENT_BACKING_BEGIN 0x100f6u
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
#include <stddef.h>
/* Research GLI routes only its already-contended call through this wrapper.
 * The real method, result and ownership contract are unchanged. This permits
 * clock-only measurement without a pid/FBT trap on every submitted GL call. */
int32_t DGClientProfileWaitSurfaceBacking(uint32_t connection,void *mapping,size_t *size);
#endif
struct DGClientProfileRow {uint64_t entered,completed,ticks,maximum,bytes;};
struct DGClientProfileReport {
    uint32_t version,numer,denom,reserved;
    uint64_t generation,started,ended,excluded;
    struct DGClientProfileRow submissions[2][DG_PROFILE_ROWS]; /* async, sync */
    struct DGClientProfileRow transfers[2]; /* read, write; nested */
    struct DGClientProfileRow pointers[3]; /* input, output clear, output seed */
    struct DGClientProfileRow draws; /* complete client array assembly, nested */
    struct DGClientProfileRow locks; /* actual shared transport mutex acquisition */
};
_Static_assert(sizeof(struct DGClientProfileRow)==40,"fixed client timing row");
_Static_assert(sizeof(struct DGClientProfileReport)==48+40*(2*DG_PROFILE_ROWS+7),"fixed client report in both guest widths");
#endif
