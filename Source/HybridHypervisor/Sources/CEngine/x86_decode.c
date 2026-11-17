// x86-64 decoder. Anything outside the supported set becomes OP_BAD or OP_UD, which only stops if it is reached.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tr_internal.h"

const signed char x86_to_arm_cc[16] = {6, 7, 3, 2, 0, 1, 9, 8, 4, 5, -1, -1, 11, 10, 13, 12};
const u8 cc_reads[16] = {F_V, F_V, F_C,   F_C,   F_Z,       F_Z,       F_C | F_Z,       F_C | F_Z,
                         F_N, F_N, F_P,   F_P,   F_N | F_V, F_N | F_V, F_N | F_V | F_Z, F_N | F_V | F_Z};

static void set_bad(Insn *in, const char *why) {
    in->op = OP_BAD;
    in->why = why;
    in->reads = F_ARITH;
    in->writes = 0;
}

// 16-bit addressing, which is 67h in compatibility mode: bx, si, di, bp only. The emitter wraps the sum at 16 bits.
static const u8 *modrm16(Insn *in, int mod, int rm, const u8 *p) {
    static const u8 base[8] = {3, 3, 5, 5, 6, 7, 5, 3}, index[8] = {6, 7, 6, 7};
    if (rm < 4) {
        in->no_index = false;
        in->index = index[rm];
    }
    if (mod == 0 && rm == 6) {
        in->no_base = true;
        in->disp = *(u16 *)p;
        return p + 2;
    }
    in->base = base[rm];
    if (mod == 1) {
        in->disp = *(int8_t *)p;
        return p + 1;
    }
    if (mod == 2) {
        in->disp = *(u16 *)p;
        return p + 2;
    }
    return p;
}

// x87 forms that stay on TCG, by name for the tally; NULL for the ones the monitor does. Trigonometry and logarithms, which TCG itself only approximates and
// nothing has shown to be hot, and the 287 leftovers and undocumented aliases, where only TCG knows what the CPU model does.
static const char *x87_rare(int esc, u8 m) {
    if (m < 0xC0)
        return NULL;
    u8 r = m & 0xF8;
    switch (esc) {
        case 1:
            switch (m) {
                case 0xF0: return "x87: f2xm1";
                case 0xF1: return "x87: fyl2x";
                case 0xF2: return "x87: fptan";
                case 0xF3: return "x87: fpatan";
                case 0xF9: return "x87: fyl2xp1";
                case 0xFB: return "x87: fsincos";
                case 0xFE: return "x87: fsin";
                case 0xFF: return "x87: fcos";
                default: break;
            }
            return r == 0xC0 || r == 0xC8 || m == 0xD0 || (m >= 0xE0 && m != 0xE2 && m != 0xE3 && m != 0xE6 && m != 0xE7 && m != 0xEF) ? NULL : "x87: reserved D9 form";
        case 2: return m < 0xE0 || m == 0xE9 ? NULL : "x87: reserved DA form";
        case 3: return m < 0xE0 || m == 0xE2 || m == 0xE3 || (m >= 0xE8 && m < 0xF8) ? NULL : "x87: reserved or 287 DB form";
        case 4: return r == 0xD0 || r == 0xD8 ? "x87: DC alias of fcom" : NULL;
        case 5: return r == 0xC0 || r == 0xD0 || r == 0xD8 || r == 0xE0 || r == 0xE8 ? NULL : "x87: reserved DD form";
        case 6: return r == 0xC0 || r == 0xC8 || m == 0xD9 || r >= 0xE0 ? NULL : "x87: reserved DE form";
        case 7: return m == 0xE0 || (m >= 0xE8 && m < 0xF8) ? NULL : "x87: reserved or alias DF form";
        default: return NULL;
    }
}

