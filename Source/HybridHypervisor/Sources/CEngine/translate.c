// Block builder: decodes x86 into a superblock, runs flag liveness, emits AArch64 and side exits, and owns block bookkeeping.
#include <libkern/OSCacheControl.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "tr_internal.h"

#define MAX_INSNS 96
#define MAX_PENDING (MAX_INSNS * 2)
#define MAP_BITS 22
#define MAP_SIZE (1u << MAP_BITS)

u32 *tr_cc;
bool tr_precise_flags, tr_has_tzcnt, tr_hybrid, tr_no_bulk_rep;
bool tr_fpcr_merges_scalar;
bool tr_profile_shift_helpers;
bool tr_inline_shr8;
bool tr_page_rep;
int tr_cpl;
bool tr_compat;
u64 tr_stub_lookup, tr_stub_ret_miss, tr_stub_pushf, tr_stub_sahf;

static TrConfig cfg;
static u32 *cc_base;
static u64 cc_limit;
static BlockEntry *block_table;
static PcMap *pc_map;
// The code cache has two parts, each with its share of the block table and pc maps. Shared domains go in the first: the kernel and system libraries, which every
// process needs and which live long. Blocks of one view go in the second, and most die with their process, unusable until a reset. Links join blocks of one key, so
// none crosses between the parts, and the second can reset alone without taking the first with it.
#define PCMAP_ENTRIES (PCMAP_BYTES / sizeof(PcMap))
static u32 pc_map_used[2] = {0, (OFF_PCMAP2 - OFF_PCMAP) / sizeof(PcMap)};
static u32 *cc_cursor[2];
int cur_part;
static EngineCtx *ctx_ptr;

typedef struct {
    u64 rip;
    u32 view;
    u32 entry_word; // arm_off
    u64 frame[2]; // ~0 when unused
    u32 next_on_frame[2]; // block index + 1
    u32 next_on_page[2]; // block index + 1: the x86 pages it was read from, for a change to their mappings
    u32 next_in_view; // block index + 1: blocks of one view, not of a shared kernel domain
    bool dead, registered;
    // For reuse. Only a block on one frame is ever reused, so the chains here are its first ones.
    bool on_frame, on_page, on_view, compat;
    u8 cpl;
    u16 read_off, read_bytes; // what translating it read of its frame, a copy of which is at read_copy. 0 bytes: never reused
    u32 read_copy;
    u32 first_word; // its entry word, which a kill overwrites
} HostBlock;
static HostBlock *hblocks;
static u32 hblock_count[2];
// Only domain blocks (part 0) need this sidecar: 32 bytes * BLOCKS_MAX = 16 MiB, allocated only for the prototype.
// Bits are a superset: replacement, forgetting and view reuse may leave stale bits, never omit a published target.
static u64 (*shared_hash_views)[MAX_VIEWS / 64];
#define BLOCK_BASE(part) ((part) ? BLOCKS_MAX : 0)
#define BLOCK_TABLE_PART2 ((OFF_BLOCKS2 - OFF_BLOCKS) / sizeof(BlockEntry))
#define KEY_PART(key) (((key) & KEY_DOMAIN) ? 0 : 1)
static u32 *frame_head; // per GPA frame: block index + 1
#define PAGE_HEADS (1u << 18)
static u32 *page_head; // by x86 page number, modulo PAGE_HEADS: block index + 1
static u32 view_head[MAX_VIEWS]; // block index + 1

static inline u32 page_slot(u64 va) { return (u32)(va >> 12) & (PAGE_HEADS - 1); }

// Lookup: (view, rip) to block index + 1. Open addressing, deleted entries keep probing.
typedef struct {
    u64 rip;
    u32 view;
    u32 block; // 0 empty, ~0 deleted, otherwise index + 1
} MapEntry;
static MapEntry *block_map;

static TrStats stats;
static mach_timebase_info_data_t invalidation_timebase;

static u64 invalidation_ns(u64 ticks) {
    return (u64)((__uint128_t)ticks * invalidation_timebase.numer / invalidation_timebase.denom);
}

static const char *stop_reasons[4096];
static u32 stop_reason_count;

u64 tr_cc_va(const u32 *p) { return PRIV_VA + OFF_CC + (u64)((const u8 *)p - (const u8 *)cc_base); }
static u32 *cc_host(u64 va) { return (u32 *)((u8 *)cc_base + (va - PRIV_VA - OFF_CC)); }

static u32 map_hash(int view, u64 rip) { return (u32)((rip ^ ((u64)view << 40)) * 0x9E3779B97F4A7C15ULL >> (64 - MAP_BITS)); }

static u32 map_find(int view, u64 rip) {
    for (u32 h = map_hash(view, rip);; h = (h + 1) & (MAP_SIZE - 1)) {
        MapEntry *e = &block_map[h];
        if (!e->block)
            return 0;
        if (e->block != ~0u && e->rip == rip && e->view == (u32)view)
            return e->block;
    }
}
static void map_insert(int view, u64 rip, u32 block) {
    for (u32 h = map_hash(view, rip);; h = (h + 1) & (MAP_SIZE - 1)) {
        MapEntry *e = &block_map[h];
        if (!e->block || e->block == ~0u) {
            e->rip = rip;
            e->view = (u32)view;
            e->block = block;
            return;
        }
    }
}
static void map_remove(int view, u64 rip) {
    for (u32 h = map_hash(view, rip);; h = (h + 1) & (MAP_SIZE - 1)) {
        MapEntry *e = &block_map[h];
        if (!e->block)
            return;
        if (e->block != ~0u && e->rip == rip && e->view == (u32)view) {
            e->block = ~0u;
            return;
        }
    }
}

static u8 *ram_at(u64 gpa) { return cfg.ram + (cfg.ram_below_4g && gpa >= 1ULL << 32 ? gpa - (1ULL << 32) + cfg.ram_below_4g : gpa); }
static bool is_ram(u64 gpa) { return gpa < cfg.ram_size && (cfg.priv[OFF_PAGEINFO + (gpa >> 12)] & PI_RAM); }

// Ring-0 code in the high half is shared by every address space that reaches it through the same PML4 entry target: an x86 kernel keeps
// those entries identical, and a change to them invalidates through the page-table write path. Everything else belongs to one view.
#define KEY_DOMAIN 0x40000000
static int view_high;
// A rip under a page directory that several address spaces share maps the same bytes in all of them, so one translation serves them all. The last page stays out,
// as a block starting there could run into the next directory.
static int shared_user_key(u64 cr3, int cpl, u64 rip) {
    bool compat = tr_compat && engine_shared_compat_address(rip);
    if ((cpl != 3 && !(compat && cpl == 0)) || !ctx_ptr->shared_dir_count ||
        (!compat && (tr_compat || rip < SHARED_USER_BASE || rip >= SHARED_USER_END - 4096)))
        return 0;
    u64 table = cr3 & 0x000FFFFFFFFFF000ULL;
    if (!is_ram(table))
        return 0;
    u64 e = *(u64 *)(ram_at(table) + ((rip >> 39) & 0x1ff) * 8);
    table = e & 0x000FFFFFFFFFF000ULL;
    if (!(e & 1) || !is_ram(table))
        return 0;
    e = *(u64 *)(ram_at(table) + ((rip >> 30) & 0x1ff) * 8);
    if (!(e & 1) || (e & 0x80))
        return 0;
    u64 directory = e & 0x000FFFFFFFFFF000ULL;
    if (compat) {
        if (!is_ram(directory))
            return 0;
        e = *(u64 *)(ram_at(directory) + ((rip >> 21) & 0x1ff) * 8);
        if (!(e & 1) || (e & 0x80))
            return 0;
        u64 page_table = e & 0x000FFFFFFFFFF000ULL;
        if (!is_ram(page_table))
            return 0;
        directory = engine_shared_compat_domain(rip, page_table);
        if (cpl == 0)
            directory |= SHARED_COMPAT_KERNEL;
    }
    for (u32 i = 0; i < ctx_ptr->shared_dir_count; i++)
        if (ctx_ptr->shared_dirs[i] == directory)
            return compat ? (int)(0xC0000000u | i) : KEY_DOMAIN | (int)((directory >> 12) & 0x3FFFFFFF);
    return 0;
}

static int block_key(int view, u64 cr3, int cpl, u64 rip) {
    if (cpl == 0 && (rip >> 47)) {
        u64 table = cr3 & 0x000FFFFFFFFFF000ULL;
        if (is_ram(table)) {
            u64 e = *(u64 *)(ram_at(table) + ((rip >> 39) & 0x1ff) * 8);
            return KEY_DOMAIN | (int)((e >> 12) & 0x3FFFFFFF);
        }
    }
    int shared = shared_user_key(cr3, cpl, rip);
    return shared ? shared : view;
}

// ---- Guest memory, host's read-only view ----
static u64 walk_cr3;
static int walk_cpl;
#define X_P 1ULL
#define X_US 4ULL
#define X_PS 0x80ULL
#define X_NX (1ULL << 63)
#define X_ADDR 0x000FFFFFFFFFF000ULL

// Instruction-fetch walk. Returns false for anything that would be a fetch fault. Permission intersection as the monitor does it.
static bool fetch_walk(u64 va, u64 *gpa) {
    u64 table = walk_cr3 & X_ADDR;
    bool user = true, nx = false;
    for (int level = 3; level >= 0; level--) {
        if (!is_ram(table))
            return false;
        u64 e = *(u64 *)(ram_at(table) + ((va >> (12 + 9 * level)) & 0x1ff) * 8);
        if (!(e & X_P))
            return false;
        user = user && (e & X_US);
        nx = nx || (e & X_NX);
        if (level && level < 3 && (e & X_PS)) {
            u64 size = 1ULL << (12 + 9 * level);
            u64 base = e & X_ADDR & ~(size - 1);
            *gpa = base | (va & (size - 1));
            goto ok;
        }
        if (!level) {
            *gpa = (e & X_ADDR) | (va & 0xfff);
            goto ok;
        }
        table = e & X_ADDR;
    }
    return false;
ok:
    if (nx || (walk_cpl == 3 && !user))
        return false;
    return is_ram(*gpa);
}

static u64 fetch_lo, fetch_hi; // all a translation has read

