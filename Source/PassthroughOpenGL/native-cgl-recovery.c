/* Lion/Mountain Lion/Mavericks/Yosemite's multi-GLI-engine context path leaves a one-record
 * hole between engines, although it allocates only the sum of screen counts.
 * A guest LLDB hardware watchpoint caught the second screen overwriting the
 * first engine's procedure table. Changing the measured index increment from
 * last+2 to last+1 makes the isolated two-engine create/destroy test pass.
 * Patch only this process's COW code; no on-disk framework/cache modification.
 * Unknown implementations fail closed instead of guessing instruction offsets. */
#include <mach/mach.h>
#include <dlfcn.h>
#include <mach-o/loader.h>
#include <libkern/OSCacheControl.h>
static unsigned vim_native_dispatch_count=VIM_GLI_DISPATCH_COUNT;
#include "rosetta-cgl-recovery.c"
static pthread_once_t vim_recovery_once=PTHREAD_ONCE_INIT;
static int vim_recovery_ready;
static void VIMFixNativeRecovery(void)
{
    const char *stage="loaded library";
    kern_return_t protection=KERN_SUCCESS;
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
    if(VIMTigerIsRosetta()){
        vim_recovery_ready=VIMFixRosettaRecovery(&stage,&protection);
        if(!vim_recovery_ready)goto failed;
        return;
    }
    if(!dladdr||!vm_region||!vm_protect){stage="native Intel image APIs";goto failed;}
#endif
#if defined(__i386__) && VIM_GLI_DISPATCH_COUNT == 757
    int native=1;size_t native_size=sizeof(native);
    if(!sysctlbyname("sysctl.proc_native",&native,&native_size,NULL,0)&&native_size==sizeof(native)&&native==0){
        vim_recovery_ready=VIMFixRosettaRecovery(&stage,&protection);
        if(!vim_recovery_ready)goto failed;
        return;
    }
#endif
    // 10.8 WindowServer initializes GLI through CoreGraphics' cgls entry
    // points BEFORE the public OpenGL image is loaded. NOLOAD therefore
    // rejects the engine only in the compositor, while app probes pass.
    // Load the native public image once, without initializing a CGL context;
    // then apply the same bounded compatibility guard used by app clients.
    void *library=dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL",RTLD_LAZY|RTLD_LOCAL
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1050
        |RTLD_FIRST
#endif
        );
    unsigned char *function=library ? dlsym(library,"CGLCreateContext") : NULL;
    Dl_info info;
    if(!library){
        const char *error=dlerror();
        syslog(LOG_ERR,"VIM PassthroughOpenGL: native OpenGL image open failed: %s",error?error:"no loader detail");
        stage="native OpenGL image open";goto failed;
    }
    if(!function){
        const char *error=dlerror();
        syslog(LOG_ERR,"VIM PassthroughOpenGL: native CGLCreateContext lookup failed: %s",error?error:"no loader detail");
        stage="native CGLCreateContext lookup";goto failed;
    }
    if(!dladdr(function,&info)){stage="native CGLCreateContext image identity";goto failed;}
    stage="Mach-O text bounds";
#if __LP64__
    typedef struct mach_header_64 VIMMachHeader;
    typedef struct segment_command_64 VIMSegment;
    typedef struct section_64 VIMSection;
    const uint32_t magic=MH_MAGIC_64,segment_command=LC_SEGMENT_64;
#else
    typedef struct mach_header VIMMachHeader;
    typedef struct segment_command VIMSegment;
    typedef struct section VIMSection;
    const uint32_t magic=MH_MAGIC,segment_command=LC_SEGMENT;