static void decode_inner(u64 rip, Insn *in, const u8 *b, int avail) {
    const u8 *p = b;
    int rex = 0, sz = 4;
    bool has_rex = false, p66 = false;
    u8 last_mand = 0;
    in->addr32 = tr_compat;
    for (;; p++) {
        if (p - b > 14) {
            set_bad(in, "too many prefixes");
            return;
        }
        u8 c = *p;
        if (c == 0x66) {
            p66 = true;
            sz = 2;
        } else if (c == 0x2E || c == 0x36 || c == 0x3E || c == 0x26)
            continue; // cs, ss, ds, es overrides are ignored in 64-bit mode
        else if (c == 0x64)
            in->seg = 1;
        else if (c == 0x65)
            in->seg = 2;
        else if (c == 0x67) {
            in->addr32 = true;
            in->addr16 = tr_compat;
        }
        else if (c == 0xF0)
            in->lock = true;
        else if (c == 0xF2 || c == 0xF3) {
            in->rep = c;
            last_mand = c;
        } else
            break;
    }
    if (!tr_compat && (*p & 0xF0) == 0x40) { // in 32-bit code these are inc and dec
        rex = *p++ & 15;
        has_rex = true;
    }
    if (rex & 8)
        sz = 8;
    int w = sz;
    const int stack_w = sz == 2 ? 2 : tr_compat ? 4 : 8;
    u8 opc = *p++;
    bool two = false;
    if (opc == 0x0F) {
        two = true;
        opc = *p++;
    }

#define MODRM()                                                                                                        \
    do {                                                                                                               \
        u8 m_ = *p++;                                                                                                  \
        int mod_ = m_ >> 6, rm_ = m_ & 7;                                                                              \
        in->reg = ((m_ >> 3) & 7) | ((rex & 4) << 1);                                                                  \
        if (mod_ == 3) {                                                                                               \
            in->rm_reg = true;                                                                                         \
            in->rm = rm_ | ((rex & 1) << 3);                                                                           \
            break;                                                                                                     \
        }                                                                                                              \
        in->memop = true;                                                                                              \
        in->no_index = true;                                                                                           \
        if (in->addr16) {                                                                                              \
            p = modrm16(in, mod_, rm_, p);                                                                             \
            break;                                                                                                     \
        }                                                                                                              \
        if (tr_compat && rm_ == 5 && mod_ == 0) { /* no rip-relative form: an absolute address */                      \
            in->no_base = true;                                                                                        \
            in->disp = *(i32 *)p;                                                                                      \
            p += 4;                                                                                                    \
            break;                                                                                                     \
        }                                                                                                              \
        if (rm_ == 4) {                                                                                                \
            u8 s_ = *p++;                                                                                              \
            int idx_ = ((s_ >> 3) & 7) | ((rex & 2) << 2), bs_ = s_ & 7;                                               \
            if (idx_ != 4) {                                                                                           \
                in->no_index = false;                                                                                  \
                in->index = idx_;                                                                                      \
                in->scale = s_ >> 6;                                                                                   \
            }                                                                                                          \
            if (bs_ == 5 && mod_ == 0) {                                                                               \
                in->no_base = true;                                                                                    \
                in->disp = *(i32 *)p;                                                                                  \
                p += 4;                                                                                                \
            } else                                                                                                     \
                in->base = bs_ | ((rex & 1) << 3);                                                                     \
        } else if (rm_ == 5 && mod_ == 0) {                                                                            \
            in->riprel = true;                                                                                         \
            in->disp = *(i32 *)p;                                                                                      \
            p += 4;                                                                                                    \
        } else                                                                                                         \
            in->base = rm_ | ((rex & 1) << 3);                                                                         \
        if (mod_ == 1) {                                                                                               \
            in->disp = *(int8_t *)p;                                                                                   \
            p++;                                                                                                       \
        } else if (mod_ == 2) {                                                                                        \
            in->disp = *(i32 *)p;                                                                                      \
            p += 4;                                                                                                    \
        }                                                                                                              \
    } while (0)
#define IMM(n)                                                                                                         \
    do {                                                                                                               \
        in->has_imm = true;                                                                                            \
        in->imm_len = (n);                                                                                             \
        if ((n) == 1) {                                                                                                \
            in->imm = *(int8_t *)p;                                                                                    \
            p += 1;                                                                                                    \
        } else if ((n) == 2) {                                                                                         \
            in->imm = *(int16_t *)p;                                                                                   \
            p += 2;                                                                                                    \
        } else if ((n) == 4) {                                                                                         \
            in->imm = *(i32 *)p;                                                                                       \
            p += 4;                                                                                                    \
        } else {                                                                                                       \
            in->imm = *(i64 *)p;                                                                                       \
            p += 8;                                                                                                    \
        }                                                                                                              \
    } while (0)
#define IMMSZ(s) ((s) == 8 ? 4 : (s))
#define BADUD(msg) do { in->op = OP_UD; in->why = msg; } while (0)
    // A byte-register number 4-7 without any REX prefix means ah, ch, dh, bh.
#define MARK_HIGH()                                                                                                    \
    do {                                                                                                               \
        if (w == 1 && !has_rex) {                                                                                      \
            if (in->reg >= 4 && in->reg <= 7)                                                                          \
                in->high_reg = true;                                                                                   \
            if (in->rm_reg && in->rm >= 4 && in->rm <= 7)                                                              \
                in->high_rm = true;                                                                                    \
        }                                                                                                              \
    } while (0)

    if (!two) {
        if (tr_compat && opc >= 0x40 && opc <= 0x4F) {
            in->op = OP_INCDEC;
            in->sub = opc >= 0x48;
            in->writes = F_V | F_Z | F_N | F_P | F_A;
            in->rm_reg = true;
            in->rm = opc & 7;
        } else if (tr_compat && (opc == 0x06 || opc == 0x0E || opc == 0x16 || opc == 0x1E)) {
            in->op = OP_PUSH;
            in->sub = (opc >> 3) + 1;
            w = stack_w;
        } else if (tr_compat && (opc == 0x07 || opc == 0x17 || opc == 0x1F)) {
            in->op = OP_MOV_SEG;
            in->pop_seg = true;
            in->sub = opc >> 3;
            w = stack_w;
        } else if (tr_compat && (opc == 0x60 || opc == 0x61)) {
            in->op = OP_PUSHA;
            in->sub = opc & 1;
            in->memop = true;
        } else if (tr_compat && opc >= 0xA0 && opc <= 0xA3) { // moffs
            w = (opc & 1) ? sz : 1;
            in->op = OP_MOV;
            in->memop = true;
            in->no_base = true;
            in->to_reg = opc < 0xA2;
            if (in->addr16) {
                in->disp = *(u16 *)p;
                p += 2;
            } else {
                in->disp = *(i32 *)p;
                p += 4;
            }
        } else if (!tr_compat && opc >= 0xA0 && opc <= 0xA3 && !in->seg && !in->addr32) { // moffs: JavaScript's baseline code stores to fixed addresses this way
            w = (opc & 1) ? sz : 1;
            in->op = OP_MOV;
            in->memop = true;
            in->no_base = in->no_index = true;
            in->to_reg = opc < 0xA2;
            in->disp = *(i64 *)p;
            p += 8;
        } else if (tr_compat && (opc == 0x9A || opc == 0xEA)) {
            in->op = OP_FAR;
            in->sub = opc == 0x9A ? 1 : 2;
            in->memop = true;
            in->reads = F_ARITH;
            in->has_imm = true;
            in->imm = sz == 2 ? *(u16 *)p : *(u32 *)p;
            p += sz;
            in->disp = *(u16 *)p;
            p += 2;
        } else if (tr_compat && opc == 0x63) {
            set_bad(in, "arpl");
        } else if (tr_compat && (opc == 0x27 || opc == 0x2F || opc == 0x37 || opc == 0x3F || opc == 0xD4 || opc == 0xD5 || opc == 0xD6)) {
            set_bad(in, "BCD adjust or salc");
        } else if (tr_compat && (opc == 0x62 || opc == 0xCE)) {
            set_bad(in, "bound or into");
        } else if (tr_compat && (opc == 0xC4 || opc == 0xC5) && (*p >> 6) != 3) {
            set_bad(in, "les or lds");
        } else if (opc < 0x40 && (opc & 7) < 6) {
            int form = opc & 7;
            in->op = OP_ALU;
            in->sub = opc >> 3;
            in->writes = F_ARITH;
            if (in->sub == ALU_ADC || in->sub == ALU_SBB)
                in->reads = F_C;
            w = (form & 1) ? sz : 1;
            if (form < 4) {
                MODRM();
                in->to_reg = form >= 2;
                MARK_HIGH();
            } else {
                in->rm_reg = true;
                in->to_reg = false;
                IMM(IMMSZ(w));
            }
        } else if (opc >= 0x50 && opc <= 0x5F) {
            in->op = opc < 0x58 ? OP_PUSH : OP_POP;
            in->rm_reg = true;
            in->rm = (opc & 7) | ((rex & 1) << 3);
            w = stack_w;
        } else if (opc == 0x63) {
            MODRM();
            in->op = OP_MOVSX;
            in->srcsz = 4;
        } else if (opc == 0x68 || opc == 0x6A) {
            in->op = OP_PUSH;
            w = stack_w;
            IMM(opc == 0x6A ? 1 : (sz == 2 ? 2 : 4));
        } else if (opc == 0x69 || opc == 0x6B) {
            MODRM();
            in->op = OP_IMUL;
            in->writes = F_ARITH;
            IMM(opc == 0x6B ? 1 : IMMSZ(sz));
        } else if (opc >= 0x70 && opc <= 0x7F) {
            in->op = OP_JCC;
            in->cc = opc & 15;
            in->reads = cc_reads[in->cc];
            IMM(1);
        } else if (opc == 0x80 || opc == 0x81 || opc == 0x83 || (tr_compat && opc == 0x82)) {
            w = opc == 0x80 || opc == 0x82 ? 1 : sz;
            MODRM();
            in->op = OP_ALU;
            in->sub = in->reg & 7;
            in->writes = F_ARITH;
            if (in->sub == ALU_ADC || in->sub == ALU_SBB)
                in->reads = F_C;
            IMM(opc == 0x81 ? IMMSZ(w) : 1);
            in->reg = 0;
            MARK_HIGH();
            in->high_reg = false;
        } else if (opc == 0x84 || opc == 0x85) {
            w = opc == 0x84 ? 1 : sz;
            MODRM();
            in->op = OP_ALU;
            in->sub = ALU_TEST;
            in->writes = F_ARITH;
            in->to_reg = false;
            MARK_HIGH();
        } else if (opc == 0x86 || opc == 0x87) {
            w = opc == 0x86 ? 1 : sz;
            MODRM();
            in->op = OP_XCHG;
            MARK_HIGH();
        } else if (opc >= 0x88 && opc <= 0x8B) {
            w = (opc & 1) ? sz : 1;
            MODRM();
            in->op = OP_MOV;
            in->to_reg = opc & 2;
            MARK_HIGH();
        } else if (opc == 0x8C || opc == 0x8E) {
            MODRM();
            in->op = OP_MOV_SEG;
            in->to_reg = opc == 0x8C;
            in->sub = in->reg & 7;
            if (in->sub > 5)
                BADUD("mov to or from a nonexistent segment");
            w = in->rm_reg ? (sz == 2 ? 2 : sz) : 2;
        } else if (opc == 0x8D) {
            MODRM();
            if (in->rm_reg)
                BADUD("lea with a register operand");
            else {
                in->op = OP_LEA;
                in->memop = false;
            }
        } else if (opc == 0x8F) {
            MODRM();
            if ((in->reg & 7) != 0)
                set_bad(in, "XOP or unknown 8F encoding");
            else {
                in->op = OP_POP;
                w = stack_w;
            }
        } else if (opc >= 0x90 && opc <= 0x97) {
            if (opc == 0x90 && !(rex & 1)) {
                in->op = OP_NOP; // also pause with F3
                in->rep = 0;
            } else {
                in->op = OP_XCHG;
                in->rm_reg = true;
                in->rm = 0;
                in->reg = (opc & 7) | ((rex & 1) << 3);
            }
        } else if (opc == 0x98) {
            in->op = OP_CBW;
        } else if (opc == 0x99) {
            in->op = OP_CWD;
        } else if (opc == 0x9B) {
            in->op = OP_X87; // fwait, with no modrm byte
            in->sub = 8;
            in->rm_reg = true;
            in->reads = F_ARITH;
        } else if (opc == 0x9C) {
            in->op = OP_PUSHF;
            in->memop = true;
            in->reads = F_ARITH;
            w = stack_w;
        } else if (opc == 0x9D) {
            in->op = OP_POPF;
            in->memop = true;
            in->writes = F_ARITH;
            w = stack_w;
        } else if (opc == 0x9E) {
            in->op = OP_SAHF;
            in->writes = F_C | F_Z | F_N | F_P | F_A;
        } else if (opc == 0x9F) {
            in->op = OP_LAHF;
            in->reads = F_C | F_Z | F_N | F_P | F_A;
        } else if (opc >= 0xA4 && opc <= 0xA7) {
            in->op = OP_STRING;
            in->sub = (opc >> 1) & 1;
            w = (opc & 1) ? sz : 1;
            in->memop = true;
        } else if (opc == 0xA8 || opc == 0xA9) {
            w = opc == 0xA8 ? 1 : sz;
            in->rm_reg = true;
            IMM(IMMSZ(w));
            in->op = OP_ALU;
            in->sub = ALU_TEST;
            in->writes = F_ARITH;
        } else if (opc >= 0xAA && opc <= 0xAF) {
            in->op = OP_STRING;
            in->sub = 2 + ((opc - 0xAA) >> 1);
            w = (opc & 1) ? sz : 1;
            in->memop = true;
        } else if (opc >= 0xB0 && opc <= 0xB7) {
            in->op = OP_MOV;
            in->to_reg = true;
            in->rm_reg = true;
            w = 1;
            in->reg = (opc & 7) | ((rex & 1) << 3);
            if (!has_rex && (opc & 7) >= 4)
                in->high_reg = true;
            IMM(1);
        } else if (opc >= 0xB8 && opc <= 0xBF) {
            in->op = OP_MOV;
            in->to_reg = true;
            in->rm_reg = true;
            in->reg = (opc & 7) | ((rex & 1) << 3);
            IMM(sz);
        } else if (opc == 0xC0 || opc == 0xC1 || (opc >= 0xD0 && opc <= 0xD3)) {
            w = (opc & 1) ? sz : 1;
            MODRM();
            in->op = OP_SHIFT;
            in->sub = in->reg & 7;
            in->reg = 0;
            MARK_HIGH();
            in->high_reg = false;
            bool rot = in->sub < SH_SHL, thru = in->sub == SH_RCL || in->sub == SH_RCR;
            if (opc >= 0xD2) {
                in->by_cl = true;
                in->reads = F_ARITH; // a zero count leaves every flag as it was
                in->writes = 0;
            } else {
                in->has_imm = true;
                if (opc <= 0xC1)
                    in->imm = *p++;
                else
                    in->imm = 1;
                in->imm &= w == 8 ? 63 : 31;
                if (thru)
                    in->reads = F_C;
                if (in->imm)
                    in->writes = rot ? (F_C | F_V) : F_ARITH;
            }
        } else if (opc == 0xC2) {
            in->op = OP_RET;
            in->memop = true;
            in->has_imm = true;
            in->imm = *(u16 *)p;
            p += 2;
        } else if (opc == 0xC3) {
            in->op = OP_RET;
            in->memop = true;
            in->rep = 0; // rep ret
        } else if (opc == 0xC6 || opc == 0xC7) {
            w = opc == 0xC6 ? 1 : sz;
            MODRM();
            if ((in->reg & 7) != 0)
                set_bad(in, "C6/C7 group entry");
            else {
                in->op = OP_MOV;
                in->reg = 0;
                IMM(IMMSZ(w));
                MARK_HIGH();
                in->high_reg = false;
            }
        } else if (opc == 0xC9) {
            in->op = OP_LEAVE;
            in->memop = true;
        } else if (opc == 0xCC) {
            in->op = OP_INT3;
        } else if (opc == 0xCD) {
            in->op = OP_INT;
            in->imm = *p++;
        } else if (opc == 0xCF) {
            in->op = OP_IRET;
            in->memop = true;
            in->writes = F_ARITH;
        } else if (opc == 0xD7) {
            in->op = OP_XLAT;
            in->memop = true;
        } else if (opc >= 0xD8 && opc <= 0xDF) {
            u8 m = *p;
            const char *rare = x87_rare(opc & 7, m);
            MODRM();
            if (rare) {
                set_bad(in, rare);
            } else {
                in->op = OP_X87;
                in->sub = opc & 7;
                in->opc = m;
                in->pfx = p66 ? 0x66 : 0; // the small fnstenv and fnsave images
                in->reads = F_ARITH; // #NM and a fallback show the flags, and fcmov reads them
                if ((in->sub == 3 || in->sub == 7) && m >= 0xE8 && m < 0xF8)
                    in->writes = F_ARITH; // fucomi, fcomi and their popping forms
            }
        } else if (opc >= 0xE0 && opc <= 0xE3) {
            in->op = OP_JRCXZ;
            in->sub = opc == 0xE3 ? (in->addr32 ? 1 : 0) : opc == 0xE2 ? 2 : opc == 0xE1 ? 3 : 4;
            if (opc == 0xE1 || opc == 0xE0)
                in->reads = F_Z;
            IMM(1);
        } else if (opc == 0xE4 || opc == 0xE5 || opc == 0xE6 || opc == 0xE7) {
            w = (opc & 1) ? (sz == 8 ? 4 : sz) : 1;
            in->op = opc < 0xE6 ? OP_IN : OP_OUT;
            in->imm = *p++;
            in->has_imm = true;
        } else if (opc == 0xE8) {
            in->op = OP_CALL;
            in->memop = true;
            IMM(4);
        } else if (opc == 0xE9) {
            in->op = OP_JMP;
            IMM(4);
        } else if (opc == 0xEB) {
            in->op = OP_JMP;
            IMM(1);
        } else if (opc == 0xEC || opc == 0xED || opc == 0xEE || opc == 0xEF) {
            w = (opc & 1) ? (sz == 8 ? 4 : sz) : 1;
            in->op = opc < 0xEE ? OP_IN : OP_OUT;
        } else if (opc == 0xF4) {
            in->op = OP_HLT;
        } else if (opc == 0xF5) {
            in->op = OP_CMC;
            in->reads = F_C;
            in->writes = F_C;
        } else if (opc == 0xF6 || opc == 0xF7) {
            w = opc == 0xF6 ? 1 : sz;
            MODRM();
            int s = in->reg & 7;
            in->reg = 0;
            MARK_HIGH();
            in->high_reg = false;
            switch (s) {
                case 0:
                case 1:
                    in->op = OP_ALU;
                    in->sub = ALU_TEST;
                    in->writes = F_ARITH;
                    IMM(IMMSZ(w));
                    break;
                case 2:
                    in->op = OP_NOT;
                    break;
                case 3:
                    in->op = OP_NEG;
                    in->writes = F_ARITH;
                    break;
                case 4:
                case 5:
                    in->op = OP_MUL;
                    in->sub = s - 4;
                    in->writes = F_ARITH;
                    break;
                default:
                    in->op = OP_DIV;
                    in->sub = s - 6;
                    in->writes = F_ARITH;
                    break;
            }
        } else if (opc == 0xF8) {
            in->op = OP_CLC;
            in->writes = F_C;
        } else if (opc == 0xF9) {
            in->op = OP_STC;
            in->writes = F_C;
        } else if (opc == 0xFA) {
            in->op = OP_CLI;
        } else if (opc == 0xFB) {
            in->op = OP_STI;
        } else if (opc == 0xFC) {
            in->op = OP_CLD;
        } else if (opc == 0xFD) {
            in->op = OP_STD;
        } else if (opc == 0xFE || opc == 0xFF) {
            w = opc == 0xFE ? 1 : sz;
            MODRM();
            int s = in->reg & 7;
            in->reg = 0;
            if (s == 0 || s == 1) {
                in->op = OP_INCDEC;
                in->sub = s;
                in->writes = F_V | F_Z | F_N | F_P | F_A;
                MARK_HIGH();
                in->high_reg = false;
            } else if (opc == 0xFF && s == 2) {
                w = tr_compat ? 4 : 8;
                in->op = OP_CALLI;
                in->memop = true;
            } else if (opc == 0xFF && s == 4) {
                w = tr_compat ? 4 : 8;
                in->op = OP_JMPI;
            } else if (opc == 0xFF && s == 6) {
                w = stack_w;
                in->op = OP_PUSH;
            } else if (opc == 0xFF && (s == 3 || s == 5)) {
                if (in->rm_reg || (tr_cpl == 0 && s == 3))
                    set_bad(in, "far call or jump through memory");
                else {
                    in->op = OP_FAR;
                    in->sub = s == 3 ? 1 : 2;
                    in->memop = true;
                    in->reads = F_ARITH;
                }
            } else
                BADUD("FE/FF group entry");
        } else if (opc == 0x6C || opc == 0x6D || opc == 0x6E || opc == 0x6F) {
            set_bad(in, "ins/outs");
        } else if ((opc == 0xCA || opc == 0xCB) && tr_cpl == 0) {
            set_bad(in, "far return");
        } else if (opc == 0xCA || opc == 0xCB) {
            in->op = OP_FAR;
            in->memop = true;
            in->reads = F_ARITH;
            if (opc == 0xCA) {
                in->imm = *(u16 *)p;
                p += 2;
            }
        } else if (opc == 0xC8) {
            in->op = OP_ENTER;
            in->memop = true;
            in->imm = *(u16 *)p;
            p += 2;
            in->sub = *p++ & 31;
            w = stack_w;
        } else if (opc == 0x06 || opc == 0x07 || opc == 0x0E || opc == 0x16 || opc == 0x17 || opc == 0x1E ||
                   opc == 0x1F || opc == 0x27 || opc == 0x2F || opc == 0x37 || opc == 0x3F || opc == 0x60 ||
                   opc == 0x61 || opc == 0x62 || opc == 0x82 || opc == 0x9A || opc == 0xC4 || opc == 0xC5 ||
                   opc == 0xCE || opc == 0xD4 || opc == 0xD5 || opc == 0xD6 || opc == 0xEA) {
            BADUD("invalid in 64-bit mode (or VEX)");
        } else {
            set_bad(in, "untranslated one-byte opcode");
        }
    } else {
        // Two-byte map. SSE decodes by opcode and mandatory prefix in emit_sse.c.
        u8 mand = last_mand ? last_mand : p66 ? 0x66 : 0;
#define SSE_FORM()                                                                                                     \
    do {                                                                                                               \
        MODRM();                                                                                                       \
        in->op = OP_SSE;                                                                                               \
        in->opc = opc;                                                                                                 \
        in->pfx = mand;                                                                                                \
        in->rep = 0;                                                                                                   \
    } while (0)
        if (opc >= 0x10 && opc <= 0x17) {
            SSE_FORM();
        } else if (opc == 0x18) {
            MODRM();
            if (in->rm_reg)
                in->op = OP_NOP;
            else {
                in->op = OP_PREFETCH;
                in->memop = false;
            }
        } else if (opc >= 0x19 && opc <= 0x1F) {
            MODRM();
            in->op = OP_NOP;
            in->memop = false;
        } else if (opc >= 0x28 && opc <= 0x2F) {
            SSE_FORM();
            if (opc >= 0x2E)
                in->writes = F_ARITH; // ucomis, comis
        } else if (opc == 0x77) {
            in->op = OP_SSE;
            in->opc = opc;
            in->pfx = mand;
            in->rep = 0;
        } else if (opc >= 0x50 && opc <= 0x7F) {
            SSE_FORM();
            if ((opc >= 0x71 && opc <= 0x73) || opc == 0x70) {
                in->has_imm = true;
                in->imm = *p++;
            }
        } else if (opc == 0xC2 || opc == 0xC4 || opc == 0xC5 || opc == 0xC6) {
            SSE_FORM();
            in->has_imm = true;
            in->imm = *p++;
        } else if (opc >= 0xD0 && opc <= 0xFE) {
            SSE_FORM();
        } else if (opc == 0x34 || opc == 0x35) {
            in->op = opc == 0x34 ? OP_SYSENTER : OP_SYSEXIT;
            in->writes = F_ARITH;
            in->reads = F_ARITH;
        } else if (opc == 0xA0 || opc == 0xA8) {
            in->op = OP_PUSH;
            in->sub = (opc == 0xA0 ? 4 : 5) + 1;
            w = stack_w;
        } else if (opc == 0xA1 || opc == 0xA9) {
            in->op = OP_MOV_SEG;
            in->pop_seg = true;
            in->sub = opc == 0xA1 ? 4 : 5;
            w = stack_w;
        } else if (opc == 0x05) {
            in->op = OP_SYSCALL;
            in->writes = F_ARITH;
            in->reads = F_ARITH;
        } else if (opc == 0x07) {
            in->op = OP_SYSRET;
            in->writes = F_ARITH;
        } else if (opc == 0x0B) {
            BADUD("ud2");
        } else if (opc == 0x0D) {
            MODRM();
            in->op = in->rm_reg ? OP_UD : OP_PREFETCH;
            in->memop = false;
        } else if (opc == 0x00) {
            MODRM();
            int s = in->reg & 7;
            if (s == 0 || s == 1 || s == 2 || s == 3) {
                in->op = OP_LTR;
                in->sub = s == 0 ? 3 : s == 1 ? 1 : s == 2 ? 2 : 0;
                w = in->rm_reg && (s < 2) ? sz : 2;
            } else
                set_bad(in, "verr, verw");
        } else if (opc == 0x01) {
            u8 m = *p;
            if (m == 0xF8) {
                p++;
                in->op = OP_SWAPGS;
            } else if (m == 0xF9) {
                p++;
                in->op = OP_RDTSCP;
            } else if (m == 0xD0) {
                set_bad(in, "xgetbv");
                p++;
            } else {
                MODRM();
                int s = in->reg & 7;
                if (in->rm_reg)
                    set_bad(in, "unsupported 0F 01 register form");
                else if (s <= 3) {
                    in->op = OP_LGDT;
                    in->sub = s == 2 ? 0 : s == 3 ? 1 : s == 0 ? 2 : 3;
                } else if (s == 7) {
                    in->op = OP_INVLPG;
                    in->memop = false;
                } else
                    set_bad(in, "smsw, lmsw");
            }
        } else if (opc == 0x06) {
            in->op = OP_CLTS;
        } else if (opc == 0x08 || opc == 0x09) {
            in->op = OP_WBINVD;
        } else if (opc == 0x20 || opc == 0x22) {
            u8 m = *p++;
            in->op = OP_MOV_CR;
            in->to_reg = opc == 0x20;
            in->rm_reg = true;
            in->rm = (m & 7) | ((rex & 1) << 3);
            in->reg = ((m >> 3) & 7) | ((rex & 4) << 1);
        } else if (opc == 0x21 || opc == 0x23) {
            u8 m = *p++;
            in->op = OP_MOV_DR;
            in->to_reg = opc == 0x21;
            in->rm = (m & 7) | ((rex & 1) << 3);
            in->reg = ((m >> 3) & 7);
        } else if (opc == 0x30) {
            in->op = OP_WRMSR;
        } else if (opc == 0x31) {
            in->op = OP_RDTSC;
        } else if (opc == 0x32) {
            in->op = OP_RDMSR;
        } else if (opc == 0x38 || opc == 0x3A) {
            if (opc == 0x38 && (p[0] == 0xF0 || p[0] == 0xF1) && last_mand == 0xF2) {
                bool byte = p[0] == 0xF0;
                p++;
                MODRM();
                in->op = OP_CRC32;
                in->srcsz = byte ? 1 : 0;
                if (byte && !has_rex && in->rm_reg && in->rm >= 4 && in->rm <= 7)
                    in->high_rm = true;
            } else if (opc == 0x38 && p[0] == 0xF0 && mand != 0xF2 && mand != 0xF3) { // movbe
                p++;
                MODRM();
                in->op = OP_MOVBE;
                in->to_reg = true;
            } else if (opc == 0x38 && p[0] == 0xF1 && mand != 0xF2 && mand != 0xF3) {
                p++;
                MODRM();
                in->op = OP_MOVBE;
                in->to_reg = false;
            } else if (mand == 0x66 && ((opc == 0x38 && (p[0] <= 0x0B || (p[0] >= 0x1C && p[0] <= 0x1E))) || (opc == 0x3A && p[0] == 0x0F))) {
                // SSSE3. macOS compilers assume it for x86_64, so real binaries contain it.
                bool imm = opc == 0x3A;
                opc = *p++;
                SSE_FORM();
                in->sub = imm ? 2 : 1;
                if (imm) {
                    in->has_imm = true;
                    in->imm = *p++;
                }
            } else if (mand == 0x66 && opc == 0x38 &&
                       (p[0] == 0x10 || p[0] == 0x14 || p[0] == 0x15 || p[0] == 0x17 || (p[0] >= 0x20 && p[0] <= 0x25) ||
                        (p[0] >= 0x28 && p[0] <= 0x2B) || (p[0] >= 0x30 && p[0] <= 0x35) || (p[0] >= 0x37 && p[0] <= 0x40) ||
                        (p[0] >= 0xDB && p[0] <= 0xDF))) {
                opc = *p++;
                SSE_FORM();
                in->sub = 3;
                if (opc == 0x17)
                    in->writes = F_ARITH; // ptest
            } else if (mand == 0x66 && opc == 0x3A &&
                       ((p[0] >= 0x08 && p[0] <= 0x0E) || (p[0] >= 0x14 && p[0] <= 0x17) || (p[0] >= 0x20 && p[0] <= 0x22) ||
                        p[0] == 0x44 || p[0] == 0xDF)) {
                opc = *p++;
                SSE_FORM();
                in->sub = 4;
                in->has_imm = true;
                in->imm = *p++;
            } else if (mand == 0x66 && opc == 0x3A && p[0] == 0x63) { // pcmpistri: a Mavericks strcmp, thousands to an application's start
                opc = *p++;
                SSE_FORM();
                in->sub = 5;
                in->has_imm = true;
                in->imm = *p++;
                in->writes = F_ARITH;
            } else
                BADUD("SSE4 opcode map: TCG has it");
        } else if (opc >= 0x40 && opc <= 0x4F) {
            MODRM();
            in->op = OP_CMOV;
            in->cc = opc & 15;
            in->reads = cc_reads[in->cc];
        } else if (opc >= 0x80 && opc <= 0x8F) {
            in->op = OP_JCC;
            in->cc = opc & 15;
            in->reads = cc_reads[in->cc];
            IMM(4);
        } else if (opc >= 0x90 && opc <= 0x9F) {
            w = 1;
            MODRM();
            in->op = OP_SETCC;
            in->cc = opc & 15;
            in->reads = cc_reads[in->cc];
            MARK_HIGH();
            in->high_reg = false;
        } else if (opc == 0xA2) {
            in->op = OP_CPUID;
        } else if (opc == 0xA3 || opc == 0xAB || opc == 0xB3 || opc == 0xBB) {
            MODRM();
            in->op = OP_BT;
            in->sub = opc == 0xA3 ? 0 : opc == 0xAB ? 1 : opc == 0xB3 ? 2 : 3;
            in->writes = F_ARITH & ~F_Z;
        } else if (opc == 0xBA) {
            MODRM();
            int s = in->reg & 7;
            if (s < 4)
                BADUD("0F BA group entry");
            else {
                in->op = OP_BT;
                in->sub = s - 4;
                in->writes = F_ARITH & ~F_Z;
                in->has_imm = true;
                in->imm = *p++;
            }
        } else if (opc == 0xA4 || opc == 0xA5 || opc == 0xAC || opc == 0xAD) {
            MODRM();
            in->op = OP_SHD;
            in->sub = opc >= 0xAC;
            if (opc & 1) {
                in->by_cl = true;
                in->reads = F_ARITH;
            } else {
                in->has_imm = true;
                in->imm = *p++;
                in->imm &= sz == 8 ? 63 : 31;
                if (in->imm)
                    in->writes = F_ARITH;
            }
        } else if (opc == 0xAE) {
            u8 m = *p;
            int s = (m >> 3) & 7;
            if ((m >> 6) == 3 && !mand) {
                p++;
                if (s == 5 || s == 6 || s == 7) {
                    in->op = OP_FENCE;
                    in->sub = s == 6 ? 0 : s == 7 ? 1 : 2;
                } else
                    set_bad(in, "0F AE register form");
            } else if ((m >> 6) != 3 && !mand) {
                MODRM();
                in->reg = s;
                if (s == 0) {
                    in->op = OP_FXSAVE;
                } else if (s == 1) {
                    in->op = OP_FXRSTOR;
                } else if (s == 2) {
                    in->op = OP_LDMXCSR;
                } else if (s == 3) {
                    in->op = OP_STMXCSR;
                } else if (s == 7) {
                    in->op = OP_CLFLUSH;
                    in->memop = false;
                } else
                    set_bad(in, "xsave family");
            } else
                set_bad(in, "0F AE with a mandatory prefix");
        } else if (opc == 0xAF) {
            MODRM();
            in->op = OP_IMUL;
            in->writes = F_ARITH;
        } else if (opc == 0xB0 || opc == 0xB1) {
            w = opc == 0xB0 ? 1 : sz;
            MODRM();
            in->op = OP_CMPXCHG;
            in->writes = F_ARITH;
            MARK_HIGH();
        } else if (opc == 0xB6 || opc == 0xB7 || opc == 0xBE || opc == 0xBF) {
            MODRM();
            in->op = opc < 0xBE ? OP_MOVZX : OP_MOVSX;
            in->srcsz = (opc & 1) ? 2 : 1;
            if (in->srcsz == 1 && !has_rex && in->rm_reg && in->rm >= 4 && in->rm <= 7)
                in->high_rm = true;
        } else if (opc == 0xB8 && mand == 0xF3) {
            MODRM();
            in->op = OP_POPCNT;
            in->rep = 0;
            in->writes = F_ARITH;
        } else if (opc == 0xBC || opc == 0xBD) {
            MODRM();
            if (mand == 0xF3) {
                in->op = opc == 0xBC ? OP_TZCNT : OP_LZCNT;
                in->rep = 0;
            } else
                in->op = opc == 0xBC ? OP_BSF : OP_BSR;
            in->writes = F_ARITH;
            // A zero source leaves the destination. Flags other than ZF are undefined.
        } else if (opc == 0xC0 || opc == 0xC1) {
            w = opc == 0xC0 ? 1 : sz;
            MODRM();
            in->op = OP_XADD;
            in->writes = F_ARITH;
            MARK_HIGH();
        } else if (opc == 0xC3) {
            MODRM();
            if (in->rm_reg)
                BADUD("movnti with a register operand");
            else
                in->op = OP_MOV;
            in->to_reg = false;
        } else if (opc == 0xC7) {
            MODRM();
            if ((in->reg & 7) == 1 && !in->rm_reg) {
                in->op = OP_CMPXCHG8B;
                in->writes = F_Z;
            } else
                set_bad(in, "0F C7 group entry");
        } else if (opc >= 0xC8 && opc <= 0xCF) {
            in->op = OP_BSWAP;
            in->rm_reg = true;
            in->rm = (opc & 7) | ((rex & 1) << 3);
        } else if (opc == 0xFF) {
            BADUD("0F FF");
        } else {
            set_bad(in, "untranslated two-byte opcode");
        }
    }
    (void)avail;
    if (in->op == OP_JCC && in->cc >= 16)
        set_bad(in, "bad condition");
    if (tr_compat && p66 && (in->op == OP_JCC || in->op == OP_JMP || in->op == OP_CALL || in->op == OP_JRCXZ || in->op == OP_JMPI || in->op == OP_CALLI || in->op == OP_RET))
        set_bad(in, "16-bit near branch"); // the target wraps at 64 KB, and call and ret move two bytes
    in->sz = w;
    in->next = rip + (p - b);
    if (in->op == OP_JCC || in->op == OP_JMP || in->op == OP_CALL || in->op == OP_JRCXZ)
        in->target = tr_compat ? (u32)(in->next + in->imm) : in->next + in->imm;
}

