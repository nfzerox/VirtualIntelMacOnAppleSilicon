// x87: memory operands move at EL0, so faults take the ordinary shadow/device
// path. Normal exact loads can widen inline; other operations use the monitor.
// Nothing commits before the last access that can fault. A store gets its value
// and new state word from the monitor, writes memory, then writes the word.
#include <stddef.h>
#include "tr_internal.h"

enum { NONE, LOAD16, LOAD32, LOAD64, LOAD80, AREA_READ, AREA_WRITE, STORE16, STORE32, STORE64, STORE80 };

static int mem_kind(int esc, int reg) {
    static const u8 table[8][8] = {
        {LOAD32, LOAD32, LOAD32, LOAD32, LOAD32, LOAD32, LOAD32, LOAD32}, // D8
        {LOAD32, NONE, STORE32, STORE32, AREA_READ, LOAD16, AREA_WRITE, STORE16}, // D9
        {LOAD32, LOAD32, LOAD32, LOAD32, LOAD32, LOAD32, LOAD32, LOAD32}, // DA
        {LOAD32, STORE32, STORE32, STORE32, NONE, LOAD80, NONE, STORE80}, // DB
        {LOAD64, LOAD64, LOAD64, LOAD64, LOAD64, LOAD64, LOAD64, LOAD64}, // DC
        {LOAD64, STORE64, STORE64, STORE64, AREA_READ, NONE, AREA_WRITE, STORE16}, // DD
        {LOAD16, LOAD16, LOAD16, LOAD16, LOAD16, LOAD16, LOAD16, LOAD16}, // DE
        {LOAD16, STORE16, STORE16, STORE16, LOAD80, LOAD64, STORE80, STORE64}, // DF
    };
    return table[esc][reg];
}

static void call_monitor(const Insn *in) {
    svc(SVC_X87);
    E((u32)(in->sub == 8 ? 0xFFFF : in->sub << 8 | in->opc) | (u32)in->seg << 16 | (in->pfx == 0x66 ? 1u << 18 : 0));
    E((u32)in->rip);
    E((u32)(in->rip >> 32));
}

// A normal binary32/64 load widens EXACTLY to x87's existing 80-bit format.
// Avoid an EL0 -> EL1 service for this common case, not x87 precision or
// exception checks. All operand accesses precede these stores. Special values,
// full stacks, pending/unmasked exceptions and CR0.EM/TS retain x87_op.
static void load_normal(const Insn *in) {
    int fraction = in->sub == 1 ? 23 : 52, exponent = in->sub == 1 ? 8 : 11;
    u32 *slow[6];
    ldst(1, 8, S1, (Addr){.base=CTX,.imm=offsetof(EngineCtx, cr0)});
    movimm(S5, 0xC);
    logic_reg(1, 0, 0, S1, S1, S5);
    slow[0]=tr_cc; cbz(1, 1, S1, 0);
    ldst(1, 2, S1, (Addr){.base=CTX,.imm=offsetof(EngineCtx, fcw)});
    movimm(S5, 0x3F);
    logic_reg(0, 0, 0, S1, S1, S5);
    logic_reg(0, 2, 0, S1, S1, S5);
    slow[1]=tr_cc; cbz(1, 0, S1, 0);
    ldst(1, 4, S3, (Addr){.base=CTX,.imm=offsetof(EngineCtx, fp_top)});
    slow[2]=tr_cc; tbz(1, S3, 23, 0); // FSW.ES, not a condition-code flag
    add_imm(0, 1, 0, S2, S3, 1);
    ubfx(0, S2, S2, 0, 3);
    movimm(S1, 1);
    var_shift(0, 0, S1, S1, S2);
    lsl_imm(0, S1, S1, 8);
    logic_reg(0, 0, 0, S5, S3, S1);
    slow[3]=tr_cc; cbz(1, 0, S5, 0);
    ubfx(1, S4, S0, fraction, exponent);
    slow[4]=tr_cc; cbz(0, 0, S4, 0);
    movimm(S5, (1u<<exponent)-1);
    logic_reg(0, 2, 0, S5, S5, S4);
    slow[5]=tr_cc; cbz(0, 0, S5, 0);

    // Sign/exponent and significand use only integer operations: host FPCR,
    // MXCSR and x87 rounding/precision modes cannot affect widening.
    movimm(S5, 16383-((1<<(exponent-1))-1));
    add_reg(0, 0, 0, S4, S4, S5, 0);
    ubfx(1, S5, S0, fraction+exponent, 1);
    bfi(0, S4, S5, 15, 1);
    lsl_imm(1, S0, S0, 63-fraction);
    movimm(S5, 1ULL<<63);
    logic_reg(1, 1, 0, S0, S0, S5);
    add_reg(1, 0, 0, S5, CTX, S2, 3);
    ldst(0, 8, S0, (Addr){.base=S5,.imm=offsetof(EngineCtx, mm)});
    add_reg(1, 0, 0, S5, CTX, S2, 1);
    ldst(0, 2, S4, (Addr){.base=S5,.imm=offsetof(EngineCtx, fp_exp)});
    movimm(S0, in->sub<<8 | in->opc);
    ldst(0, 2, S0, (Addr){.base=CTX,.imm=offsetof(EngineCtx, fop)});
    movimm(S0, in->rip);
    ldst(0, 8, S0, (Addr){.base=CTX,.imm=offsetof(EngineCtx, fip)});
    ldst(1, 2, S0, (Addr){.base=CTX,.imm=offsetof(EngineCtx, seg_sel)+2});
    ldst(0, 2, S0, (Addr){.base=CTX,.imm=offsetof(EngineCtx, fcs)});
    ldst(0, 8, AD, (Addr){.base=CTX,.imm=offsetof(EngineCtx, fdp)});
    int segment=in->seg==1 ? 4 : in->seg==2 ? 5 : 3;
    ldst(1, 2, S0, (Addr){.base=CTX,.imm=offsetof(EngineCtx, seg_sel)+segment*2});
    ldst(0, 2, S0, (Addr){.base=CTX,.imm=offsetof(EngineCtx, fds)});
    movimm(S0, (1u<<25)|7); // clear C1 and replace TOP; preserve every other flag
    logic_reg(0, 0, 1, S3, S3, S0);
    logic_reg(0, 1, 0, S3, S3, S2);
    logic_reg(0, 1, 0, S3, S3, S1);
    ldst(0, 4, S3, (Addr){.base=CTX,.imm=offsetof(EngineCtx, fp_top)});
    u32 *done=tr_cc; b_rel(0);
    for(unsigned i=0;i<6;i++)*slow[i]|=(u32)(tr_cc-slow[i])<<5;
    call_monitor(in);
    *done|=(u32)(tr_cc-done);
}