#endif
    const VIMMachHeader *header=info.dli_fbase;
    if(header->magic!=magic) goto failed;
    const struct load_command *command=(void *)(header+1);
    const char *commands_end=(const char *)command+header->sizeofcmds;
    uintptr_t text=0,end=0;
    for(uint32_t i=0;i<header->ncmds;++i) {
        if((const char *)command>commands_end-sizeof(*command) ||
           command->cmdsize<sizeof(*command) ||
           command->cmdsize>(size_t)(commands_end-(const char *)command)) goto failed;
        if(command->cmd==segment_command) {
            const VIMSegment *segment=(void *)command;
            if(command->cmdsize<sizeof(*segment) ||
               segment->nsects>(command->cmdsize-sizeof(*segment))/sizeof(VIMSection)) goto failed;
            if(!strcmp(segment->segname,SEG_TEXT)) {
                intptr_t slide=(intptr_t)header-(intptr_t)segment->vmaddr;
                const VIMSection *section=(void *)(segment+1);
                for(uint32_t j=0;j<segment->nsects;++j)
                    if(!strcmp(section[j].sectname,SECT_TEXT)) {
                        text=slide+section[j].addr;end=text+section[j].size;
                    }
            }
        }
        command=(void *)((const char *)command+command->cmdsize);
    }
    if((uintptr_t)function<text || (uintptr_t)function>=end) goto failed;
    size_t length=end-(uintptr_t)function;if(length>4096) length=4096;
#if (VIM_GLI_DISPATCH_COUNT == 686 || VIM_GLI_DISPATCH_COUNT == 759 || VIM_GLI_DISPATCH_COUNT == 764 || VIM_GLI_DISPATCH_COUNT == 757) && !__LP64__
    // 10K549 i386 splits allocation into a private regparm helper. Follow
    // only the uniquely matched call in public CGLCreateContext and require
    // its destination to remain inside this exact Mach-O text section.
#if VIM_GLI_DISPATCH_COUNT == 686
    // Actual8R3032 CGLCreateContext passes the native format/share/out in
    // EAX/EDX/ECX to cglInitializeContext. Follow only this unique call.
    static const unsigned char call_prefix[]={0x8d,0x4d,0xe0,0x8b,0x55,0x0c,0x8b,0x45,0x08,0xe8};
#elif VIM_GLI_DISPATCH_COUNT == 759
    /* Actual8R4088: its later table still uses Tiger's six-argument engine
     * initialization but CGL holds the selected format in EDI at this call. */
    static const unsigned char call_prefix[]={0x8d,0x4d,0xe0,0x8b,0x55,0x0c,0x89,0xf8,0xe8};
#elif VIM_GLI_DISPATCH_COUNT == 757
    // Native 9L30 CGLCreateContext uses its regparm cglInitializeContext.
    static const unsigned char call_prefix[]={0x8b,0x55,0x0c,0x8d,0x4d,0xe0,0x89,0xf8,0xe8};
#else
    static const unsigned char call_prefix[]={0x8d,0x4d,0xdc,0x8b,0x55,0x0c,0x89,0xf0,0xe8};
#endif
    unsigned calls=0;unsigned char *allocation=NULL;
    stage="native allocation-helper call match";
    struct {const unsigned char *bytes;size_t size;unsigned slots;} helpers[]={
        {call_prefix,sizeof(call_prefix),VIM_GLI_DISPATCH_COUNT}
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040 && VIM_GLI_DISPATCH_COUNT == 759
        /* The native8S2167 combo reverts to the SDK's686-slot layout. Its
         * six-argument GLI boundary and first686 named slots are identical;
         * select that bounded prefix from the actual public CGL helper. */
        ,{(const unsigned char[]){0x8d,0x4d,0xe0,0x8b,0x55,0x0c,0x8b,0x45,0x08,0xe8},10,686}
#endif
    };
    for(size_t h=0;h<sizeof(helpers)/sizeof(helpers[0]);++h)
        for(size_t i=0;i+helpers[h].size+4<=length;++i)if(!memcmp(function+i,helpers[h].bytes,helpers[h].size)){
            int32_t displacement;memcpy(&displacement,function+i+helpers[h].size,4);
            allocation=function+i+helpers[h].size+4+displacement;
            vim_native_dispatch_count=helpers[h].slots;++calls;
        }
    if(calls!=1||(uintptr_t)allocation<text||(uintptr_t)allocation>=end)goto failed;
    function=allocation;length=end-(uintptr_t)function;if(length>4096)length=4096;
