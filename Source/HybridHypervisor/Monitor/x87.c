// x87 in the monitor: 80-bit arithmetic on integers, included by monitor.c. Every exception here is masked: an
// operation with one unmasked, or one pending, goes to TCG before it starts (see x87_op), since only TCG raises #MF. So
// each response below is the SDM's masked one, and all state is in EngineCtx.
#include "x87_helper_profile.h"
typedef unsigned __int128 u128;
typedef int32_t i32;

typedef struct {
    u64 m;
    u32 se; // sign and exponent, 16 bits
} F;

enum {
    FE_I = 1,
    FE_D = 2,
    FE_Z = 4,
    FE_O = 8,
    FE_U = 0x10,
    FE_P = 0x20,
    FE_SF = 0x40,
    FE_ES = 0x80,
    FC0 = 0x100,
    FC1 = 0x200,
    FC2 = 0x400,
    FC3 = 0x4000,
    FB = 0x8000
};
#define BIT63 (1ULL << 63)
#define BIT62 (1ULL << 62)
#define EXPMAX 0x7FFF
#define BIAS 16383

// xfl: exception flags and rounded-up (FC1) raised by the operation in progress

static inline u32 f_exp(F a) { return a.se & EXPMAX; }
static inline u32 f_sign(F a) { return a.se >> 15 & 1; }
static inline bool f_unsupported(F a) {
    return f_exp(a) && !(a.m & BIT63);
} // unnormals, pseudo-NaNs, pseudo-infinities
static inline bool f_isnan(F a) { return f_exp(a) == EXPMAX && (a.m << 1); }
static inline bool f_issnan(F a) { return f_isnan(a) && !(a.m & BIT62); }
static inline bool f_isinf(F a) { return f_exp(a) == EXPMAX && !(a.m << 1); }
static inline bool f_iszero(F a) { return !f_exp(a) && !a.m; }
static inline bool f_isdenorm(F a) { return !f_exp(a) && a.m; }
static inline F f_make(u32 sign, u32 exp, u64 m) { return (F){m, sign << 15 | exp}; }
static inline F f_indef(void) { return f_make(1, EXPMAX, BIT63 | BIT62); }
static inline F f_zero(u32 sign) { return f_make(sign, 0, 0); }
static inline F f_inf(u32 sign) { return f_make(sign, EXPMAX, BIT63); }
static inline F f_invalid(void) {
    xfl |= FE_I;
    return f_indef();
}

// Value is m / 2^63 * 2^(e - BIAS) with the top bit of m set. Not for zero.
static inline i32 f_unpack(F a, u64 *m) {
    i32 e = f_exp(a);
    *m = a.m;
    if (!e) {
        int s = __builtin_clzll(a.m);
        *m = a.m << s;
        e = 1 - s;
    }
    return e;
}

// Rounds at 24, 53 or 64 bits by RC and packs: the masked responses for overflow and underflow included. Tiny is judged
// after rounding, as x87 does.
static F round_pack(u32 sign, i32 e, u64 s0, u64 s1, int prec) {
    u32 rc = c->fcw >> 10 & 3;
    u64 orig;
    if (prec != 64) {
        u64 mask = prec == 53 ? 0x7FF : 0xFFFFFFFFFFULL;
        u64 half = prec == 53 ? 0x400 : 0x8000000000ULL;
        s0 |= s1 != 0;
        u64 inc = rc == 0 ? half : rc == 3 ? 0 : (rc == 2) != sign ? mask : 0;
        if ((u32)(e - 1) >= 0x7FFD) {
            if (e > 0x7FFE || (e == 0x7FFE && s0 + inc < s0)) {
                xfl |= FE_O | FE_P;
                if (rc == 3 || (rc == 2 && sign) || (rc == 1 && !sign))
                    return f_make(sign, 0x7FFE, ~mask);
                xfl |= FC1;
                return f_inf(sign);
            }
            if (e <= 0) {
                bool tiny = e < 0 || s0 + inc >= s0;
                int n = 1 - e;
                s0 = n > 63 ? s0 != 0 : s0 >> n | (s0 << (64 - n) != 0);
                u64 bits = s0 & mask;
                if (bits) {
                    xfl |= FE_P;
                    if (tiny)
                        xfl |= FE_U;
                }
                orig = s0 & ~mask;
                s0 += inc;
                e = s0 >> 63;
                if (rc == 0 && bits == half)
                    s0 &= ~(mask + 1);
                s0 &= ~mask;
                if (s0 != orig)
                    xfl |= FC1;
                return f_make(sign, e, s0);
            }
        }
        u64 bits = s0 & mask;
        if (bits)
            xfl |= FE_P;
        orig = s0 & ~mask;
        s0 += inc;
        if (s0 < inc) {
            e++;
            s0 = BIT63;
            xfl |= FC1;
            return f_make(sign, e, s0);
        }
        if (rc == 0 && bits == half)
            s0 &= ~(mask + 1);
        s0 &= ~mask;
        if (s0 != orig)
            xfl |= FC1;
        return f_make(sign, s0 ? e : 0, s0);
    }
    bool inc = rc == 0 ? (i64)s1 < 0 : rc == 3 ? false : (rc == 2) != sign && s1;
    if ((u32)(e - 1) >= 0x7FFD) {
        if (e > 0x7FFE || (e == 0x7FFE && s0 == ~0ULL && inc)) {
            xfl |= FE_O | FE_P;
            if (rc == 3 || (rc == 2 && sign) || (rc == 1 && !sign))
                return f_make(sign, 0x7FFE, ~0ULL);
            xfl |= FC1;
            return f_inf(sign);
        }
        if (e <= 0) {
            bool tiny = e < 0 || !inc || s0 != ~0ULL;
            int n = 1 - e;
            if (n < 64) {
                s1 = s0 << (64 - n) | (s1 != 0);
                s0 >>= n;
            } else {
                s1 = n == 64 ? s0 | (s1 != 0) : (s0 | s1) != 0;
                s0 = 0;
            }
            if (s1) {
                xfl |= FE_P;
                if (tiny)
                    xfl |= FE_U;
            }
            inc = rc == 0 ? (i64)s1 < 0 : rc == 3 ? false : (rc == 2) != sign && s1;
            e = 0;
            if (inc) {
                orig = s0;
                s0++;
                if (!(s1 << 1) && rc == 0)
                    s0 &= ~1ULL;
                if (s0 != orig)
                    xfl |= FC1;
                e = s0 >> 63;
            }
            return f_make(sign, e, s0);
        }
    }
    if (s1)
        xfl |= FE_P;
    if (inc) {
        orig = s0;
        s0++;
        if (!s0) {
            e++;
            s0 = BIT63;
            xfl |= FC1;
        } else {
            if (!(s1 << 1) && rc == 0)
                s0 &= ~1ULL;
            if (s0 != orig)
                xfl |= FC1;
        }
    } else if (!s0)
        e = 0;
    return f_make(sign, e, s0);
}

