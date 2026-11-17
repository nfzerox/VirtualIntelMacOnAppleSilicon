// Translator internals: AArch64 encoders, the decoded x86 instruction, and what the emit files share.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "engine_abi.h"
#include "translator.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t i32;
typedef int64_t i64;

#define S0 R_S0
#define S1 R_S1
#define S2 R_S2
#define S3 R_S3
#define S4 R_S4
#define S5 R_S5
#define AD R_AD
#define PFR R_PF
#define AFR R_AF
#define RS R_RS
#define LR R_LR
#define CTX R_CTX
#define XZR 31
#define V_S0 24 // SIMD scratch, v24-v31 are free
#define V_S1 25
#define V_S2 26
#define V_MEM 30 // memory operand of the current SSE instruction

// x86 flags as tracked by liveness.
enum { F_V = 1, F_C = 2, F_Z = 4, F_N = 8, F_P = 16, F_A = 32, F_ARITH = 63, F_NZCV = 15 };

enum { ALU_ADD, ALU_OR, ALU_ADC, ALU_SBB, ALU_AND, ALU_SUB, ALU_XOR, ALU_CMP, ALU_TEST };
enum { SH_ROL, SH_ROR, SH_RCL, SH_RCR, SH_SHL, SH_SHR, SH_SAL, SH_SAR };

enum {
    OP_BAD,
    OP_NOP,
    OP_ALU,
    OP_INCDEC, // sub: 0 inc, 1 dec
    OP_MOV,
    OP_LEA,
    OP_MOVZX,
    OP_MOVSX,
    OP_XCHG,
    OP_XADD,
    OP_CMPXCHG,
    OP_CMPXCHG8B,
    OP_PUSH, // reg, imm or mem
    OP_POP, // reg or mem
    OP_PUSHF,
    OP_POPF,
    OP_LEAVE,
    OP_ENTER, // imm size, sub level
    OP_SHIFT,
    OP_SHD, // shld/shrd; sub 0 left, 1 right
    OP_NEG,
    OP_NOT,
    OP_MUL, // one operand; sub 0 unsigned, 1 signed
    OP_IMUL, // two or three operand
    OP_DIV, // sub 0 unsigned, 1 signed
    OP_CMOV,
    OP_SETCC,
    OP_JCC,
    OP_JRCXZ, // sub 0 jrcxz, 1 jecxz, 2 loop, 3 loope, 4 loopne
    OP_JMP,
    OP_CALL,
    OP_JMPI,
    OP_CALLI,
    OP_RET, // imm: extra pop
    OP_BT, // sub 0 bt, 1 bts, 2 btr, 3 btc
    OP_BSF,
    OP_BSR,
    OP_TZCNT,
    OP_LZCNT,
    OP_POPCNT,
    OP_CRC32,
    OP_BSWAP,
    OP_CBW, // cbw, cwde, cdqe per size
    OP_CWD, // cwd, cdq, cqo per size
    OP_LAHF,
    OP_SAHF,
    OP_CMC,
    OP_CLC,
    OP_STC,
    OP_CLD,
    OP_STD,
    OP_STRING, // sub: 0 movs, 1 cmps, 2 stos, 3 lods, 4 scas
    OP_XLAT,
    OP_MOVBE,
    OP_FENCE, // sub 0 mfence, 1 sfence, 2 lfence
    OP_PREFETCH,
    OP_CLFLUSH,
    OP_SSE,
    OP_SYSCALL,
    OP_SYSRET,
    OP_IRET,
    OP_INT,
    OP_INT3,
    OP_HLT,
    OP_UD,
    OP_CPUID,
    OP_RDTSC,
    OP_RDTSCP,
    OP_RDMSR,
    OP_WRMSR,
    OP_CLI,
    OP_STI,
    OP_MOV_CR, // to_reg: read
    OP_MOV_DR,
    OP_INVLPG,
    OP_INVPCID,
    OP_WBINVD,
    OP_CLTS,
    OP_LGDT, // sub 0 lgdt, 1 lidt, 2 sgdt, 3 sidt
    OP_LTR, // sub 0 ltr, 1 str, 2 lldt, 3 sldt
    OP_MOV_SEG, // to_reg: read segment into reg
    OP_IN,
    OP_OUT,
    OP_SWAPGS,
    OP_LDMXCSR,
    OP_STMXCSR,
    OP_FXSAVE,
    OP_FXRSTOR,
    OP_X87, // sub: low three bits of the escape byte, 8 for fwait. opc: the modrm byte
    OP_RDFSGS_UNUSED,
    OP_PUSHA, // sub 0 pushad, 1 popad
    OP_SYSENTER,
    OP_SYSEXIT,
    OP_FAR, // sub 0 retf, 1 call, 2 jmp. has_imm: direct, imm the offset, disp the selector
};

