// Integer x86-64 to AArch64 emitters. Rule for every emitter: nothing guest-visible (registers, NZCV, lazy PF/AF) changes
// before the instruction's last access that can fault, so any data abort is precise at the x86 instruction start.
#include <stddef.h>
#include "tr_internal.h"

// ---- basics ----
void movimm(int rd, u64 v) {
    int zeros = 0, ones = 0;
    for (int i = 0; i < 4; i++) {
        u32 h = (v >> (i * 16)) & 0xffff;
        zeros += h == 0;
        ones += h == 0xffff;
    }
    if (zeros == 4) {
        mov_reg(1, rd, XZR);
        return;
    }
    bool inv = ones > zeros, first = true;
    for (int i = 0; i < 4; i++) {
        u32 h = (v >> (i * 16)) & 0xffff;
        if (h == (inv ? 0xffffu : 0))
            continue;
        if (first)
            E((inv ? 0x92800000u : 0xD2800000u) | i << 21 | (inv ? ~h & 0xffff : h) << 5 | rd);
        else
            E(0xF2800000u | i << 21 | h << 5 | rd);
        first = false;
    }
    if (first)
        E(0x92800000u | rd); // all ones
}

bool logimm(u64 v, int sf, u32 *out) {
    if (!sf)
        v = (v & 0xffffffffULL) | (v << 32);
    if (v == 0 || v == ~0ULL)
        return false;
    int size = 64;
    while (size > 2) {
        int half = size / 2;
        u64 mask = (1ULL << half) - 1;
        if ((v & mask) != ((v >> half) & mask))
            break;
        size = half;
    }
    u64 mask = size == 64 ? ~0ULL : (1ULL << size) - 1, elt = v & mask;
    for (int r = 0; r < size; r++) {
        u64 rot = r ? ((elt >> r) | (elt << (size - r))) & mask : elt;
        if ((rot & (rot + 1)) == 0) {
            int ones = __builtin_popcountll(rot), immr = (size - r) % size;
            int imms = ((~(size * 2 - 1)) & 0x3f) | (ones - 1);
            *out = (size == 64) << 22 | immr << 16 | imms << 10;
            return true;
        }
    }
    return false;
}

static void and_imm_w(int rd, int rn, u32 imm) { // imm must encode
    u32 enc;
    logimm(imm, 0, &enc);
    E(0x12000000u | enc | rn << 5 | rd);
}

int addr_reg(const Insn *in) {
    int cur = XZR;
    int segr = in->seg == 1 ? R_FS : in->seg == 2 ? R_GS : -1;
    int sf = !tr_compat; // 32-bit code's sums wrap at 4 GB, and its registers already have clear top halves
    if (in->riprel) {
        movimm(AD, in->next + in->disp);
        cur = AD;
    } else {
        if (!in->no_base)
            cur = in->base;
        if (!in->no_index) {
            add_reg(sf, 0, 0, AD, cur, in->index, in->scale);
            cur = AD;
        }
        if (in->disp) {
            if (in->disp > 0 && in->disp < 4096 && cur != XZR)
                add_imm(sf, 0, 0, AD, cur, (u32)in->disp);
            else if (in->disp < 0 && in->disp > -4096 && cur != XZR)
                add_imm(sf, 1, 0, AD, cur, (u32)-in->disp);
            else if (cur == XZR)
                movimm(AD, tr_compat ? (u32)in->disp : (u64)in->disp);
            else {
                movimm(S4, (u64)in->disp);
                add_reg(sf, 0, 0, AD, cur, S4, 0);
            }
            cur = AD;
        }
    }
    if (in->addr16) {
        ubfx(0, AD, cur, 0, 16);
        cur = AD;
    } else if (in->addr32 && !tr_compat) {
        mov_reg(0, AD, cur);
        cur = AD;
    }
    if (segr >= 0) {
        add_reg(sf, 0, 0, AD, cur, segr, 0);
        cur = AD;
    }
    if (cur == XZR) { // address 0: x31 as a base would be SP
        mov_reg(1, AD, XZR);
        cur = AD;
    }
    return cur;
}

Addr addr_prepare(const Insn *in, int access) {
    Addr a = {0};
    int segr = in->seg == 1 ? R_FS : in->seg == 2 ? R_GS : -1;
    if (in->addr32 || in->riprel) {
        a.base = addr_reg(in);
        return a;
    }
    if (segr >= 0) {
        if (in->no_base && in->no_index) {
            a.base = segr;
            a.imm = in->disp;
            return a;
        }
        if (in->no_index) {
            add_reg(1, 0, 0, AD, in->base, segr, 0);
            a.base = AD;
            a.imm = in->disp;
            return a;
        }
        a.base = addr_reg(in);
        return a;
    }
    if (in->no_base && in->no_index) {
        movimm(AD, (u64)in->disp);
        a.base = AD;
        return a;
    }
    if (in->no_base) {
        add_reg(1, 0, 0, AD, XZR, in->index, in->scale);
        a.base = AD;
        a.imm = in->disp;
        return a;
    }
    if (in->no_index) {
        a.base = in->base;
        a.imm = in->disp;
        return a;
    }
    if (!in->disp && (in->scale == 0 || in->scale == log2sz(access))) {
        a.base = in->base;
        a.index = in->index;
        a.shift = in->scale;
        a.reg_form = true;
        return a;
    }
    add_reg(1, 0, 0, AD, in->base, in->index, in->scale);
    a.base = AD;
    a.imm = in->disp;
    return a;
}

void ldst(int opc, int sz, int rt, Addr a) {
    u32 s = log2sz(sz);
    if (a.reg_form) {
        E(s << 30 | 0x38200800u | opc << 22 | a.index << 16 | 3u << 13 | (a.shift ? 1u : 0) << 12 | a.base << 5 | rt);
    } else if (a.imm >= 0 && !(a.imm & (sz - 1)) && (a.imm >> s) < 4096) {
        E(s << 30 | 0x39000000u | opc << 22 | (u32)(a.imm >> s) << 10 | a.base << 5 | rt);
    } else if (a.imm >= -256 && a.imm < 256) {
        E(s << 30 | 0x38000000u | opc << 22 | ((u32)a.imm & 0x1ff) << 12 | a.base << 5 | rt);
    } else {
        movimm(S4, (u64)a.imm);
        E(s << 30 | 0x38200800u | opc << 22 | S4 << 16 | 3u << 13 | a.base << 5 | rt);
    }
}

int gpr_read(int reg, int sz, bool high, int scratch) {
    if (sz == 1 && high) {
        ubfx(0, scratch, reg - 4, 8, 8);
        return scratch;
    }
    return reg;
}

void gpr_write(int reg, int sz, bool high, int src) {
    if (sz == 8) {
        if (reg != src)
            mov_reg(1, reg, src);
    } else if (sz == 4)
        mov_reg(0, reg, src);
    else if (sz == 2)
        bfi(1, reg, src, 0, 16);
    else if (high)
        bfi(1, reg - 4, src, 8, 8);
    else
        bfi(1, reg, src, 0, 8);
}

void emit_parity(int rd) {
    logic_sh(0, 2, 0, S5, R_PF, R_PF, 1, 4);
    and_imm_w(S5, S5, 0xf);
    movimm(rd, 0x9669);
    var_shift(0, 1, rd, rd, S5);
    and_imm_w(rd, rd, 1);
}

static void uext(int rd, int rn, int sz) { // zero-extend to the operand size
    if (sz == 8) {
        if (rd != rn)
            mov_reg(1, rd, rn);
    } else if (sz == 4)
        mov_reg(0, rd, rn);
    else
        ubfx(0, rd, rn, 0, sz * 8);
}
static void sext64(int rd, int rn, int sz) {
    if (sz == 8) {
        if (rd != rn)
            mov_reg(1, rd, rn);
    } else
        sbfx(1, rd, rn, 0, sz * 8);
}

static void bl_abs(u64 target) { bl_rel((int)((i64)(target - tr_cc_va(tr_cc)) / 4)); }

// Atomics. op: 0 add, 1 clr, 2 eor, 3 set; 8 is swp.
static void lse(int op, int sz, int rs, int rt, int rn) {
    u32 s = log2sz(sz);
    E(s << 30 | 0x38E00000u | (op & 7) << 12 | (op == 8 ? 0x8000u : 0) | rs << 16 | rn << 5 | rt);
}
static void casal(int sz, int rs, int rt, int rn) {
    E((u32)log2sz(sz) << 30 | 0x08E0FC00u | rs << 16 | rn << 5 | rt);
}

// ---- ALU ----
static void alu_value(int alu, int sz, int rd, int a, int b) {
    int sf = sz == 8;
    switch (alu) {
        case ALU_ADD:
            add_reg(sf, 0, 0, rd, a, b, 0);
            break;
        case ALU_SUB:
        case ALU_CMP:
            add_reg(sf, 1, 0, rd, a, b, 0);
            break;
        case ALU_AND:
        case ALU_TEST:
            logic_reg(sf, 0, 0, rd, a, b);
            break;
        case ALU_OR:
            logic_reg(sf, 1, 0, rd, a, b);
            break;
        case ALU_XOR:
            logic_reg(sf, 2, 0, rd, a, b);
            break;
        case ALU_ADC: // NZCV must survive: flags are computed after the store
            cset(S4, 3);
            add_reg(sf, 0, 0, rd, a, b, 0);
            add_reg(sf, 0, 0, rd, rd, S4, 0);
            break;
        case ALU_SBB:
            adc(sf, 1, 0, rd, a, b);
            break;
    }
}

