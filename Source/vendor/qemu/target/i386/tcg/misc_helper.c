/*
 *  x86 misc helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "accel/tcg/cpu-loop.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/cputlb.h"
#include "helper-tcg.h"
#ifndef CONFIG_USER_ONLY
#include "accel/tcg/tb-hash.h"
#endif

#if defined(__APPLE__) && defined(__aarch64__) && !defined(CONFIG_USER_ONLY)
/* Local experiment, NOT a C-callable function. Enter only via goto_ptr after
 * tcg_reg_alloc_bb_end has saved globals. AArch64 TCG reserves x19 for env;
 * x9..x17 and LR are dead at a block boundary. The prologue saved the caller's
 * LR/FP and callee-saved registers in its own frame. Do not modify SP, x18,
 * x19 or FP here. A miss calls the stock helper using the existing aligned
 * TCG call area, then branches to its block/epilogue result, never returns.
 * All structure offsets are compiled from this build, not donor constants.
 */
__attribute__((naked, noinline)) void pvg_tcg_dispatch_tail(void)
{
    __asm__ volatile(
        "bti j\n"
        "adrp x9, _x86_tcg_inline_jmp_cache@PAGE\n"
        "ldrb w9, [x9, _x86_tcg_inline_jmp_cache@PAGEOFF]\n"
        "cbz w9, 9f\n"
        "mov x9, #%c[env_offset]\n"
        "sub x9, x19, x9\n"
        "ldr x10, [x9, #%c[breakpoints]]\n"
        "cbnz x10, 9f\n"
        "adrp x10, _qemu_loglevel@PAGE\n"
        "ldr w10, [x10, _qemu_loglevel@PAGEOFF]\n"
        "mov w17, #%c[log_mask]\n"
        "tst w10, w17\n"
        "b.ne 9f\n"
        "ldr x10, [x9, #%c[cache]]\n"
        "add x11, x10, #%c[generation]\n"
        "ldar x11, [x11]\n"
        "ldr x12, [x19, #%c[eip]]\n"
        "eor x13, x12, x12, lsr #%c[hash_shift]\n"
        "lsr x14, x13, #%c[hash_shift]\n"
        "and x14, x14, #%c[hash_high]\n"
        "and x13, x13, #%c[hash_low]\n"
        "orr x13, x13, x14\n"
        "mov x14, #%c[entry_size]\n"
        "madd x13, x13, x14, x10\n"
        "add x13, x13, #%c[array]\n"
        "ldr x14, [x13, #%c[entry_tb]]\n"
        "cbz x14, 9f\n"
        "ldr x15, [x13, #%c[entry_generation]]\n"
        "cmp x15, x11\n"
        "b.ne 9f\n"
        /* Reject expired pointers before reading any TB storage. */
        "ldr x15, [x13, #%c[entry_context]]\n"
        "ldr x16, [x10, #%c[context]]\n"
        "cmp x15, x16\n"
        "b.ne 9f\n"
        "ldr x15, [x13, #%c[entry_pc]]\n"
        "cmp x15, x12\n"
        "b.ne 9f\n"
        "ldr x15, [x14, #%c[cs_base]]\n"
        "cbnz x15, 9f\n"
        "ldr w15, [x19, #%c[eflags]]\n"
        "movz w17, #%c[eflags_mask_lo]\n"
        "movk w17, #%c[eflags_mask_hi], lsl #16\n"
        "and w15, w15, w17\n"
        "ldr w16, [x19, #%c[hflags]]\n"
        "orr w15, w15, w16\n"
        "ldr w16, [x14, #%c[tb_flags]]\n"
        "cmp w15, w16\n"
        "b.ne 9f\n"
        "ldr w15, [x9, #%c[cpu_cflags]]\n"
        "ldr w16, [x14, #%c[tb_cflags]]\n"
        "cmp w15, w16\n"
        "b.ne 9f\n"
        "mov w15, #1\n"
        "strb w15, [x9, #%c[can_do_io]]\n"
        "ldr x16, [x14, #%c[code]]\n"
        "br x16\n"
        "9:\n"
        "mov x0, x19\n"
        "bl _helper_lookup_tb_ptr\n"
        "br x0\n"
        :
        : [env_offset] "i" (offsetof(X86CPU, env)),
          [breakpoints] "i" (offsetof(CPUState, breakpoints.tqh_first)),
          [log_mask] "i" (CPU_LOG_TB_CPU | CPU_LOG_EXEC),
          [cache] "i" (offsetof(CPUState, tb_jmp_cache)),
          [generation] "i" (offsetof(CPUJumpCache, generation)),
          [context] "i" (offsetof(CPUJumpCache, context_key)),
          [array] "i" (offsetof(CPUJumpCache, array)),
          [entry_size] "i" (sizeof(((CPUJumpCache *)0)->array[0])),
          [entry_tb] "i" (offsetof(CPUJumpCache, array[0].tb) - offsetof(CPUJumpCache, array)),
          [entry_generation] "i" (offsetof(CPUJumpCache, array[0].generation) - offsetof(CPUJumpCache, array)),
          [entry_context] "i" (offsetof(CPUJumpCache, array[0].context_key) - offsetof(CPUJumpCache, array)),
          [entry_pc] "i" (offsetof(CPUJumpCache, array[0].pc) - offsetof(CPUJumpCache, array)),
          [hash_shift] "i" (TARGET_PAGE_BITS - TB_JMP_CACHE_BITS / 2),
          [hash_high] "i" (TB_JMP_CACHE_SIZE - (1 << (TB_JMP_CACHE_BITS / 2))),
          [hash_low] "i" ((1 << (TB_JMP_CACHE_BITS / 2)) - 1),
          [eip] "i" (offsetof(CPUX86State, eip)),
          [eflags] "i" (offsetof(CPUX86State, eflags)),
          [hflags] "i" (offsetof(CPUX86State, hflags)),
          [eflags_mask_lo] "i" ((IOPL_MASK | TF_MASK | RF_MASK | VM_MASK | AC_MASK) & 0xffff),
          [eflags_mask_hi] "i" ((IOPL_MASK | TF_MASK | RF_MASK | VM_MASK | AC_MASK) >> 16),
          [cs_base] "i" (offsetof(TranslationBlock, cs_base)),
          [tb_flags] "i" (offsetof(TranslationBlock, flags)),
          [tb_cflags] "i" (offsetof(TranslationBlock, cflags)),
          [cpu_cflags] "i" (offsetof(CPUState, tcg_cflags)),
          [can_do_io] "i" (offsetof(CPUState, neg.can_do_io)),
          [code] "i" (offsetof(TranslationBlock, tc.ptr)));
}
#endif