typedef struct {
    u8 op, sz, srcsz, sub, cc, rm, reg, base, index, scale;
    u8 reads, writes; // flag liveness
    u8 seg; // 0 none, 1 fs, 2 gs
    u8 rep; // 0 none, 0xF2, 0xF3
    u8 pfx; // SSE mandatory prefix: 0 none, 0x66, 0xF2, 0xF3
    u8 opc; // SSE: second opcode byte; 0F38/0F3A are not advertised and stop
    u8 imm_len;
    bool rm_reg, to_reg, has_imm, by_cl, lock, addr32, addr16, pop_seg, riprel, no_base, no_index, high_reg, high_rm, memop;
    i64 disp, imm;
    u64 rip, next, target;
    const char *why;
} Insn;

extern const signed char x86_to_arm_cc[16];
extern const u8 cc_reads[16];

// Reads up to n guest bytes at va. Returns the count readable (may stop at an unmapped page).
int tr_fetch(u64 va, u8 *out, int n);
void x86_decode(u64 rip, Insn *in);
bool insn_is_terminator(const Insn *in);

// ---- emit state ----
extern u32 *tr_cc; // write cursor
extern bool tr_precise_flags, tr_has_tzcnt, tr_hybrid, tr_no_bulk_rep;
extern bool tr_profile_shift_helpers;
extern bool tr_inline_shr8;
extern bool tr_page_rep;
extern int tr_cpl;
extern bool tr_compat; // the block being built is 32-bit user code
static inline void E(u32 v) { *tr_cc++ = v; }

// Global to the block being built.
u64 tr_cc_va(const u32 *p);

