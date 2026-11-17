// Host-side translator interface. The Swift control plane calls these while the vCPU is stopped in a hypervisor call.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "engine_abi.h"

enum {
    TR_REUSE_DISABLED, TR_REUSE_NO_SLOT, TR_REUSE_NOT_DEAD, TR_REUSE_RIP, TR_REUSE_KEY,
    TR_REUSE_NO_READ_COPY, TR_REUSE_CPL, TR_REUSE_COMPAT, TR_REUSE_FETCH, TR_REUSE_FRAME,
    TR_REUSE_SMP_ENTRY, TR_REUSE_BYTES, TR_REUSE_ENTRY_CAS, TR_REUSE_REJECT_KINDS
};
enum {
    TR_CI_SINGLE, TR_CI_UNAVAILABLE, TR_CI_CROSS_PAGE, TR_CI_PARTIAL, TR_CI_INCONSISTENT,
    TR_CI_MIXED_FRAME, TR_CI_UNPROTECTED, TR_CI_FETCH_FAULT, TR_CI_UNSUPPORTED, TR_CI_EMPTY,
    TR_CI_REJECT_KINDS
};

typedef struct {
    uint8_t *priv; // host mapping of private memory
    uint8_t *ram; // host mapping of x86 RAM
    uint64_t ram_size;
    bool precise_flags; // keep every arithmetic flag exact at every instruction that can fault
    uint64_t stub_lookup, stub_ret_miss, stub_pushf, stub_sahf; // guest VAs
    uint64_t code_cache_bytes; // 0 for all of it; tests shrink it to exercise the flush
    bool has_tzcnt_lzcnt; // CPUID advertises BMI1 and ABM; without them these encodings are bsf and bsr
    bool hybrid; // CR0.TS is live, so integer instructions must not need NEON outside a stub that can back out
    // Comparison switches, each giving up one optimisation to see whether a fault goes with it.
    bool keep_kernel_tables, no_bulk_rep, no_return_prediction, all_flags_live, no_reuse;
    uint64_t ram_below_4g; // 0: RAM is one piece. Otherwise GPA 4 GB and up is host RAM at this offset
    bool profile_invalidations; // opt-in: time and count hash probes for one of every 256 non-dead block kills
    bool invalidation_membership; // opt-in prototype: conservative view-membership bits for shared-block hash invalidation
    bool profile_shift_helpers; // opt-in: descriptor metadata for the per-vCPU monitor helper census
    bool inline_shr8; // experimental/default off: register-immediate byte SHR without the helper
    bool retain_shared_dead; // experimental/default off: preserve existing shared candidate hints across private-only resets
    bool profile_compile_inputs; // opt-in inert full-population analyzed-input recurrence census
    bool page_rep; // experimental/default off: new page-service stubs, otherwise scalar; never old bulk
} TrConfig;

typedef struct {
    uint64_t arm; // guest VA of the translation
    uint64_t fetch_fault_va; // nonzero: the instruction runs onto this unmapped or non-executable page
    uint64_t frames[2]; // GPA frames the block was read from (second is ~0 when unused)
    bool flushed; // the code cache was reset to make room: every pointer into it is stale
    int key; // the translation domain of the target: tr_link needs it
    uint64_t unprotected; // nonzero: nothing translated, as this frame must be write-protected first (gpa + 1)
} TrResult;

void tr_init(const TrConfig *config);

// Translates x86 `rip` in the given view. `cr3` and `cpl` describe the view. With `single` the block is exactly one
// instruction, never registered for lookup: it runs once after a self-modifying write.
TrResult tr_translate(int view, uint64_t cr3, int cpl, bool compat, uint64_t rip, bool single);

// Patches an exit stub: site is the guest VA reported in the request, kind the svc number, arm the linked target.
void tr_link(uint64_t site, int kind, uint64_t arm, int key);

// Installs a lookup entry in the view's indirect-branch table.
void tr_hash_insert(int view, uint64_t rip, uint64_t arm);