// Sets flags for a op b; r holds the value (needed for PF and for or/xor). Uses S3, S4, S5, AD.
static void alu_flags(int alu, int sz, int live, int a, int b, int r) {
    if (!live)
        return;
    int sf = sz == 8, narrow = sz < 4, k = 32 - sz * 8;
    bool arith = alu == ALU_ADD || alu == ALU_SUB || alu == ALU_CMP || alu == ALU_ADC || alu == ALU_SBB;
    if (arith && (live & F_A))
        logic_reg(1, 2, 0, R_AF, a, b);
    if (alu == ALU_ADD || alu == ALU_SUB || alu == ALU_CMP) {
        bool sub = alu != ALU_ADD;
        if (narrow) {
            lsl_imm(0, S5, a, k);
            add_reg(0, sub, 1, XZR, S5, b, k);
        } else
            add_reg(sf, sub, 1, XZR, a, b, 0);
        if (!sub && (live & F_C))
            cfinv();
    } else if (alu == ALU_ADC || alu == ALU_SBB) {
        bool sub = alu == ALU_SBB;
        if (!narrow) {
            if (!sub)
                cfinv();
            adc(sf, sub, 1, XZR, a, b);
            if (!sub && (live & F_C))
                cfinv();
        } else {
            cset(S4, 3); // CF. AD is free here: any store already happened, and a or b may be S3 (a high byte)
            sext64(S5, a, sz);
            sext64(AD, b, sz);
            add_reg(1, sub, 0, S5, S5, AD, 0);
            add_reg(1, sub, 0, S5, S5, S4, 0);
            if (sz == 1)
                setf8(S5);
            else
                setf16(S5);
            uext(S5, a, sz);
            uext(AD, b, sz);
            add_reg(1, sub, 0, S5, S5, AD, 0);
            add_reg(1, sub, 0, S5, S5, S4, 0);
            logic_reg(1, 1, 1, S5, XZR, S5); // canonical C is the inverse of carry or borrow
            rmif(S5, (sz * 8 - 1) & 63, 2);
        }
    } else { // logic: C=0 in x86 terms, V=0
        if (narrow) {
            if (alu == ALU_AND || alu == ALU_TEST) {
                lsl_imm(0, S5, a, k);
                logic_sh(0, 3, 0, XZR, S5, b, 0, k);
            } else {
                lsl_imm(0, S5, r, k);
                logic_reg(0, 3, 0, XZR, S5, S5);
            }
        } else if (alu == ALU_AND || alu == ALU_TEST)
            logic_reg(sf, 3, 0, XZR, a, b);
        else
            logic_reg(sf, 3, 0, XZR, r, r);
        if (live & F_C)
            cfinv();
    }
    if (!arith && (live & F_A))
        mov_reg(0, R_AF, r);
    if (live & F_P)
        mov_reg(0, R_PF, r);
}

static int imm_to(const Insn *in, int scratch) {
    u64 v = (u64)in->imm;
    if (in->sz == 4)
        v &= 0xffffffffULL;
    else if (in->sz == 2)
        v &= 0xffff;
    else if (in->sz == 1)
        v &= 0xff;
    if (!v)
        return XZR;
    movimm(scratch, v);
    return scratch;
}

static void emit_ud(void) { svc(SVC_UD); }

// lock-prefixed ALU, memory destination. Value is computed without touching NZCV; flags follow the atomic.
static void emit_lock_alu(const Insn *in, int live) {
    int alu = in->sub, sz = in->sz;
    if (in->rm_reg || alu == ALU_CMP || alu == ALU_TEST) {
        emit_ud();
        return;
    }
    int src = in->has_imm ? imm_to(in, S1) : gpr_read(in->reg, sz, in->high_reg, S1);
    int addr = addr_reg(in);
    int r = S2;
    switch (alu) {
        case ALU_ADD:
            lse(0, sz, src, S0, addr);
            break;
        case ALU_SUB:
            add_reg(1, 1, 0, S3, XZR, src, 0);
            lse(0, sz, S3, S0, addr);
            break;
        case ALU_AND:
            logic_reg(1, 1, 1, S3, XZR, src);
            lse(1, sz, S3, S0, addr);
            break;
        case ALU_XOR:
            lse(2, sz, src, S0, addr);
            break;
        case ALU_OR:
            lse(3, sz, src, S0, addr);
            break;
        default: { // adc, sbb: compare and swap loop; C is read inside and NZCV stays put until the swap lands
            u32 *head = tr_cc;
            Addr a = {.base = addr};
            ldst(1, sz, S0, a);
            alu_value(alu, sz, r, S0, src);
            mov_reg(1, S3, S0);
            casal(sz, S3, r, addr);
            logic_reg(1, 2, 0, S3, S3, S0);
            cbz(1, 1, S3, (int)(head - tr_cc));
        }
    }
    if ((live & F_P) || alu == ALU_OR || alu == ALU_XOR) {
        if (alu != ALU_ADC && alu != ALU_SBB)
            alu_value(alu, sz, r, S0, src);
    }
    alu_flags(alu, sz, live, S0, src, r);
}

static void emit_alu(const Insn *in, int live) {
    if (in->lock) {
        emit_lock_alu(in, live);
        return;
    }
    int sz = in->sz, sf = sz == 8, alu = in->sub;
    bool narrow = sz < 4, wb = alu != ALU_CMP && alu != ALU_TEST;
    bool mem = !in->rm_reg;
    Addr a = {0};
    if (mem) {
        a = addr_prepare(in, sz);
        ldst(1, sz, S0, a);
    }
    int dst, src = XZR;
    bool use_imm = false;
    if (in->to_reg) {
        dst = gpr_read(in->reg, sz, in->high_reg, S3);
        src = mem ? S0 : gpr_read(in->rm, sz, in->high_rm, S1);
    } else {
        dst = mem ? S0 : gpr_read(in->rm, sz, in->high_rm, S3);
        if (in->has_imm)
            use_imm = true;
        else
            src = gpr_read(in->reg, sz, in->high_reg, S1);
    }
    bool dst_is_reg = in->to_reg || !mem;
    int dst_gpr = in->to_reg ? in->reg : in->rm;
    bool dst_high = in->to_reg ? in->high_reg : in->high_rm;

    // Fast path: 32/64-bit, no store. One flag-setting ALU instruction.
    if (!narrow && (dst_is_reg || !wb)) {
        int rd = !wb ? XZR : dst_gpr;
        bool sets = live != 0;
        bool arith = alu == ALU_ADD || alu == ALU_SUB || alu == ALU_CMP;
        i64 imm = (u64)in->imm;
        if (sz == 4)
            imm &= 0xffffffffLL;
        if (use_imm && arith) {
            bool sub = alu != ALU_ADD;
            if (imm >= 0 && imm < 4096) {
                if (live & F_A) {
                    // Lazy AF observes only bit 4 of (R_AF ^ R_PF).
                    u32 enc;
                    if (logimm((u64)imm & 0x10, 1, &enc))
                        E(0xD2000000u | enc | dst << 5 | R_AF);
                    else
                        mov_reg(1, R_AF, dst);
                }
                if (rd == XZR && !sets)
                    return; // a dead compare
                if ((live & F_P) && rd == XZR) { // compare whose result feeds PF
                    add_imm(sf, sub, 1, S2, dst, (u32)imm);
                    rd = S2;
                } else
                    add_imm(sf, sub, sets, rd, dst, (u32)imm);
                if (live & F_P)
                    mov_reg(0, R_PF, rd);
                if (!sub && (live & F_C))
                    cfinv();
                return;
            }
        }
        if (use_imm && !arith && alu != ALU_ADC && alu != ALU_SBB) {
            u32 enc;
            if (logimm((u64)imm, sf, &enc)) {
                u32 opc = alu == ALU_OR ? 1 : alu == ALU_XOR ? 2 : sets ? 3 : 0;
                int d = rd == XZR ? (live & F_P ? S2 : XZR) : rd;
                E((u32)sf << 31 | opc << 29 | 0x12000000u | enc | dst << 5 | d);
                if (sets && (alu == ALU_OR || alu == ALU_XOR))
                    logic_reg(sf, 3, 0, XZR, d, d);
                if (live & F_A)
                    mov_reg(0, R_AF, d);
                if (live & F_P)
                    mov_reg(0, R_PF, d);
                if (live & F_C)
                    cfinv();
                return;
            }
        }
        if (use_imm)
            src = imm_to(in, S1);
        if (alu == ALU_XOR && dst == src && dst_is_reg && !mem && !(live & F_P) && !live) {
            mov_reg(sf, rd, XZR);
            return;
        }
        if (arith || alu == ALU_ADC || alu == ALU_SBB) {
            if (live & F_A)
                logic_reg(1, 2, 0, R_AF, dst, src);
            int d = rd == XZR ? (live & F_P ? S2 : XZR) : rd;
            if (alu == ALU_ADC) {
                cfinv();
                adc(sf, 0, sets, d, dst, src);
                if (live & F_C)
                    cfinv();
            } else if (alu == ALU_SBB)
                adc(sf, 1, sets, d, dst, src);
            else {
                add_reg(sf, alu != ALU_ADD, sets, d, dst, src, 0);
                if (alu == ALU_ADD && (live & F_C))
                    cfinv();
            }
            if (live & F_P)
                mov_reg(0, R_PF, d);
            return;
        }
        int d = rd == XZR ? (live & F_P ? S2 : XZR) : rd;
        if (alu == ALU_AND || alu == ALU_TEST)
            logic_reg(sf, sets ? 3 : 0, 0, d, dst, src);
        else {
            logic_reg(sf, alu == ALU_OR ? 1 : 2, 0, d, dst, src);
            if (sets)
                logic_reg(sf, 3, 0, XZR, d, d);
        }
        if (live & F_A)
            mov_reg(0, R_AF, d);
        if (live & F_P)
            mov_reg(0, R_PF, d);
        if (live & F_C)
            cfinv();
        return;
    }

    // General path: value, then store or register write, then flags.
    if (use_imm)
        src = imm_to(in, S1);
    bool need_r = wb || (live & F_P) || alu == ALU_OR || alu == ALU_XOR;
    if (need_r)
        alu_value(alu, sz, S2, dst, src);
    if (wb && !dst_is_reg)
        ldst(0, sz, S2, a);
    if (live) {
        // A register destination must keep its old value until flags are done: dst may be a pinned register.
        alu_flags(alu, sz, live, dst, src, S2);
    }
    if (wb && dst_is_reg)
        gpr_write(dst_gpr, sz, dst_high, S2);
}