#endif
#if __LP64__
    /* INC ESI; ADD R12D,2; load next engine offset; INC RDX; compare engine count. */
    static const unsigned char pattern[]={0xff,0xc6,0x41,0x83,0xc4,0x02,
        0x48,0x8b,0x95,0xf8,0xfe,0xff,0xff,0x48,0xff,0xc2,
        0x3b,0x95,0x20,0xff,0xff,0xff};
    const size_t increment=5;
    /* 12F2560 x64 CGLCreateContext: inner screen index already reached last+1;
     * outer loop reconstructs last+2 in EAX, then stores it in R15D. Verified
     * against the decompiled 972-entry record path, not a guessed OS offset. */
    static const unsigned char earlier[]={0xff,0xc6,0x8b,0x8d,0x04,0xff,0xff,0xff,
        0xff,0xc1,0x48,0x8b,0x95,0xf0,0xfe,0xff,0xff,0x48,0xff,0xc2,
        0x83,0xc0,0x02,0x41,0x89,0xc7,0x89,0x8d,0x04,0xff,0xff,0xff};
    const size_t earlier_increment=22;
    /* 11G63: screen ECX/R12D already denotes the next free screen. The
     * outer loop adds one again with LEA before advancing the 968-slot
     * engine record. Remove that extra one, not the inner screen count. */
    static const unsigned char lion[]={0x44,0x8b,0xa5,0xdc,0xfe,0xff,0xff,
        0x45,0x8d,0x64,0x04,0x01,0x44,0x89,0xa5,0xdc,0xfe,0xff,0xff,
        0x48,0x81,0xc3,0x58,0x1e,0x00,0x00};
    const size_t lion_increment=11;
    /* Actual14F27 x64: the inner loop advances R12+1, but the outer
     * loop reconstructs R12+2. Independently decompiled on the Intel
     * reference with the native974-slot engine record. */
    static const unsigned char yosemite[]={0x49,0x83,0xc4,0x02,
        0x48,0x8b,0x8d,0xf0,0xfe,0xff,0xff,0x48,0xff,0xc1,
        0x48,0x8b,0x95,0xd8,0xfe,0xff,0xff,0xff,0xc2,0x44,0x39,0xd1};
    const size_t yosemite_increment=3;
    /* Actual15G31: the completed inner screen loop computes R12+1 at
     * 4bf9; the outer loop then reconstructs R12+2. Keep the engine INC
     * and comparison, correcting only that duplicated screen increment. */
    static const unsigned char elcapitan[]={0x49,0x83,0xc4,0x02,0x49,0xff,0xc6,
        0x48,0x8b,0x95,0xe0,0xfe,0xff,0xff,0xff,0xc2,0x4d,0x39,0xc6};
    const size_t elcapitan_increment=3;
    /* Actual16G2136: the completed inner loop computes RBX+1 at4c18,
     * then the engine loop reconstructs RBX+2. Preserve R14's engine
     * increment and the native974-slot stride; correct only the hole. */
    static const unsigned char sierra[]={0x48,0x83,0xc3,0x02,0x49,0xff,0xc6,
        0x48,0x8b,0x95,0xf8,0xfe,0xff,0xff,0xff,0xc2,0x4d,0x39,0xce};
    const size_t sierra_increment=3;
    /* Actual17G14033: inner LEA RDX,[R15+1] at4fac is followed by
     * outer ADD R15,2 at4fb9. Keep the native engine/count increments. */
    static const unsigned char highsierra[]={0x49,0x83,0xc7,0x02,
        0x48,0x8b,0x95,0x28,0xff,0xff,0xff,0x48,0xff,0xc2,
        0x48,0x8b,0xb5,0xd8,0xfe,0xff,0xff,0xff,0xc6,0x4c,0x39,0xc2};
    const size_t highsierra_increment=3;
