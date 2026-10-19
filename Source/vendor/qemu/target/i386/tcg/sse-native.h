/*
 * Local AArch64 packed-SSE experiment, not an upstream contribution.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef I386_SSE_NATIVE_H
#define I386_SSE_NATIVE_H

bool x86_tcg_native_packed_fp;
bool x86_tcg_native_exact_fp;

static void __attribute__((constructor)) init_native_packed_fp(void)
{
    const char *value = getenv("PVG_TCG_NATIVE_PACKED_FP");
    x86_tcg_native_packed_fp = value && !strcmp(value, "1");
    value = getenv("PVG_TCG_NATIVE_EXACT_FP");
    x86_tcg_native_exact_fp = value && !strcmp(value, "1");
}

enum {
    SSE_NATIVE_add, SSE_NATIVE_sub, SSE_NATIVE_mul, SSE_NATIVE_div,
    SSE_NATIVE_min, SSE_NATIVE_max,
};

#if defined(__aarch64__) && !defined(__FAST_MATH__)
#include <arm_neon.h>

/* For normal products, the exact significand has 47/48 or 105/106 bits.
 * Its discarded low 23/24 or 52/53 bits determine inexact without reading or
 * clearing host FPSR. A zero operand also gives zero discarded bits here.
 * Exceptional or tiny results must be rejected before using this predicate. */
static inline bool sse_mul_inexact32(uint32_t a, uint32_t b)
{
    uint64_t product = (uint64_t)((a & 0x7fffff) | 0x800000) *
                      ((b & 0x7fffff) | 0x800000);
    return product & ((product >> 47) ? 0xffffff : 0x7fffff);
}

static inline bool sse_mul_inexact64(uint64_t a, uint64_t b)
{
    __uint128_t product = (__uint128_t)((a & UINT64_C(0xfffffffffffff)) |
                                      UINT64_C(0x10000000000000)) *
                         ((b & UINT64_C(0xfffffffffffff)) |
                          UINT64_C(0x10000000000000));
    return (uint64_t)product & ((product >> 105) ?
        UINT64_C(0x1fffffffffffff) : UINT64_C(0xfffffffffffff));
}

/* Normal add/sub: align the integer significands without rounding, then
 * inspect the bits below the result's precision. Large exponent gaps cannot
 * produce an exact sum with a nonzero smaller operand. The bounded alignment
 * needs at most 49 bits (binary32) or 107 bits (binary64), including a carry.
 * Signed zero and tiny/exceptional results are handled by the caller. */
static inline bool sse_add_inexact32(uint32_t a, uint32_t b, bool subtract)
{
    uint32_t aa = a & 0x7fffffff, bb = b & 0x7fffffff;
    uint64_t x, y, sum;
    int gap, width;
    bool opposite = ((a ^ b) >> 31) ^ subtract;

    if (!aa || !bb) {
        return false;
    }
    if (aa < bb) {
        uint32_t tmp = aa; aa = bb; bb = tmp;
    }
    gap = (aa >> 23) - (bb >> 23);
    if (gap > 25) {
        return true;
    }
    x = (uint64_t)((aa & 0x7fffff) | 0x800000) << gap;
    y = (bb & 0x7fffff) | 0x800000;
    sum = opposite ? x - y : x + y;
    if (!sum) {
        return false;
    }
    width = 64 - __builtin_clzll(sum);
    return width > 24 && (sum & ((UINT64_C(1) << (width - 24)) - 1));
}

static inline bool sse_add_inexact64(uint64_t a, uint64_t b, bool subtract)
{
    uint64_t aa = a & UINT64_C(0x7fffffffffffffff);
    uint64_t bb = b & UINT64_C(0x7fffffffffffffff), high;
    __uint128_t x, y, sum;
    int gap, width;
    bool opposite = ((a ^ b) >> 63) ^ subtract;

    if (!aa || !bb) {
        return false;
    }
    if (aa < bb) {
        uint64_t tmp = aa; aa = bb; bb = tmp;
    }
    gap = (aa >> 52) - (bb >> 52);
    if (gap > 54) {
        return true;
    }
    x = (__uint128_t)((aa & UINT64_C(0xfffffffffffff)) |
                     UINT64_C(0x10000000000000)) << gap;
    y = (bb & UINT64_C(0xfffffffffffff)) | UINT64_C(0x10000000000000);
    sum = opposite ? x - y : x + y;
    if (!sum) {
        return false;
    }
    high = sum >> 64;
    width = high ? 128 - __builtin_clzll(high) :
                   64 - __builtin_clzll((uint64_t)sum);
    return width > 53 && ((uint64_t)sum &
                         ((UINT64_C(1) << (width - 53)) - 1));
}