static void emit_incdec(const Insn *in, int live) {
    int sz = in->sz, sf = sz == 8;
    bool dec = in->sub, mem = !in->rm_reg;
    if (in->lock && mem) {
        movimm(S1, dec ? ~0ULL : 1);
        int addr = addr_reg(in);
        lse(0, sz, S1, S0, addr);
        if (live & F_C)
            cset(S2, 2);
        if (live) {
            movimm(S1, 1);
            alu_value(dec ? ALU_SUB : ALU_ADD, sz, S3, S0, S1);
            mov_reg(1, AD, S2);
            alu_flags(dec ? ALU_SUB : ALU_ADD, sz, live & ~F_C, S0, S1, S3);
            if (live & F_C)
                rmif(AD, 63, 2);
        }
        return;
    }
    Addr a = {0};
    if (mem) {
        a = addr_prepare(in, sz);
        ldst(1, sz, S0, a);
    }
    int d = mem ? S0 : gpr_read(in->rm, sz, in->high_rm, S3);
    if (sz >= 4 && !mem) {
        if (live & F_C)
            cset(S2, 2);
        if (live & F_A) {
            logic_reg(1, 2, 0, R_AF, d, XZR);
            E(0xD2400000u | R_AF << 5 | R_AF); // eor x25, x25, #1
        }
        add_imm(sf, dec, live != 0, in->rm, d, 1);
        if (live & F_P)
            mov_reg(0, R_PF, in->rm);
        if (live & F_C)
            rmif(S2, 63, 2);
        return;
    }
    movimm(S1, 1);
    alu_value(dec ? ALU_SUB : ALU_ADD, sz, S2, d, S1);
    if (mem)
        ldst(0, sz, S2, a);
    if (live) {
        if (live & F_C)
            cset(S0 == d ? S4 : S0, 2);
        int saved = S0 == d ? S4 : S0;
        if (saved == S4) {
            // alu_flags clobbers S4 only for adc/sbb, which is not this path.
        }
        alu_flags(dec ? ALU_SUB : ALU_ADD, sz, live & ~F_C, d, S1, S2);
        if (live & F_C)
            rmif(saved, 63, 2);
    }
    if (!mem)
        gpr_write(in->rm, sz, in->high_rm, S2);
}

static void emit_neg_not(const Insn *in, int live) {
    int sz = in->sz, sf = sz == 8;
    bool mem = !in->rm_reg, neg = in->op == OP_NEG;
    if (in->lock && !mem) {
        emit_ud();
        return;
    }
    if (in->lock) {
        int addr = addr_reg(in);
        Addr a0 = {.base = addr};
        u32 *head = tr_cc;
        ldst(1, sz, S0, a0);
        if (neg)
            add_reg(1, 1, 0, S2, XZR, S0, 0);
        else
            logic_reg(1, 1, 1, S2, XZR, S0);
        mov_reg(1, S3, S0);
        casal(sz, S3, S2, addr);
        logic_reg(1, 2, 0, S3, S3, S0);
        cbz(1, 1, S3, (int)(head - tr_cc));
        if (neg)
            alu_flags(ALU_SUB, sz, live, XZR, S0, S2);
        return;
    }
    Addr a = {0};
    if (mem) {
        a = addr_prepare(in, sz);
        ldst(1, sz, S0, a);
    }
    int d = mem ? S0 : gpr_read(in->rm, sz, in->high_rm, S3);
    if (!mem && sz >= 4) {
        if (!neg) {
            logic_reg(sf, 1, 1, in->rm, XZR, d);
            return;
        }
        if (live & F_A)
            mov_reg(1, R_AF, d);
        add_reg(sf, 1, live != 0, in->rm, XZR, d, 0);
        if (live & F_P)
            mov_reg(0, R_PF, in->rm);
        return;
    }
    if (neg)
        add_reg(1, 1, 0, S2, XZR, d, 0);
    else
        logic_reg(1, 1, 1, S2, XZR, d);
    if (mem)
        ldst(0, sz, S2, a);
    if (neg)
        alu_flags(ALU_SUB, sz, live, XZR, d, S2);
    if (!mem)
        gpr_write(in->rm, sz, in->high_rm, S2);
}

// ---- shifts ----
static void apply_helper_flags(void) {
    msr_nzcv(S2);
    mov_reg(1, R_PF, S3);
    mov_reg(1, R_AF, S4);
}

static u64 shift_helper_descriptor(const Insn *in, int kind) {
    u64 descriptor = (u64)kind | (u64)in->sz << 8;
    if (tr_profile_shift_helpers) {
        descriptor |= SHIFT_HELPER_PROFILE_MARK;
        if (!in->rm_reg)
            descriptor |= SHIFT_HELPER_PROFILE_MEM;
        if (in->by_cl)
            descriptor |= SHIFT_HELPER_PROFILE_CL;
    }
    return descriptor;
}