// ---- AArch64 encoders, integer ----
static inline void add_imm(int sf, int sub, int s, int rd, int rn, u32 imm) {
    E((u32)sf << 31 | sub << 30 | s << 29 | 0x11u << 24 | imm << 10 | rn << 5 | rd);
}
// shift: 0 lsl, 1 lsr, 2 asr
static inline void add_reg_sh(int sf, int sub, int s, int rd, int rn, int rm, int shift, int amt) {
    E((u32)sf << 31 | sub << 30 | s << 29 | 0x0Bu << 24 | shift << 22 | rm << 16 | amt << 10 | rn << 5 | rd);
}
static inline void add_reg(int sf, int sub, int s, int rd, int rn, int rm, int lsl) {
    add_reg_sh(sf, sub, s, rd, rn, rm, 0, lsl);
}
static inline void adc(int sf, int sub, int s, int rd, int rn, int rm) {
    E((u32)sf << 31 | sub << 30 | s << 29 | 0x1A000000u | rm << 16 | rn << 5 | rd);
}
// opc: 0 and, 1 orr, 2 eor, 3 ands. n inverts rm. shift 0 lsl, 1 lsr, 2 asr, 3 ror
static inline void logic_sh(int sf, int opc, int n, int rd, int rn, int rm, int shift, int amt) {
    E((u32)sf << 31 | opc << 29 | 0x0Au << 24 | shift << 22 | n << 21 | rm << 16 | amt << 10 | rn << 5 | rd);
}
static inline void logic_reg(int sf, int opc, int n, int rd, int rn, int rm) { logic_sh(sf, opc, n, rd, rn, rm, 0, 0); }
static inline void mov_reg(int sf, int rd, int rm) { logic_reg(sf, 1, 0, rd, XZR, rm); }
// add x, sp-capable form is never needed: x31 is never used as sp here.
static inline void bitfield(int sf, int opc, int rd, int rn, int immr, int imms) { // opc 0 sbfm, 1 bfm, 2 ubfm
    E((u32)sf << 31 | opc << 29 | 0x26u << 23 | sf << 22 | immr << 16 | imms << 10 | rn << 5 | rd);
}
static inline void lsl_imm(int sf, int rd, int rn, int n) {
    int sz = sf ? 64 : 32;
    bitfield(sf, 2, rd, rn, (sz - n) % sz, sz - 1 - n);
}
static inline void lsr_imm(int sf, int rd, int rn, int n) { bitfield(sf, 2, rd, rn, n, sf ? 63 : 31); }
static inline void asr_imm(int sf, int rd, int rn, int n) { bitfield(sf, 0, rd, rn, n, sf ? 63 : 31); }
static inline void ubfx(int sf, int rd, int rn, int lsb, int width) { bitfield(sf, 2, rd, rn, lsb, lsb + width - 1); }
static inline void sbfx(int sf, int rd, int rn, int lsb, int width) { bitfield(sf, 0, rd, rn, lsb, lsb + width - 1); }
// rd[lsb+width-1:lsb] = rn[width-1:0], rest of rd kept
static inline void bfi(int sf, int rd, int rn, int lsb, int width) {
    int sz = sf ? 64 : 32;
    bitfield(sf, 1, rd, rn, (sz - lsb) % sz, width - 1);
}
static inline void extr(int sf, int rd, int rn, int rm, int lsb) {
    E((u32)sf << 31 | 0x13800000u | (u32)sf << 22 | rm << 16 | lsb << 10 | rn << 5 | rd);
}
static inline void var_shift(int sf, int op2, int rd, int rn, int rm) { // op2 0 lsl, 1 lsr, 2 asr, 3 ror
    E(0x1AC02000u | (u32)sf << 31 | rm << 16 | op2 << 10 | rn << 5 | rd);
}
static inline void csel(int sf, int rd, int rn, int rm, int cond) {
    E(0x1A800000u | (u32)sf << 31 | rm << 16 | cond << 12 | rn << 5 | rd);
}
static inline void csinc(int sf, int rd, int rn, int rm, int cond) {
    E(0x1A800400u | (u32)sf << 31 | rm << 16 | cond << 12 | rn << 5 | rd);
}
static inline void csinv(int sf, int rd, int rn, int rm, int cond) {
    E(0x5A800000u | (u32)sf << 31 | rm << 16 | cond << 12 | rn << 5 | rd);
}
static inline void cset(int rd, int cond) { csinc(1, rd, XZR, XZR, cond ^ 1); }
static inline void cfinv(void) { E(0xD500401Fu); }
static inline void rmif(int rn, int shift, int mask) { E(0xBA000400u | shift << 15 | rn << 5 | mask); }
static inline void setf8(int rn) { E(0x3A00080Du | rn << 5); }
static inline void setf16(int rn) { E(0x3A00480Du | rn << 5); }
static inline void axflag(void) { E(0xD500405Fu); }
static inline void mrs_nzcv(int rt) { E(0xD53B4200u | rt); }
static inline void msr_nzcv(int rt) { E(0xD51B4200u | rt); }
static inline void svc(u32 imm) { E(0xD4000001u | imm << 5); }
static inline void cbz(int nz, int sf, int rt, int words) {
    E((u32)sf << 31 | 0x34000000u | nz << 24 | ((u32)words & 0x7ffff) << 5 | rt);
}
static inline void tbz(int nz, int rt, int bit, int words) {
    E((u32)(bit >> 5) << 31 | 0x36000000u | nz << 24 | (bit & 31) << 19 | ((u32)words & 0x3fff) << 5 | rt);
}
static inline void b_cond(int cond, int words) { E(0x54000000u | ((u32)words & 0x7ffff) << 5 | cond); }
static inline void b_rel(int words) { E(0x14000000u | ((u32)words & 0x3ffffff)); }
static inline void bl_rel(int words) { E(0x94000000u | ((u32)words & 0x3ffffff)); }
static inline void br(int rn) { E(0xD61F0000u | rn << 5); }
static inline void blr(int rn) { E(0xD63F0000u | rn << 5); }
static inline void ret_reg(int rn) { E(0xD65F0000u | rn << 5); }
static inline void adr_words(int rd, int words) { // pc-relative, words forward or back
    i32 off = words * 4;
    E(0x10000000u | ((u32)off & 3) << 29 | (((u32)off >> 2) & 0x7ffff) << 5 | rd);
}
static inline void madd(int sf, int rd, int rn, int rm, int ra) {
    E(0x1B000000u | (u32)sf << 31 | rm << 16 | ra << 10 | rn << 5 | rd);
}
static inline void msub(int sf, int rd, int rn, int rm, int ra) {
    E(0x1B008000u | (u32)sf << 31 | rm << 16 | ra << 10 | rn << 5 | rd);
}
static inline void mul(int sf, int rd, int rn, int rm) { madd(sf, rd, rn, rm, XZR); }
static inline void smulh(int rd, int rn, int rm) { E(0x9B407C00u | rm << 16 | rn << 5 | rd); }
static inline void umulh(int rd, int rn, int rm) { E(0x9BC07C00u | rm << 16 | rn << 5 | rd); }
static inline void udiv(int sf, int rd, int rn, int rm) { E(0x1AC00800u | (u32)sf << 31 | rm << 16 | rn << 5 | rd); }
static inline void sdiv(int sf, int rd, int rn, int rm) { E(0x1AC00C00u | (u32)sf << 31 | rm << 16 | rn << 5 | rd); }
static inline void rbit(int sf, int rd, int rn) { E(0x5AC00000u | (u32)sf << 31 | rn << 5 | rd); }
static inline void clz(int sf, int rd, int rn) { E(0x5AC01000u | (u32)sf << 31 | rn << 5 | rd); }
static inline void rev16w(int rd, int rn) { E(0x5AC00400u | rn << 5 | rd); }
static inline void rev32(int sf, int rd, int rn) { E(sf ? 0xDAC00800u | rn << 5 | rd : 0x5AC00800u | rn << 5 | rd); }
static inline void rev64(int rd, int rn) { E(0xDAC00C00u | rn << 5 | rd); }
static inline void dmb(int opt) { E(0xD50330BFu | opt << 8); }
void movimm(int rd, u64 v);
bool logimm(u64 v, int sf, u32 *out);