static inline int fcw_prec(void) {
    u32 pc = c->fcw >> 8 & 3;
    return pc == 0 ? 24 : pc == 2 ? 53 : 64;
}

// Normalizes a 128-bit significand, top bit anywhere, and rounds. Zero is the caller's business.
static F norm_round(u32 sign, i32 e, u128 s, int prec) {
    u64 hi = (u64)(s >> 64);
    int n = hi ? __builtin_clzll(hi) : 64 + __builtin_clzll((u64)s);
    s <<= n;
    return round_pack(sign, e - n, (u64)(s >> 64), (u64)s, prec);
}

static F quiet(F a) {
    if (f_issnan(a))
        xfl |= FE_I;
    a.m |= BIT62;
    return a;
}

// The rules of SDM table 4-7: a QNaN beats an SNaN, the larger significand beats the smaller, and on a tie the positive
// sign wins.
static F pick_nan(F a, F b) {
    if (!f_isnan(a))
        return quiet(b);
    if (!f_isnan(b))
        return quiet(a);
    bool sa = f_issnan(a), sb = f_issnan(b);
    xfl |= sa || sb ? FE_I : 0;
    F r;
    if (sa != sb)
        r = sa ? b : a;
    else if (a.m != b.m)
        r = a.m > b.m ? a : b;
    else
        r = f_sign(a) ? b : a;
    r.m |= BIT62;
    return r;
}

static inline void note_denormal(F a) {
    if (f_isdenorm(a))
        xfl |= FE_D;
}

static F f_add(F a, F b, bool subtract) {
    if (f_unsupported(a) || f_unsupported(b))
        return f_invalid();
    if (f_isnan(a) || f_isnan(b))
        return pick_nan(a, b);
    note_denormal(a);
    note_denormal(b);
    u32 sb = f_sign(b) ^ subtract, sa = f_sign(a);
    if (f_isinf(a)) {
        if (f_isinf(b) && sa != sb)
            return f_invalid();
        return a;
    }
    if (f_isinf(b))
        return f_inf(sb);
    bool za = f_iszero(a), zb = f_iszero(b);
    if (za && zb)
        return f_zero(sa == sb ? sa : (c->fcw >> 10 & 3) == 1);
    u64 ma = 0, mb = 0;
    i32 ea = za ? 0 : f_unpack(a, &ma), eb = zb ? 0 : f_unpack(b, &mb);
    int prec = fcw_prec();
    if (za)
        return round_pack(sb, eb, mb, 0, prec);
    if (zb)
        return round_pack(sa, ea, ma, 0, prec);
    // A bit free on top for the carry, and 63 below the operands for guard and sticky.
    u128 x = (u128)ma << 63, y = (u128)mb << 63;
    i32 e = ea, d = ea - eb;
    if (d < 0) {
        u128 t = x;
        x = y;
        y = t;
        u32 ts = sa;
        sa = sb;
        sb = ts;
        e = eb;
        d = -d;
    }
    if (d >= 127)
        y = y != 0;
    else if (d)
        y = y >> d | ((y << (128 - d)) != 0);
    u128 s;
    u32 sign = sa;
    if (sa == sb)
        s = x + y;
    else if (x >= y)
        s = x - y;
    else {
        s = y - x;
        sign = sb;
    }
    if (!s)
        return f_zero((c->fcw >> 10 & 3) == 1);
    return norm_round(sign, e + 1, s, prec);
}

static F f_mul(F a, F b) {
    if (f_unsupported(a) || f_unsupported(b))
        return f_invalid();
    if (f_isnan(a) || f_isnan(b))
        return pick_nan(a, b);
    note_denormal(a);
    note_denormal(b);
    u32 sign = f_sign(a) ^ f_sign(b);
    if (f_isinf(a) || f_isinf(b)) {
        if (f_iszero(a) || f_iszero(b))
            return f_invalid();
        return f_inf(sign);
    }
    if (f_iszero(a) || f_iszero(b))
        return f_zero(sign);
    u64 ma, mb;
    i32 ea = f_unpack(a, &ma), eb = f_unpack(b, &mb);
    return norm_round(sign, ea + eb - BIAS + 1, (u128)ma * mb, fcw_prec());
}

static F f_div(F a, F b) {
    if (f_unsupported(a) || f_unsupported(b))
        return f_invalid();
    if (f_isnan(a) || f_isnan(b))
        return pick_nan(a, b);
    u32 sign = f_sign(a) ^ f_sign(b);
    if (f_iszero(b)) { // Rosetta shows no #D for a denormal over zero
        if (f_iszero(a) || f_isinf(a))
            return f_iszero(a) ? f_invalid() : f_inf(sign);
        xfl |= FE_Z;
        return f_inf(sign);
    }
    note_denormal(a);
    note_denormal(b);
    if (f_isinf(a)) {
        if (f_isinf(b))
            return f_invalid();
        return f_inf(sign);
    }
    if (f_isinf(b))
        return f_zero(sign);
    if (f_iszero(a))
        return f_zero(sign);
    u64 ma, mb;
    i32 e = f_unpack(a, &ma) + BIAS;
    e -= f_unpack(b, &mb);
    u128 rem = ma;
    if (rem < mb) {
        rem <<= 1;
        e--;
    }
    // One bit a turn: the top bit, 63 more, and a guard. Sticky from what's left.
    u64 q0 = 0, q1 = 0;
    for (int i = 0; i < 65; i++) {
        q0 = q0 << 1 | q1 >> 63;
        q1 <<= 1;
        if (rem >= mb) {
            rem -= mb;
            q1 |= 1;
        }
        rem <<= 1;
    }
    u64 s1 = q1 << 63 | (rem != 0);
    u64 s0 = q0 << 63 | q1 >> 1;
    return round_pack(sign, e, s0, s1, fcw_prec());
}

static F f_sqrt(F a) {
    if (f_unsupported(a))
        return f_invalid();
    if (f_isnan(a))
        return quiet(a);
    if (f_iszero(a))
        return a;
    if (f_sign(a))
        return f_invalid();
    if (f_isinf(a))
        return a;
    note_denormal(a);
    u64 m;
    i32 e = f_unpack(a, &m) - BIAS;
    // Two integer bits and 64 of fraction: an even exponent, and the first root bit is always one.
    u128 n = (u128)m << 1;
    if (e & 1) {
        n <<= 1;
        e--;
    }
    u128 rem = 0, root = 0;
    for (int i = 0; i < 65; i++) {
        int shift = 64 - 2 * i;
        u32 two = shift >= 0 ? (u32)(n >> shift) & 3 : 0;
        rem = rem << 2 | two;
        u128 trial = root << 2 | 1;
        root <<= 1;
        if (rem >= trial) {
            rem -= trial;
            root |= 1;
        }
    }
    u64 s0 = (u64)(root >> 1);
    u64 s1 = (u64)root << 63 | (rem != 0);
    return round_pack(0, e / 2 + BIAS, s0, s1, fcw_prec());
}