static void emit_shift(const Insn *in, int live) {
    int sz = in->sz, sf = sz == 8, bits = sz * 8, kind = in->sub == SH_SAL ? SH_SHL : in->sub;
    bool mem = !in->rm_reg, rot = kind < SH_SHL, thru = kind == SH_RCL || kind == SH_RCR;
    Addr a = {0};
    if (in->has_imm && !in->imm && !(thru && false)) {
        // Masked count of zero: nothing happens except the implicit zero-extension of a 32-bit register.
        if (!mem && sz == 4)
            mov_reg(0, in->rm, in->rm);
        return;
    }
    if (mem) {
        a = addr_prepare(in, sz);
        ldst(1, sz, S0, a);
    }
    int d = mem ? S0 : gpr_read(in->rm, sz, in->high_rm, S3);
    if (sz < 4 && !thru && !live && !(in->by_cl && rot)) {
        // Narrow with no flags wanted: the value extended to 32 bits, a 32-bit shift, whose count is masked to 5 bits as x86's is, then the low part.
        if (kind == SH_SAR)
            sbfx(0, S0, d, 0, bits);
        else
            ubfx(0, S0, d, 0, bits);
        if (in->by_cl) {
            var_shift(0, kind == SH_SHL ? 0 : kind == SH_SHR ? 1 : 2, S0, S0, 1);
        } else {
            int count = (int)in->imm & 31;
            if (rot) {
                int k = count % bits;
                k = kind == SH_ROL ? k : (bits - k) % bits;
                if (k) { // as a left rotate
                    lsl_imm(0, S1, S0, k);
                    logic_sh(0, 1, 0, S0, S1, S0, 1, bits - k);
                }
            } else if (count) {
                if (kind == SH_SHL)
                    lsl_imm(0, S0, S0, count);
                else if (kind == SH_SHR)
                    lsr_imm(0, S0, S0, count);
                else
                    asr_imm(0, S0, S0, count);
            }
        }
        if (mem)
            ldst(0, sz, S0, a);
        else
            gpr_write(in->rm, sz, in->high_rm, S0);
        return;
    }
    if (!mem && sz == 2 && kind == SH_SAR && in->has_imm) {
        // Keep the narrow helper's complete flag model live without an SVC. The signed 32-bit result has
        // the same N/Z as its low word; BFI below preserves the destination's other 48 bits.
        int n = (int)in->imm & 31; // zero was handled above; SAR saturates at the operand's sign bit
        sbfx(0, S0, d, 0, 16);
        asr_imm(0, S0, S0, n);
        logic_reg(0, 3, 0, XZR, S0, S0); // N/Z, V=0 for every nonzero count, as h_shift
        if (live & F_C) {
            int bit = n < 16 ? n - 1 : 15;
            rmif(d, (bit - 1) & 63, 2);
        }
        cfinv(); // canonical C is inverse x86 CF
        if (live & F_P)
            mov_reg(0, R_PF, S0);
        if (live & F_A)
            mov_reg(0, R_AF, S0); // h_shift reports undefined AF as zero: AF = R_AF ^ R_PF
        gpr_write(in->rm, sz, in->high_rm, S0);
        return;
    }
    if (!mem && sz == 2 && kind == SH_SHR && in->has_imm) {
        // Common in period 32-bit graphics libraries. Preserve the helper's
        // complete flag model, including deterministic undefined OF/AF,
        // without an EL0->monitor service for an ordinary register shift.
        // Masked-zero counts returned above; no guest memory can fault here.
        int n = (int)in->imm & 31;
        ubfx(0, S1, d, 0, 16);
        if (n < 16)
            lsr_imm(0, S0, S1, n);
        else
            movimm(S0, 0);
        logic_reg(0, 3, 0, XZR, S0, S0);
        if ((live & F_C) && n <= 16)
            rmif(S1, (n - 2) & 63, 2);
        cfinv();
        if (live & F_V)
            rmif(S1, 15, 1);
        if (live & (F_P | F_A))
            mov_reg(0, R_PF, S0);
        if (live & F_A)
            mov_reg(1, R_AF, R_PF);
        gpr_write(in->rm, sz, in->high_rm, S0);
        return;
    }
    if (tr_inline_shr8 && !mem && sz == 1 && kind == SH_SHR && in->has_imm) {
        // Match h_shift, including its deterministic undefined OF/AF model.
        // Keep the original byte for CF/OF; high-byte aliases were extracted above.
        int n = (int)in->imm & 31; // the masked-zero path returned before any write
        ubfx(0, S1, d, 0, 8);
        if (n < 8)
            lsr_imm(0, S0, S1, n);
        else
            movimm(S0, 0);
        logic_reg(0, 3, 0, XZR, S0, S0); // N/Z, C=0, V=0
        if ((live & F_C) && n <= 8)
            rmif(S1, (n - 2) & 63, 2);
        cfinv(); // CF is zero for counts above 8; canonical C is inverted
        if (live & F_V)
            rmif(S1, 7, 1); // h_shift keeps the original bit 7 for every nonzero count
        if (live & (F_P | F_A))
            mov_reg(0, R_PF, S0);
        if (live & F_A)
            mov_reg(1, R_AF, R_PF); // AF = bit4(R_AF ^ R_PF) = 0, even for AF-only liveness
        gpr_write(in->rm, sz, in->high_rm, S0);
        return;
    }
    if (sz < 4 || thru || (in->by_cl && rot && live)) {
        // Helper: exact semantics for the rare forms. Flags come back as data so a store can still fault first.
        if (d != S0)
            mov_reg(1, S0, d);
        if (in->by_cl)
            mov_reg(1, S1, 1);
        else
            movimm(S1, (u64)in->imm);
        movimm(S2, shift_helper_descriptor(in, kind));
        svc(SVC_H_SHIFT);
        if (mem)
            ldst(0, sz, S0, a);
        apply_helper_flags();
        if (!mem)
            gpr_write(in->rm, sz, in->high_rm, S0);
        return;
    }
    if (mem || live) {
        if (d != S0)
            mov_reg(1, S0, d);
        d = S0;
    }
    int rd = mem ? S2 : in->rm;
    if (in->by_cl) {
        if (rot) { // flags are dead here
            if (kind == SH_ROL) {
                add_reg(1, 1, 0, S1, XZR, 1, 0);
                var_shift(sf, 3, rd, d, S1);
            } else
                var_shift(sf, 3, rd, d, 1);
            if (mem)
                ldst(0, sz, rd, a);
            return;
        }
        int op2 = kind == SH_SHL ? 0 : kind == SH_SHR ? 1 : 2;
        // RCX/ECX can be both the value and CL. Save the effective flag count before writing it.
        // Do not move this ahead of memory shifts: their prepared address may still occupy S1.
        bool count_in_destination = !mem && in->rm == 1 && live;
        if (count_in_destination)
            and_imm_w(S1, 1, bits - 1);
        var_shift(sf, op2, rd, d, 1);
        if (mem)
            ldst(0, sz, rd, a);
        if (live) {
            if (!count_in_destination)
                and_imm_w(S1, 1, bits - 1);
            u32 *skip = tr_cc;
            cbz(0, 0, S1, 0);
            if (kind == SH_SHL) {
                add_reg(0, 1, 0, S3, XZR, S1, 0); // -count, mod width: bits - count
                var_shift(sf, 1, S3, d, S3);
            } else {
                add_imm(0, 1, 0, S3, S1, 1);
                var_shift(sf, 1, S3, d, S3);
            }
            logic_reg(sf, 3, 0, XZR, rd, rd);
            rmif(S3, 63, 2);
            cfinv();
            if (live & F_V) { // only count 1 defines OF; the other counts may take the same value
                if (kind == SH_SHL) {
                    logic_sh(1, 2, 0, S3, S3, rd, 1, bits - 1);
                    rmif(S3, 0, 1);
                } else if (kind == SH_SHR)
                    rmif(d, bits - 1, 1);
            }
            if (live & F_P)
                mov_reg(0, R_PF, rd);
            *skip |= ((u32)(tr_cc - skip) & 0x7ffff) << 5;
        }
        return;
    }
    int n = (int)in->imm;
    if (rot) {
        int amount = kind == SH_ROL ? bits - n : n;
        extr(sf, rd, d, d, amount);
        if (mem)
            ldst(0, sz, rd, a);
        if (live & F_C) {
            rmif(rd, kind == SH_ROL ? 63 : (bits - 2) & 63, 2);
            cfinv();
        }
        if ((live & F_V) && n == 1) {
            if (kind == SH_ROL) {
                logic_sh(1, 2, 0, S3, rd, rd, 1, bits - 1); // r ^ (r >> msb): bit 0 is msb ^ bit 0
                rmif(S3, 0, 1);
            } else {
                logic_sh(1, 2, 0, S3, rd, rd, 1, 1);
                rmif(S3, bits - 2, 1);
            }
        }
        return;
    }
    if (kind == SH_SHL)
        lsl_imm(sf, rd, d, n);
    else
        bitfield(sf, kind == SH_SHR ? 2 : 0, rd, d, n, bits - 1);
    if (mem)
        ldst(0, sz, rd, a);
    if (live) {
        logic_reg(sf, 3, 0, XZR, rd, rd);
        if (live & F_C) {
            int bit = kind == SH_SHL ? bits - n : n - 1;
            rmif(d, (bit - 1) & 63, 2);
        }
        cfinv();
        if ((live & F_V) && n == 1) {
            if (kind == SH_SHL) {
                logic_sh(1, 2, 0, S3, d, d, 0, 1);
                rmif(S3, bits - 1, 1);
            } else if (kind == SH_SHR)
                rmif(d, bits - 1, 1);
        }
        if (live & F_P)
            mov_reg(0, R_PF, rd);
    }
}

static void emit_shd(const Insn *in, int live) {
    int sz = in->sz, sf = sz == 8, bits = sz * 8;
    bool mem = !in->rm_reg, right = in->sub;
    Addr a = {0};
    if (in->has_imm && !in->imm) {
        if (!mem && sz == 4)
            mov_reg(0, in->rm, in->rm);
        return;
    }
    if (mem) {
        a = addr_prepare(in, sz);
        ldst(1, sz, S0, a);
    }
    int d = mem ? S0 : in->rm;
    if (sz >= 4 && in->has_imm) {
        int n = (int)in->imm;
        if (d != S0) {
            mov_reg(1, S0, d);
            d = S0;
        }
        int rd = mem ? S2 : in->rm;
        if (right)
            extr(sf, rd, in->reg, d, n);
        else
            extr(sf, rd, d, in->reg, bits - n);
        if (mem)
            ldst(0, sz, rd, a);
        if (live) {
            logic_reg(sf, 3, 0, XZR, rd, rd);
            if (live & F_C) {
                int bit = right ? n - 1 : bits - n;
                rmif(d, (bit - 1) & 63, 2);
            }
            cfinv();
            if ((live & F_V) && n == 1) {
                logic_reg(1, 2, 0, S3, d, rd);
                rmif(S3, bits - 1, 1);
            }
            if (live & F_P)
                mov_reg(0, R_PF, rd);
        }
        return;
    }
    if (sz == 4 && in->by_cl && !mem && !live) {
        // 32-bit code's 64-bit shifts, in every libgcc division: with flags dead, a wide shift of the pair. The helper is a trap each.
        and_imm_w(S1, 1, 31);
        mov_reg(0, S0, right ? d : in->reg);
        logic_sh(1, 1, 0, S0, S0, right ? in->reg : d, 0, 32);
        var_shift(1, right ? 1 : 0, S0, S0, S1);
        if (!right)
            lsr_imm(1, S0, S0, 32);
        mov_reg(0, in->rm, S0);
        return;
    }
    if (d != S0)
        mov_reg(1, S0, d);
    mov_reg(1, S1, in->reg);
    if (in->by_cl)
        mov_reg(1, S2, 1);
    else
        movimm(S2, (u64)in->imm);
    movimm(S3, (u64)sz | (u64)right << 8);
    svc(SVC_H_SHD);
    if (mem)
        ldst(0, sz, S0, a);
    apply_helper_flags();
    if (!mem)
        gpr_write(in->rm, sz, false, S0);
}

// ---- multiply and divide ----
// Flags after multiply: CF=OF=(hi != cmp). The other arithmetic flags are undefined and left.
static void mul_flags(int hi, int cmp_val, int live) {
    if (!(live & (F_C | F_V)))
        return;
    add_reg(1, 1, 1, XZR, hi, cmp_val, 0);
    cset(S4, 0);
    add_imm(1, 0, 0, S4, S4, 1);
    rmif(S4, 0, 3);
}