/* Local shared-fast-path experiment. A miss returns NULL to the unchanged
 * generic lookup helper. NO_WG_SE matches that helper's synchronization ABI. */
const void *helper_pvg_cached_tb_ptr(CPUX86State *env)
{
#ifndef CONFIG_USER_ONLY
    CPUState *cpu = env_cpu(env);
    uint32_t flags = env->hflags |
        (env->eflags & (IOPL_MASK | TF_MASK | RF_MASK | VM_MASK | AC_MASK));
    if (!(flags & HF_CS64_MASK) || (flags & HF_CPL_MASK) != 3 ||
        !QTAILQ_EMPTY(&cpu->breakpoints) ||
        qemu_loglevel_mask(CPU_LOG_TB_CPU | CPU_LOG_EXEC)) {
        return NULL;
    }
    CPUJumpCache *jc = cpu->tb_jmp_cache;
    unsigned hash = tb_jmp_cache_hash_func(env->eip);
    uint64_t generation = qatomic_load_acquire(&jc->generation);
    TranslationBlock *tb = qatomic_read(&jc->array[hash].tb);
    if (tb && jc->array[hash].generation == generation &&
        jc->array[hash].context_key == jc->context_key &&
        jc->array[hash].pc == env->eip && !tb->cs_base &&
        tb->flags == flags && tb_cflags(tb) == cpu->tcg_cflags) {
        cpu->neg.can_do_io = true;
        return tb->tc.ptr;
    }
#endif
    return NULL;
}