// Invalidates every block translated from a guest frame, in all views. Returns the count. why: a KILL_ value.
int tr_invalidate_frame(uint64_t gpa, uint32_t why);
// Invalidates the blocks of one view at one x86 page (its mapping changed). unmoved: the page still maps the same frame.
// keeper: this view holds the marks of a shared domain. Other views forget the domain's blocks here but do not kill them.
int tr_invalidate_view_va(int view, uint64_t va, uint32_t why, bool unmoved, bool keeper);
// Empties a view's lookup table, leaving every block alive.
void tr_view_forget(int view);
// Forgets a view being reused for another address space: its own blocks and its lookup table.
void tr_view_drop(int view);
// Forgets one whole view.
void tr_flush_all(void);
void tr_lookup_tail(uint64_t site, uint64_t target);
// Whether the next translation would need the code cache reset. With several vCPUs that takes them all out of the cache first, and then tr_flush_parked.
bool tr_needs_flush(void);
void tr_flush_parked(bool everything);

// Text for a stop reason written by the translator.
const char *tr_stop_reason(uint32_t index);

typedef struct {
    uint64_t blocks, insns, words;
    uint64_t invalidated;
    uint64_t translate_ns;
    uint64_t killed[KILL_KINDS], killed_unmoved[KILL_KINDS], kill_calls[KILL_KINDS];
    // Cold blocks at a rip and frame some earlier block already covers, live in another view or dead with the frame unchanged:
    // what translations keyed by frame would have saved. Looks at only the first few blocks on the frame, so these are lower bounds.
    uint64_t again_live, again_dead, reused, call_tails, jump_tails;
    uint64_t hash_refills, hash_forgets, cache_resets, cache_private_resets;
    uint64_t op_insns[128], op_words[128], exit_words; // cold code by x86 operation, the OP_ numbers in tr_internal.h; exit_words is what follows the last instruction
    uint64_t i386_blocks, i386_again; // cold 32-bit user blocks, and those again_live or again_dead counted
    uint64_t shared_blocks, shared_found; // translated into a shared user domain; found there by a view that had not looked before
    // Global translator totals, repeated in each vCPU tally: do not sum across vCPUs. All costs are unscaled sampled nanoseconds.
    uint64_t invalidation_profile_enabled, invalidation_profiled_kills, invalidation_sampled_kills;
    uint64_t invalidation_sampled_registered, invalidation_sampled_shared;
    uint64_t invalidation_hash_view_visits, invalidation_hash_probes, invalidation_hash_removals;
    uint64_t invalidation_map_unlink_ns, invalidation_hash_fanout_ns, invalidation_icache_ns;
    uint64_t invalidation_membership_enabled, invalidation_membership_reliable, invalidation_membership_bytes;
    uint64_t invalidation_membership_inserts, invalidation_membership_view_visits, invalidation_membership_stale_visits;
    uint64_t invalidation_membership_removals, invalidation_membership_tracking_failures, invalidation_membership_full_scans;
    // Opt-in global translator census (profile_invalidations). First rejected condition only, in original check order.
    uint64_t reuse_attempts, reuse_reject[TR_REUSE_REJECT_KINDS];
    uint64_t inline_shr8_enabled; // configuration state, not a counter or per-vCPU value
    // Global, repeated in each vCPU tally. Enabled state is not a delta. Counts are populated only when enabled,
    // since tr_init: nonzero hint-slot actions per flush, not unique bodies, successful reuse, or saved time.
    uint64_t retain_shared_dead_enabled, shared_dead_retained_hints, shared_dead_cleared_hints;
    // Global translator census, repeated per vCPU: never sum. Full population (sampling_shift=0), no cached execution.
    // Empty/evicted means missing bounded history, not first-ever code. Reject is the first encountered reason.
    uint64_t compile_inputs_enabled, compile_inputs_available, compile_inputs_bytes, compile_inputs_allocation_failures;
    uint64_t compile_inputs_sampling_shift, compile_inputs_config_base;
    uint64_t compile_inputs_attempts, compile_inputs_admitted, compile_inputs_reject[TR_CI_REJECT_KINDS];
    uint64_t compile_inputs_empty, compile_inputs_evictions, compile_inputs_prior, compile_inputs_exact;
    uint64_t compile_inputs_coverage_mismatch, compile_inputs_bytes_mismatch;
    uint64_t compile_inputs_exact_same_key, compile_inputs_exact_different_key;
    uint64_t compile_inputs_exact_same_pfn, compile_inputs_exact_different_pfn;
    // Exact matches versus the preceding retained observation: index (different_key * 2 + different_pfn).
    uint64_t compile_inputs_exact_joint[4];
    uint64_t page_rep_enabled; // configuration state, global and repeated; not a counter or per-vCPU value
} TrStats;
TrStats tr_stats(void);