static void emit_mul1(const Insn *in, int live) {
    int sz = in->sz;
    bool sign = in->sub;
    if (!in->rm_reg)
        ldst(1, sz, S0, addr_prepare(in, sz));
    int s = in->rm_reg ? gpr_read(in->rm, sz, in->high_rm, S0) : S0;
    if (sz == 8) {
        if (sign)
            smulh(S2, 0, s);
        else
            umulh(S2, 0, s);
        mul(1, S3, 0, s);
        if (sign) {
            asr_imm(1, S1, S3, 63);
            mul_flags(S2, S1, live);
        } else
            mul_flags(S2, XZR, live);
        mov_reg(1, 0, S3);
        mov_reg(1, 2, S2);
        return;
    }
    if (sign) {
        sext64(S1, 0, sz);
        sext64(S3, s, sz);
    } else {
        uext(S1, 0, sz);
        uext(S3, s, sz);
    }
    mul(1, S2, S1, S3);
    if (sign)
        sext64(S1, S2, sz);
    else
        uext(S1, S2, sz);
    mul_flags(S2, S1, live);
    if (sz == 4) {
        mov_reg(0, 0, S2);
        lsr_imm(1, S2, S2, 32);
        mov_reg(0, 2, S2);
    } else if (sz == 2) {
        bfi(1, 0, S2, 0, 16);
        lsr_imm(0, S2, S2, 16);
        bfi(1, 2, S2, 0, 16);
    } else
        bfi(1, 0, S2, 0, 16);
}

static void emit_imul(const Insn *in, int live) {
    int sz = in->sz;
    if (!in->rm_reg)
        ldst(1, sz, S0, addr_prepare(in, sz));
    int s = in->rm_reg ? in->rm : S0;
    int a = in->has_imm ? S1 : in->reg;
    if (in->has_imm)
        movimm(S1, (u64)in->imm);
    if (sz == 8) {
        if (live & (F_C | F_V)) {
            smulh(S3, a, s);
            mul(1, S2, a, s);
            asr_imm(1, S4, S2, 63);
            add_reg(1, 1, 1, XZR, S3, S4, 0);
            cset(S4, 0);
            add_imm(1, 0, 0, S4, S4, 1);
            rmif(S4, 0, 3);
            mov_reg(1, in->reg, S2);
        } else
            mul(1, in->reg, a, s);
        return;
    }
    if (sz == 4 && !(live & (F_C | F_V))) {
        mul(0, in->reg, a, s);
        return;
    }
    sext64(S3, a, sz);
    sext64(S2, s, sz);
    mul(1, S2, S3, S2);
    sext64(S3, S2, sz);
    mul_flags(S2, S3, live);
    gpr_write(in->reg, sz, false, S2);
}

static void emit_div(const Insn *in) {
    int sz = in->sz;
    bool sign = in->sub;
    if (!in->rm_reg)
        ldst(1, sz, S0, addr_prepare(in, sz));
    int s = in->rm_reg ? gpr_read(in->rm, sz, in->high_rm, S0) : S0;
    if (sz == 4) {
        // edx:eax in 64 bits fits the hardware divider; a quotient that does not fit is #DE.
        if (sign)
            sext64(S1, s, 4);
        else
            uext(S1, s, 4);
        cbz(1, 1, S1, 2);
        svc(SVC_DE);
        mov_reg(0, S3, 0);
        logic_sh(1, 1, 0, S3, S3, 2, 0, 32);
        if (sign)
            sdiv(1, S2, S3, S1);
        else
            udiv(1, S2, S3, S1);
        if (sign)
            sext64(S4, S2, 4);
        else
            uext(S4, S2, 4);
        logic_reg(1, 2, 0, S4, S4, S2);
        cbz(0, 1, S4, 2);
        svc(SVC_DE);
        msub(1, S3, S2, S1, S3);
        mov_reg(0, 0, S2);
        mov_reg(0, 2, S3);
        return;
    }
    if (sz == 8) {
        cbz(1, 1, s, 2);
        svc(SVC_DE);
        u32 *to_slow;
        if (sign) {
            logic_sh(1, 2, 0, S1, 2, 0, 2, 63); // rdx ^ (rax >> 63), arithmetic
            to_slow = tr_cc;
            cbz(1, 1, S1, 0);
            add_imm(1, 0, 0, S1, s, 1);
            u32 *to_slow2 = tr_cc;
            cbz(0, 1, S1, 0);
            sdiv(1, S2, 0, s);
            msub(1, S3, S2, s, 0);
            mov_reg(1, 0, S2);
            mov_reg(1, 2, S3);
            u32 *done = tr_cc;
            b_rel(0);
            *to_slow |= ((u32)(tr_cc - to_slow) & 0x7ffff) << 5;
            *to_slow2 |= ((u32)(tr_cc - to_slow2) & 0x7ffff) << 5;
            mov_reg(1, S2, s);
            mov_reg(1, S0, 0);
            mov_reg(1, S1, 2);
            movimm(S3, 8 | 1 << 8);
            svc(SVC_H_DIV);
            mov_reg(1, 0, S0);
            mov_reg(1, 2, S1);
            *done |= (u32)(tr_cc - done) & 0x3ffffff;
        } else {
            to_slow = tr_cc;
            cbz(1, 1, 2, 0);
            udiv(1, S2, 0, s);
            msub(1, S3, S2, s, 0);
            mov_reg(1, 0, S2);
            mov_reg(1, 2, S3);
            u32 *done = tr_cc;
            b_rel(0);
            *to_slow |= ((u32)(tr_cc - to_slow) & 0x7ffff) << 5;
            mov_reg(1, S2, s);
            mov_reg(1, S0, 0);
            mov_reg(1, S1, 2);
            movimm(S3, 8);
            svc(SVC_H_DIV);
            mov_reg(1, 0, S0);
            mov_reg(1, 2, S1);
            *done |= (u32)(tr_cc - done) & 0x3ffffff;
        }
        return;
    }
    // 8 and 16 bits: helper.
    mov_reg(1, S2, s);
    uext(S0, 0, sz == 1 ? 2 : 2);
    if (sz == 2)
        uext(S1, 2, 2);
    else
        mov_reg(1, S1, XZR);
    movimm(S3, (u64)sz | (u64)sign << 8);
    svc(SVC_H_DIV);
    if (sz == 2) {
        bfi(1, 0, S0, 0, 16);
        bfi(1, 2, S1, 0, 16);
    } else {
        bfi(1, 0, S0, 0, 8);
        bfi(1, 0, S1, 8, 8);
    }
}

// ---- bit operations ----
static void emit_bt(const Insn *in, int live) {
    int sz = in->sz, bits = sz * 8, sub = in->sub;
    bool mem = !in->rm_reg;
    if (in->lock && (!mem || sub == 0)) {
        emit_ud();
        return;
    }
    int cnt = S1;
    Addr a = {0};
    int addr = -1;
    if (in->has_imm)
        movimm(S1, (u64)in->imm & (bits - 1));
    else if (!mem)
        and_imm_w(S1, in->reg, bits - 1);
    else {
        // The bit offset is signed and can reach outside the addressed operand.
        addr = addr_reg(in);
        sext64(S3, in->reg, sz);
        asr_imm(1, S3, S3, bits == 64 ? 6 : bits == 32 ? 5 : 4);
        add_reg(!tr_compat, 0, 0, AD, addr, S3, log2sz(sz));
        addr = AD;
        and_imm_w(S1, in->reg, bits - 1);
    }
    if (mem && addr < 0) {
        if (in->lock || sub) {
            addr = addr_reg(in);
        }
    }
    if (in->lock) {
        movimm(S4, 1);
        var_shift(1, 0, S4, S4, cnt);
        lse(sub == 1 ? 3 : sub == 2 ? 1 : 2, sz, S4, S0, addr);
        var_shift(1, 1, S3, S0, cnt);
        if (live & F_C) {
            rmif(S3, 63, 2);
            cfinv();
        }
        return;
    }
    if (mem) {
        if (addr >= 0) {
            a.base = addr;
            a.imm = 0;
            a.reg_form = false;
        } else
            a = addr_prepare(in, sz);
        ldst(1, sz, S0, a);
    }
    int v = mem ? S0 : in->rm;
    var_shift(1, 1, S3, v, cnt);
    if (sub) {
        movimm(S4, 1);
        var_shift(1, 0, S4, S4, cnt);
        logic_reg(1, sub == 1 ? 1 : sub == 2 ? 0 : 2, sub == 2, S2, v, S4);
        if (mem)
            ldst(0, sz, S2, a);
        else
            gpr_write(in->rm, sz, false, S2);
    } else if (!mem && sz == 4) {
        // bt does not write its register operand.
    }
    if (live & F_C) {
        rmif(S3, 63, 2);
        cfinv();
    }
}

static void emit_bitscan(const Insn *in, int live) {
    int sz = in->sz, sf = sz == 8;
    if (!in->rm_reg)
        ldst(1, sz, S0, addr_prepare(in, sz));
    int s0 = in->rm_reg ? in->rm : S0;
    uext(S1, s0, sz);
    if (in->op == OP_BSF || in->op == OP_BSR) {
        logic_reg(1, 3, 0, XZR, S1, S1); // Z set for a zero source; the other flags are undefined
        if (in->op == OP_BSF) {
            rbit(1, S2, S1);
            clz(1, S2, S2);
        } else {
            clz(1, S2, S1);
            logic_reg(1, 1, 1, S2, XZR, S2); // ~clz
            add_imm(1, 0, 0, S2, S2, 64);
        }
        if (sz == 2) {
            mov_reg(0, S3, in->reg);
            csel(0, S3, S3, S2, 0);
            bfi(1, in->reg, S3, 0, 16);
        } else if (sz == 4) {
            // A zero source leaves the register unchanged, including its top half.
            csel(1, in->reg, in->reg, S2, 0);
        } else
            csel(sf, in->reg, in->reg, S2, 0);
        if (live & F_P)
            mov_reg(0, R_PF, S1);
        return;
    }
    if (tr_hybrid) { // CR0.TS may be set, and a kernel's own popcnt must not take #NM
        emit_stop(in->rip, "popcnt: the emitter needs NEON, unavailable under CR0.TS");
        return;
    }
    // popcnt: ZF for a zero source, every other arithmetic flag clear.
    // fmov d24, x17; cnt v24.8b, v24.8b; addv b24, v24.8b; fmov w22, s24
    E(0x9E670000u | S1 << 5 | V_S0);
    E(0x0E205800u | V_S0 << 5 | V_S0);
    E(0x0E31B800u | V_S0 << 5 | V_S0);
    E(0x1E260000u | V_S0 << 5 | S2);
    gpr_write(in->reg, sz, false, S2);
    if (live) {
        logic_reg(1, 3, 0, XZR, S1, S1); // N from the source's sign bit is wrong for popcnt: clear N below
        rmif(XZR, 0, 8);
        cfinv();
        if (live & F_P) {
            movimm(R_PF, 1);
        }
        if (live & F_A)
            mov_reg(1, R_AF, R_PF);
    }
}