/*
 * NOTE: the translator must set DisasContext.cc_op to CC_OP_EFLAGS
 * after generating a call to a helper that uses this.
 */
void cpu_load_eflags(CPUX86State *env, int eflags, int update_mask)
{
    CC_SRC = eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    CC_OP = CC_OP_EFLAGS;
    env->df = 1 - (2 * ((eflags >> 10) & 1));
    env->eflags = (env->eflags & ~update_mask) |
        (eflags & update_mask) | 0x2;
}

void helper_into(CPUX86State *env, int next_eip_addend)
{
    int eflags;

    eflags = cpu_cc_compute_all(env);
    if (eflags & CC_O) {
        raise_interrupt(env, EXCP04_INTO, next_eip_addend);
    }
}

void helper_cpuid(CPUX86State *env)
{
    uint32_t eax, ebx, ecx, edx;

    cpu_svm_check_intercept_param(env, SVM_EXIT_CPUID, 0, GETPC());

    cpu_x86_cpuid(env, (uint32_t)env->regs[R_EAX], (uint32_t)env->regs[R_ECX],
                  &eax, &ebx, &ecx, &edx);
    env->regs[R_EAX] = eax;
    env->regs[R_EBX] = ebx;
    env->regs[R_ECX] = ecx;
    env->regs[R_EDX] = edx;
}

void helper_rdtsc(CPUX86State *env)
{
    uint64_t val;

    if ((env->cr[4] & CR4_TSD_MASK) && ((env->hflags & HF_CPL_MASK) != 0)) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
    cpu_svm_check_intercept_param(env, SVM_EXIT_RDTSC, 0, GETPC());

    val = cpu_get_tsc(env) + env->tsc_offset;
    env->regs[R_EAX] = (uint32_t)(val);
    env->regs[R_EDX] = (uint32_t)(val >> 32);
}

G_NORETURN void helper_rdpmc(CPUX86State *env)
{
    if (((env->cr[4] & CR4_PCE_MASK) == 0 ) &&
        ((env->hflags & HF_CPL_MASK) != 0)) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
    cpu_svm_check_intercept_param(env, SVM_EXIT_RDPMC, 0, GETPC());

    /* currently unimplemented */
    qemu_log_mask(LOG_UNIMP, "x86: unimplemented rdpmc\n");
    raise_exception_err(env, EXCP06_ILLOP, 0);
}

G_NORETURN void helper_pause(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);

    /* Do gen_eob() tasks before going back to the main loop.  */
    do_end_instruction(env);
    helper_rechecking_single_step(env);

    /* Just let another CPU run.  */
    cs->exception_index = EXCP_INTERRUPT;
    cpu_loop_exit(cs);
}

uint64_t helper_rdpkru(CPUX86State *env, uint32_t ecx)
{
    if ((env->cr[4] & CR4_PKE_MASK) == 0) {
        raise_exception_err_ra(env, EXCP06_ILLOP, 0, GETPC());
    }
    if (ecx != 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }

    return env->pkru;
}

void helper_wrpkru(CPUX86State *env, uint32_t ecx, uint64_t val)
{
    CPUState *cs = env_cpu(env);

    if ((env->cr[4] & CR4_PKE_MASK) == 0) {
        raise_exception_err_ra(env, EXCP06_ILLOP, 0, GETPC());
    }
    if (ecx != 0 || (val & 0xFFFFFFFF00000000ull)) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }

    env->pkru = val;
    tlb_flush(cs);
}

target_ulong HELPER(rdpid)(CPUX86State *env)
{
#if !defined CONFIG_USER_ONLY
    return env->tsc_aux;
#elif defined CONFIG_LINUX && defined CONFIG_GETCPU
    unsigned cpu, node;
    getcpu(&cpu, &node);
    return (node << 12) | (cpu & 0xfff);
#elif defined CONFIG_SCHED_GETCPU
    return sched_getcpu();
#else
    return 0;
#endif
}