// fprem and fprem1. *cc: the condition codes, from the low quotient bits or C2 for a partial remainder. How far a
// partial one goes is what QEMU does, and it says Intel does too.
static F f_rem(F a, F b, bool nearest, u32 *cc) {
    *cc = 0;
    if (f_unsupported(a) || f_unsupported(b))
        return f_invalid();
    if (f_isnan(a) || f_isnan(b))
        return pick_nan(a, b);
    if (f_isinf(a) || f_iszero(b))
        return f_invalid();
    note_denormal(a);
    note_denormal(b);
    if (f_iszero(a))
        return a;
    u64 ma, mb;
    u32 sign = f_sign(a);
    i32 ea = f_unpack(a, &ma);
    if (f_isinf(b))
        return round_pack(sign, ea, ma, 0, 64);
    i32 eb = f_unpack(b, &mb), diff = ea - eb;
    if (diff < 0 && (!nearest || diff < -1))
        return round_pack(sign, ea, ma, 0, 64);
    bool partial = diff >= 64;
    int steps = partial ? 32 + diff % 32 : diff;
    i32 eres = ea - steps; // the remainder's scale, the divisor's at the last step
    u128 r = ma;
    u64 q = 0;
    if (diff >= 0) {
        for (int i = 0; i <= steps; i++) {
            q <<= 1;
            if (r >= mb) {
                r -= mb;
                q |= 1;
            }
            if (i < steps)
                r <<= 1;
        }
    } else {
        r = ma;
        eres = ea;
    }
    if (nearest && !partial) {
        u128 d = diff < 0 ? (u128)mb << 1 : mb;
        u128 twice = r << 1;
        if (twice > d || (twice == d && (q & 1))) {
            r = d - r;
            sign ^= 1;
            q++;
        }
    }
    if (partial)
        *cc = FC2;
    else
        *cc = (q & 4 ? FC0 : 0) | (q & 2 ? FC3 : 0) | (q & 1 ? FC1 : 0);
    if (!r)
        return f_zero(f_sign(a));
    return norm_round(sign, eres + 64, r, 64);
}

// Rounds to an integer value by rc, for frndint and the integer stores. Returns the magnitude; sets *inexact and *up.
static u64 to_integer(F a, u32 rc, bool *inexact, bool *up, bool *too_big) {
    *inexact = *up = *too_big = false;
    u32 sign = f_sign(a);
    if (f_iszero(a))
        return 0;
    i32 e = (i32)f_exp(a) - BIAS;
    if (e >= 64) {
        *too_big = true;
        return 0;
    }
    u64 v, rest;
    int shift = 63 - e;
    if (shift == 0) {
        v = a.m;
        rest = 0;
    } else if (shift >= 64) {
        v = 0;
        rest = shift == 64 ? a.m : 1; // sticky alone, below one half
    } else {
        v = a.m >> shift;
        rest = a.m << (64 - shift);
    }
    if (!rest)
        return v;
    *inexact = true;
    bool inc = rc == 0 ? rest > BIT63 || (rest == BIT63 && (v & 1)) : rc == 3 ? false : (rc == 2) != sign;
    if (inc) {
        v++;
        *up = true;
        if (!v)
            *too_big = true;
    }
    return v;
}

static F f_rndint(F a) {
    if (f_unsupported(a))
        return f_invalid();
    if (f_isnan(a))
        return quiet(a);
    if (f_isinf(a) || f_iszero(a))
        return a;
    note_denormal(a);
    if (f_exp(a) >= BIAS + 63)
        return a;
    bool inexact, up, big;
    u64 v = to_integer(a, c->fcw >> 10 & 3, &inexact, &up, &big);
    if (inexact)
        xfl |= FE_P | (up ? FC1 : 0);
    if (!v)
        return f_zero(f_sign(a));
    int n = __builtin_clzll(v);
    return f_make(f_sign(a), BIAS + 63 - n, v << n);
}

static F f_scale(F a, F b) {
    if (f_unsupported(a) || f_unsupported(b))
        return f_invalid();
    if (f_isnan(a) || f_isnan(b))
        return pick_nan(a, b);
    note_denormal(a);
    note_denormal(b);
    if (f_isinf(b)) {
        if (f_sign(b)) {
            if (f_isinf(a))
                return f_invalid();
            return f_zero(f_sign(a));
        }
        if (f_iszero(a))
            return f_invalid();
        return f_inf(f_sign(a));
    }
    if (f_isinf(a) || f_iszero(a))
        return a;
    i32 n = 0;
    if (f_exp(b) >= BIAS + 16)
        n = f_sign(b) ? -0x10000 : 0x10000;
    else if (f_exp(b) >= BIAS) {
        n = (i32)(b.m >> (BIAS + 63 - f_exp(b)));
        if (f_sign(b))
            n = -n;
    }
    u64 m;
    i32 e = f_unpack(a, &m);
    return round_pack(f_sign(a), e + n, m, 0, 64);
}

// A float or double to ext, exact, an SNaN staying signalling for whoever uses it. A denormal reports #D.
static F from_fp(u64 v, int fraction, int exp_bits) {
    u32 sign = v >> (fraction + exp_bits) & 1;
    u32 exp = v >> fraction & ((1u << exp_bits) - 1);
    u64 frac = v & ((1ULL << fraction) - 1);
    u32 emax = (1u << exp_bits) - 1;
    if (exp == emax) {
        if (!frac)
            return f_inf(sign);
        return f_make(sign, EXPMAX, BIT63 | frac << (63 - fraction));
    }
    i32 bias = (1 << (exp_bits - 1)) - 1;
    if (!exp) {
        if (!frac)
            return f_zero(sign);
        xfl |= FE_D;
        int n = __builtin_clzll(frac);
        return f_make(sign, (u32)(BIAS - bias + 1 + (64 - n - 1) - fraction), frac << n);
    }
    return f_make(sign, (u32)((i32)exp - bias + BIAS), BIT63 | frac << (63 - fraction));
}