// ---- strings ----
static void emit_string(const Insn *in) {
    int sz = in->sz, sub = in->sub, ls = log2sz(sz);
    if ((in->addr32 && !tr_compat) || in->addr16 || in->seg) {
        emit_stop(in->rip, "string instruction with an address-size or segment override");
        return;
    }
    int sf = !tr_compat;
    u32 *start = tr_cc;
    u32 *skip = 0;
    if (in->rep && !tr_no_bulk_rep && (sub == 0 || sub == 2)) {
        // Bulk first: a stub moves whole 32-byte chunks with exact registers between them. The plain loop below does what is left.
        u32 *wraps[2] = {0, 0};
        if (tr_compat) { // the stub's sums are 64 bits wide: a run that would pass 4 GB takes the plain loop, which wraps
            add_reg(1, 0, 0, S0, 7, 1, ls);
            lsr_imm(1, S0, S0, 32);
            wraps[0] = tr_cc;
            cbz(1, 1, S0, 0);
            if (sub == 0) {
                add_reg(1, 0, 0, S0, 6, 1, ls);
                lsr_imm(1, S0, S0, 32);
                wraps[1] = tr_cc;
                cbz(1, 1, S0, 0);
            }
        }
        movimm(S1, (u64)(32 / sz));
        if (sub == 2) { // the pattern, in an integer register: under CR0.TS, as in a kernel's bzero, SIMD would trap
            if (sz == 8)
                mov_reg(1, S2, 0);
            else
                ubfx(1, S2, 0, 0, sz * 8);
            for (int width = sz * 8; width < 64; width *= 2)
                logic_sh(1, 1, 0, S2, S2, S2, 0, width); // orr x22, x22, x22, lsl #width
        }
        u64 stub = tr_page_rep ? (sub == 0 ? STUB_OFF_REP_PAGE_MOVS : STUB_OFF_REP_PAGE_STOS)
                               : (sub == 0 ? STUB_OFF_REP_MOVS : STUB_OFF_REP_STOS);
        bl_abs(STUB(PRIV_VA + OFF_STUBS + stub));
        for (int i = 0; i < 2; i++)
            if (wraps[i])
                *wraps[i] |= ((u32)(tr_cc - wraps[i]) & 0x7ffff) << 5;
    }
    if (in->rep) {
        skip = tr_cc;
        cbz(0, 1, 1, 0);
    }
    E(0xF9400000u | (u32)(offsetof(EngineCtx, df) / 8) << 10 | CTX << 5 | S4);
    if (ls)
        lsl_imm(1, S4, S4, ls);
    Addr rsi = {.base = 6}, rdi = {.base = 7};
    switch (sub) {
        case 0: // movs
            ldst(1, sz, S2, rsi);
            ldst(0, sz, S2, rdi);
            add_reg(sf, 0, 0, 6, 6, S4, 0);
            add_reg(sf, 0, 0, 7, 7, S4, 0);
            break;
        case 2: // stos
            ldst(0, sz, 0, rdi);
            add_reg(sf, 0, 0, 7, 7, S4, 0);
            break;
        case 3: // lods
            ldst(1, sz, S2, rsi);
            gpr_write(0, sz, false, S2);
            add_reg(sf, 0, 0, 6, 6, S4, 0);
            break;
        case 4: // scas: rax minus [rdi]
            ldst(1, sz, S0, rdi);
            add_reg(sf, 0, 0, 7, 7, S4, 0);
            alu_value(ALU_SUB, sz, S2, 0, S0);
            alu_flags(ALU_SUB, sz, F_ARITH, 0, S0, S2);
            break;
        case 1: // cmps: [rsi] minus [rdi]
            ldst(1, sz, S0, rsi);
            ldst(1, sz, S1, rdi);
            add_reg(sf, 0, 0, 6, 6, S4, 0);
            add_reg(sf, 0, 0, 7, 7, S4, 0);
            alu_value(ALU_SUB, sz, S2, S0, S1);
            alu_flags(ALU_SUB, sz, F_ARITH, S0, S1, S2);
            break;
    }
    if (in->rep) {
        add_imm(1, 1, 0, 1, 1, 1);
        if (sub == 1 || sub == 4)
            b_cond(in->rep == 0xF3 ? 0 : 1, (int)(start - tr_cc));
        else
            b_rel((int)(start - tr_cc));
        *skip |= ((u32)(tr_cc - skip) & 0x7ffff) << 5;
    }
}

// ---- everything else ----
static void emit_mov(const Insn *in) {
    int sz = in->sz;
    if (in->has_imm) {
        if (in->rm_reg || in->to_reg) {
            int rd = in->to_reg ? in->reg : in->rm;
            bool hi = in->to_reg ? in->high_reg : in->high_rm;
            if (sz == 8)
                movimm(rd, (u64)in->imm);
            else if (sz == 4)
                movimm(rd, (u64)in->imm & 0xffffffffULL);
            else {
                movimm(S0, (u64)in->imm & (sz == 2 ? 0xffff : 0xff));
                gpr_write(rd, sz, hi, S0);
            }
        } else {
            Addr a = addr_prepare(in, sz);
            int v = XZR;
            u64 m = sz == 8 ? (u64)in->imm : (u64)in->imm & ((1ULL << (sz * 8)) - 1);
            if (m) {
                movimm(S0, m);
                v = S0;
            }
            ldst(0, sz, v, a);
        }
    } else if (in->rm_reg) {
        int src = gpr_read(in->to_reg ? in->rm : in->reg, sz, in->to_reg ? in->high_rm : in->high_reg, S0);
        gpr_write(in->to_reg ? in->reg : in->rm, sz, in->to_reg ? in->high_reg : in->high_rm, src);
    } else {
        Addr a = addr_prepare(in, sz);
        if (!in->to_reg) {
            int src = gpr_read(in->reg, sz, in->high_reg, S0);
            ldst(0, sz, src, a);
        } else if (sz >= 4)
            ldst(1, sz, in->reg, a);
        else {
            ldst(1, sz, S0, a);
            gpr_write(in->reg, sz, in->high_reg, S0);
        }
    }
}

static void emit_lea(const Insn *in) {
    Insn t = *in;
    t.seg = 0; // lea ignores segment bases
    int r = addr_reg(&t);
    gpr_write(in->reg, in->sz, false, r == XZR ? XZR : r);
}

static void emit_movx(const Insn *in) {
    bool sx = in->op == OP_MOVSX;
    int rd = in->reg, sz = in->sz, ss = in->srcsz;
    if (in->rm_reg) {
        int src = gpr_read(in->rm, ss, in->high_rm, S0);
        if (sz >= 4) {
            if (!sx && ss == 4)
                mov_reg(0, rd, src);
            else if (sx)
                bitfield(sz == 8, 0, rd, src, 0, ss * 8 - 1);
            else
                bitfield(0, 2, rd, src, 0, ss * 8 - 1);
        } else {
            if (sx)
                sext64(S0, src, ss);
            else
                uext(S0, src, ss);
            gpr_write(rd, sz, false, S0);
        }
        return;
    }
    Addr a = addr_prepare(in, ss);
    if (sz >= 4) {
        ldst(sx ? (sz == 8 ? 2 : (ss == 4 ? 1 : 3)) : 1, ss, rd, a);
    } else {
        ldst(sx ? 2 : 1, ss, S0, a);
        gpr_write(rd, sz, false, S0);
    }
}

static void emit_xchg(const Insn *in) {
    int sz = in->sz;
    if (in->rm_reg) {
        int a = gpr_read(in->rm, sz, in->high_rm, S0), b = gpr_read(in->reg, sz, in->high_reg, S1);
        mov_reg(1, S2, a);
        mov_reg(1, S3, b);
        gpr_write(in->rm, sz, in->high_rm, S3);
        gpr_write(in->reg, sz, in->high_reg, S2);
        return;
    }
    int addr = addr_reg(in);
    int src = gpr_read(in->reg, sz, in->high_reg, S1);
    lse(8, sz, src, S0, addr);
    gpr_write(in->reg, sz, in->high_reg, S0);
}

static void emit_xadd(const Insn *in, int live) {
    int sz = in->sz;
    int src = gpr_read(in->reg, sz, in->high_reg, S1);
    if (in->rm_reg) {
        int d = gpr_read(in->rm, sz, in->high_rm, S3);
        mov_reg(1, S0, d);
        add_reg(1, 0, 0, S2, S0, src, 0);
        alu_flags(ALU_ADD, sz, live, S0, src, S2);
        gpr_write(in->reg, sz, in->high_reg, S0); // destination last: xadd of a register with itself leaves the sum
        gpr_write(in->rm, sz, in->high_rm, S2);
        return;
    }
    if (in->lock) {
        int addr = addr_reg(in);
        lse(0, sz, src, S0, addr);
        add_reg(1, 0, 0, S2, S0, src, 0);
    } else {
        Addr a = addr_prepare(in, sz);
        ldst(1, sz, S0, a);
        add_reg(1, 0, 0, S2, S0, src, 0);
        ldst(0, sz, S2, a);
    }
    alu_flags(ALU_ADD, sz, live, S0, src, S2);
    gpr_write(in->reg, sz, in->high_reg, S0);
}