void x86_decode(u64 rip, Insn *in) {
    // Decoding is from a buffer with zero padding, then checked: the instruction must fit in what was fetched.
    u8 b[32] = {0};
    memset(in, 0, sizeof *in);
    in->rip = rip;
    in->no_index = true;
    int got = tr_fetch(rip, b, 15);
    decode_inner(rip, in, b, got);
    int len = (int)(in->next - rip);
    if (len > 15 || in->next == 0) {
        memset(in, 0, sizeof *in);
        in->rip = rip;
        in->next = rip + 1;
        in->op = OP_UD;
        in->why = "instruction longer than 15 bytes";
    } else if (len > got) {
        // The rest of the instruction lies on an unmapped page.
        u64 fault_va = rip + got;
        memset(in, 0, sizeof *in);
        in->rip = rip;
        in->next = fault_va;
        in->target = fault_va;
        set_bad(in, "FETCH_FAULT");
    }
    if (in->op == OP_MOVSX || in->op == OP_MOVZX || in->op == OP_LEA || in->op == OP_CMOV || in->op == OP_IMUL) {
        // Size prefix is honoured; nothing else to fix up.
    }
}

bool insn_is_terminator(const Insn *in) {
    switch (in->op) {
        case OP_JMP:
        case OP_JMPI:
        case OP_RET:
        case OP_BAD:
        case OP_UD:
        case OP_SYSCALL:
        case OP_SYSRET:
        case OP_IRET:
        case OP_INT:
        case OP_INT3:
        case OP_HLT:
        case OP_POPF:
        case OP_CLI:
        case OP_STI:
        case OP_CPUID:
        case OP_RDTSC:
        case OP_RDTSCP:
        case OP_RDMSR:
        case OP_WRMSR:
        case OP_MOV_CR:
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
        case OP_LDMXCSR:
        case OP_SYSENTER:
        case OP_SYSEXIT:
        case OP_FAR:
        case OP_FXSAVE:
        case OP_FXRSTOR:
            return true;
        default:
            return false;
    }
}