// Ext to a float or double, with the responses for overflow and underflow. Rounds by RC; PC doesn't apply.
static u64 to_fp(F a, int fraction, int exp_bits) {
    u32 sign = f_sign(a);
    u64 sbit = (u64)sign << (fraction + exp_bits);
    u32 emax = (1u << exp_bits) - 1;
    u64 quiet_nan = (u64)emax << fraction | 1ULL << (fraction - 1);
    if (f_unsupported(a)) {
        xfl |= FE_I;
        return 1ULL << (fraction + exp_bits) | quiet_nan;
    }
    if (f_isnan(a)) {
        if (f_issnan(a))
            xfl |= FE_I;
        return sbit | quiet_nan | (a.m << 1 >> (64 - fraction));
    }
    if (f_isinf(a))
        return sbit | (u64)emax << fraction;
    if (f_iszero(a))
        return sbit;
    u64 s;
    i32 e = f_unpack(a, &s) - BIAS + (1 << (exp_bits - 1)) - 1;
    u32 rc = c->fcw >> 10 & 3;
    int shift = 63 - fraction;
    u64 mask = (1ULL << shift) - 1, half = 1ULL << (shift - 1);
    u64 inc = rc == 0 ? half : rc == 3 ? 0 : (rc == 2) != sign ? mask : 0;
    if (e >= (i32)emax - 1 && (e > (i32)emax - 1 || s + inc < s)) {
        xfl |= FE_O | FE_P;
        if (rc == 3 || (rc == 2 && sign) || (rc == 1 && !sign))
            return sbit | ((u64)(emax - 1) << fraction | ((1ULL << fraction) - 1));
        xfl |= FC1;
        return sbit | (u64)emax << fraction;
    }
    u64 orig;
    if (e <= 0) {
        bool tiny = e < 0 || s + inc >= s;
        int n = 1 - e;
        s = n > 63 ? 1 : s >> n | (s << (64 - n) != 0);
        u64 bits = s & mask;
        if (bits) {
            xfl |= FE_P;
            if (tiny)
                xfl |= FE_U;
        }
        orig = s & ~mask;
        s += inc;
        if (rc == 0 && bits == half)
            s &= ~(mask + 1);
        s &= ~mask;
        if (s != orig)
            xfl |= FC1;
        return sbit | s >> shift; // a carry into the hidden bit lands in the exponent, as it should
    }
    u64 bits = s & mask;
    if (bits)
        xfl |= FE_P;
    orig = s & ~mask;
    s += inc;
    if (s < inc) { // carried out of the top
        xfl |= FC1;
        return sbit | (u64)(e + 1) << fraction;
    }
    if (rc == 0 && bits == half)
        s &= ~(mask + 1);
    s &= ~mask;
    if (s != orig)
        xfl |= FC1;
    return sbit | (((u64)(e - 1) << fraction) + (s >> shift));
}

static inline F f_load(F a) { return f_isnan(a) ? quiet(a) : a; }

static F from_int(i64 v) {
    if (!v)
        return f_zero(0);
    u32 sign = v < 0;
    u64 m = sign ? 0 - (u64)v : (u64)v;
    int n = __builtin_clzll(m);
    return f_make(sign, BIAS + 63 - n, m << n);
}

// Stores as a signed integer of this many bits, or its indefinite on #IA.
static u64 to_int(F a, int bits, u32 rc) {
    u64 indefinite = 1ULL << (bits - 1);
    if (f_unsupported(a) || f_isnan(a) || f_isinf(a)) {
        xfl |= FE_I;
        return indefinite;
    }
    bool inexact, up, big;
    u64 v = to_integer(a, rc, &inexact, &up, &big);
    if (big || v > (f_sign(a) ? indefinite : indefinite - 1)) {
        xfl |= FE_I;
        return indefinite;
    }
    if (inexact)
        xfl |= FE_P | (up ? FC1 : 0);
    v = f_sign(a) ? 0 - v : v;
    return bits == 64 ? v : v & ((1ULL << bits) - 1);
}

// 0 greater, 1 less, 2 equal, 3 unordered, as ST0 against the other.
static u32 f_cmp(F a, F b, bool quiet_only) {
    if (f_unsupported(a) || f_unsupported(b)) {
        xfl |= FE_I;
        return 3;
    }
    if (f_isnan(a) || f_isnan(b)) {
        if (!quiet_only || f_issnan(a) || f_issnan(b))
            xfl |= FE_I;
        return 3;
    }
    note_denormal(a);
    note_denormal(b);
    bool za = f_iszero(a), zb = f_iszero(b);
    if (za && zb)
        return 2;
    u32 sa = f_sign(a), sb = f_sign(b);
    if (za)
        return sb ? 0 : 1;
    if (zb)
        return sa ? 1 : 0;
    if (sa != sb)
        return sa ? 1 : 0;
    u64 ma, mb;
    i32 ea = f_isinf(a) ? 0x10000 : f_unpack(a, &ma), eb = f_isinf(b) ? 0x10000 : f_unpack(b, &mb);
    if (ea == 0x10000)
        ma = BIT63;
    if (eb == 0x10000)
        mb = BIT63;
    if (ea == eb && ma == mb)
        return 2;
    bool less = ea < eb || (ea == eb && ma < mb);
    return less != (bool)sa ? 1 : 0;
}

// ---- The stack ----
static inline F reg_get(u32 phys) { return (F){c->mm[phys], c->fp_exp[phys]}; }
static inline void reg_set(u32 phys, F v) {
    c->mm[phys] = v.m;
    c->fp_exp[phys] = (u16)v.se;
}

typedef struct {
    u32 top, tags, fsw; // working copies: a store commits them after its memory access
} X87;

static inline u32 st_phys(const X87 *s, u32 i) { return (s->top + i) & 7; }
static inline bool st_empty(const X87 *s, u32 i) { return !(s->tags >> st_phys(s, i) & 1); }
static inline F st_get(const X87 *s, u32 i) { return reg_get(st_phys(s, i)); }
static inline void st_set(X87 *s, u32 i, F v) {
    u32 p = st_phys(s, i);
    reg_set(p, v);
    s->tags |= 1u << p;
}
static inline void st_pop(X87 *s) {
    s->tags &= ~(1u << s->top);
    s->top = (s->top + 1) & 7;
}
static inline void underflow(void) { xfl |= FE_I | FE_SF; }

// Stack overflow: register 7 from TOP is in use. The masked response loads the indefinite instead of the value, in
// place.
static inline bool st_push(X87 *s, F v) {
    s->top = (s->top - 1) & 7;
    bool over = s->tags >> s->top & 1;
    if (over) {
        xfl |= FE_I | FE_SF | FC1;
        v = f_indef();
    }
    st_set(s, 0, v);
    return !over;
}

static inline bool st_full(const X87 *s) { return s->tags >> ((s->top - 1) & 7) & 1; }

static F arith(u32 kind, F a, F b) { // kind as modrm reg: 0 add, 1 mul, 4 sub, 5 subr, 6 div, 7 divr
    switch (kind) {
        case 0:
            return f_add(a, b, false);
        case 1:
            return f_mul(a, b);
        case 4:
            return f_add(a, b, true);
        case 5:
            return f_add(b, a, true);
        case 6:
            return f_div(a, b);
        default:
            return f_div(b, a);
    }
}