static void emit_cmpxchg(const Insn *in, int live) {
    int sz = in->sz;
    int src = gpr_read(in->reg, sz, in->high_reg, S1);
    uext(S0, 0, sz);
    uext(S3, 0, sz); // S3: accumulator, kept for the flags
    if (in->rm_reg) {
        int d = gpr_read(in->rm, sz, in->high_rm, S2);
        mov_reg(1, S0, d); // S0: old destination
        uext(S0, S0, sz);
        logic_reg(1, 2, 0, S4, S0, S3);
        u32 *ne = tr_cc;
        cbz(1, 1, S4, 0);
        gpr_write(in->rm, sz, in->high_rm, src);
        u32 *done = tr_cc;
        b_rel(0);
        *ne |= ((u32)(tr_cc - ne) & 0x7ffff) << 5;
        if (sz == 4)
            mov_reg(0, in->rm, in->rm); // the destination is written back in both cases, which zero-extends it
        gpr_write(0, sz, false, S0);
        *done |= (u32)(tr_cc - done) & 0x3ffffff;
    } else {
        int addr = addr_reg(in);
        casal(sz, S0, src, addr);
        uext(S0, S0, sz);
        logic_reg(1, 2, 0, S4, S0, S3);
        u32 *eq = tr_cc;
        cbz(0, 1, S4, 0);
        gpr_write(0, sz, false, S0);
        *eq |= ((u32)(tr_cc - eq) & 0x7ffff) << 5;
    }
    add_reg(1, 1, 0, S2, S3, S0, 0);
    alu_flags(ALU_CMP, sz, live, S3, S0, S2);
}

static void emit_cmpxchg8b(const Insn *in, int live) {
    int addr = addr_reg(in);
    if (in->sz == 8) {
        if (addr == AD) {
            ubfx(1, S4, AD, 0, 4);
            cbz(0, 1, S4, 2);
            svc(SVC_GP0);
        } else {
            ubfx(1, S4, addr, 0, 4);
            cbz(0, 1, S4, 2);
            svc(SVC_GP0);
        }
        mov_reg(1, S0, 0);
        mov_reg(1, S1, 2);
        mov_reg(1, S2, 3);
        mov_reg(1, S3, 1);
        E(0x4860FC00u | S0 << 16 | addr << 5 | S2); // caspal x16, x17, x22, x23, [addr]
        logic_reg(1, 2, 0, S4, S0, 0);
        logic_reg(1, 2, 0, S5, S1, 2);
        logic_reg(1, 1, 0, S4, S4, S5);
        clz(1, S5, S4);
        lsr_imm(1, S5, S5, 6);
        if (live & F_Z)
            rmif(S5, 62, 4);
        u32 *eq = tr_cc;
        cbz(0, 1, S4, 0);
        mov_reg(1, 0, S0);
        mov_reg(1, 2, S1);
        *eq |= ((u32)(tr_cc - eq) & 0x7ffff) << 5;
        return;
    }
    mov_reg(0, S0, 0);
    logic_sh(1, 1, 0, S0, S0, 2, 0, 32);
    mov_reg(1, S1, S0);
    mov_reg(0, S2, 3);
    logic_sh(1, 1, 0, S2, S2, 1, 0, 32);
    casal(8, S0, S2, addr);
    logic_reg(1, 2, 0, S4, S0, S1);
    clz(1, S5, S4);
    lsr_imm(1, S5, S5, 6);
    if (live & F_Z)
        rmif(S5, 62, 4);
    u32 *eq = tr_cc;
    cbz(0, 1, S4, 0);
    mov_reg(0, 0, S0);
    lsr_imm(1, 2, S0, 32);
    *eq |= ((u32)(tr_cc - eq) & 0x7ffff) << 5;
}

static void emit_push(const Insn *in) {
    int sz = in->sz;
    int v;
    if (in->sub) { // a segment register, zero-extended as TCG writes it. Real parts may write only 16 bits.
        E(0x79400000u | (u32)((offsetof(EngineCtx, seg_sel) + (in->sub - 1) * 2) / 2) << 10 | CTX << 5 | S0);
        v = S0;
    } else if (in->has_imm) {
        movimm(S0, (u64)in->imm);
        v = S0;
    } else if (in->rm_reg) {
        v = in->rm;
        if (v == 4) {
            mov_reg(1, S0, 4);
            v = S0;
        }
    } else {
        ldst(1, sz, S0, addr_prepare(in, sz));
        v = S0;
    }
    if (sz == 8)
        E(0xF8000C00u | ((u32)-8 & 0x1ff) << 12 | 4 << 5 | v);
    else if (sz == 4)
        E(0xB8000C00u | ((u32)-4 & 0x1ff) << 12 | 4 << 5 | v);
    else
        E(0x78000C00u | ((u32)-2 & 0x1ff) << 12 | 4 << 5 | v);
}

static void emit_pusha(const Insn *in) {
    static const u8 order[8] = {7, 6, 5, 4, 3, 2, 1, 0}; // edi at the lowest address
    int sz = in->sz;
    if (!in->sub) { // through a scratch pointer: esp moves only after the last store
        add_imm(0, 1, 0, S0, 4, sz * 8);
        for (int i = 7; i >= 0; i--) {
            Addr a = {.base = S0, .imm = i * sz};
            ldst(0, sz, order[i], a);
        }
        mov_reg(0, 4, S0);
        return;
    }
    // Both ends first, which covers any page boundary in 32 bytes: no register changes until no load can fault.
    Addr top = {.base = 4, .imm = 7 * sz}, bottom = {.base = 4};
    ldst(1, sz, XZR, top);
    ldst(1, sz, XZR, bottom);
    for (int i = 0; i < 8; i++) {
        if (order[i] == 4)
            continue; // the saved esp is dropped
        Addr a = {.base = 4, .imm = i * sz};
        if (sz == 4)
            ldst(1, 4, order[i], a);
        else {
            ldst(1, 2, S0, a);
            bfi(1, order[i], S0, 0, 16);
        }
    }
    add_imm(0, 0, 0, 4, 4, sz * 8);
}

static void emit_pop(const Insn *in) {
    int sz = in->sz;
    if (in->rm_reg) {
        if (in->rm == 4) {
            Addr a = {.base = 4};
            if (sz >= 4)
                ldst(1, sz, 4, a);
            else {
                ldst(1, 2, S0, a);
                add_imm(1, 0, 0, 4, 4, 2);
                bfi(1, 4, S0, 0, 16);
            }
            return;
        }
        if (sz == 8)
            E(0xF8400400u | 8 << 12 | 4 << 5 | in->rm);
        else if (sz == 4)
            E(0xB8400400u | 4 << 12 | 4 << 5 | in->rm);
        else {
            Addr a = {.base = 4};
            ldst(1, 2, S0, a);
            bfi(1, in->rm, S0, 0, 16);
            add_imm(1, 0, 0, 4, 4, 2);
        }
        return;
    }
    // pop m: load, then the address is computed with rsp already incremented; rsp itself moves after the store.
    Addr s = {.base = 4};
    ldst(1, sz, S2, s);
    Insn t = *in;
    add_imm(1, 0, 0, S3, 4, sz);
    if (!t.no_base && t.base == 4)
        t.base = S3;
    if (!t.no_index && t.index == 4)
        t.index = S3;
    int addr = addr_reg(&t);
    Addr d = {.base = addr};
    ldst(0, sz, S2, d);
    add_imm(1, 0, 0, 4, 4, sz);
}