// ---- Inert analyzed-input recurrence census ----
// One entry per direct-mapped identity, full population: this is not a sampled cache with an inflated retention budget.
// Canonical zero holes plus an exact occupancy mask describe only bytes actually returned by tr_fetch, including liveness.
// No executable pointers, cached instructions, extra guest reads, or changes to compilation decisions live here.
#define COMPILE_INPUT_SLOTS 8192u
#define COMPILE_INPUT_PAGE 4096u
typedef struct {
    u64 rip, config, frame;
    u32 key;
    u8 cpl;
    bool compat, valid;
    u64 mask[COMPILE_INPUT_PAGE / 64];
    u8 bytes[COMPILE_INPUT_PAGE];
} CompileInputEntry;
typedef struct {
    CompileInputEntry input;
    u32 reject;
    bool active;
} CompileInputCapture;
_Static_assert(sizeof(CompileInputEntry) * COMPILE_INPUT_SLOTS + sizeof(CompileInputCapture) <= 40u * 1024 * 1024,
               "compile-input census must remain bounded by 40 MiB");
static CompileInputEntry *compile_inputs_table;
static CompileInputCapture *compile_inputs_capture;

static void compile_inputs_init(void) {
    free(compile_inputs_table);
    free(compile_inputs_capture);
    compile_inputs_table = NULL;
    compile_inputs_capture = NULL;
    stats.compile_inputs_enabled = cfg.profile_compile_inputs;
    stats.compile_inputs_available = stats.compile_inputs_bytes = stats.compile_inputs_allocation_failures = 0;
    stats.compile_inputs_sampling_shift = stats.compile_inputs_config_base = 0;
    stats.compile_inputs_attempts = stats.compile_inputs_admitted = 0;
    memset(stats.compile_inputs_reject, 0, sizeof stats.compile_inputs_reject);
    stats.compile_inputs_empty = stats.compile_inputs_evictions = stats.compile_inputs_prior = stats.compile_inputs_exact = 0;
    stats.compile_inputs_coverage_mismatch = stats.compile_inputs_bytes_mismatch = 0;
    stats.compile_inputs_exact_same_key = stats.compile_inputs_exact_different_key = 0;
    stats.compile_inputs_exact_same_pfn = stats.compile_inputs_exact_different_pfn = 0;
    memset(stats.compile_inputs_exact_joint, 0, sizeof stats.compile_inputs_exact_joint);
    if (!cfg.profile_compile_inputs)
        return;
    // Schema 1. The table is cleared only here, so this tr_init is its fixed configuration epoch.
    stats.compile_inputs_config_base = 1ULL << 32 | (u64)cfg.precise_flags | (u64)cfg.has_tzcnt_lzcnt << 1 |
        (u64)cfg.hybrid << 2 | (u64)cfg.keep_kernel_tables << 3 | (u64)cfg.no_bulk_rep << 4 |
        (u64)cfg.no_return_prediction << 5 | (u64)cfg.all_flags_live << 6 |
        (u64)cfg.profile_shift_helpers << 7 | (u64)cfg.inline_shr8 << 8 | (u64)cfg.page_rep << 9;
    compile_inputs_table = calloc(COMPILE_INPUT_SLOTS, sizeof *compile_inputs_table);
    compile_inputs_capture = calloc(1, sizeof *compile_inputs_capture);
    if (!compile_inputs_table || !compile_inputs_capture) {
        free(compile_inputs_table);
        free(compile_inputs_capture);
        compile_inputs_table = NULL;
        compile_inputs_capture = NULL;
        stats.compile_inputs_allocation_failures = 1;
        return;
    }
    stats.compile_inputs_available = 1;
    stats.compile_inputs_bytes = sizeof(CompileInputEntry) * COMPILE_INPUT_SLOTS + sizeof(CompileInputCapture);
}

static void compile_inputs_begin(u64 rip, int cpl, bool compat, u32 key, u64 frame, bool single) {
    if (!cfg.profile_compile_inputs)
        return;
    stats.compile_inputs_attempts++;
    if (compile_inputs_capture)
        compile_inputs_capture->active = false;
    if (single || !compile_inputs_capture) {
        stats.compile_inputs_reject[single ? TR_CI_SINGLE : TR_CI_UNAVAILABLE]++;
        return;
    }
    memset(compile_inputs_capture, 0, sizeof *compile_inputs_capture);
    CompileInputCapture *c = compile_inputs_capture;
    c->active = true;
    c->reject = TR_CI_REJECT_KINDS;
    c->input.rip = rip;
    // vcpu_count is installed after tr_init and can grow on SMP attachment: capture the emitter-relevant bit now.
    c->input.config = stats.compile_inputs_config_base | (u64)(ctx_ptr->vcpu_count > 1) << 16;
    c->input.frame = frame;
    c->input.key = key;
    c->input.cpl = (u8)cpl;
    c->input.compat = compat;
}

static void compile_inputs_reject(u32 reason) {
    CompileInputCapture *c = compile_inputs_capture;
    if (cfg.profile_compile_inputs && c && c->active && c->reject == TR_CI_REJECT_KINDS)
        c->reject = reason;
}

static void compile_inputs_request(u64 va, int n) {
    CompileInputCapture *c = compile_inputs_capture;
    if (!c || !c->active)
        return;
    u64 page = c->input.rip & ~0xfffULL;
    if (n <= 0)
        compile_inputs_reject(TR_CI_EMPTY);
    else if (va < page || va - page >= COMPILE_INPUT_PAGE || (u64)n > COMPILE_INPUT_PAGE - (va - page))
        compile_inputs_reject(TR_CI_CROSS_PAGE);
}

static void compile_inputs_read(u64 va, const u8 *bytes, int n, u64 gpa) {
    CompileInputCapture *c = compile_inputs_capture;
    if (!c || !c->active || c->reject != TR_CI_REJECT_KINDS)
        return;
    compile_inputs_request(va, n);
    if (c->reject != TR_CI_REJECT_KINDS)
        return;
    if ((gpa & ~0xfffULL) != c->input.frame) {
        compile_inputs_reject(TR_CI_MIXED_FRAME);
        return;
    }
    u32 off = (u32)(va & 0xfff);
    for (int i = 0; i < n; i++) {
        u32 at = off + (u32)i;
        u64 bit = 1ULL << (at & 63);
        if ((c->input.mask[at >> 6] & bit) && c->input.bytes[at] != bytes[i]) {
            compile_inputs_reject(TR_CI_INCONSISTENT);
            return;
        }
        c->input.mask[at >> 6] |= bit;
        c->input.bytes[at] = bytes[i];
    }
}

static void compile_inputs_finish(void) {
    CompileInputCapture *c = compile_inputs_capture;
    if (!cfg.profile_compile_inputs || !c || !c->active)
        return;
    c->active = false;
    bool any = false;
    for (u32 i = 0; i < COMPILE_INPUT_PAGE / 64; i++)
        any |= c->input.mask[i] != 0;
    if (!any && c->reject == TR_CI_REJECT_KINDS)
        c->reject = TR_CI_EMPTY;
    if (c->reject != TR_CI_REJECT_KINDS) {
        stats.compile_inputs_reject[c->reject]++;
        return;
    }
    stats.compile_inputs_admitted++;
    CompileInputEntry *in = &c->input;
    u64 hash = (in->rip ^ in->config ^ (u64)in->cpl << 61 ^ (u64)in->compat << 60) * 0x9E3779B97F4A7C15ULL;
    CompileInputEntry *old = &compile_inputs_table[hash >> (64 - 13)];
    if (!old->valid) {
        stats.compile_inputs_empty++;
    } else if (old->rip != in->rip || old->config != in->config || old->cpl != in->cpl || old->compat != in->compat) {
        stats.compile_inputs_evictions++;
    } else {
        stats.compile_inputs_prior++;
        if (memcmp(old->mask, in->mask, sizeof in->mask)) {
            stats.compile_inputs_coverage_mismatch++;
        } else if (memcmp(old->bytes, in->bytes, sizeof in->bytes)) {
            stats.compile_inputs_bytes_mismatch++;
        } else {
            stats.compile_inputs_exact++;
            bool dk = old->key != in->key, df = old->frame != in->frame;
            if (dk) stats.compile_inputs_exact_different_key++; else stats.compile_inputs_exact_same_key++;
            if (df) stats.compile_inputs_exact_different_pfn++; else stats.compile_inputs_exact_same_pfn++;
            stats.compile_inputs_exact_joint[dk * 2 + df]++;
        }
    }
    // Keep only the most recent observation of this identity, including mismatches; no multi-version history.
    *old = *in;
    old->valid = true;
}
// ---- End inert analyzed-input recurrence census ----

int tr_fetch(u64 va, u8 *out, int n) {
    int got = 0;
    if (cfg.profile_compile_inputs)
        compile_inputs_request(va, n);
    if (va < fetch_lo)
        fetch_lo = va;
    if (va + (u64)n > fetch_hi)
        fetch_hi = va + (u64)n;
    while (got < n) {
        u64 gpa;
        if (!fetch_walk(va + got, &gpa)) {
            compile_inputs_reject(TR_CI_PARTIAL);
            return got;
        }
        int chunk = 4096 - (int)((va + got) & 0xfff);
        if (chunk > n - got)
            chunk = n - got;
        memcpy(out + got, ram_at(gpa), chunk);
        if (cfg.profile_compile_inputs)
            compile_inputs_read(va + got, out + got, chunk, gpa);
        got += chunk;
    }
    return got;
}

// With several vCPUs a block is found the moment it exists, by vCPUs that may already have seen a later store. So its bytes must be read
// protected. A writer clears the mark before it takes the translator lock to kill, so a mark seen here means any later store kills what this reads.
static bool frame_unprotected(u64 va, u64 *frame) {
    u64 gpa;
    if (ctx_ptr->vcpu_count < 2 || !fetch_walk(va, &gpa))
        return false;
    *frame = gpa & ~0xfffULL;
    return !(__atomic_load_n(&cfg.priv[OFF_PAGEINFO + (gpa >> 12)], __ATOMIC_ACQUIRE) & PI_CODE);
}

// ---- Liveness ----
static int insn_reads(const Insn *in) {
    int r = in->reads;
    if (tr_precise_flags && in->memop)
        r = F_ARITH; // a fault here shows every flag in the x86 exception frame
    return r;
}

// Which flags can be read from `rip` on before being overwritten. Only looks inside the page `page`: anything else can change
// without invalidating the block asking, so it counts as all live.
// Use the instructions this block will actually emit when a local branch or
// loop returns into it. Apart from avoiding repeated decoding/page walks, this
// keeps its flag analysis consistent with that decoded input. Nothing survives
// this translation: other targets still go through the normal tracked fetch.
static void flags_decode(u64 rip, Insn *out, const Insn *block, int count) {
    int lo = 0, hi = count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (block[mid].rip < rip)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < count && block[lo].rip == rip)
        *out = block[lo];
    else
        x86_decode(rip, out);
}