#else
    /* 13F1911 i386 CGLCreateContext +0x73c: INC ECX; ADD EDX,2;
     * load/increment next engine; compare count; move screen index to EDI.
     * The inner screen loop has already advanced EDI to last+1, but this
     * ADD reconstructs last+2 from EDX. Same one-record hole as x86_64. */
    static const unsigned char pattern[]={0x41,0x83,0xc2,0x02,
        0x8b,0xb5,0x28,0xff,0xff,0xff,0x46,
        0x8b,0x85,0x34,0xff,0xff,0xff,0x39,0xc6,0x89,0xc8,0x89,0xd7};
    const size_t increment=3;
    /* 12F2560 i386: increment engine EAX/ESI; ADD EDX,2; restore screen EDI.
     * Same independently decompiled hole with the 972-entry GLI stride. */
    static const unsigned char earlier[]={0x8b,0x85,0x14,0xff,0xff,0xff,0x40,
        0x8b,0xb5,0x20,0xff,0xff,0xff,0x46,0x83,0xc2,0x02,
        0x89,0xc1,0x89,0xd7,0x3b,0xb5,0x38,0xff,0xff,0xff};
    const size_t earlier_increment=16;
    /* Same native 11G63 loop in i386: LEA EDI,[EDI+ECX+1] follows the
     * completed inner screen loop, then advances the 0xf2c engine record. */
    static const unsigned char lion[]={0x8b,0xbd,0x14,0xff,0xff,0xff,
        0x8d,0x7c,0x0f,0x01,0x89,0xbd,0x14,0xff,0xff,0xff,
        0x81,0x85,0x30,0xff,0xff,0xff,0x2c,0x0f,0x00,0x00};
    const size_t lion_increment=9;
    /* Same measured14F27 native hole in i386. Preserve both engine
     * increments and the completed inner screen list. */
    static const unsigned char yosemite[]={0x83,0xc7,0x02,
        0x8b,0x8d,0x28,0xff,0xff,0xff,0x41,
        0x8b,0x85,0x14,0xff,0xff,0xff,0x40,
        0x3b,0x8d,0x2c,0xff,0xff,0xff,0x89,0xfe};
    const size_t yosemite_increment=2;
    /* Same15G31 i386 loop: inner LEA EDI,[EDX+1], then outer ADD EDX,2.
     * The0xf44 engine records and actual engine count remain unchanged. */
    static const unsigned char elcapitan[]={0x83,0xc2,0x02,
        0x8b,0xb5,0x28,0xff,0xff,0xff,0x46,
        0x8b,0x85,0x18,0xff,0xff,0xff,0x40,
        0x8b,0x8d,0x30,0xff,0xff,0xff,0x39,0xce,0x89,0xd7};
    const size_t elcapitan_increment=2;
    /* Same16G2136 i386 loop: inner LEA EDI,[EDX+1] at4c99, followed
     * by outer ADD EDX,2. Keep both engine/count updates unchanged. */
    static const unsigned char sierra[]={0x83,0xc2,0x02,
        0x8b,0xb5,0x40,0xff,0xff,0xff,0x46,
        0x8b,0x85,0x2c,0xff,0xff,0xff,0x40,
        0x8b,0x8d,0x48,0xff,0xff,0xff,0x39,0xce,0x89,0xd7};
    const size_t sierra_increment=2;
    /* Same17G14033 i386 loop: inner LEA EDI,[EDX+1] at4fe5, outer
     * ADD EDX,2 at4fee. Preserve both engine counters and0xf44 stride. */
    static const unsigned char highsierra[]={0x83,0xc2,0x02,
        0x8b,0x8d,0x40,0xff,0xff,0xff,0x41,
        0x8b,0x85,0x24,0xff,0xff,0xff,0x40,
        0x3b,0x8d,0x50,0xff,0xff,0xff,0x89,0xd7};
    const size_t highsierra_increment=2;
#if VIM_GLI_DISPATCH_COUNT == 975
    /* Actual18G9323 i386: inner LEA EBX,[EAX+1] has advanced the screen
     * count, then the outer ADD EAX,2 reconstructs last+2. Ghidra and the
     * native disassembly agree on the0xf48 record stride. Correct only the
     * duplicate increment; retain both engine counters and their comparison.
     * Search this instruction shape, never a build-specific file offset. */
    static const unsigned char mojave[]={0x83,0xc0,0x02,
        0x8b,0xb5,0x40,0xff,0xff,0xff,0x46,0x89,0xc2,
        0x8b,0x85,0x20,0xff,0xff,0xff,0x40,0x89,0xd3,
        0x3b,0xb5,0x58,0xff,0xff,0xff};
    const size_t mojave_increment=2;