void emit_int(const Insn *in, int live) {
    if (live & F_A)
        live |= F_P; // AF is read as R_AF ^ R_PF, so R_PF must be current as well
    int sz = in->sz;
    switch (in->op) {
        case OP_NOP:
            break;
        case OP_ALU:
            emit_alu(in, live);
            break;
        case OP_INCDEC:
            emit_incdec(in, live);
            break;
        case OP_NEG:
        case OP_NOT:
            emit_neg_not(in, live);
            break;
        case OP_SHIFT:
            emit_shift(in, live);
            break;
        case OP_SHD:
            emit_shd(in, live);
            break;
        case OP_MOV:
            emit_mov(in);
            break;
        case OP_LEA:
            emit_lea(in);
            break;
        case OP_MOVZX:
        case OP_MOVSX:
            emit_movx(in);
            break;
        case OP_XCHG:
            emit_xchg(in);
            break;
        case OP_XADD:
            emit_xadd(in, live);
            break;
        case OP_CMPXCHG:
            emit_cmpxchg(in, live);
            break;
        case OP_CMPXCHG8B:
            emit_cmpxchg8b(in, live);
            break;
        case OP_PUSH:
            emit_push(in);
            break;
        case OP_POP:
            emit_pop(in);
            break;
        case OP_PUSHA:
            emit_pusha(in);
            break;
        case OP_PUSHF:
            bl_abs(STUB(tr_stub_pushf));
            if (sz == 8)
                E(0xF8000C00u | ((u32)-8 & 0x1ff) << 12 | 4 << 5 | S0);
            else if (sz == 4)
                E(0xB8000C00u | ((u32)-4 & 0x1ff) << 12 | 4 << 5 | S0);
            else
                E(0x78000C00u | ((u32)-2 & 0x1ff) << 12 | 4 << 5 | S0);
            break;
        case OP_POPF: {
            Addr a = {.base = 4};
            ldst(1, sz, S0, a);
            add_imm(1, 0, 0, S1, 4, sz);
            movimm(S2, (u64)sz);
            svc(SVC_POPF);
            break;
        }
        case OP_ENTER: {
            // All stores go below rsp through a scratch pointer; registers change only after the last one.
            int level = in->sub, size = (int)in->imm;
            Addr a = {.base = S0};
            add_imm(1, 1, 0, S0, 4, sz);
            ldst(0, sz, 5, a);
            mov_reg(1, S3, S0);
            for (int i = 1; i < level; i++) {
                Addr prev = {.base = 5, .imm = -(i64)i * sz};
                ldst(1, sz, S1, prev);
                add_imm(1, 1, 0, S0, S0, sz);
                ldst(0, sz, S1, a);
            }
            if (level) {
                add_imm(1, 1, 0, S0, S0, sz);
                ldst(0, sz, S3, a);
            }
            movimm(S2, (u64)size);
            add_reg(!tr_compat, 1, 0, 4, S0, S2, 0);
            gpr_write(5, sz, false, S3);
            break;
        }
        case OP_LEAVE: {
            if (sz == 2) {
                emit_stop(in->rip, "16-bit leave");
                break;
            }
            Addr a = {.base = 5};
            if (tr_compat) {
                ldst(1, 4, S0, a);
                add_imm(0, 0, 0, 4, 5, 4);
                mov_reg(0, 5, S0);
                break;
            }
            ldst(1, 8, S0, a);
            add_imm(1, 0, 0, 4, 5, 8);
            mov_reg(1, 5, S0);
            break;
        }
        case OP_MUL:
            emit_mul1(in, live);
            break;
        case OP_IMUL:
            emit_imul(in, live);
            break;
        case OP_DIV:
            emit_div(in);
            break;
        case OP_BT:
            emit_bt(in, live);
            break;
        case OP_BSF:
        case OP_BSR:
        case OP_POPCNT:
            emit_bitscan(in, live);
            break;
        case OP_TZCNT:
        case OP_LZCNT: {
            if (!tr_has_tzcnt) { // as on a CPU without BMI1 and ABM, these encodings execute as bsf and bsr
                Insn t = *in;
                t.op = in->op == OP_TZCNT ? OP_BSF : OP_BSR;
                emit_bitscan(&t, live);
                break;
            }
            if (!in->rm_reg)
                ldst(1, sz, S0, addr_prepare(in, sz));
            uext(S1, in->rm_reg ? in->rm : S0, sz);
            if (in->op == OP_TZCNT) {
                if (sz < 8) {
                    movimm(S2, 1ULL << (sz * 8));
                    logic_reg(1, 1, 0, S2, S1, S2);
                    rbit(1, S2, S2);
                } else
                    rbit(1, S2, S1);
                clz(1, S2, S2);
            } else {
                clz(1, S2, S1);
                if (sz < 8)
                    add_imm(1, 1, 0, S2, S2, 64 - sz * 8);
            }
            if (live & (F_C | F_Z)) {
                add_imm(1, 1, 1, XZR, S1, 1); // C holds exactly when the source is non-zero, and C is the inverse of CF
                uext(S3, S2, sz);
                clz(1, S3, S3);
                lsr_imm(1, S3, S3, 6);
                rmif(S3, 62, 4); // ZF: the result is zero
            }
            gpr_write(in->reg, sz, false, S2);
            break;
        }
        case OP_BSWAP:
            if (sz == 8)
                rev64(in->rm, in->rm);
            else if (sz == 4)
                rev32(0, in->rm, in->rm);
            else
                bfi(1, in->rm, XZR, 0, 16); // bswap r16 clears the low word on real parts; the manual says undefined
            break;
        case OP_CBW:
            if (sz == 8)
                sbfx(1, 0, 0, 0, 32);
            else if (sz == 4)
                sbfx(0, 0, 0, 0, 16);
            else {
                sbfx(0, S0, 0, 0, 8);
                bfi(1, 0, S0, 0, 16);
            }
            break;
        case OP_CWD:
            if (sz == 8)
                asr_imm(1, 2, 0, 63);
            else if (sz == 4) {
                sbfx(0, S0, 0, 0, 32);
                asr_imm(0, 2, S0, 31);
            } else {
                sbfx(0, S0, 0, 0, 16);
                asr_imm(0, S0, S0, 15);
                bfi(1, 2, S0, 0, 16);
            }
            break;
        case OP_LAHF:
            bl_abs(STUB(tr_stub_pushf));
            bfi(1, 0, S0, 8, 8);
            break;
        case OP_SAHF:
            ubfx(1, S0, 0, 8, 8);
            bl_abs(STUB(tr_stub_sahf));
            break;
        case OP_CMC:
            cfinv();
            break;
        case OP_CLC:
            movimm(S0, 2);
            rmif(S0, 0, 2);
            break;
        case OP_STC:
            rmif(XZR, 0, 2);
            break;
        case OP_CLD:
        case OP_STD:
            movimm(S0, in->op == OP_CLD ? 1 : ~0ULL);
            E(0xF9000000u | (u32)(offsetof(EngineCtx, df) / 8) << 10 | CTX << 5 | S0);
            break;
        case OP_CMOV: {
            int src = in->rm_reg ? in->rm : S0;
            if (!in->rm_reg)
                ldst(1, sz, S0, addr_prepare(in, sz));
            int acc = x86_to_arm_cc[in->cc];
            if (acc < 0) {
                emit_parity(S4);
                u32 *over = tr_cc;
                cbz(in->cc == 11, 0, S4, 0); // skip the move unless the condition holds
                gpr_write(in->reg, sz, false, src);
                u32 *done = tr_cc;
                if (sz == 4) { // a 32-bit cmov zero-extends even when the move does not happen
                    b_rel(2);
                    *over |= ((u32)(tr_cc - over) & 0x7ffff) << 5;
                    mov_reg(0, in->reg, in->reg);
                } else
                    *over |= ((u32)(tr_cc - over) & 0x7ffff) << 5;
                (void)done;
            } else if (sz >= 4)
                csel(sz == 8, in->reg, src, in->reg, acc);
            else {
                csel(0, S1, src, in->reg, acc);
                bfi(1, in->reg, S1, 0, 16);
            }
            break;
        }
        case OP_SETCC: {
            int acc = x86_to_arm_cc[in->cc];
            if (acc < 0) {
                emit_parity(S0);
                if (in->cc == 11)
                    E(0x52000000u | S0 << 5 | S0); // eor w16, w16, #1
            } else
                cset(S0, acc);
            if (in->rm_reg)
                gpr_write(in->rm, 1, in->high_rm, S0);
            else
                ldst(0, 1, S0, addr_prepare(in, 1));
            break;
        }
        case OP_STRING:
            emit_string(in);
            break;
        case OP_XLAT:
            if ((in->addr32 && !tr_compat) || in->addr16 || in->seg) {
                emit_stop(in->rip, "xlat with an override");
                break;
            }
            ubfx(0, S0, 0, 0, 8);
            if (tr_compat) {
                add_reg(0, 0, 0, S0, 3, S0, 0);
                E(0x39400000u | S0 << 5 | S0); // ldrb w16, [x16]
            } else
                E(0x38606800u | S0 << 16 | 3 << 5 | S0); // ldrb w16, [x3, x16]
            bfi(1, 0, S0, 0, 8);
            break;
        case OP_CRC32: {
            int width = in->srcsz ? in->srcsz : sz, src = S0;
            if (in->rm_reg)
                src = gpr_read(in->rm, width, in->high_rm, S0);
            else
                ldst(1, width, S0, addr_prepare(in, width));
            int size = width == 1 ? 0 : width == 2 ? 1 : width == 4 ? 2 : 3;
            E((width == 8 ? 0x9AC05000u : 0x1AC05000u) | (u32)src << 16 | (u32)size << 10 | (u32)in->reg << 5 | S2); // crc32c, which is the polynomial x86's crc32 uses
            gpr_write(in->reg, 4, false, S2);
            break;
        }
        case OP_MOVBE: {
            if (in->rm_reg) {
                emit_ud();
                break;
            }
            Addr a = addr_prepare(in, sz);
            if (in->to_reg) {
                ldst(1, sz, S0, a);
                if (sz == 8)
                    rev64(S0, S0);
                else if (sz == 4)
                    rev32(0, S0, S0);
                else
                    rev16w(S0, S0);
                gpr_write(in->reg, sz, false, S0);
            } else {
                if (sz == 8)
                    rev64(S0, in->reg);
                else if (sz == 4)
                    rev32(0, S0, in->reg);
                else
                    rev16w(S0, in->reg);
                ldst(0, sz, S0, a);
            }
            break;
        }
        case OP_FENCE:
            dmb(in->sub == 0 ? 0xB : in->sub == 1 ? 0xA : 0x9);
            break;
        case OP_PREFETCH:
            break; // prefetch never faults
        case OP_CLFLUSH: {
            Addr a = addr_prepare(in, 1);
            ldst(1, 1, XZR, a); // probes the page as x86 does. shortcut: no cache maintenance, fine while all RAM is coherent
            break;
        }
        default:
            emit_stop(in->rip, "no emitter for this instruction");
    }
}