static inline void set_cc(X87 *s, u32 result) {
    static const u16 codes[4] = {0, FC0, FC3, FC3 | FC2 | FC0};
    s->fsw = (s->fsw & ~(FC0 | FC2 | FC3)) | codes[result];
}

// CF, ZF, PF in the lazy forms; OF, SF and AF clear.
static void set_rflags_cmp(u32 result) {
    static const u8 zpc[4] = {0, 1, 4, 7}; // bits: CF 1, PF 2, ZF 4
    u32 f = zpc[result];
    u64 nzcv = 0;
    if (!(f & 1))
        nzcv |= 1ULL << 29;
    if (f & 4)
        nzcv |= 1ULL << 30;
    c->spsr = (c->spsr & ~(0xFULL << 28)) | nzcv;
    c->x[R_PF] = f & 2 ? 0 : 1;
    c->x[R_AF] = c->x[R_PF];
}

static bool rflags_cond(u32 which) { // 0 CF, 1 ZF, 2 CF or ZF, 3 PF
    bool cf = !(c->spsr & (1ULL << 29)), zf = c->spsr & (1ULL << 30);
    switch (which) {
        case 0:
            return cf;
        case 1:
            return zf;
        case 2:
            return cf || zf;
        default: {
            u32 p = (u32)c->x[R_PF] & 0xFF;
            p ^= p >> 4;
            p ^= p >> 2;
            p ^= p >> 1;
            return !(p & 1);
        }
    }
}

// To the nearest. round 2: RC up takes the next value. round 1: RC down and chop take the one before.
static const struct {
    u64 m;
    u32 se, round;
} fp_consts[7] = {
    {BIT63, 0x3FFF, 0},
    {0xD49A784BCD1B8AFEULL, 0x4000, 2}, // log2(10)
    {0xB8AA3B295C17F0BCULL, 0x3FFF, 1}, // log2(e)
    {0xC90FDAA22168C235ULL, 0x4000, 1}, // pi
    {0x9A209A84FBCFF799ULL, 0x3FFD, 1}, // log10(2)
    {0xB17217F7D1CF79ACULL, 0x3FFE, 1}, // ln(2)
    {0, 0, 0},
};

static u32 full_tag(const X87 *s) {
    u32 tag = 0;
    for (int i = 7; i >= 0; i--) {
        tag <<= 2;
        if (!(s->tags >> i & 1)) {
            tag |= 3;
            continue;
        }
        F r = reg_get((u32)i);
        if (f_iszero(r))
            tag |= 1;
        else if (!f_exp(r) || f_exp(r) == EXPMAX || !(r.m & BIT63))
            tag |= 2;
    }
    return tag;
}

static void x87_fninit(X87 *s) {
    c->fcw = 0x37F;
    s->fsw = 0;
    s->top = 0;
    s->tags = 0;
    c->fop = 0;
    c->fip = 0;
    c->fdp = 0;
    c->fcs = 0;
    c->fds = 0;
}

// The images of fnstenv and fldenv. fnsave and frstor add the registers after them.
static inline void put16(u8 *p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}
static inline void put32(u8 *p, u32 v) {
    put16(p, v);
    put16(p + 2, v >> 16);
}
static inline u32 get16(const u8 *p) { return p[0] | (u32)p[1] << 8; }
static inline u32 get32(const u8 *p) { return get16(p) | get16(p + 2) << 16; }

static void env_put(u8 *p, const X87 *s, bool data16) {
    u32 sw = s->fsw | s->top << 11;
    if (data16) {
        put16(p, c->fcw);
        put16(p + 2, sw);
        put16(p + 4, full_tag(s));
        put16(p + 6, (u32)c->fip);
        put16(p + 8, c->fcs);
        put16(p + 10, (u32)c->fdp);
        put16(p + 12, c->fds);
        return;
    }
    put32(p, c->fcw | 0xFFFF0000u);
    put32(p + 4, sw | 0xFFFF0000u);
    put32(p + 8, full_tag(s) | 0xFFFF0000u);
    put32(p + 12, (u32)c->fip);
    put32(p + 16, c->fcs | (u32)c->fop << 16);
    put32(p + 20, (u32)c->fdp);
    put32(p + 24, c->fds | 0xFFFF0000u);
}

static void env_get(const u8 *p, X87 *s, bool data16) {
    u32 sw, tag;
    if (data16) {
        c->fcw = (u16)get16(p);
        sw = get16(p + 2);
        tag = get16(p + 4);
        c->fip = get16(p + 6);
        c->fcs = (u16)get16(p + 8);
        c->fdp = get16(p + 10);
        c->fds = (u16)get16(p + 12);
    } else {
        c->fcw = (u16)get16(p);
        sw = get16(p + 4);
        tag = get16(p + 8);
        c->fip = get32(p + 12);
        c->fcs = (u16)get16(p + 16);
        c->fop = get16(p + 18) & 0x7FF;
        c->fdp = get32(p + 20);
        c->fds = (u16)get16(p + 24);
    }
    s->fsw = sw & ~0x3800u;
    s->top = sw >> 11 & 7;
    s->tags = 0;
    for (int i = 0; i < 8; i++)
        if ((tag >> (2 * i) & 3) != 3)
            s->tags |= 1u << i;
}

// Writing guest memory from here sees past the store traps that keep translations and shadow tables honest, so a frame
// holding either goes to TCG, which notes its writes.
static bool fp_area_write(u64 va, const void *in, u32 n) {
    u32 access = (c->cpl == 3 ? 4 : 0) | 2;
    for (int end = 0; c->hybrid && end < 2; end++) {
        Walk w = x86_walk(c->cr3, end ? va + n - 1 : va, access, false);
        if (!w.ok || !is_ram(w.gpa) || (page_info[w.gpa >> 12] & (PI_CODE | PI_PT)))
            return false;
    }
    return guest_write(va, in, n, access);
}

_Noreturn static void fp_cannot(u64 rip, u32 op, u32 why, u32 code) {
    if (c->hybrid)
        stop(STOP_FALLBACK, rip, why, op, 0);
    stop(STOP_UNSUPPORTED, rip, code, op, 0);
}

static void x87_area(u32 op, bool data16, u64 va, X87 *s, u64 rip) {
    u8 *area = me->fx_area;
    u32 reg = op >> 3 & 7;
    bool full = (op >> 8 & 7) == 5; // fnsave and frstor; D9 is the environment alone
    u32 env = data16 ? 14 : 28, n = env + (full ? 80 : 0);
    if (reg == 6) {
        env_put(area, s, data16);
        for (u32 i = 0; full && i < 8; i++) {
            F r = st_get(s, i);
            for (int b = 0; b < 8; b++)
                area[env + i * 10 + (u32)b] = (u8)(r.m >> (8 * b));
            put16(area + env + i * 10 + 8, r.se);
        }
        if (!fp_area_write(va, area, n))
            fp_cannot(rip, op, FB_X87, 17);
        if (full)
            x87_fninit(s);
        else
            c->fcw |= 0x3F; // fnstenv masks everything
    } else {
        if (!guest_read(va, area, n, c->cpl == 3 ? 4 : 0))
            fp_cannot(rip, op, FB_X87, 17);
        env_get(area, s, data16);
        for (u32 i = 0; full && i < 8; i++) {
            u64 m = 0;
            for (int b = 7; b >= 0; b--)
                m = m << 8 | area[env + i * 10 + (u32)b];
            reg_set(st_phys(s, i), (F){m, get16(area + env + i * 10 + 8)});
        }
    }
}