static int flags_needed_at(u64 rip, u64 page, int depth, const Insn *block, int count) {
    int need = 0, written = 0;
    for (int i = 0; i < 16; i++) {
        if ((rip & ~0xfffULL) != page || ((rip + 15) & ~0xfffULL) != page)
            return need | (F_ARITH & ~written);
        Insn in;
        flags_decode(rip, &in, block, count);
        need |= insn_reads(&in) & ~written;
        written |= in.writes;
        if ((written & F_ARITH) == F_ARITH)
            return need;
        if (in.op == OP_JCC) {
            if (!depth)
                return need | (F_ARITH & ~written);
            return need | ((flags_needed_at(in.target, page, depth - 1, block, count) | flags_needed_at(in.next, page, depth - 1, block, count)) & ~written);
        }
        if (in.op == OP_JMP) {
            rip = in.target;
            continue;
        }
        if (insn_is_terminator(&in) || in.op == OP_CALL || in.op == OP_CALLI || in.op == OP_JRCXZ)
            return need | (F_ARITH & ~written);
        rip = in.next;
    }
    return need | (F_ARITH & ~written);
}

// ---- Stubs ----
typedef struct {
    u32 *site; // the branch to fix up, or NULL for a plain fall-through stub
    u64 target;
    int kind; // SVC_DIRECT, SVC_JCC, SVC_CALL
    int form; // 0 b.cond/cbz (19-bit), 1 b/bl (26-bit), 2 tbz (unused)
} Pending;
static Pending pend[MAX_PENDING];
static int pend_count;

void emit_stop(u64 rip, const char *why) {
    compile_inputs_reject(TR_CI_UNSUPPORTED);
    (void)rip;
    u32 idx = stop_reason_count;
    for (u32 i = 0; i < stop_reason_count; i++)
        if (stop_reasons[i] == why || !strcmp(stop_reasons[i], why)) {
            idx = i;
            break;
        }
    if (idx == stop_reason_count && stop_reason_count < 4096)
        stop_reasons[stop_reason_count++] = why;
    svc(SVC_STOP);
    E(idx);
}

const char *tr_stop_reason(uint32_t index) { return index < stop_reason_count ? stop_reasons[index] : "?"; }

static void add_pending(u32 *site, u64 target, int kind, int form) {
    pend[pend_count].site = site;
    pend[pend_count].target = target;
    pend[pend_count].kind = kind;
    pend[pend_count].form = form;
    pend_count++;
}

// Reserves a b.cond/cbz/b word to be resolved to a stub at the block end.
static void stub_lit(u64 target) {
    E((u32)target);
    E((u32)(target >> 32));
}

static void emit_indirect_tail(void) {
    // x16 holds the x86 target. Lookup returns the translation in x22, and the branch stays here so that each site has
    // its own predictor entry.
    bl_rel((int)((i64)(STUB(tr_stub_lookup) - tr_cc_va(tr_cc)) / 4));
    br(S2);
}

static const Insn *block_insns;
static u32 *insn_arm[MAX_INSNS];
static int insn_total;

// If target is an instruction already emitted in this block, returns where.
static u32 *internal_target(u64 target, int upto) {
    for (int i = 0; i <= upto && i < insn_total; i++)
        if (block_insns[i].rip == target)
            return insn_arm[i];
    return NULL;
}

static void emit_monitor_op(const Insn *in);

static void emit_control(const Insn *in, int idx, int live_after) {
    switch (in->op) {
        case OP_JCC: {
            u32 *back = internal_target(in->target, idx);
            int acc = x86_to_arm_cc[in->cc];
            if (acc < 0) {
                emit_parity(S4);
                if (back) {
                    cbz(in->cc == 10, 0, S4, (int)(back - tr_cc));
                } else {
                    add_pending(tr_cc, in->target, SVC_JCC, 0);
                    cbz(in->cc == 10, 0, S4, 0);
                }
            } else if (back) {
                b_cond(acc, (int)(back - tr_cc));
            } else {
                add_pending(tr_cc, in->target, SVC_JCC, 0);
                b_cond(acc, 0);
            }
            break;
        }
        case OP_JRCXZ: {
            u32 *back = internal_target(in->target, idx);
            int sub = in->sub;
            int sf = in->addr32 ? 0 : 1; // with 67h the count is ecx
            if (in->addr16) { // 32-bit code with 67h: the count is cx, and ecx's top half stays
                if (sub >= 2) {
                    add_imm(0, 1, 0, S0, 1, 1);
                    bfi(1, 1, S0, 0, 16);
                }
                ubfx(0, S0, 1, 0, 16);
                u32 *over = 0;
                if (sub >= 3) {
                    over = tr_cc;
                    b_cond(sub == 3 ? 1 : 0, 0);
                }
                if (!back)
                    add_pending(tr_cc, in->target, SVC_JCC, 0);
                cbz(sub >= 2, 0, S0, back ? (int)(back - tr_cc) : 0);
                if (over)
                    *over |= ((u32)(tr_cc - over) & 0x7ffff) << 5;
                break;
            }
            if (sub >= 2)
                add_imm(sf, 1, 0, 1, 1, 1);
            if (sub == 0 || sub == 1) {
                if (!back)
                    add_pending(tr_cc, in->target, SVC_JCC, 0);
                cbz(0, sub == 1 ? 0 : 1, 1, back ? (int)(back - tr_cc) : 0);
            } else if (sub == 2) {
                if (!back)
                    add_pending(tr_cc, in->target, SVC_JCC, 0);
                cbz(1, sf, 1, back ? (int)(back - tr_cc) : 0);
            } else {
                u32 *over = tr_cc;
                b_cond(sub == 3 ? 1 : 0, 0); // loope leaves when ZF is clear, loopne when it is set
                if (!back)
                    add_pending(tr_cc, in->target, SVC_JCC, 0);
                cbz(1, sf, 1, back ? (int)(back - tr_cc) : 0);
                *over |= ((u32)(tr_cc - over) & 0x7ffff) << 5;
            }
            break;
        }
        case OP_JMP: {
            u32 *back = internal_target(in->target, idx);
            if (back) {
                b_rel((int)(back - tr_cc));
            } else {
                add_pending(tr_cc, in->target, SVC_DIRECT, 1);
                b_rel(0);
            }
            break;
        }
        case OP_CALL: {
            // Guest push first (it can fault), shadow pair next, then a real bl so that the matching ret is predicted.
            movimm(S0, in->next);
            if (tr_compat)
                E(0xB8000C00u | ((u32)-4 & 0x1ff) << 12 | 4 << 5 | S0);
            else
                E(0xF8000C00u | ((u32)-8 & 0x1ff) << 12 | 4 << 5 | S0);
            adr_words(S1, 3);
            E(0xA9800000u | ((u32)-2 & 0x7f) << 15 | S1 << 10 | RS << 5 | S0); // stp x16, x17, [x26, #-16]!
            add_pending(tr_cc, in->target, SVC_CALL, 1);
            bl_rel(0);
            break;
        }
        case OP_CALLI: {
            if (in->rm_reg)
                mov_reg(1, S0, in->rm);
            else
                ldst(1, in->sz, S0, addr_prepare(in, in->sz));
            movimm(S1, in->next);
            if (tr_compat)
                E(0xB8000C00u | ((u32)-4 & 0x1ff) << 12 | 4 << 5 | S1);
            else
                E(0xF8000C00u | ((u32)-8 & 0x1ff) << 12 | 4 << 5 | S1);
            adr_words(AD, 4);
            E(0xA9800000u | ((u32)-2 & 0x7f) << 15 | AD << 10 | RS << 5 | S1);
            bl_rel((int)((i64)(STUB(tr_stub_lookup) - tr_cc_va(tr_cc)) / 4));
            E(0xD63F0000u | S2 << 5); // blr x22
            // landing pad: the next instruction (adr above counts from the adr: push, stp, bl, blr, then here)
            break;
        }
        case OP_JMPI:
            if (in->rm_reg)
                mov_reg(1, S0, in->rm);
            else
                ldst(1, in->sz, S0, addr_prepare(in, in->sz));
            emit_indirect_tail();
            break;
        case OP_RET: {
            // shortcut: post-index leaves esp past 4 GB for a stack at the very top of memory. No XNU process has one, as the commpage sits there
            if (tr_compat)
                E(0xB8400400u | 4 << 12 | 4 << 5 | S0); // ldr w16, [x4], #4
            else
                E(0xF8400400u | 8 << 12 | 4 << 5 | S0); // ldr x16, [x4], #8
            if (in->imm) {
                if (in->imm < 4096)
                    add_imm(!tr_compat, 0, 0, 4, 4, (u32)in->imm);
                else {
                    movimm(S1, (u64)in->imm);
                    add_reg(!tr_compat, 0, 0, 4, 4, S1, 0);
                }
            }
            if (cfg.no_return_prediction) { // the guest's value decides, by lookup
                b_rel((int)((i64)(STUB(tr_stub_ret_miss) - tr_cc_va(tr_cc)) / 4));
                break;
            }
            E(0xA8C00000u | 2 << 15 | LR << 10 | RS << 5 | S1); // ldp x17, x30, [x26], #16
            logic_reg(1, 2, 0, S1, S1, S0);
            cbz(1, 1, S1, 2);
            ret_reg(LR);
            b_rel((int)((i64)(STUB(tr_stub_ret_miss) - tr_cc_va(tr_cc)) / 4));
            break;
        }
        default:
            break;
    }
    (void)live_after;
}

// Patch helper: resolves a forward reference.
static void fix(u32 *site, int form, u32 *to) {
    i64 d = to - site;
    if (form == 0)
        *site = (*site & ~(0x7ffffu << 5)) | ((u32)d & 0x7ffff) << 5;
    else
        *site = (*site & 0xFC000000u) | ((u32)d & 0x3ffffff);
}

static bool part_full(int part) {
    u32 *start = cc_base + part * CC_PART2_WORD;
    u32 first_map = part ? (OFF_PCMAP2 - OFF_PCMAP) / sizeof(PcMap) : 0;
    return (u8 *)cc_cursor[part] - (u8 *)start > (long)(cc_limit - 0x40000) || hblock_count[part] >= BLOCKS_MAX - 2 ||
           pc_map_used[part] + MAX_INSNS >= first_map + PCMAP_ENTRIES;
}