#endif
#endif
#if VIM_GLI_DISPATCH_COUNT == 764
#if __LP64__
    // Inner screen loop already incremented EDI. The second INC (not the
    // inner one) creates the same one-record hole seen on later releases.
    static const unsigned char snow[]={0xff,0xc7,0x48,0x8b,0x1b,0x49,0x83,0xc0,0x18,
        0x48,0x85,0xdb,0x75,0xa4,0xff,0xc7,0x89,0xbd,0x0c,0xff,0xff,0xff,
        0xff,0x85,0x08,0xff,0xff,0xff,0xff,0x85,0xf4,0xfe,0xff,0xff};
    const size_t snow_increment=14,snow_width=2;
#else
    static const unsigned char snow[]={0x8b,0x95,0x3c,0xff,0xff,0xff,0x42,
        0x89,0x95,0x40,0xff,0xff,0xff,0xff,0x85,0x38,0xff,0xff,0xff,
        0xff,0x85,0x18,0xff,0xff,0xff};
    const size_t snow_increment=6,snow_width=1;
#endif
#endif
#if VIM_GLI_DISPATCH_COUNT == 757
#if __LP64__
    // 9L30: the inner loop has already advanced EDI. Its outer second INC
    // creates a hole beyond the sum-of-screens allocation (Ghidra + native
    // instruction control); NOP only that second INC, not the engine count.
    static const unsigned char leopard[]={0xff,0x45,0x98,0xff,0xc7,
        0xff,0x85,0x78,0xff,0xff,0xff,0x89,0x7d,0x9c,
        0x48,0x81,0x45,0x90,0xc0,0x17,0x00,0x00};
    const size_t leopard_increment=3,leopard_width=2;
    const unsigned char leopard_replacement=0x90;
#else
    // The 32-bit helper accumulates the already-incremented count then adds
    // one again in LEA EDX,[EDX+ECX+1]. Preserve its count and 0xbe0 stride.
    static const unsigned char leopard[]={0x81,0x85,0x78,0xff,0xff,0xff,0xe0,0x0b,0x00,0x00,
        0xff,0x45,0x9c,0x39,0x75,0xa4,0x8d,0x54,0x0a,0x01,0x89,0x55,0x98};
    const size_t leopard_increment=19,leopard_width=1;
    const unsigned char leopard_replacement=0;
#endif
#endif
#if VIM_GLI_DISPATCH_COUNT == 686 || VIM_GLI_DISPATCH_COUNT == 759
    /* Native8R3032: the inner loop increments localC4, then the outer LEA
     * adds one again. Preserve native counts and the0xac4 engine stride. */
    static const unsigned char tiger686[]={0x8b,0x55,0xc4,0x8b,0x4d,0x98,0x8d,0x54,0x0a,0x01,
        0x89,0x55,0x98,0x83,0x45,0xc0,0x01,0x83,0x85,0x74,0xff,0xff,0xff,0x3c,
        0x83,0x85,0x78,0xff,0xff,0xff,0x3c,0x81,0x45,0xb4,0xc4,0x0a,0x00,0x00};
    /* Actual8R4088 cglInitializeContext: inner count localC4 already
     * advanced; remove only the outer LEA's extra1. Keep stride0xbe8. */
    static const unsigned char tiger759[]={0x8b,0x55,0xc4,0x8b,0x4d,0x94,0x8d,0x54,0x0a,0x01,
        0x89,0x55,0x94,0x83,0x45,0xc0,0x01,0x83,0x45,0x98,0x01,0x81,0x45,0xb4,0xe8,0x0b,0,0};
    const unsigned char *tiger=vim_native_dispatch_count==686?tiger686:tiger759;
    size_t tiger_size=vim_native_dispatch_count==686?sizeof(tiger686):sizeof(tiger759);