static F bcd_load(u64 lo, u64 hi) {
    u64 v = 0;
    for (int i = 8; i >= 0; i--) {
        u32 b = (u32)((i == 8 ? hi : lo >> (8 * i)) & 0xFF);
        v = v * 100 + (b >> 4) * 10 + (b & 15);
    }
    F r = from_int((i64)v);
    if (hi & 0x8000)
        r.se |= 0x8000;
    return r;
}

static void bcd_store(F a, u64 *lo, u64 *hi) {
    u64 v = 0;
    if (!f_unsupported(a) && !f_isnan(a) && !f_isinf(a)) {
        bool inexact, up, big;
        v = to_integer(a, c->fcw >> 10 & 3, &inexact, &up, &big);
        if (!big && v < 1000000000000000000ULL) {
            if (inexact)
                xfl |= FE_P | (up ? FC1 : 0);
            u64 packed = 0;
            for (int i = 0; i < 9; i++) {
                u32 d = (u32)(v % 100);
                v /= 100;
                d = (d / 10) << 4 | (d % 10);
                if (i == 8)
                    *hi = d;
                else
                    packed |= (u64)d << (8 * i);
            }
            *lo = packed;
            *hi |= f_sign(a) ? 0x8000 : 0;
            return;
        }
    }
    xfl |= FE_I;
    *lo = 0xC000000000000000ULL;
    *hi = 0xFFFF;
}

