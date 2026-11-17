// Focused real Engine fixture: shared source code must select each address
// space's private destination, including after remaps, permission changes/SMC.
#define kmain lookup_unused_base_kmain
#define trap_dispatch lookup_base_trap_dispatch
#include "kernel.c"
#undef trap_dispatch
#undef kmain

void trap_dispatch(Frame *frame) {
    if (frame->vector == 128 && (frame->cs & 3)) {
        user_exit_status = frame->rax;
        return_from_user();
    }
    lookup_base_trap_dispatch(frame);
}

extern void enter_lookup_compat(u64 rip, u64 rsp);
__asm__(".text\n.globl _enter_lookup_compat\n_enter_lookup_compat:\n"
        "pushq %rbx\n pushq %rbp\n pushq %r12\n pushq %r13\n pushq %r14\n pushq %r15\n"
        "movq %rsp, %gs:16\n"
        "movw $0x23, %ax\n movw %ax, %ds\n movw %ax, %es\n"
        "pushq $0x23\n pushq %rsi\n pushq $2\n pushq $0x1b\n pushq %rdi\n"
        "swapgs\n iretq\n");

static void word32(u8 *p, u32 word) { for (u32 i=0;i<4;i++) p[i]=(u8)(word>>(i*8)); }
static u32 exit_bytes(u8 *p, bool compat, bool function) {
    if (function) { p[0]=0xc3; return 1; }
    if (compat) { p[0]=0xcd;p[1]=0x80;p[2]=0x0f;p[3]=0x0b;return 4; }
    p[0]=0x89;p[1]=0xc7;p[2]=0xb8;word32(p+3,1);
    p[7]=0x0f;p[8]=0x05;p[9]=0x0f;p[10]=0x0b;return 11;
}
static void destination(u64 frame, u32 value, bool compat, bool function) {
    u8 *p=(u8 *)phys(frame);
    // Both ZF and CF must survive the lookup, in addition to the GPR result.
    p[0]=0x75;p[2]=0x73;p[4]=0xb8;word32(p+5,value);
    u32 fail=9+exit_bytes(p+9,compat,function);
    p[1]=(u8)(fail-2);p[3]=(u8)(fail-4);
    p[fail]=0xb8;word32(p+fail+1,99);exit_bytes(p+fail+5,compat,function);
}
static void source_code(u64 frame, u64 va, u64 target, u32 kind, bool compat) {
    u8 *p=(u8 *)phys(frame);
    p[0]=0x31;p[1]=0xc0;p[2]=0xf9; // xor eax,eax; stc: ZF=CF=1
    if (kind==1) { p[3]=0x0f;p[4]=0x84;word32(p+5,(u32)(target-va-9)); }
    else { p[3]=kind==2?0xe8:0xe9;word32(p+4,(u32)(target-va-8)); }
    if(kind==2)exit_bytes(p+8,compat,false);
}
static u64 *directory_entry(u64 root,u64 va) {
    u64 pdpt=phys(root)[(va>>39)&511]&ADDR;
    return phys(phys(pdpt)[(va>>30)&511]&ADDR)+((va>>21)&511);
}
static u64 execute_lookup(u64 root,u64 va,bool compat) {
    switch_to(root,0,false);user_exit_status=~0ULL;
    if(compat)enter_lookup_compat(va,0x70000000-64);
    else enter_user(va,USTACK_TOP-64,2);
    return user_exit_status;
}
static void exercise(bool compat) {
    u64 src=compat?0x20000000ULL:0x7FFF10000000ULL;
    u64 dst=compat?0x20400000ULL:0x7FFF50000000ULL;
    u64 roots[2]={new_space(),new_space()},frames[2][2];
    for(u32 r=0;r<2;r++) {
        if(compat)for(u32 p=1;p<=8;p++)map4k(roots[r],0x70000000-p*4096,frame_alloc(),P|W|U|NX);
        for(u32 f=0;f<2;f++) {
            frames[r][f]=frame_alloc();destination(frames[r][f],r?33:17,compat,f!=0);
            map4k(roots[r],dst+f*4096,frames[r][f],P|U);
        }
        // The opposite cache direction: private code jumps into shared code,
        // which then selects this root's private destination.
        for(u32 kind=0;kind<2;kind++) {
            u64 page=frame_alloc(),va=dst+(kind+2)*4096;
            source_code(page,va,src,kind,compat);
            map4k(roots[r],va,page,P|U);
        }
    }
    for(u32 kind=0;kind<3;kind++) {
        u64 page=frame_alloc();source_code(page,src+kind*4096,dst+(kind==2?4096:0),kind,compat);
        map4k(roots[0],src+kind*4096,page,P|U);
        map4k(roots[1],src+kind*4096,page,P|U);
    }
    if(compat)*directory_entry(roots[1],src)=*directory_entry(roots[0],src);
    else {
        u64 pa=phys(roots[0])[(src>>39)&511]&ADDR;
        u64 pb=phys(roots[1])[(src>>39)&511]&ADDR;
        phys(pb)[(src>>30)&511]=phys(pa)[(src>>30)&511];
    }
    for(u32 n=0;n<24;n++)for(u32 kind=0;kind<3;kind++)for(u32 r=0;r<2;r++)
        CHECK(execute_lookup(roots[r],src+kind*4096,compat)==(r?33:17),
              "lookup mode %d kind %d root %d",(u64)compat,(u64)kind,(u64)r);
    for(u32 n=0;n<24;n++)for(u32 kind=0;kind<2;kind++)for(u32 r=0;r<2;r++)
        CHECK(execute_lookup(roots[r],dst+(kind+2)*4096,compat)==(r?33:17),
              "private-to-shared mode %d root %d",(u64)compat,(u64)r);
    u64 replacement=frame_alloc();destination(replacement,49,compat,false);
    *pte_slot(roots[1],dst,false)=replacement|P|U;
    for(u32 kind=0;kind<2;kind++) {
        CHECK(execute_lookup(roots[1],src+kind*4096,compat)==49,"remapped target stale");
        CHECK(execute_lookup(roots[0],src+kind*4096,compat)==17,"other root target changed");
    }
    *pte_slot(roots[1],dst,false)=replacement|P|U|NX;
    CHECK(execute_lookup(roots[1],src,compat)==0x10e && (last_err&0x14)==0x14,"NX target did not fault");
    *pte_slot(roots[1],dst,false)=replacement|P|U;
    CHECK(execute_lookup(roots[1],src,compat)==49,"NX restore stale");
    destination(replacement,61,compat,false);
    CHECK(execute_lookup(roots[1],src,compat)==61,"SMC target stale");
    CHECK(execute_lookup(roots[0],src,compat)==17,"SMC leaked to other root");
    kprintf("PASS cross-key %d-bit jumps/calls, per-root targets, flags, remap, NX and SMC\n",compat?32ULL:64ULL);
}
void kmain(u64 ram) {
    ram_size=ram;next_frame=32ULL<<20;setup_tables();
    memcpy(kernel_pml4,phys(read_cr3()&ADDR),4096);fault_mode=M_EXIT_ON_FAULT;
    exercise(false);
    gdt[3]=0x00CFFA000000FFFFULL; // actual flat ring-3 compatibility CS 0x1b
    exercise(true);
    kprintf("cross-key fixture done, %d failures\n",(u64)fails);
    kexit(fails?1:0);
}