static inline bool sse_inexact32(uint32_t a, uint32_t b, int op)
{
    return op == SSE_NATIVE_mul ? sse_mul_inexact32(a, b) :
                                 sse_add_inexact32(a, b, op == SSE_NATIVE_sub);
}

static inline bool sse_inexact64(uint64_t a, uint64_t b, int op)
{
    return op == SSE_NATIVE_mul ? sse_mul_inexact64(a, b) :
                                 sse_add_inexact64(a, b, op == SSE_NATIVE_sub);
}

static inline bool sse_native_scalar_exact(CPUX86State *env, ZMMReg *d,
                                           ZMMReg *v, ZMMReg *s,
                                           int bits, int op)
{
    float_status *status = &env->sse_status;
    uint64_t fpcr;
    bool inexact;

    if (op > SSE_NATIVE_mul || !x86_tcg_native_exact_fp ||
        (get_float_exception_flags(status) & float_flag_inexact) ||
        get_float_rounding_mode(status) != float_round_nearest_even) {
        return false;
    }
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    if (fpcr != 0) {
        return false;
    }
    if (bits == 32) {
        uint32_t a = v->ZMM_S(0), b = s->ZMM_S(0), r, aa, bb, rr;
        float fa, fb, fr;
        aa = a & 0x7fffffff;
        bb = b & 0x7fffffff;
        if ((aa && aa < 0x00800000) || (bb && bb < 0x00800000) ||
            aa >= 0x7f800000 || bb >= 0x7f800000) {
            return false;
        }
        memcpy(&fa, &a, 4);
        memcpy(&fb, &b, 4);
        switch (op) {
        case SSE_NATIVE_add:
            asm volatile("fadd %s0, %s1, %s2" : "=w"(fr) : "w"(fa), "w"(fb));
            break;
        case SSE_NATIVE_sub:
            asm volatile("fsub %s0, %s1, %s2" : "=w"(fr) : "w"(fa), "w"(fb));
            break;
        default:
            asm volatile("fmul %s0, %s1, %s2" : "=w"(fr) : "w"(fa), "w"(fb));
            break;
        }
        memcpy(&r, &fr, 4);
        rr = r & 0x7fffffff;
        if (rr >= 0x7f800000 ||
            (rr <= 0x00800000 &&
             !(rr == 0 && (op <= SSE_NATIVE_sub || aa == 0 || bb == 0)))) {
            return false;
        }
        inexact = sse_inexact32(a, b, op);
        d->ZMM_S(0) = r;
    } else {
        uint64_t a = v->ZMM_D(0), b = s->ZMM_D(0), r, aa, bb, rr;
        double fa, fb, fr;
        aa = a & UINT64_C(0x7fffffffffffffff);
        bb = b & UINT64_C(0x7fffffffffffffff);
        if ((aa && aa < UINT64_C(0x0010000000000000)) ||
            (bb && bb < UINT64_C(0x0010000000000000)) ||
            aa >= UINT64_C(0x7ff0000000000000) ||
            bb >= UINT64_C(0x7ff0000000000000)) {
            return false;
        }
        memcpy(&fa, &a, 8);
        memcpy(&fb, &b, 8);
        switch (op) {
        case SSE_NATIVE_add:
            asm volatile("fadd %d0, %d1, %d2" : "=w"(fr) : "w"(fa), "w"(fb));
            break;
        case SSE_NATIVE_sub:
            asm volatile("fsub %d0, %d1, %d2" : "=w"(fr) : "w"(fa), "w"(fb));
            break;
        default:
            asm volatile("fmul %d0, %d1, %d2" : "=w"(fr) : "w"(fa), "w"(fb));
            break;
        }
        memcpy(&r, &fr, 8);
        rr = r & UINT64_C(0x7fffffffffffffff);
        if (rr >= UINT64_C(0x7ff0000000000000) ||
            (rr <= UINT64_C(0x0010000000000000) &&
             !(rr == 0 && (op <= SSE_NATIVE_sub || aa == 0 || bb == 0)))) {
            return false;
        }
        inexact = sse_inexact64(a, b, op);
        d->ZMM_D(0) = r;
    }
    if (inexact) {
        float_raise(float_flag_inexact, status);
    }
    return true;
}