// One operation. The literal after the svc gives the opcode and rip; EL0 has moved any memory operand.
static void x87_op(void) {
    u32 lit0 = *(u32 *)c->elr, op = lit0 & 0xFFFF;
    u64 rip = *(u64 *)(c->elr + 4);
    engine_profile_x87_helper(c,op);
    if (op == 0xFFFF) { // fwait
        if ((c->cr0 & 0xA) == 0xA) {
            c->stat_nm++;
            deliver(7, 0, false, rip, false);
            return;
        }
        if (c->fsw & FE_ES)
            fp_cannot(rip, op, FB_X87, 16);
        c->elr += 12;
        return;
    }
    if (c->cr0 & 0xC) { // TS or EM
        c->stat_nm++;
        deliver(7, 0, false, rip, false);
        return;
    }
    u32 esc = op >> 8 & 7, m = op & 0xFF, reg = m >> 3 & 7, rm = m & 7;
    bool mem = m < 0xC0;
    bool control = mem ? (esc == 1 && reg >= 4) || (esc == 5 && reg >= 4)
                       : (esc == 3 && (m == 0xE2 || m == 0xE3)) || (esc == 7 && m == 0xE0);
    if (!control) {
        if ((c->fcw & 0x3F) != 0x3F || (c->fsw & FE_ES))
            fp_cannot(rip, op, FB_X87, 16);
        c->fop = (u16)op;
        c->fip = rip;
        c->fcs = c->seg_sel[1];
        if (mem) {
            c->fdp = c->x[R_AD]; // shortcut: holds an fs or gs base too, which a CPU's offset wouldn't. Nothing reads
                                 // it for meaning; upgrade if something does.
            u32 seg = lit0 >> 16 & 3;
            c->fds = c->seg_sel[seg == 1 ? 4 : seg == 2 ? 5 : 3];
        }
    }
    c->elr += 12;
    X87 st = {c->fp_top & 7u, c->fp_tags, c->fsw}, *s = &st;
    u64 v = c->x[R_S0], hi = c->x[R_S1];
    bool store = false, pop = false, c1_kept = control;
    u64 out = 0, out_hi = 0;
    xfl = 0;
    if (mem) {
        switch (esc << 3 | reg) {
            case 0x08: // fld m32
                st_push(s, f_load(from_fp(v, 23, 8)));
                break;
            case 0x28: // fld m64
                st_push(s, f_load(from_fp(v, 52, 11)));
                break;
            case 0x18: // fild m32
                st_push(s, from_int((int32_t)v));
                break;
            case 0x38: // fild m16
                st_push(s, from_int((int16_t)v));
                break;
            case 0x3D: // fild m64
                st_push(s, from_int((i64)v));
                break;
            case 0x1D: // fld m80
                st_push(s, (F){v, (u32)hi & 0xFFFF});
                break;
            case 0x3C: // fbld
                st_push(s, bcd_load(v, hi));
                break;
            case 0x0D: // fldcw
                c->fcw = (u16)v;
                // Flags now unmasked are pending, as the SDM has it, for the next waiting operation.
                if (st.fsw & ~v & 0x3F)
                    st.fsw |= FE_ES | FB;
                else
                    st.fsw &= ~(FE_ES | FB);
                break;
            case 0x0F: // fnstcw
                store = true;
                out = c->fcw;
                break;
            case 0x2F: // fnstsw
                store = true;
                out = st.fsw | st.top << 11;
                break;
            case 0x0C:
            case 0x0E:
            case 0x2C:
            case 0x2E:
                x87_area(op, lit0 >> 18 & 1, c->x[R_AD], s, rip);
                break;
            case 0x0A:
            case 0x0B: // fst, fstp m32
            case 0x2A:
            case 0x2B: // m64
            case 0x19:
            case 0x1A:
            case 0x1B: // fisttp, fist, fistp m32
            case 0x29: // fisttp m64
            case 0x39:
            case 0x3A:
            case 0x3B: // fisttp, fist, fistp m16
            case 0x3F: // fistp m64
            case 0x1F: // fstp m80
            case 0x3E: { // fbstp
                u32 form = esc << 3 | reg;
                store = true;
                pop = !(form == 0x0A || form == 0x2A || form == 0x1A || form == 0x3A);
                bool empty = st_empty(s, 0);
                F a = empty ? f_indef() : st_get(s, 0);
                if (empty)
                    underflow();
                u32 rc = c->fcw >> 10 & 3;
                switch (form) {
                    case 0x0A:
                    case 0x0B:
                        out = to_fp(a, 23, 8);
                        break;
                    case 0x2A:
                    case 0x2B:
                        out = to_fp(a, 52, 11);
                        break;
                    case 0x19:
                        out = to_int(a, 32, 3);
                        break;
                    case 0x1A:
                    case 0x1B:
                        out = to_int(a, 32, rc);
                        break;
                    case 0x29:
                        out = to_int(a, 64, 3);
                        break;
                    case 0x39:
                        out = to_int(a, 16, 3);
                        break;
                    case 0x3A:
                    case 0x3B:
                        out = to_int(a, 16, rc);
                        break;
                    case 0x3F:
                        out = to_int(a, 64, rc);
                        break;
                    case 0x1F:
                        out = a.m;
                        out_hi = a.se;
                        break;
                    default:
                        bcd_store(a, &out, &out_hi);
                        break;
                }
                break;
            }
            default: { // the arithmetic group, D8, DA, DC, DE
                F b = esc == 0   ? from_fp(v, 23, 8)
                      : esc == 4 ? from_fp(v, 52, 11)
                      : esc == 2 ? from_int((int32_t)v)
                                 : from_int((int16_t)v);
                bool empty = st_empty(s, 0);
                if (!empty && (f_isnan(st_get(s, 0)) || f_unsupported(st_get(s, 0))))
                    xfl &= ~(u32)FE_D; // these come ahead of a denormal operand
                if (reg == 2 || reg == 3) {
                    if (empty) {
                        xfl = FE_I | FE_SF;
                        set_cc(s, 3);
                    } else
                        set_cc(s, f_cmp(st_get(s, 0), b, false));
                    pop = reg == 3;
                } else if (empty) {
                    xfl = FE_I | FE_SF;
                    st_set(s, 0, f_indef());
                } else
                    st_set(s, 0, arith(reg, st_get(s, 0), b));
                break;
            }
        }
    } else {
        bool e0 = st_empty(s, 0), ei = st_empty(s, rm);
        F a = st_get(s, 0), b = st_get(s, rm);
        switch (esc) {
            case 0: // D8
                if (reg == 2 || reg == 3) {
                    if (e0 || ei) {
                        underflow();
                        set_cc(s, 3);
                    } else
                        set_cc(s, f_cmp(a, b, false));
                    pop = reg == 3;
                } else if (e0 || ei) {
                    underflow();
                    st_set(s, 0, f_indef());
                } else
                    st_set(s, 0, arith(reg, a, b));
                break;
            case 1: // D9
                if (reg == 0) { // fld st(i)
                    if (ei) {
                        underflow();
                        b = f_indef();
                    }
                    st_push(s, b);
                } else if (reg == 1) { // fxch
                    if (e0 || ei) {
                        underflow();
                        if (e0)
                            a = f_indef();
                        if (ei)
                            b = f_indef();
                    }
                    st_set(s, 0, b);
                    st_set(s, rm, a);
                } else if (m == 0xD0) {
                } else if (m == 0xE0 || m == 0xE1) { // fchs, fabs
                    if (e0) {
                        underflow();
                        st_set(s, 0, f_indef());
                    } else {
                        a.se = m == 0xE0 ? a.se ^ 0x8000 : a.se & 0x7FFF;
                        st_set(s, 0, a);
                    }
                } else if (m == 0xE4) { // ftst
                    if (e0) {
                        underflow();
                        set_cc(s, 3);
                    } else
                        set_cc(s, f_cmp(a, f_zero(0), false));
                } else if (m == 0xE5) { // fxam
                    u32 cc = FC1 * f_sign(a);
                    if (e0)
                        cc |= FC3 | FC0;
                    else if (f_iszero(a))
                        cc |= FC3;
                    else if (f_unsupported(a))
                        cc |= 0;
                    else if (f_isnan(a))
                        cc |= FC0;
                    else if (f_isinf(a))
                        cc |= FC2 | FC0;
                    else if (!f_exp(a))
                        cc |= FC3 | FC2;
                    else
                        cc |= FC2;
                    st.fsw = (st.fsw & ~(FC0 | FC1 | FC2 | FC3)) | cc;
                    c1_kept = true;
                } else if (m >= 0xE8 && m <= 0xEE) { // fld1, fldl2t, fldl2e, fldpi, fldlg2, fldln2, fldz
                    F k = {fp_consts[m - 0xE8].m, fp_consts[m - 0xE8].se};
                    u32 rc = c->fcw >> 10 & 3, adjust = fp_consts[m - 0xE8].round;
                    if (adjust == 2 && rc == 2)
                        k.m++;
                    else if (adjust == 1 && (rc == 1 || rc == 3))
                        k.m--;
                    st_push(s, k);
                } else if (m == 0xF4) { // fxtract
                    if (e0) {
                        underflow();
                        st_set(s, 0, f_indef());
                        st_push(s, f_indef());
                    } else if (st_full(s)) { // ST7 in use, so the result has nowhere to go
                        xfl |= FE_I | FE_SF | FC1 | (f_iszero(a) ? FE_Z : 0);
                        note_denormal(a);
                        st_set(s, 0, f_indef());
                        st_push(s, f_indef());
                    } else if (f_unsupported(a)) {
                        xfl |= FE_I;
                        st_set(s, 0, f_indef());
                        st_push(s, f_indef());
                    } else if (f_isnan(a)) {
                        a = quiet(a);
                        st_set(s, 0, a);
                        st_push(s, a);
                    } else if (f_iszero(a)) {
                        xfl |= FE_Z;
                        st_set(s, 0, f_inf(1));
                        st_push(s, a);
                    } else if (f_isinf(a)) {
                        st_set(s, 0, f_inf(0));
                        st_push(s, a);
                    } else {
                        note_denormal(a);
                        u64 sig;
                        i32 e = f_unpack(a, &sig);
                        st_set(s, 0, from_int(e - BIAS));
                        st_push(s, f_make(f_sign(a), BIAS, sig));
                    }
                } else if (m == 0xF5 || m == 0xF8) { // fprem1, fprem
                    u32 cc = 0;
                    F b1 = st_get(s, 1);
                    st.fsw &= ~(FC0 | FC1 | FC2 | FC3);
                    if (e0 || st_empty(s, 1)) {
                        underflow();
                        st_set(s, 0, f_indef());
                    } else {
                        st_set(s, 0, f_rem(a, b1, m == 0xF5, &cc));
                        st.fsw |= cc;
                    }
                    c1_kept = true;
                } else if (m == 0xF6 || m == 0xF7) { // fdecstp, fincstp
                    st.top = (st.top + (m == 0xF7 ? 1 : 7)) & 7;
                } else if (m == 0xFA || m == 0xFC) { // fsqrt, frndint
                    if (e0) {
                        underflow();
                        st_set(s, 0, f_indef());
                    } else
                        st_set(s, 0, m == 0xFA ? f_sqrt(a) : f_rndint(a));
                } else if (m == 0xFD) { // fscale
                    if (e0 || st_empty(s, 1)) {
                        underflow();
                        st_set(s, 0, f_indef());
                    } else
                        st_set(s, 0, f_scale(a, st_get(s, 1)));
                }
                break;
            case 2:
            case 3:
                if (reg <= 3) { // fcmov
                    bool hit = rflags_cond(reg) == (esc == 2);
                    if (e0 || ei) {
                        underflow();
                        st_set(s, 0, f_indef());
                    } else if (hit)
                        st_set(s, 0, b);
                } else if (esc == 2) { // fucompp
                    F b1 = st_get(s, 1);
                    if (e0 || st_empty(s, 1)) {
                        underflow();
                        set_cc(s, 3);
                    } else
                        set_cc(s, f_cmp(a, b1, true));
                    st_pop(s);
                    pop = true;
                } else if (m == 0xE2) { // fnclex
                    st.fsw &= ~(0x7F | FE_ES | FB);
                } else if (m == 0xE3) { // fninit
                    x87_fninit(s);
                } else { // fucomi, fcomi
                    u32 r = 3;
                    if (e0 || ei)
                        underflow();
                    else
                        r = f_cmp(a, b, reg == 5);
                    set_rflags_cmp(r);
                }
                break;
            case 4: // DC
            case 6: // DE
                if (esc == 6 && m == 0xD9) { // fcompp
                    F b1 = st_get(s, 1);
                    if (e0 || st_empty(s, 1)) {
                        underflow();
                        set_cc(s, 3);
                    } else
                        set_cc(s, f_cmp(a, b1, false));
                    st_pop(s);
                    pop = true;
                    break;
                }
                if (e0 || ei) {
                    underflow();
                    st_set(s, rm, f_indef());
                } else
                    st_set(s, rm, arith(reg >= 4 ? reg ^ 1 : reg, b, a));
                pop = esc == 6;
                break;
            case 5: // DD
                if (reg == 0) { // ffree
                    st.tags &= ~(1u << st_phys(s, rm));
                } else if (reg == 2 || reg == 3) { // fst, fstp st(i)
                    if (e0) {
                        underflow();
                        a = f_indef();
                    }
                    st_set(s, rm, a);
                    pop = reg == 3;
                } else { // fucom, fucomp
                    if (e0 || ei) {
                        underflow();
                        set_cc(s, 3);
                    } else
                        set_cc(s, f_cmp(a, b, true));
                    pop = reg == 5;
                }
                break;
            default: // DF
                if (m == 0xE0) { // fnstsw ax
                    c->x[R_S0] = st.fsw | st.top << 11;
                } else { // fucomip, fcomip
                    u32 r = 3;
                    if (e0 || ei)
                        underflow();
                    else
                        r = f_cmp(a, b, reg == 5);
                    set_rflags_cmp(r);
                    pop = true;
                }
                break;
        }
    }
    if (pop)
        st_pop(s);
    if (!control)
        st.fsw = (st.fsw & (c1_kept ? ~0u : ~(u32)FC1)) | xfl;
    if (store) {
        c->x[R_S0] = out;
        c->x[R_S1] = out_hi;
        c->x[R_S3] = st.top | st.tags << 8 | (u64)st.fsw << 16;
    } else {
        c->fp_top = (u8)st.top;
        c->fp_tags = (u8)st.tags;
        c->fsw = (u16)st.fsw;
    }
}