bool tr_needs_flush(void) { return part_full(0) || part_full(1); }

static void flush_everything(void) {
    tr_flush_all();
}

// ---- Reusing dead blocks ----
// Blocks often die with nothing wrong with them: a view reused, a mapping rewritten. A block is a function of its x86 address, the bytes translating it read,
// its CPL and code size, and settings fixed for the run. So if those match it can come back, under the protection a new translation would read them under.
#define DEAD_SLOTS (1u << 20)
#define READ_COPY_BYTES (512u << 20) // virtual: touched as used. Past it, blocks just aren't reusable until the next reset
#define WORD_NOP 0xD503201Fu
#define WORD_DEAD (0xD4000001u | SVC_DEAD << 5)
static u32 *dead_slots; // by key and rip: a block index + 1, the last to die there. A lost entry only means translating again
static u8 *read_copies;
static u32 read_copied[2] = {1, READ_COPY_BYTES / 2};

static inline u32 dead_slot(u32 key, u64 rip) { return (u32)((rip * 0x9E3779B97F4A7C15ULL + key) >> 40) & (DEAD_SLOTS - 1); }

static void flush_dead_slots(bool everything) {
    if (!cfg.retain_shared_dead) {
        memset(dead_slots, 0, DEAD_SLOTS * sizeof(u32));
        return;
    }
    // Values are encoded block index + 1. A private reset preserves P0's code,
    // HostBlocks and saved bytes; P1 indices will be recycled. Keep only existing
    // allocated P0 hints, without repopulating candidates lost to a collision.
    // Reuse still applies every original key/frame/byte/permission/entry guard.
    u32 shared_count = hblock_count[0];
    for (u32 slot = 0; slot < DEAD_SLOTS; slot++) {
        u32 hint = dead_slots[slot];
        if (!hint)
            continue;
        if (!everything && hint <= shared_count && hint <= BLOCKS_MAX) {
            stats.shared_dead_retained_hints++;
        } else {
            dead_slots[slot] = 0;
            stats.shared_dead_cleared_hints++;
        }
    }
}

static void remember_reads(HostBlock *hb, u64 rip) {
    u64 page_end = (rip & ~0xfffULL) + 4096, lo = fetch_lo, hi = fetch_hi < page_end ? fetch_hi : page_end;
    u32 end = cur_part ? READ_COPY_BYTES : READ_COPY_BYTES / 2;
    if (cfg.no_reuse || hb->frame[1] != ~0ULL || hb->frame[0] >= cfg.ram_size || read_copied[cur_part] + (hi - lo) > end)
        return;
    memcpy(read_copies + read_copied[cur_part], ram_at(hb->frame[0] + (lo & 0xfff)), hi - lo);
    hb->read_copy = read_copied[cur_part];
    hb->read_off = (u16)(lo & 0xfff);
    hb->read_bytes = (u16)(hi - lo);
    read_copied[cur_part] += (u32)(hi - lo);
}

static inline bool reuse_rejected(u32 reason) {
    if (cfg.profile_invalidations)
        stats.reuse_reject[reason]++;
    return false;
}

