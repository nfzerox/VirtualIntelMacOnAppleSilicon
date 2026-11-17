/* Explicit research-only command timing wire format. No clocks, state or
 * instrumentation is added to ordinary executors by including this header. */
#ifndef VIM_DIRECT_COMMAND_PROFILE_H
#define VIM_DIRECT_COMMAND_PROFILE_H
#include <stdint.h>
#define DG_CAP_COMMAND_PROFILE (UINT64_C(1)<<52)
#define DG_CAP_SNAPSHOT_IMPORT_POLICY (UINT64_C(1)<<53) // Explicit research executor ONLY.
#define DG_CAP_SNAPSHOT_RECYCLE_POLICY (UINT64_C(1)<<54)
#define DG_COMMAND_PROFILE 0x10056u
#define DG_PROFILE_ROWS (974u+256u+1u)
#define DG_PROFILE_UNKNOWN (DG_PROFILE_ROWS-1)
/* Nested host-only attribution rows, NOT executable wire opcodes. Never add
 * these to command/batch totals: they overlap actual submitted operations. */
#define DG_PROFILE_BACKING_SUBSCRIBE 0x100f0u
#define DG_PROFILE_BACKING_RETIRE 0x100f1u
#define DG_PROFILE_COMPLETION_WAIT 0x100f2u
#define DG_PROFILE_COMPLETION_DELETE 0x100f3u
struct DGCommandProfileRow { uint64_t calls,ticks,maximum,bytes; };
#define DG_PROFILE_SPANS 512u
/* Completed outer batches/fast reads >=0.5ms. Host Mach timestamps align
 * with the presentation journal. Endpoints can overlap; not additive GPU time. */
struct DGCommandProfileSpan { uint64_t sequence,started,ticks;uint32_t kind,bytes; };
struct DGCommandProfileReport {
    uint32_t version,numer,denom,reserved;
    uint64_t generation,started,ended,excluded;
    struct DGCommandProfileRow commands[DG_PROFILE_ROWS];
    struct DGCommandProfileRow batches;
    struct DGCommandProfileRow fast_reads;
    uint64_t span_count;
    struct DGCommandProfileSpan spans[DG_PROFILE_SPANS];
};
struct DGSnapshotImportPolicyReport {
    uint32_t version,flags; // bit0: source sharing; bit1: empty FBO reuse
    uint64_t source_reuse_attempts,source_import_attempts,framebuffer_allocations;
};
struct DGSnapshotRecyclePolicyReport {
    uint32_t version,flags; // bit0: track real consumer completion for source reuse
    uint64_t allocations,reuses,consumer_fences,reuse_waits,foreign_refusals,tracking_refusals;
};
_Static_assert(sizeof(struct DGSnapshotRecyclePolicyReport)==56,"fixed snapshot recycle metadata");
_Static_assert(sizeof(struct DGSnapshotImportPolicyReport)==32,"fixed snapshot policy metadata");
_Static_assert(sizeof(struct DGCommandProfileRow)==32,"fixed timing row in both guest widths");
_Static_assert(sizeof(struct DGCommandProfileSpan)==32,"fixed host span in both guest widths");
_Static_assert(sizeof(struct DGCommandProfileReport)==56+32*(DG_PROFILE_ROWS+2+DG_PROFILE_SPANS),"fixed timing report in both guest widths");
static unsigned DGCommandProfileIndex(uint32_t op)
{return op<974?op:op>=0x10000 && op<0x10100?974+op-0x10000:DG_PROFILE_UNKNOWN;}
#endif