// fxsave and fxrstor, in 64-bit (REX.W, x22 set) or 32-bit format. Leaves bytes 416 up alone, as the CPU does: software
// may keep its own there.
static void do_fx(bool save) {
    u8 *area = me->fx_area;
    u64 va = c->x[R_S0], here = rip_here(c->elr - 4);
    bool wide = c->x[R_S2];
    if (c->cr0 & 0xC) {
        c->stat_nm++;
        deliver(7, 0, false, here, false);
        return;
    }
    if (c->hybrid && (!(c->cr4 & 0x200) || (c->efer & (1ULL << 14))))
        stop(STOP_FALLBACK, here, FB_FX, 0, 0); // what these leave out then is QEMU's to decide
    if (save) {
        memset(area, 0, 416);
        put16(area, c->fcw);
        put16(area + 2, c->fsw | (u32)(c->fp_top & 7) << 11);
        area[4] = c->fp_tags;
        put16(area + 6, c->fop);
        put32(area + 8, (u32)c->fip);
        put32(area + 16, (u32)c->fdp);
        if (wide) {
            put32(area + 12, (u32)(c->fip >> 32));
            put32(area + 20, (u32)(c->fdp >> 32));
        } else {
            put16(area + 12, c->fcs);
            put16(area + 20, c->fds);
        }
        put32(area + 24, mxcsr_read());
        put32(area + 28, 0xFFFF);
        for (u32 i = 0; i < 8; i++) {
            u32 p = (c->fp_top + i) & 7;
            u8 *r = area + 32 + i * 16;
            put32(r, (u32)c->mm[p]);
            put32(r + 4, (u32)(c->mm[p] >> 32));
            put16(r + 8, c->fp_exp[p]);
        }
        mon_fp_save(area + 160);
        if (!fp_area_write(va, area, 416))
            stop(c->hybrid ? STOP_FALLBACK : STOP_UNSUPPORTED, here, c->hybrid ? FB_FX : 12, va, 0);
        return;
    }
    if (!guest_read(va, area, 416, c->cpl == 3 ? 4 : 0))
        stop(c->hybrid ? STOP_FALLBACK : STOP_UNSUPPORTED, here, c->hybrid ? FB_FX : 12, va, 0);
    u32 mx = get32(area + 24);
    if (mx & 0xFFFF0000u) {
        x86_gp0();
        return;
    }
    if ((mx & 0x1F80) != 0x1F80) // unmasked SSE exceptions cannot trap here
        stop(c->hybrid ? STOP_FALLBACK : STOP_UNSUPPORTED, here, c->hybrid ? FB_FX : 14, mx, 0);
    u32 sw = get16(area + 2);
    c->fcw = (u16)get16(area);
    c->fsw = (u16)(sw & ~0x3800u);
    c->fp_top = sw >> 11 & 7;
    c->fp_tags = area[4];
    c->fop = get16(area + 6) & 0x7FF;
    c->fip = get32(area + 8);
    c->fdp = get32(area + 16);
    if (wide) {
        c->fip |= (u64)get32(area + 12) << 32;
        c->fdp |= (u64)get32(area + 20) << 32;
    } else {
        c->fcs = (u16)get16(area + 12);
        c->fds = (u16)get16(area + 20);
    }
    for (u32 i = 0; i < 8; i++) {
        u32 p = (c->fp_top + i) & 7;
        const u8 *r = area + 32 + i * 16;
        c->mm[p] = get32(r) | (u64)get32(r + 4) << 32;
        c->fp_exp[p] = (u16)get16(r + 8);
    }
    mon_fp_restore(area + 160);
    mxcsr_apply(mx);
}
