// Real CPL0 compatibility-mode table-register loads/stores. Six-byte inputs
// end at a mapped page boundary; an eight-byte base load MUST fault here.
#define kmain descriptor_unused_base_kmain
#define trap_dispatch descriptor_base_trap_dispatch
#include "kernel.c"
#undef trap_dispatch
#undef kmain

void trap_dispatch(Frame *frame) {
    if(frame->vector==128 && !(frame->cs&3) && frame->rax==77) {
        user_exit_status=77;return_from_user();
    }
    descriptor_base_trap_dispatch(frame);
}
extern void enter_descriptor32(u64 rip,u64 rsp);
__asm__(".text\n.globl _enter_descriptor32\n_enter_descriptor32:\n"
        "pushq %rbx\n pushq %rbp\n pushq %r12\n pushq %r13\n pushq %r14\n pushq %r15\n"
        "movq %rsp, %gs:16\n"
        "pushq $0x10\n pushq %rsi\n pushq $2\n pushq $0x18\n pushq %rdi\n iretq\n");
static void word32(u8 *p,u32 v) {for(u32 i=0;i<4;i++)p[i]=(u8)(v>>(i*8));}
static void pseudo(u64 frame,u16 limit,u32 base) {
    u8 *p=(u8 *)phys(frame)+4090;p[0]=(u8)limit;p[1]=(u8)(limit>>8);word32(p+2,base);
}
static u32 table_op(u8 *p,u8 modrm,u32 address,bool narrow) {
    u32 n=0;if(narrow)p[n++]=0x66;
    p[n++]=0x0f;p[n++]=0x01;p[n++]=modrm;word32(p+n,address);return n+4;
}
void kmain(u64 ram) {
    ram_size=ram;next_frame=32ULL<<20;setup_tables();
    gdt[3]=0x00CF9A000000FFFFULL; // flat CPL0 compatibility CS0x18
    memcpy(kernel_pml4,phys(read_cr3()&ADDR),4096);
    struct {u16 limit;u64 base;} __attribute__((packed)) saved_gdt,saved_idt;
    __asm__ volatile("sgdt %0\n sidt %1":"=m"(saved_gdt),"=m"(saved_idt));
    u64 root=new_space(),g=frame_alloc(),id=frame_alloc(),out=frame_alloc(),code=frame_alloc();
    memcpy(phys(g),gdt,sizeof gdt);memcpy(phys(id),idt,sizeof idt);memset(phys(out),0xa5,4096);
    map4k(root,0x10000000,g,P|W);map4k(root,0x00123000,g,P|W);
    map4k(root,0x10001000,id,P|W);map4k(root,0x00124000,id,P|W);
    map4k(root,0x10004000,out,P|W);map4k(root,0x20000000,code,P);
    map4k(root,0x6ffff000,frame_alloc(),P|W|NX);
    const u32 inputs[4]={0x10002ffa,0x10006ffa,0x10008ffa,0x1000affa};
    const u32 bases[4]={0x10000000,0x10001000,0xab123000,0xcd124000};
    for(u32 i=0;i<4;i++) {
        u64 f=frame_alloc();pseudo(f,(u16)(i&1?sizeof(idt)-1:sizeof(gdt)-1),bases[i]);
        map4k(root,inputs[i]&~0xfffULL,f,P|W);
    }
    u8 *p=(u8 *)phys(code);u32 n=0;
    for(u32 i=0;i<4;i++) {
        n+=table_op(p+n,(i&1)?0x1d:0x15,inputs[i],i>=2); // LIDT/LGDT
        n+=table_op(p+n,(i&1)?0x0d:0x05,0x10004000+i*16,false); // SIDT/SGDT
    }
    n+=table_op(p+n,0x15,inputs[0],false);n+=table_op(p+n,0x1d,inputs[1],false);
#ifdef TEST_KERNEL_FAR_JUMPS
    // The actual Leopard trampoline forms: compatibility EA ptr16:32 into
    // long mode, followed by RIP-relative FF/5 ptr16:32 back to compatibility.
    // No far calls, stack synthesis or generic gate support is involved.
    p[n++]=0xb9;word32(p+n,1024);n+=4;
    u32 jump=0x20000000+n;
    p[n++]=0x31;p[n++]=0xc0;p[n++]=0xf9; // ZF=CF=1 before each round trip
    p[n++]=0xea;word32(p+n,0x20000100);n+=4;p[n++]=8;p[n++]=0;
    n=256;p[n++]=0x75;p[n++]=46;p[n++]=0x73;p[n++]=44;
#ifdef TEST_KERNEL_FAR_NULL_SS
    // Real early-boot form: null SS is legal in long mode. The destination
    // immediately restores SS before touching the stack. Keep this path owned
    // by TCG until the Engine can represent the intervening segment state.
    p[n++]=0xb8;word32(p+n,0);n+=4;p[n++]=0x8e;p[n++]=0xd0;
#endif
    p[n++]=0xff;p[n++]=0x2d;word32(p+n,0);n+=4;
    word32(p+n,0x20000200);n+=4;p[n++]=0x18;p[n++]=0;
    p[304]=0x0f;p[305]=0x0b; // flag failure in long mode
    n=512;
#ifdef TEST_KERNEL_FAR_NULL_SS
    p[n++]=0xb8;word32(p+n,0x10);n+=4;p[n++]=0x8e;p[n++]=0xd0;
#endif
    p[n++]=0x75;p[n]=(u8)(560-(n+1));n++;
    p[n++]=0x73;p[n]=(u8)(560-(n+1));n++;
    p[560]=0x0f;p[561]=0x0b; // flag failure in compatibility mode
    p[n++]=0x49;p[n++]=0x0f;p[n++]=0x85;word32(p+n,jump-(0x20000000+n+4));n+=4;
#endif
    p[n++]=0xb8;word32(p+n,77);n+=4;p[n++]=0xcd;p[n++]=0x80;p[n++]=0x0f;p[n++]=0x0b;
    switch_to(root,0,false);user_exit_status=~0ULL;
    enter_descriptor32(0x20000000,0x70000000-64);
    __asm__ volatile("lgdt %0\n lidt %1"::"m"(saved_gdt),"m"(saved_idt));
    CHECK(user_exit_status==77,"descriptor32 exit %x",user_exit_status);
    for(u32 i=0;i<4;i++) {
        u8 *r=(u8 *)phys(out)+i*16;u32 base;memcpy(&base,r+2,4);
        u32 expected=i<2?bases[i]:bases[i]&0xffffff;
        CHECK(base==expected,"descriptor %d base %x expected %x",(u64)i,(u64)base,(u64)expected);
        CHECK(*(u16 *)r==(u16)(i&1?sizeof(idt)-1:sizeof(gdt)-1),"descriptor limit mismatch");
        for(u32 j=6;j<16;j++)CHECK(r[j]==0xa5,"compatibility table store overwrote canary");
    }
    kprintf("PASS CPL0 LGDT/LIDT32 page-end reads, 16-bit base masking, six-byte SGDT/SIDT writes; %d failures\n",(u64)fails);
#ifdef TEST_KERNEL_FAR_JUMPS
    puts_("PASS 1024 CPL0 EA/FF5 ptr16:32 compatibility/long-mode jump round trips\n");
#endif
#ifdef TEST_KERNEL_FAR_NULL_SS
    puts_("PASS null-SS transition restores a valid compatibility stack\n");
#endif
    kexit(fails?1:0);
}