// Addressing.
typedef struct {
    int base, index, shift;
    i64 imm;
    bool reg_form;
} Addr;
Addr addr_prepare(const Insn *in, int access);
// Leaves the effective address in AD (or returns a reg holding it).
int addr_reg(const Insn *in);
// opc: 0 store, 1 load, 2 signed load to 64, 3 signed load to 32. sz 1,2,4,8.
void ldst(int opc, int sz, int rt, Addr a);
static inline int log2sz(int sz) { return sz == 8 ? 3 : sz == 4 ? 2 : sz == 2 ? 1 : 0; }

// Exact load/store of guest GPRs (handles ah..bh).
int gpr_read(int reg, int sz, bool high, int scratch);
void gpr_write(int reg, int sz, bool high, int src);

// Pushes the condition on `reg`: nonzero iff cc holds, via branches-free flags. PF conditions use lazy state.
void emit_parity(int rd); // rd = 1 if PF set, from R_PF. Clobbers S4 and S5 unless rd is one of them.
void emit_stop(u64 rip, const char *why);

void emit_int(const Insn *in, int live); // every non-control x86 op
void emit_sse(const Insn *in, int live); // OP_SSE, ldmxcsr, stmxcsr
void emit_x87(const Insn *in); // OP_X87

// Stub entry points, relative to the stubs region. Filled in by the Swift loader from the monitor image's symbols.
extern u64 tr_stub_lookup, tr_stub_ret_miss, tr_stub_pushf, tr_stub_sahf;
extern int cur_part;
#define STUB(address) ((address) + (cur_part ? OFF_STUBS2 - OFF_STUBS : 0)) // the copy in reach of the part being written