static bool reuse_dead_block(u32 key, int cpl, u64 rip, TrResult *res) {
    if (cfg.profile_invalidations)
        stats.reuse_attempts++;
    u32 it = cfg.no_reuse ? 0 : dead_slots[dead_slot(key, rip)];
    if (!it)
        return reuse_rejected(cfg.no_reuse ? TR_REUSE_DISABLED : TR_REUSE_NO_SLOT);
    HostBlock *hb = &hblocks[it - 1];
    u64 gpa;
    // Keep the original short-circuit order, especially the permission walk, byte comparison and entry CAS.
    if (!hb->dead) return reuse_rejected(TR_REUSE_NOT_DEAD);
    if (hb->rip != rip) return reuse_rejected(TR_REUSE_RIP);
    if (hb->view != key) return reuse_rejected(TR_REUSE_KEY);
    if (!hb->read_bytes) return reuse_rejected(TR_REUSE_NO_READ_COPY);
    if (hb->cpl != cpl) return reuse_rejected(TR_REUSE_CPL);
    if (hb->compat != tr_compat) return reuse_rejected(TR_REUSE_COMPAT);
    if (!fetch_walk(rip, &gpa)) return reuse_rejected(TR_REUSE_FETCH);
    if ((gpa & ~0xfffULL) != hb->frame[0]) return reuse_rejected(TR_REUSE_FRAME);
    if (ctx_ptr->vcpu_count > 1 && hb->first_word != WORD_NOP) // only a nop can come back under a peer running it
        return reuse_rejected(TR_REUSE_SMP_ENTRY);
    if (memcmp(ram_at(hb->frame[0] + hb->read_off), read_copies + hb->read_copy, hb->read_bytes))
        return reuse_rejected(TR_REUSE_BYTES);
    u32 *entry = cc_base + hb->entry_word, dead = WORD_DEAD;
    if (!__atomic_compare_exchange_n(entry, &dead, hb->first_word, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
        return reuse_rejected(TR_REUSE_ENTRY_CAS); // something that ran into it has since been sent on to a newer block, for good
    sys_icache_invalidate(entry, 4);
    hb->dead = false;
    u32 idx = it - 1;
    if (!hb->on_frame) {
        u32 *head = &frame_head[hb->frame[0] >> 12];
        hb->next_on_frame[0] = *head;
        *head = idx + 1;
        hb->on_frame = true;
    }
    if (!hb->on_page) {
        u32 *head = &page_head[page_slot(rip)];
        hb->next_on_page[0] = *head;
        *head = idx + 1;
        hb->on_page = true;
    }
    if (!(key & KEY_DOMAIN) && !hb->on_view) {
        hb->next_in_view = view_head[key];
        view_head[key] = idx + 1;
        hb->on_view = true;
    }
    map_insert((int)key, rip, idx + 1);
    res->arm = tr_cc_va(entry);
    res->frames[0] = hb->frame[0];
    stats.reused++;
    return true;
}

// A block joins the chains that find it to kill it.
static void link_block(u32 idx) {
    HostBlock *hb = &hblocks[idx];
    for (int f = 0; f < 2; f++) {
        if (hb->frame[f] != ~0ULL && hb->frame[f] < cfg.ram_size) {
            u32 *head = &frame_head[hb->frame[f] >> 12];
            hb->next_on_frame[f] = *head;
            *head = idx + 1;
        }
    }
    u32 *head = &page_head[page_slot(hb->rip)];
    hb->next_on_page[0] = *head;
    *head = idx + 1;
    if (hb->frame[1] != ~0ULL) { // it runs on into the next page
        head = &page_head[page_slot(hb->rip + 4096)];
        hb->next_on_page[1] = *head;
        *head = idx + 1;
    }
    if (!(hb->view & KEY_DOMAIN)) {
        hb->next_in_view = view_head[hb->view];
        view_head[hb->view] = idx + 1;
    }
    hb->on_frame = hb->on_page = hb->on_view = true;
}

static TrResult do_translate(int key, int view, u64 cr3, int cpl, u64 rip, bool single) {
    (void)view;
    u64 t0 = mach_absolute_time();
    TrResult res = {0};
    res.frames[0] = res.frames[1] = ~0ULL;
    static Insn list[MAX_INSNS];
    walk_cr3 = cr3;
    walk_cpl = cpl;
    tr_cpl = cpl;
    if (tr_needs_flush()) {
        flush_everything();
        res.flushed = true;
    }
    cur_part = KEY_PART(key);
    tr_cc = cc_cursor[cur_part];
    int count = 0;
    u64 pc = rip, page = rip & ~0xfffULL, frame;
    if (!single) {
        if (frame_unprotected(rip, &frame)) {
            res.unprotected = frame + 1;
            return res;
        }
        if (reuse_dead_block((u32)key, cpl, rip, &res)) {
            stats.translate_ns += mach_absolute_time() - t0;
            return res;
        }
    }
    fetch_lo = ~0ULL;
    fetch_hi = 0;
    // Before the bytes, as the mapping can move at any time: a frame walked after them could be one the block was never read from, and looking again later would agree with it.
    u64 gpa0 = 0, gpa1 = ~0ULL;
    fetch_walk(rip, &gpa0);
    res.frames[0] = gpa0 & ~0xfffULL;
    compile_inputs_begin(rip, cpl, tr_compat, (u32)key, res.frames[0], single);
    do {
        if ((pc & 0xfff) > 4096 - 15) {
            if (!fetch_walk((pc | 0xfff) + 1, &gpa1))
                gpa1 = ~0ULL;
            if (!single && frame_unprotected((pc | 0xfff) + 1, &frame)) {
                compile_inputs_reject(TR_CI_UNPROTECTED);
                compile_inputs_finish();
                res.unprotected = frame + 1;
                return res;
            }
        }
        x86_decode(pc, &list[count]);
        Insn *in = &list[count];
        if (in->op == OP_BAD && in->why && !strcmp(in->why, "FETCH_FAULT")) {
            if (count == 0) {
                compile_inputs_reject(TR_CI_FETCH_FAULT);
                compile_inputs_finish();
                res.fetch_fault_va = in->target ? in->target : pc;
                return res;
            }
            break; // cut the block before it, the fault belongs to the instruction when it runs
        }
        if (in->op == OP_BAD || in->op == OP_UD)
            compile_inputs_reject(TR_CI_UNSUPPORTED);
        pc = in->next;
        count++;
        if (in->next > page + 4096 && count > 0) {
            // Crossed into the next page: record it, end here.
            if (gpa1 != ~0ULL)
                res.frames[1] = gpa1 & ~0xfffULL;
            break;
        }
    } while (!single && count < MAX_INSNS && !insn_is_terminator(&list[count - 1]) && (pc & ~0xfffULL) == page);

    Insn *last = &list[count - 1];
    bool fallthrough_end = !(last->op == OP_JMP || last->op == OP_JMPI || last->op == OP_RET || last->op == OP_BAD ||
                             last->op == OP_UD || last->op == OP_IRET);
    int live;
    if (last->op == OP_JMP)
        live = flags_needed_at(last->target, page, 2, list, count);
    else if (!fallthrough_end)
        live = F_ARITH;
    else
        live = flags_needed_at(last->next, page, 2, list, count);
    static u8 live_after[MAX_INSNS];
    for (int i = count - 1; i >= 0; i--) {
        Insn *in = &list[i];
        if (in->op == OP_JCC || in->op == OP_JRCXZ)
            live |= flags_needed_at(in->target, page, 1, list, count);
        else if (in->op == OP_CALL || in->op == OP_CALLI)
            live = F_ARITH; // the callee and the landing pad are unknown
        else if (in->op == OP_JMP || in->op == OP_JMPI || in->op == OP_RET)
            live = i == count - 1 ? live : F_ARITH;
        live_after[i] = (u8)(cfg.all_flags_live ? F_ARITH : live);
        live = (live & ~in->writes) | insn_reads(in);
    }

    block_insns = list;
    insn_total = count;
    pend_count = 0;
    u32 *start = tr_cc;
    if (ctx_ptr->vcpu_count > 1) // a kill patches the entry word under peers running it: nop to svc is a change the architecture allows, an arbitrary word isn't
        E(0xD503201Fu);
    u32 map0 = pc_map_used[cur_part];
    for (int i = 0; i < count; i++) {
        const Insn *in = &list[i];
        insn_arm[i] = tr_cc;
        if (i)
            stats.op_words[list[i - 1].op & 127] += (u64)(tr_cc - insn_arm[i - 1]);
        stats.op_insns[in->op & 127]++;
        pc_map[pc_map_used[cur_part]].arm_word = (u16)(tr_cc - start);
        pc_map[pc_map_used[cur_part]].rip_off = (u16)(in->rip - rip);
        pc_map_used[cur_part]++;
        stats.insns++;
        switch (in->op) {
            case OP_JCC:
            case OP_JRCXZ:
            case OP_JMP:
            case OP_CALL:
            case OP_CALLI:
            case OP_JMPI:
            case OP_RET:
                emit_control(in, i, live_after[i]);
                break;
            case OP_BAD:
                emit_stop(in->rip, in->why);
                break;
            case OP_UD:
                svc(SVC_UD);
                break;
            case OP_SSE:
            case OP_LDMXCSR:
            case OP_STMXCSR:
                emit_sse(in, live_after[i]);
                break;
            case OP_X87:
                emit_x87(in);
                break;
            case OP_SYSCALL:
            case OP_SYSRET:
            case OP_IRET:
            case OP_INT:
            case OP_INT3:
            case OP_HLT:
            case OP_CPUID:
            case OP_RDTSC:
            case OP_RDTSCP:
            case OP_RDMSR:
            case OP_WRMSR:
            case OP_CLI:
            case OP_STI:
            case OP_MOV_CR:
                if (i && in->reg == 3 && !in->to_reg && in->rm == 0 && !cfg.keep_kernel_tables && tr_cpl == 0) {
                    // XNU's Meltdown fix, from 10.13.2: a process runs on tables holding little of the kernel, and every way in and out changes between them and the full ones,
                    // through two fields of its per-CPU data. Those are the same to a process, which can't read the kernel's pages either way, so it stays on the full tables.
                    // Going out, that is nothing to do. Coming in, the tables may be the other ones if TCG took it out, so look.
                    const Insn *p = &list[i - 1];
                    if (p->op == OP_MOV && p->seg == 2 && p->to_reg && p->reg == 0 && !p->rm_reg) {
                        if (p->disp == 0x138)
                            break;
                        if (p->disp == 0x140) {
                            Addr a = {.base = CTX, .imm = __builtin_offsetof(EngineCtx, cr3)};
                            ldst(1, 8, S0, a);
                            logic_sh(1, 2, 0, S0, S0, 0, 0, 0); // eor with rax
                            ubfx(1, S0, S0, 12, 40); // the tables, whatever the PCID
                            u32 *same = tr_cc;
                            cbz(0, 1, S0, 0);
                            emit_monitor_op(in);
                            *same |= ((u32)(tr_cc - same) & 0x7ffff) << 5;
                            break;
                        }
                    }
                }
                emit_monitor_op(in);
                break;
            case OP_MOV_DR:
            case OP_INVLPG:
            case OP_INVPCID:
            case OP_WBINVD:
            case OP_CLTS:
            case OP_LGDT:
            case OP_LTR:
            case OP_MOV_SEG:
            case OP_IN:
            case OP_OUT:
            case OP_SWAPGS:
            case OP_SYSENTER:
            case OP_SYSEXIT:
            case OP_FAR:
            case OP_FXSAVE:
            case OP_FXRSTOR:
                emit_monitor_op(in);
                break;
            default:
                emit_int(in, live_after[i]);
                break;
        }
    }
    u32 *tail = tr_cc;
    stats.op_words[last->op & 127] += (u64)(tr_cc - insn_arm[count - 1]);
    if (fallthrough_end) {
        svc(SVC_DIRECT);
        stub_lit(last->next);
    }
    for (int i = 0; i < pend_count; i++) {
        u32 *stub = tr_cc;
        svc(pend[i].kind);
        stub_lit(pend[i].target);
        if (pend[i].kind == SVC_JCC)
            E((u32)(stub - pend[i].site));
        fix(pend[i].site, pend[i].form, stub);
    }
    compile_inputs_finish();
    (void)tail;

    // How many blocks a cache keyed by frame would have saved, and of those, 32-bit user code's: i386 libraries aren't in SHARED_USER_BASE.
    bool i386_user = tr_compat && cpl == 3;
    if (i386_user)
        stats.i386_blocks++;
    if (res.frames[0] != ~0ULL && res.frames[0] < cfg.ram_size) {
        u32 it = frame_head[res.frames[0] >> 12];
        for (int look = 0; it && look < 32; look++) {
            HostBlock *other = &hblocks[it - 1];
            if (other->rip == rip) {
                if (other->dead)
                    stats.again_dead++;
                else
                    stats.again_live++;
                if (i386_user)
                    stats.i386_again++;
                break;
            }
            it = other->frame[0] == res.frames[0] ? other->next_on_frame[0] : other->next_on_frame[1];
        }
    }

    // Finish instruction-cache maintenance before publishing this body in
    // any lookup or in the monitor's address-to-block directory.
    sys_icache_invalidate(start, (u8 *)tr_cc - (u8 *)start);

    // Registers the block.
    u32 idx = BLOCK_BASE(cur_part) + hblock_count[cur_part]++;
    HostBlock *hb = &hblocks[idx];
    memset(hb, 0, sizeof *hb);
    hb->rip = rip;
    hb->view = (u32)key; // one reading: the tables can change under this, and which part of the cache it's in goes by this
    hb->entry_word = (u32)(start - cc_base);
    hb->frame[0] = res.frames[0];
    hb->frame[1] = res.frames[1];
    hb->cpl = (u8)cpl;
    hb->compat = tr_compat;
    hb->first_word = *start;
    hb->on_frame = hb->on_page = hb->on_view = true;
    if (!single)
        remember_reads(hb, rip);
    link_block(idx);
    BlockEntry *be = &block_table[cur_part ? BLOCK_TABLE_PART2 + (idx - BLOCK_BASE(1)) : idx];
    be->rip = rip;
    be->key = hb->view;
    be->arm_off = (u32)(start - cc_base);
    be->arm_words = (u32)(tr_cc - start);
    be->map_off = map0;
    be->map_count = (u32)count;
    if (!single) {
        map_insert((int)hb->view, rip, idx + 1);
        hb->registered = true;
    }
    __sync_synchronize();
    ctx_ptr->block_count = hblock_count[0];
    ctx_ptr->block_count_private = hblock_count[1];
    cc_cursor[cur_part] = tr_cc;
    stats.blocks++;
    stats.words += tr_cc - start;
    stats.exit_words += (u64)(tr_cc - tail);
    res.arm = tr_cc_va(start);
    stats.translate_ns += mach_absolute_time() - t0;
    return res;
}

// Privileged and rare instructions run in the monitor. Views are per CPL, so CPL-3 code simply takes #GP where it must.
static void emit_monitor_op(const Insn *in) {
    bool cpl0_only = false;
    switch (in->op) {
        case OP_HLT:
        case OP_RDMSR:
        case OP_WRMSR:
        case OP_MOV_CR:
        case OP_MOV_DR:
        case OP_INVLPG:
        case OP_INVPCID:
        case OP_WBINVD:
        case OP_CLTS:
        case OP_SYSRET:
        case OP_SYSEXIT:
        case OP_SWAPGS:
            cpl0_only = true;
            break;
        case OP_LGDT:
            cpl0_only = in->sub <= 1; // sgdt and sidt are not privileged without UMIP, which isn't offered: libsystem reads the CPU number with sidt
            break;
        case OP_LTR:
            cpl0_only = in->sub == 0 || in->sub == 2;
            break;
        default:
            break;
    }
    if (cpl0_only && tr_cpl != 0) {
        svc(SVC_GP0);
        return;
    }
    switch (in->op) {
        case OP_SYSCALL:
            svc(tr_compat ? SVC_UD : SVC_SYSCALL); // as Intel's parts, and TCG with Intel's vendor: no CSTAR path. In hybrid mode TCG decides anyway
            break;
        case OP_SYSENTER:
            svc(SVC_SYSENTER);
            break;
        case OP_SYSEXIT:
            movimm(S0, in->sz == 8);
            svc(SVC_SYSEXIT);
            break;
        case OP_FAR:
            if (in->has_imm) {
                movimm(S0, (u64)in->imm);
                movimm(S1, (u64)in->disp);
            } else if (in->sub) { // both words before anything changes
                Addr a = {.base = addr_reg(in)};
                ldst(1, in->sz, S0, a);
                a.imm = in->sz;
                ldst(1, 2, S1, a);
            }
            movimm(S2, (u64)in->sub | (u64)in->sz << 8 | (u64)(in->has_imm ? 0 : in->imm) << 16);
            svc(SVC_FAR);
            break;
        case OP_SYSRET:
            mov_reg(1, S0, XZR);
            movimm(S0, in->sz == 8);
            svc(SVC_SYSRET);
            break;
        case OP_IRET:
            movimm(S0, in->sz);
            svc(SVC_IRET);
            break;
        case OP_INT:
            movimm(S1, (u64)in->imm);
            svc(SVC_INT);
            break;
        case OP_INT3:
            svc(SVC_INT3);
            break;
        case OP_HLT:
            svc(SVC_HLT);
            break;
        case OP_CPUID:
            svc(SVC_CPUID);
            break;
        case OP_RDTSC: {
            // tsc_now(), in place. CNTKCTL lets EL0 read the virtual counter, and nothing else runs ARM at EL0.
            Addr a = {.base = CTX};
            E(0xD53BE040u | S0); // mrs x, cntvct_el0
            a.imm = __builtin_offsetof(EngineCtx, tsc_counter_offset);
            ldst(1, 8, S1, a);
            add_reg(1, 0, 0, S0, S0, S1, 0);
            movimm(S1, 125);
            mul(1, S0, S0, S1);
            movimm(S1, 3);
            udiv(1, S0, S0, S1);
            a.imm = __builtin_offsetof(EngineCtx, tsc_ns_offset);
            ldst(1, 8, S1, a);
            add_reg(1, 0, 0, S0, S0, S1, 0);
            a.imm = __builtin_offsetof(EngineCtx, tsc_num);
            ldst(1, 8, S1, a);
            mul(1, S1, S0, S1);
            a.imm = __builtin_offsetof(EngineCtx, tsc_den);
            ldst(1, 8, S2, a);
            udiv(1, S1, S1, S2);
            add_reg(1, 0, 0, S0, S0, S1, 0);
            a.imm = __builtin_offsetof(EngineCtx, tsc_offset);
            ldst(1, 8, S1, a);
            add_reg(1, 0, 0, S0, S0, S1, 0);
            mov_reg(0, 0, S0);
            lsr_imm(1, 2, S0, 32);
            break;
        }
        case OP_RDTSCP:
            svc(SVC_RDTSCP);
            break;
        case OP_RDMSR:
            svc(SVC_RDMSR);
            break;
        case OP_WRMSR:
            svc(SVC_WRMSR);
            break;
        case OP_CLI: {
            if (tr_cpl != 0) { // IOPL is the monitor's to check
                svc(SVC_CLI);
                break;
            }
            Addr a = {.base = CTX, .imm = __builtin_offsetof(EngineCtx, rflags_sys)};
            ldst(1, 8, S0, a);
            movimm(S1, 0x200);
            logic_sh(1, 0, 1, S0, S0, S1, 0, 0); // bic
            ldst(0, 8, S0, a);
            break;
        }
        case OP_STI: {
            if (tr_cpl != 0 || !tr_hybrid) { // the in-VM APIC's vectors are the monitor's to look at
                svc(SVC_STI);
                break;
            }
            // Opens IF and the one-instruction shadow, as the monitor does. With an interrupt already waiting it goes on through the monitor, whose
            // way out takes it; one that arrives later raises an IRQ here, which goes there too.
            Addr a = {.base = CTX, .imm = __builtin_offsetof(EngineCtx, rflags_sys)};
            ldst(1, 8, S0, a);
            u32 *already = tr_cc;
            E(0x37000000u | 9u << 19 | S0); // tbnz x, #9
            movimm(S1, 0x200);
            logic_sh(1, 1, 0, S0, S0, S1, 0, 0);
            ldst(0, 8, S0, a);
            movimm(S1, in->next);
            a.imm = __builtin_offsetof(EngineCtx, irq_shadow_rip);
            ldst(0, 8, S1, a);
            a.imm = __builtin_offsetof(EngineCtx, irq_pending);
            ldst(1, 4, S0, a);
            a.imm = __builtin_offsetof(EngineCtx, irq_line);
            ldst(1, 4, S1, a);
            logic_sh(0, 1, 0, S0, S0, S1, 0, 0);
            u32 *quiet = tr_cc;
            cbz(0, 0, S0, 0);
            svc(SVC_STI);
            *already |= ((u32)(tr_cc - already) & 0x3fff) << 5;
            *quiet |= ((u32)(tr_cc - quiet) & 0x7ffff) << 5;
            break;
        }
        case OP_MOV_CR: {
            int cr = in->reg;
            if (cr != 0 && cr != 2 && cr != 3 && cr != 4 && cr != 8) {
                svc(SVC_UD);
                break;
            }
            if (in->to_reg && cr != 8) { // the context holds them, and only the monitor changes them
                Addr a = {.base = CTX};
                a.imm = cr == 0   ? __builtin_offsetof(EngineCtx, cr0)
                        : cr == 2 ? __builtin_offsetof(EngineCtx, cr2)
                        : cr == 3 ? __builtin_offsetof(EngineCtx, cr3)
                                  : __builtin_offsetof(EngineCtx, cr4);
                ldst(1, 8, in->rm, a);
                break;
            }
            movimm(S0, (u64)cr);
            if (in->to_reg) {
                svc(SVC_MOV_CR_READ);
                mov_reg(1, in->rm, S0);
            } else {
                mov_reg(1, S1, in->rm);
                svc(SVC_MOV_CR_WRITE);
            }
            break;
        }
        case OP_MOV_DR:
            emit_stop(in->rip, "debug registers");
            break;
        case OP_INVLPG: {
            Insn t = *in;
            mov_reg(1, S0, addr_reg(&t));
            svc(SVC_INVLPG);
            break;
        }
        case OP_INVPCID:
            svc(SVC_INVPCID);
            break;
        case OP_WBINVD:
            svc(SVC_WBINVD);
            break;
        case OP_CLTS:
            svc(SVC_CLTS);
            break;
        case OP_LGDT: {
            // Limit word, then base. The load comes before anything changes.
            int r = addr_reg(in);
            if (in->sub <= 1) {
                Addr a = {.base = r};
                ldst(1, 2, S1, a);
                a.imm = 2;
                ldst(1, tr_compat ? 4 : 8, S0, a);
                if (tr_compat && in->sz == 2)
                    ubfx(0, S0, S0, 0, 24);
                svc(in->sub == 0 ? SVC_LGDT : SVC_LIDT);
            } else {
                mov_reg(1, S2, r);
                Addr from = {.base = CTX, .imm = in->sub == 2 ? __builtin_offsetof(EngineCtx, gdtr_base) : __builtin_offsetof(EngineCtx, idtr_base)};
                ldst(1, 8, S0, from);
                from.imm = in->sub == 2 ? __builtin_offsetof(EngineCtx, gdtr_limit) : __builtin_offsetof(EngineCtx, idtr_limit);
                ldst(1, 4, S1, from);
                Addr a = {.base = S2};
                ldst(0, 2, S1, a);
                a.imm = 2;
                ldst(0, tr_compat ? 4 : 8, S0, a);
            }
            break;
        }
        case OP_LTR: {
            int mode = in->sub; // 0 ltr, 1 str, 2 lldt, 3 sldt
            bool read = mode == 1 || mode == 3;
            int svcn = mode == 0 ? SVC_LTR : mode == 1 ? SVC_STR : mode == 2 ? SVC_LLDT : SVC_SLDT;
            if (read) {
                svc(svcn);
                if (in->rm_reg)
                    gpr_write(in->rm, in->sz, false, S0);
                else
                    ldst(0, 2, S0, addr_prepare(in, 2));
            } else {
                if (in->rm_reg)
                    mov_reg(1, S0, in->rm);
                else
                    ldst(1, 2, S0, addr_prepare(in, 2));
                svc(svcn);
            }
            break;
        }
        case OP_MOV_SEG: {
            if (in->pop_seg) { // the monitor moves the stack, once the load is accepted
                movimm(S0, (u64)in->sub | (u64)in->sz << 8);
                Addr a = {.base = 4};
                ldst(1, in->sz, S1, a);
                svc(SVC_MOV_SEG_WRITE);
            } else if (in->to_reg) {
                Addr from = {.base = CTX, .imm = (i64)__builtin_offsetof(EngineCtx, seg_sel) + in->sub * 2};
                ldst(1, 2, S0, from);
                if (in->rm_reg)
                    gpr_write(in->rm, in->sz == 2 ? 2 : in->sz, false, S0);
                else
                    ldst(0, 2, S0, addr_prepare(in, 2));
            } else {
                movimm(S0, in->sub);
                if (in->rm_reg)
                    mov_reg(1, S1, in->rm);
                else
                    ldst(1, 2, S1, addr_prepare(in, 2));
                svc(SVC_MOV_SEG_WRITE);
            }
            break;
        }
        case OP_IN:
        case OP_OUT: {
            if (in->has_imm)
                movimm(S0, (u64)in->imm & 0xff);
            else
                ubfx(0, S0, 2, 0, 16);
            movimm(S1, in->sz);
            if (in->op == OP_IN) {
                svc(SVC_IN);
                gpr_write(0, in->sz, false, S0);
            } else {
                mov_reg(1, S2, 0);
                svc(SVC_OUT);
            }
            break;
        }
        case OP_SWAPGS:
            E(0xF9400000u | (u32)(__builtin_offsetof(EngineCtx, kernel_gs_base) / 8) << 10 | CTX << 5 | S0);
            E(0xF9000000u | (u32)(__builtin_offsetof(EngineCtx, kernel_gs_base) / 8) << 10 | CTX << 5 | R_GS);
            mov_reg(1, R_GS, S0);
            break;
        case OP_FXSAVE:
        case OP_FXRSTOR: {
            // Probe both ends, so the monitor's access cannot fault for the guest's reasons. TS comes before any fault, hence the NEON word first.
            E(0x4F00E400u | V_MEM);
            int r = addr_reg(in);
            if (r != S0)
                mov_reg(1, S0, r);
            movimm(S2, in->sz == 8);
            ubfx(1, S1, S0, 0, 4);
            cbz(0, 1, S1, 2);
            svc(SVC_GP0);
            Addr a = {.base = S0};
            ldst(1, 8, XZR, a);
            a.imm = 504;
            ldst(1, 8, XZR, a);
            if (in->op == OP_FXSAVE) {
                a.imm = 0;
                ldst(1, 8, S1, a);
                ldst(0, 8, S1, a);
                a.imm = 504;
                ldst(1, 8, S1, a);
                ldst(0, 8, S1, a);
            }
            svc(in->op == OP_FXSAVE ? SVC_FXSAVE : SVC_FXRSTOR);
            break;
        }
        default:
            emit_stop(in->rip, "no monitor path for this instruction");
    }
}

void tr_init(const TrConfig *config) {
    cfg = *config;
    if (cfg.profile_invalidations && (mach_timebase_info(&invalidation_timebase) != KERN_SUCCESS || !invalidation_timebase.denom))
        cfg.profile_invalidations = false;
    stats.invalidation_profile_enabled = cfg.profile_invalidations;
    cc_base = (u32 *)(cfg.priv + OFF_CC);
    tr_cc = cc_base;
    cc_cursor[0] = cc_base;
    cc_cursor[1] = cc_base + CC_PART2_WORD;
    block_table = (BlockEntry *)(cfg.priv + OFF_BLOCKS);
    pc_map = (PcMap *)(cfg.priv + OFF_PCMAP);
    ctx_ptr = (EngineCtx *)(cfg.priv + OFF_CTX);
    compile_inputs_init();
    tr_precise_flags = cfg.precise_flags;
    tr_has_tzcnt = cfg.has_tzcnt_lzcnt;
    tr_hybrid = cfg.hybrid;
    tr_no_bulk_rep = cfg.no_bulk_rep;
    tr_profile_shift_helpers = cfg.profile_shift_helpers;
    tr_inline_shr8 = cfg.inline_shr8;
    tr_page_rep = cfg.page_rep;
    stats.inline_shr8_enabled = cfg.inline_shr8;
    stats.page_rep_enabled = cfg.page_rep;
    stats.retain_shared_dead_enabled = cfg.retain_shared_dead;
    stats.shared_dead_retained_hints = stats.shared_dead_cleared_hints = 0;
    cc_limit = cfg.code_cache_bytes ? cfg.code_cache_bytes : CC_SIZE;
    tr_stub_lookup = cfg.stub_lookup;
    tr_stub_ret_miss = cfg.stub_ret_miss;
    tr_stub_pushf = cfg.stub_pushf;
    tr_stub_sahf = cfg.stub_sahf;
    hblocks = calloc(BLOCKS_MAX * 2, sizeof(HostBlock));
    free(shared_hash_views);
    shared_hash_views = NULL;
    stats.invalidation_membership_enabled = cfg.invalidation_membership;
    stats.invalidation_membership_reliable = 0;
    stats.invalidation_membership_bytes = 0;
    if (cfg.invalidation_membership) {
        shared_hash_views = calloc(BLOCKS_MAX, sizeof(*shared_hash_views));
        stats.invalidation_membership_reliable = shared_hash_views != NULL;
        stats.invalidation_membership_bytes = shared_hash_views ? BLOCKS_MAX * sizeof(*shared_hash_views) : 0;
        if (!shared_hash_views)
            stats.invalidation_membership_tracking_failures++;
    }
    block_map = calloc(MAP_SIZE, sizeof(MapEntry));
    frame_head = calloc(cfg.ram_size >> 12, sizeof(u32));
    dead_slots = calloc(DEAD_SLOTS, sizeof(u32));
    read_copies = mmap(NULL, READ_COPY_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    page_head = calloc(PAGE_HEADS, sizeof(u32));
}

TrResult tr_translate(int view, uint64_t cr3, int cpl, bool compat, uint64_t rip, bool single) {
    tr_compat = compat; // the asking vCPU's: ctx_ptr is the first vCPU's, whatever it is running
    tr_fpcr_merges_scalar = ctx_ptr->fpcr_merges_scalar != 0;
    if (view > view_high)
        view_high = view;
    int key = block_key(view, cr3, cpl, rip);
    if (!single) {
        u32 hit = map_find(key, rip);
        if (hit) {
            if ((key & KEY_DOMAIN) && cpl == 3)
                stats.shared_found++;
            TrResult r = {0};
            r.arm = tr_cc_va(cc_base + hblocks[hit - 1].entry_word);
            r.frames[0] = hblocks[hit - 1].frame[0];
            r.frames[1] = hblocks[hit - 1].frame[1];
            r.key = key;
            return r;
        }
    }
    TrResult r = do_translate(key, view, cr3, cpl, rip, single);
    if ((key & KEY_DOMAIN) && cpl == 3 && !r.unprotected)
        stats.shared_blocks++;
    r.key = key;
    return r;
}

// The block holding a code cache word. Blocks only grow upward, so the table is sorted.
static HostBlock *block_at(u32 word) {
    int part = word >= CC_PART2_WORD;
    u32 first = BLOCK_BASE(part), lo = first, hi = first + hblock_count[part];
    while (lo < hi) {
        u32 mid = (lo + hi) / 2;
        if (hblocks[mid].entry_word <= word)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo != first ? &hblocks[lo - 1] : 0;
}

// A cross-key transfer cannot bind permanently to one view's ARM target. Give
// calls AND jumps an ordinary view lookup instead of trapping on every visit.
// The lookup preserves NZCV; keep LR too, whether the original site was BL or
// a jump stub. Nothing else enters this tail: interrupted tails are stepped
// through in the same way as the existing call and lookup stubs.
void tr_lookup_tail(uint64_t site, uint64_t target) {
    u32 *s = cc_host(site), was = *s;
    bool call = (was & 0xFC000000u) == 0x94000000u;
    bool jump = was == (0xD4000001u | SVC_DIRECT << 5) || was == (0xD4000001u | SVC_JCC << 5);
    if (!call && !jump)
        return;
    cur_part = (u32)(s - cc_base) >= CC_PART2_WORD; // in reach of its site
    tr_cc = cc_cursor[cur_part];
    u32 *tail = tr_cc;
    movimm(S0, target);
    mov_reg(1, S4, LR);
    bl_rel((int)((i64)(STUB(tr_stub_lookup) - tr_cc_va(tr_cc)) / 4));
    mov_reg(1, LR, S4);
    br(S2);
    cc_cursor[cur_part] = tr_cc;
    sys_icache_invalidate(tail, (u8 *)tr_cc - (u8 *)tail);
    i64 d = tail - s;
    if (__atomic_compare_exchange_n(s, &was, (call ? 0x94000000u : 0x14000000u) | ((u32)d & 0x3ffffff), false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        sys_icache_invalidate(s, 4);
        if (call)
            stats.call_tails++;
        else
            stats.jump_tails++;
    }
}

void tr_link(uint64_t site, int kind, uint64_t arm, int key) {
    u32 *s = cc_host(site);
    // A link is forever, so it may only join blocks of one domain: a chain from shared code into one view's code would be wrong in the next.
    HostBlock *from = block_at((u32)(s - cc_base));
    u32 was = *s;
    if (from && (int)from->view != key && (kind == SVC_CALL || kind == SVC_DIRECT || kind == SVC_JCC)) {
        HostBlock *to = block_at((u32)(cc_host(arm) - cc_base));
        if (to)
            tr_lookup_tail(site, to->rip);
        return;
    }
    if (!from || (int)from->view != key || site == arm || (kind != SVC_CALL && (was & 0xFFE0001Fu) != 0xD4000001u)) // a dead block's entry may have come back: that site runs on as it is
        return;
    i64 d = ((i64)arm - (i64)site) >> 2;
    if (d < -(1 << 25) || d >= (1 << 25)) // the parts of the cache are out of reach of each other
        return;
    if (!__atomic_compare_exchange_n(s, &was, (kind == SVC_CALL ? 0x94000000u : 0x14000000u) | ((u32)d & 0x3ffffff), false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
        return;
    sys_icache_invalidate(s, 4);
    if (kind == SVC_JCC) { // the stub's fourth word locates the conditional branch: go straight to the block if it reaches
        u32 back = s[3];
        u32 *cond = s - back;
        i64 off = ((i64)arm - (i64)tr_cc_va(cond)) >> 2;
        if (off >= -(1 << 18) && off < (1 << 18)) {
            *cond = (*cond & ~(0x7ffffu << 5)) | ((u32)off & 0x7ffff) << 5;
            sys_icache_invalidate(cond, 4);
        }
    }
}

static u64 *hash_table(int view) { return (u64 *)(cfg.priv + OFF_HASH + (u64)view * HASH_BYTES); }

static inline u32 crc32c64(u64 v) {
    u32 c = 0;
    __asm__("crc32cx %w0, %w0, %x1" : "+r"(c) : "r"(v));
    return c;
}

// A lookup ends at an empty slot, in the stub and in the monitor, so a table must always have some. It is only a cache of the block map: when it fills, start again.
static u32 hash_slots_used[MAX_VIEWS];

// Keys only: another vCPU may be reading a slot, and a key it has seen stays good with the address beside it, if only for a block that may be dead by now, which is safe to enter.
static void hash_empty(int view) {
    u64 *t = hash_table(view);
    for (u32 i = 0; i < HASH_ENTRIES; i++)
        t[i * 2] = 0;
}

// Called under the existing translator lock, before publishing any hash value/key. Unknown targets disable the
// optimization until a global hash flush: a missing membership bit must never silently leave an executable lookup.
static void membership_track(int view, u64 rip, u64 arm) {
    if (!stats.invalidation_membership_reliable)
        return;
    u64 base = PRIV_VA + OFF_CC;
    if (view < 0 || view >= MAX_VIEWS || arm < base || ((arm - base) & 3) != 0 || (arm - base) / 4 > UINT32_MAX)
        goto uncertain;
    u32 word = (u32)((arm - base) / 4);
    HostBlock *hb = block_at(word);
    if (!hb || hb->entry_word != word || hb->rip != rip || hb->dead || !hb->registered)
        goto uncertain;
    if (hb->view & KEY_DOMAIN) {
        u64 idx = (u64)(hb - hblocks);
        if (idx >= BLOCKS_MAX)
            goto uncertain;
        shared_hash_views[idx][view / 64] |= 1ULL << (view % 64);
        stats.invalidation_membership_inserts++;
    }
    return;
uncertain:
    stats.invalidation_membership_tracking_failures++;
    stats.invalidation_membership_reliable = 0;
}

static void membership_clear_all(void) {
    if (shared_hash_views) {
        memset(shared_hash_views, 0, BLOCKS_MAX * sizeof(*shared_hash_views));
        stats.invalidation_membership_reliable = 1; // Every hash is empty; future insertions can be tracked from scratch.
    }
}

void tr_hash_insert(int view, uint64_t rip, uint64_t arm) {
    if (cfg.invalidation_membership) {
        membership_track(view, rip, arm);
        __atomic_thread_fence(__ATOMIC_RELEASE); // Publish membership before an existing-key value as well as a new key.
    }
    u64 *t = hash_table(view);
    if (hash_slots_used[view] >= HASH_ENTRIES / 4 * 3) {
        hash_empty(view);
        hash_slots_used[view] = 0;
        stats.hash_refills++;
    }
    u32 h = crc32c64(rip) & (HASH_ENTRIES - 1);
    u32 tombstone = ~0u;
    for (u32 n = 0; n < HASH_ENTRIES; n++, h = (h + 1) & (HASH_ENTRIES - 1)) {
        u64 key = t[h * 2];
        if (key == rip) {
            t[h * 2 + 1] = arm;
            return;
        }
        if (key == 1 && tombstone == ~0u)
            tombstone = h; // not yet: rip may be further on, and two entries for it would outlive a kill
        if (key == 0) {
            if (tombstone != ~0u)
                h = tombstone;
            else
                hash_slots_used[view]++;
            t[h * 2 + 1] = arm;
            __sync_synchronize();
            t[h * 2] = rip;
            return;
        }
    }
}

static void hash_remove(int view, u64 rip) {
    u64 *t = hash_table(view);
    u32 h = crc32c64(rip) & (HASH_ENTRIES - 1);
    for (u32 n = 0; n < HASH_ENTRIES; n++, h = (h + 1) & (HASH_ENTRIES - 1)) {
        u64 key = t[h * 2];
        if (key == 0)
            return;
        if (key == rip) {
            t[h * 2] = 1; // tombstone: nonzero and never a valid target of a lookup that matters
            return;
        }
    }
}

// The disabled path continues to use hash_remove unchanged, with no per-probe profiling branch.
static void hash_remove_profiled(int view, u64 rip) {
    u64 *t = hash_table(view);
    u32 h = crc32c64(rip) & (HASH_ENTRIES - 1);
    u64 probes = 0, removed = 0;
    for (u32 n = 0; n < HASH_ENTRIES; n++, h = (h + 1) & (HASH_ENTRIES - 1)) {
        u64 key = t[h * 2];
        probes++;
        if (key == 0)
            break;
        if (key == rip) {
            t[h * 2] = 1;
            removed = 1;
            break;
        }
    }
    stats.invalidation_hash_view_visits++;
    stats.invalidation_hash_probes += probes;
    stats.invalidation_hash_removals += removed;
}

// Conservative bits can outlive a hash entry or view. Never remove a different block that now has the same RIP.
static void hash_remove_entry(int view, u64 rip, u64 arm, bool sample) {
    u64 *t = hash_table(view);
    u32 h = crc32c64(rip) & (HASH_ENTRIES - 1);
    u64 probes = 0, removed = 0;
    for (u32 n = 0; n < HASH_ENTRIES; n++, h = (h + 1) & (HASH_ENTRIES - 1)) {
        u64 key = t[h * 2];
        probes++;
        if (!key)
            break;
        if (key == rip) {
            if (t[h * 2 + 1] == arm) {
                t[h * 2] = 1;
                removed = 1;
            }
            break;
        }
    }
    stats.invalidation_membership_view_visits++;
    stats.invalidation_membership_stale_visits += !removed;
    stats.invalidation_membership_removals += removed;
    if (sample) {
        stats.invalidation_hash_view_visits++;
        stats.invalidation_hash_probes += probes;
        stats.invalidation_hash_removals += removed;
    }
}

static void invalidate_shared_hashes(u32 idx, bool sample) {
    HostBlock *hb = &hblocks[idx];
    if (cfg.invalidation_membership && stats.invalidation_membership_reliable && idx < BLOCKS_MAX) {
        for (u32 group = 0; group < MAX_VIEWS / 64; group++) {
            u64 bits = shared_hash_views[idx][group];
            while (bits) {
                u32 bit = (u32)__builtin_ctzll(bits);
                bits &= bits - 1;
                hash_remove_entry((int)(group * 64 + bit), hb->rip, tr_cc_va(cc_base + hb->entry_word), sample);
            }
        }
    } else {
        if (cfg.invalidation_membership)
            stats.invalidation_membership_full_scans++;
        for (int view = 0; view <= view_high; view++) {
            if (sample)
                hash_remove_profiled(view, hb->rip);
            else
                hash_remove(view, hb->rip);
        }
    }
}

static void kill_block(u32 idx) {
    HostBlock *hb = &hblocks[idx];
    if (hb->dead)
        return;
    bool sample = cfg.profile_invalidations && (++stats.invalidation_profiled_kills & 255) == 0;
    if (sample) {
        stats.invalidation_sampled_kills++;
        stats.invalidation_sampled_shared += (hb->view & KEY_DOMAIN) != 0;
    }
    hb->dead = true;
    if (hb->registered) {
        if (sample) {
            stats.invalidation_sampled_registered++;
            u64 began = mach_absolute_time();
            map_remove((int)hb->view, hb->rip);
            stats.invalidation_map_unlink_ns += invalidation_ns(mach_absolute_time() - began);
            began = mach_absolute_time();
            if (hb->view & KEY_DOMAIN)
                invalidate_shared_hashes(idx, true);
            else
                hash_remove_profiled((int)hb->view, hb->rip);
            stats.invalidation_hash_fanout_ns += invalidation_ns(mach_absolute_time() - began);
        } else {
            map_remove((int)hb->view, hb->rip);
            if (hb->view & KEY_DOMAIN)
                invalidate_shared_hashes(idx, false);
            else
                hash_remove((int)hb->view, hb->rip);
        }
    }
    if (shared_hash_views && idx < BLOCKS_MAX)
        memset(shared_hash_views[idx], 0, sizeof(*shared_hash_views));
    // Keep eager removal of every lookup to this entry and entry poisoning: SVC_DEAD starts with view_lookup,
    // and dead-block reuse can revive an entry. Conservative membership never makes lookup entries lazy.
    u32 *entry = cc_base + hb->entry_word;
    *entry = WORD_DEAD;
    if (sample) {
        u64 began = mach_absolute_time();
        sys_icache_invalidate(entry, 4);
        stats.invalidation_icache_ns += invalidation_ns(mach_absolute_time() - began);
    } else
        sys_icache_invalidate(entry, 4);
    stats.invalidated++;
    if (hb->read_bytes)
        dead_slots[dead_slot(hb->view, hb->rip)] = idx + 1;
}

int tr_invalidate_frame(uint64_t gpa, uint32_t why) {
    int n = 0;
    stats.kill_calls[why]++;
    if (gpa >= cfg.ram_size)
        return 0;
    u32 fr = (u32)(gpa >> 12);
    u32 it = frame_head[fr];
    frame_head[fr] = 0;
    u64 frame = gpa & ~0xfffULL;
    while (it) {
        HostBlock *hb = &hblocks[it - 1];
        u32 next = hb->frame[0] == frame ? hb->next_on_frame[0] : hb->next_on_frame[1];
        hb->on_frame = false;
        if (!hb->dead) {
            kill_block(it - 1);
            n++;
        }
        it = next;
    }
    stats.killed[why] += (u64)n;
    return n;
}

void tr_view_forget(int view) {
    hash_empty(view);
    hash_slots_used[view] = 0;
    stats.hash_forgets++;
}

int tr_invalidate_view_va(int view, uint64_t va, uint32_t why, bool unmoved, bool keeper) {
    int n = 0;
    u64 page = va & ~0xfffULL;
    u32 slot = page_slot(page);
    u32 *link = &page_head[slot];
    while (*link) {
        HostBlock *hb = &hblocks[*link - 1];
        u32 *next = &hb->next_on_page[(hb->rip >> 12 & (PAGE_HEADS - 1)) == slot ? 0 : 1];
        if (hb->dead) { // dead blocks leave here, which keeps the chains as short as the live blocks on these pages
            hb->on_page = false;
            *link = *next;
            continue;
        }
        u64 first = hb->rip & ~0xfffULL;
        bool here = first == page || (hb->frame[1] != ~0ULL && first + 4096 == page);
        if (here && (hb->view & KEY_DOMAIN) && !keeper) { // only this view's sight of it changed: the block stands for the others
            hash_remove(view, hb->rip);
        } else if (here && ((int)hb->view == view || (hb->view & KEY_DOMAIN))) {
            kill_block(*link - 1);
            n++;
            hb->on_page = false;
            *link = *next;
            continue;
        }
        link = next;
    }
    stats.kill_calls[why]++;
    stats.killed[why] += (u64)n;
    if (unmoved)
        stats.killed_unmoved[why] += (u64)n;
    return n;
}

void tr_view_drop(int view) {
    stats.kill_calls[KILL_VIEW_DROP]++;
    for (u32 it = view_head[view]; it; it = hblocks[it - 1].next_in_view) {
        hblocks[it - 1].on_view = false;
        if (!hblocks[it - 1].dead) {
            kill_block(it - 1);
            stats.killed[KILL_VIEW_DROP]++;
        }
    }
    view_head[view] = 0;
    memset(hash_table(view), 0, HASH_BYTES);
    hash_slots_used[view] = 0;
    ctx_ptr->tr_flushed = 1;
}

void tr_flush_all(void) {
    if (ctx_ptr->vcpu_count > 1) { // other vCPUs are running in this cache: the host gets them out first, and calls tr_flush_parked
        fprintf(stderr, "engine stop: code cache reset with other vCPUs running\n");
        abort();
    }
    tr_flush_parked(false);
}

void tr_flush_parked(bool everything) {
    everything = everything || part_full(0);
    ctx_ptr->tr_flushed = 1;
    memset(frame_head, 0, (cfg.ram_size >> 12) * sizeof(u32));
    memset(page_head, 0, PAGE_HEADS * sizeof(u32));
    memset(view_head, 0, sizeof view_head);
    memset(block_map, 0, MAP_SIZE * sizeof(MapEntry));
    flush_dead_slots(everything);
    for (int v = 0; v <= view_high; v++)
        memset(hash_table(v), 0, HASH_BYTES);
    memset(hash_slots_used, 0, sizeof hash_slots_used);
    membership_clear_all(); // Both full and private-part flushes empty ALL view hashes before any block index is reused.
    cc_cursor[1] = cc_base + CC_PART2_WORD;
    hblock_count[1] = 0;
    pc_map_used[1] = (OFF_PCMAP2 - OFF_PCMAP) / sizeof(PcMap);
    read_copied[1] = READ_COPY_BYTES / 2;
    ctx_ptr->block_count_private = 0;
    if (everything) {
        stats.cache_resets++;
        cc_cursor[0] = cc_base;
        hblock_count[0] = 0;
        pc_map_used[0] = 0;
        read_copied[0] = 1;
        ctx_ptr->block_count = 0;
        return;
    }
    stats.cache_private_resets++;
    // The shared part stays, and finds its way back onto what was just emptied. Its dead blocks stay off, as a kill took them.
    for (u32 idx = 0; idx < hblock_count[0]; idx++) {
        HostBlock *hb = &hblocks[idx];
        if (hb->dead) {
            hb->on_frame = hb->on_page = hb->on_view = false;
            continue;
        }
        link_block(idx);
        if (hb->registered)
            map_insert((int)hb->view, hb->rip, idx + 1);
    }
}

TrStats tr_stats(void) { return stats; }