#endif
    struct {const unsigned char *bytes;size_t size,increment;unsigned char replacement;size_t width;} variants[]={
        {pattern,sizeof(pattern),increment,1,1},{earlier,sizeof(earlier),earlier_increment,1,1},
        {lion,sizeof(lion),lion_increment,0,1},
        {yosemite,sizeof(yosemite),yosemite_increment,1,1},
        {elcapitan,sizeof(elcapitan),elcapitan_increment,1,1},
        {sierra,sizeof(sierra),sierra_increment,1,1},
        {highsierra,sizeof(highsierra),highsierra_increment,1,1}
#if !__LP64__ && VIM_GLI_DISPATCH_COUNT == 975
        ,{mojave,sizeof(mojave),mojave_increment,1,1}
#endif
#if VIM_GLI_DISPATCH_COUNT == 764
        ,{snow,sizeof(snow),snow_increment,0x90,snow_width}
#endif
#if VIM_GLI_DISPATCH_COUNT == 757
        ,{leopard,sizeof(leopard),leopard_increment,leopard_replacement,leopard_width}
#endif
#if VIM_GLI_DISPATCH_COUNT == 686 || VIM_GLI_DISPATCH_COUNT == 759
        ,{tiger,tiger_size,9,0,1}
#endif
    };
    unsigned char *match=NULL,replacement=0;unsigned matches=0;size_t width=0;
    for(size_t variant=0;variant<sizeof(variants)/sizeof(variants[0]);++variant)
        for(size_t i=0;i+variants[variant].size<=length;++i)
            if(!memcmp(function+i,variants[variant].bytes,variants[variant].size)) {
                match=function+i+variants[variant].increment;
                replacement=variants[variant].replacement;width=variants[variant].width;++matches;
            }
    stage="instruction match";
    if(matches!=1) goto failed;
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
    /* Rosetta's native GL loader does not expose arbitrary libSystem data.
     * Ask the period API rather than binding vm_page_size across that seam. */
    int native_page_size=getpagesize();
    if(native_page_size<=0||(native_page_size&(native_page_size-1)))goto failed;
    const vm_size_t protection_page_size=(vm_size_t)native_page_size;
#else
    const vm_size_t protection_page_size=vm_page_size;
#endif
    vm_address_t page=(vm_address_t)match&~((vm_address_t)protection_page_size-1);
    vm_size_t span=(((vm_address_t)match+width+protection_page_size-1)&~((vm_address_t)protection_page_size-1))-page;
    vm_prot_t original_protection=VM_PROT_READ|VM_PROT_EXECUTE;
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
    /* Native8R4088 maps the split-library submap READ/max READ even for
     * executable i386 text. COPY grants private WRITE, not a new EXECUTE
     * maximum: requesting invented RWX fails with KERN_PROTECTION_FAILURE.
     * Query the actual containing region and restore its exact permissions.
     * The own-process protection probe verifies COPY|RW and restoration on
     * that native mapping. Do not change later systems' qualified path. */
    vm_address_t region=page;vm_size_t region_size=0;
    vm_region_basic_info_data_t region_info={0};
    mach_msg_type_number_t region_count=VM_REGION_BASIC_INFO_COUNT;
    mach_port_t region_object=MACH_PORT_NULL;
    stage="native code page permissions";
    protection=vm_region(mach_task_self(),&region,&region_size,VM_REGION_BASIC_INFO,
        (vm_region_info_t)&region_info,&region_count,&region_object);
    if(region_object!=MACH_PORT_NULL)mach_port_deallocate(mach_task_self(),region_object);
    if(protection!=KERN_SUCCESS||region>page||region_size<span||
       page-region>region_size-span||!(region_info.protection&VM_PROT_READ))goto failed;
    original_protection=region_info.protection;
#endif
    stage="private writable code";
    protection=vm_protect(mach_task_self(),page,span,FALSE,
        VM_PROT_COPY|original_protection|VM_PROT_WRITE);
    if(protection!=KERN_SUCCESS) goto failed;
    memset(match,replacement,width);
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
    // Tiger has no public sys_icache_invalidate API. Native x86 instruction
    // caches are coherent; serialize this CPU after the private code write.
    // This is NOT sufficient for Rosetta's separate translated-code cache,
    // which is deliberately excluded above until its BDL path is measured.
    unsigned a,b,c,d;
    __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"0"(0):"memory");
#else
    sys_icache_invalidate(match,width);
#endif
    stage="restore executable code";
    protection=vm_protect(mach_task_self(),page,span,FALSE,original_protection);
    if(protection!=KERN_SUCCESS) goto failed;
    vim_recovery_ready=1;return;
failed:
    syslog(LOG_ERR,"VIM PassthroughOpenGL: native multi-engine recovery fix unavailable at %s (Mach %d); declining engine initialization",stage,protection);
}