/* Keep arithmetic inside the FPSR interval, not compiler-scheduled across it.
 * For an already-sticky inexact flag, finite normal results need no new flag.
 * All other exceptions and boundary cases are left to the original helpers.
 */
#define SSE_NATIVE_OP(insn, lanes)                                      \
    do {                                                               \
        if (track) {                                                   \
            asm volatile("mrs %0, fpsr\n\t"                            \
                         "msr fpsr, xzr\n\t"                          \
                         insn " %2." lanes ", %3." lanes ", %4." lanes \
                         "\n\tmrs %1, fpsr\n\tmsr fpsr, %0"           \
                         : "=&r"(saved), "=&r"(raised), "=w"(r)       \
                         : "w"(a), "w"(b) : "memory");                \
        } else {                                                       \
            asm volatile(insn " %0." lanes ", %1." lanes ", %2." lanes \
                         : "=w"(r) : "w"(a), "w"(b));                 \
        }                                                              \
    } while (0)

static inline bool sse_native_packed(CPUX86State *env, ZMMReg *d,
                                     ZMMReg *v, ZMMReg *s,
                                     int bits, int shift, int op)
{
    float_status *status = &env->sse_status;
    uint64_t fpcr, saved, raised = 0;
    bool track, integer_flags;

    /* Restrict initial qualification to SSE's 128-bit vectors. */
    if (shift != 1 || op >= SSE_NATIVE_min || !x86_tcg_native_packed_fp ||
        get_float_rounding_mode(status) != float_round_nearest_even) {
        return false;
    }
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    if (fpcr != 0) {
        return false;
    }
    track = !(get_float_exception_flags(status) & float_flag_inexact);
    integer_flags = track && op <= SSE_NATIVE_mul && x86_tcg_native_exact_fp;
    track &= !integer_flags;

    if (bits == 32) {
        uint32x4_t a, b, r, aa, bb, rr, valid, zero_ok;
        memcpy(&a, &v->ZMM_S(0), 16);
        memcpy(&b, &s->ZMM_S(0), 16);
        aa = vandq_u32(a, vdupq_n_u32(0x7fffffff));
        bb = vandq_u32(b, vdupq_n_u32(0x7fffffff));
        valid = vandq_u32(
            vorrq_u32(vceqq_u32(aa, vdupq_n_u32(0)),
                      vcgeq_u32(aa, vdupq_n_u32(0x00800000))),
            vorrq_u32(vceqq_u32(bb, vdupq_n_u32(0)),
                      vcgeq_u32(bb, vdupq_n_u32(0x00800000))));
        valid = vandq_u32(valid, vandq_u32(
            vcltq_u32(aa, vdupq_n_u32(0x7f800000)),
            vcltq_u32(bb, vdupq_n_u32(0x7f800000))));
        if (vminvq_u32(valid) != UINT32_MAX) {
            return false;
        }
        switch (op) {
        case SSE_NATIVE_add: SSE_NATIVE_OP("fadd", "4s"); break;
        case SSE_NATIVE_sub: SSE_NATIVE_OP("fsub", "4s"); break;
        case SSE_NATIVE_mul: SSE_NATIVE_OP("fmul", "4s"); break;
        case SSE_NATIVE_div: SSE_NATIVE_OP("fdiv", "4s"); break;
        default: g_assert_not_reached();
        }
        rr = vandq_u32(r, vdupq_n_u32(0x7fffffff));
        valid = vandq_u32(vcgtq_u32(rr, vdupq_n_u32(0x00800000)),
                         vcltq_u32(rr, vdupq_n_u32(0x7f800000)));
        /* With normal/zero inputs, add/sub can yield zero only by exact
         * cancellation. Mul/div may underflow to zero: accept those zeros
         * only when an input already establishes an exact zero result. */
        zero_ok = op <= SSE_NATIVE_sub ? vdupq_n_u32(UINT32_MAX) :
            op == SSE_NATIVE_mul ? vorrq_u32(vceqq_u32(aa, vdupq_n_u32(0)),
                                             vceqq_u32(bb, vdupq_n_u32(0))) :
                                   vceqq_u32(aa, vdupq_n_u32(0));
        valid = vorrq_u32(valid, vandq_u32(zero_ok,
                                         vceqq_u32(rr, vdupq_n_u32(0))));
        if ((raised & ~UINT64_C(16)) || vminvq_u32(valid) != UINT32_MAX) {
            return false;
        }
        if (integer_flags) {
            for (int i = 0; i < 4; i++) {
                raised |= sse_inexact32(v->ZMM_S(i), s->ZMM_S(i), op) ? 16 : 0;
            }
        }
        memcpy(&d->ZMM_S(0), &r, 16);
    } else {
        uint64x2_t a, b, r, aa, bb, rr, valid, zero_ok;
        memcpy(&a, &v->ZMM_D(0), 16);
        memcpy(&b, &s->ZMM_D(0), 16);
        aa = vandq_u64(a, vdupq_n_u64(UINT64_C(0x7fffffffffffffff)));
        bb = vandq_u64(b, vdupq_n_u64(UINT64_C(0x7fffffffffffffff)));
        valid = vandq_u64(
            vorrq_u64(vceqq_u64(aa, vdupq_n_u64(0)),
                      vcgeq_u64(aa, vdupq_n_u64(UINT64_C(0x0010000000000000)))),
            vorrq_u64(vceqq_u64(bb, vdupq_n_u64(0)),
                      vcgeq_u64(bb, vdupq_n_u64(UINT64_C(0x0010000000000000)))));
        valid = vandq_u64(valid, vandq_u64(
            vcltq_u64(aa, vdupq_n_u64(UINT64_C(0x7ff0000000000000))),
            vcltq_u64(bb, vdupq_n_u64(UINT64_C(0x7ff0000000000000)))));
        if (vgetq_lane_u64(valid, 0) != UINT64_MAX ||
            vgetq_lane_u64(valid, 1) != UINT64_MAX) {
            return false;
        }
        switch (op) {
        case SSE_NATIVE_add: SSE_NATIVE_OP("fadd", "2d"); break;
        case SSE_NATIVE_sub: SSE_NATIVE_OP("fsub", "2d"); break;
        case SSE_NATIVE_mul: SSE_NATIVE_OP("fmul", "2d"); break;
        case SSE_NATIVE_div: SSE_NATIVE_OP("fdiv", "2d"); break;
        default: g_assert_not_reached();
        }
        rr = vandq_u64(r, vdupq_n_u64(UINT64_C(0x7fffffffffffffff)));
        valid = vandq_u64(
            vcgtq_u64(rr, vdupq_n_u64(UINT64_C(0x0010000000000000))),
            vcltq_u64(rr, vdupq_n_u64(UINT64_C(0x7ff0000000000000))));
        zero_ok = op <= SSE_NATIVE_sub ? vdupq_n_u64(UINT64_MAX) :
            op == SSE_NATIVE_mul ? vorrq_u64(vceqq_u64(aa, vdupq_n_u64(0)),
                                             vceqq_u64(bb, vdupq_n_u64(0))) :
                                   vceqq_u64(aa, vdupq_n_u64(0));
        valid = vorrq_u64(valid, vandq_u64(zero_ok,
                                         vceqq_u64(rr, vdupq_n_u64(0))));
        if ((raised & ~UINT64_C(16)) ||
            vgetq_lane_u64(valid, 0) != UINT64_MAX ||
            vgetq_lane_u64(valid, 1) != UINT64_MAX) {
            return false;
        }
        if (integer_flags) {
            raised = (sse_inexact64(v->ZMM_D(0), s->ZMM_D(0), op) |
                      sse_inexact64(v->ZMM_D(1), s->ZMM_D(1), op)) ? 16 : 0;
        }
        memcpy(&d->ZMM_D(0), &r, 16);
    }
    if (raised & 16) {
        float_raise(float_flag_inexact, status);
    }
    return true;
}
#undef SSE_NATIVE_OP
#else
static inline bool sse_native_scalar_exact(CPUX86State *env, ZMMReg *d,
                                           ZMMReg *v, ZMMReg *s,
                                           int bits, int op)
{
    return false;
}

static inline bool sse_native_packed(CPUX86State *env, ZMMReg *d,
                                     ZMMReg *v, ZMMReg *s,
                                     int bits, int shift, int op)
{
    return false;
}
#endif
#endif