void emit_x87(const Insn *in) {
    if (in->rm_reg) {
        call_monitor(in);
        if (in->sub == 7 && in->opc == 0xE0) // fnstsw ax
            gpr_write(0, 2, false, S0);
        return;
    }
    int reg = (in->opc >> 3) & 7, kind = mem_kind(in->sub, reg);
    if (kind == NONE) {
        emit_stop(in->rip, "x87: reserved memory form");
        return;
    }
    E(0x4F00E400u | V_MEM); // movi v30.16b, #0: TS gives #NM ahead of any fault on the operand
    int r = addr_reg(in);
    if (r != AD)
        mov_reg(1, AD, r);
    Addr a = {.base = AD}, hi = {.base = AD, .imm = 8};
    switch (kind) {
        case LOAD16:
            ldst(1, 2, S0, a);
            break;
        case LOAD32:
            ldst(1, 4, S0, a);
            break;
        case LOAD64:
            ldst(1, 8, S0, a);
            break;
        case LOAD80:
            ldst(1, 8, S0, a);
            ldst(1, 2, S1, hi);
            break;
        case AREA_READ:
        case AREA_WRITE: {
            // fldenv, fnstenv, frstor, fnsave: the monitor copies these through its walker, so make sure both ends are
            // there and writable.
            int bytes = in->sub == 1 ? (in->pfx == 0x66 ? 14 : 28) : (in->pfx == 0x66 ? 94 : 108);
            Addr last = {.base = AD, .imm = bytes - 1};
            ldst(1, 1, S0, a);
            ldst(1, 1, S1, last);
            if (kind == AREA_WRITE) {
                ldst(0, 1, S0, a);
                ldst(0, 1, S1, last);
            }
            break;
        }
        default:
            break;
    }
    if((in->sub==1 || in->sub==5) && reg==0) {
        load_normal(in);
        return;
    }
    call_monitor(in);
    if (kind < STORE16)
        return;
    switch (kind) {
        case STORE16:
            ldst(0, 2, S0, a);
            break;
        case STORE32:
            ldst(0, 4, S0, a);
            break;
        case STORE64:
            ldst(0, 8, S0, a);
            break;
        default:
            // A fault on either page must leave both as they were: write the first eight bytes back, then the last two,
            // then the first eight for real.
            ldst(1, 8, S2, a);
            ldst(0, 8, S2, a);
            ldst(0, 2, S1, hi);
            ldst(0, 8, S0, a);
            break;
    }
    Addr state = {.base = CTX, .imm = offsetof(EngineCtx, fp_top)};
    ldst(0, 4, S3, state);
}
