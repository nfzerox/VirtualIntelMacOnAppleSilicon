/* Darwin9's native GLI runs inside Rosetta's restricted i386 environment.
 * Loading the Intel OpenGL image pulls Security against the intentionally
 * smaller CF shim. More importantly, it is the PPC CGL allocation loop that
 * must be repaired. Resolve that subject image through Apple's measured BDL
 * boundary, never load a second Intel framework or guess a Rosetta address. */
#if defined(__i386__) && (VIM_GLI_DISPATCH_COUNT == 757 || __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040)
#include <sys/sysctl.h>
struct VIMSubjectFunction {const char *name;uint32_t address;};
typedef uint32_t (*VIMSubjectLookup)(const char *);
typedef uint32_t (*VIMSubjectCall)(struct VIMSubjectFunction *,uint32_t,const uint32_t *);
static uint32_t VIMSubjectWord(const void *data)
{const unsigned char *p=data;return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
static void VIMSubjectPutWord(unsigned char *p,uint32_t word)
{p[0]=(unsigned char)(word>>24);p[1]=(unsigned char)(word>>16);p[2]=(unsigned char)(word>>8);p[3]=(unsigned char)word;}
#endif
static int VIMSubjectRead(uint32_t address,void *data,size_t size)
{
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
    if(!vm_read_overwrite){
        VIMSubjectLookup lookup=(VIMSubjectLookup)dlsym(RTLD_DEFAULT,"bdl_lookup_subject_symbol");
        VIMSubjectCall call=(VIMSubjectCall)dlsym(RTLD_DEFAULT,"bdl_call_subject_function_int");
        struct VIMSubjectFunction read={"vm_read_overwrite",lookup?lookup("vm_read_overwrite"):0};
        unsigned char copied[4]={0};
        uint32_t args[]={mach_task_self(),address,(uint32_t)size,(uint32_t)(uintptr_t)data,(uint32_t)(uintptr_t)copied};
        return address&&size<=UINT32_MAX-address&&read.address&&call&&
            call(&read,5,args)==KERN_SUCCESS&&VIMSubjectWord(copied)==size;
    }
#endif
    vm_size_t copied=0;
    return address&&size<=UINT32_MAX-address&&
        vm_read_overwrite(mach_task_self(),address,(vm_size_t)size,(vm_address_t)data,&copied)==KERN_SUCCESS&&copied==size;
}
static int VIMSubjectPatch(uint32_t address,const unsigned char *replacement,size_t size,
    VIMSubjectCall call,struct VIMSubjectFunction *flush,kern_return_t *protection)
{
    vm_size_t page_size;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
    int measured=getpagesize();if(measured<=0||(measured&(measured-1)))return 0;
    page_size=(vm_size_t)measured;
    VIMSubjectLookup lookup=(VIMSubjectLookup)dlsym(RTLD_DEFAULT,"bdl_lookup_subject_symbol");
    struct VIMSubjectFunction protect={"vm_protect",lookup?lookup("vm_protect"):0};
    if(!protect.address)return 0;
#else
    page_size=vm_page_size;
#endif
    vm_address_t page=address&~((vm_address_t)page_size-1);
    vm_size_t span=((address+size+page_size-1)&~((vm_address_t)page_size-1))-page;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
    /* Genuine10.4 SDK: vm_region/basic32 is seven arguments and eight
     * integer slots. All subject out words are PPC-endian. Tiger's split
     * library text can be READ/max READ, not RX; preserve its actual
     * protection just as the native Intel correction does. */
    struct VIMSubjectFunction region={"vm_region",lookup("vm_region")};
    unsigned char region_address[4],region_size[4]={0},info[sizeof(vm_region_basic_info_data_t)]={0},
        count[4],object[4]={0};
    VIMSubjectPutWord(region_address,page);VIMSubjectPutWord(count,VM_REGION_BASIC_INFO_COUNT);
    uint32_t region_args[]={mach_task_self(),(uint32_t)(uintptr_t)region_address,
        (uint32_t)(uintptr_t)region_size,VM_REGION_BASIC_INFO,(uint32_t)(uintptr_t)info,
        (uint32_t)(uintptr_t)count,(uint32_t)(uintptr_t)object};
    if(!region.address)return 0;
    *protection=(kern_return_t)call(&region,7,region_args);
    mach_port_t region_object=VIMSubjectWord(object);
    if(region_object)mach_port_deallocate(mach_task_self(),region_object);
    uint32_t region_start=VIMSubjectWord(region_address),region_bytes=VIMSubjectWord(region_size);
    vm_prot_t original=(vm_prot_t)VIMSubjectWord(info);
    if(*protection!=KERN_SUCCESS||VIMSubjectWord(count)<2||region_start>page||
       region_bytes<span||page-region_start>region_bytes-span||!(original&VM_PROT_READ))return 0;
    uint32_t protect_args[]={mach_task_self(),page,span,FALSE,VM_PROT_COPY|original|VM_PROT_WRITE};
    *protection=(kern_return_t)call(&protect,5,protect_args);
#else
    *protection=vm_protect(mach_task_self(),page,span,FALSE,VM_PROT_COPY|VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE);
#endif
    if(*protection!=KERN_SUCCESS)return 0;
    memcpy((void *)(uintptr_t)address,replacement,size);
    // Flush through the SUBJECT implementation: an Intel cache flush alone
    // does not establish invalidation of Rosetta's translated PPC blocks.
    uint32_t args[]={address,(uint32_t)size};call(flush,2,args);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
    protect_args[4]=original;
    *protection=(kern_return_t)call(&protect,5,protect_args);
#else
    *protection=vm_protect(mach_task_self(),page,span,FALSE,VM_PROT_READ|VM_PROT_EXECUTE);
#endif
    return *protection==KERN_SUCCESS;
}
#ifdef VIM_ROSETTA_RECOVERY_DIAGNOSTICS
static int VIMSubjectCodeFixture(VIMSubjectLookup lookup,VIMSubjectCall call,
    struct VIMSubjectFunction *flush,kern_return_t *protection)
{
    // Research-only own-function control. Execute before changing its actual
    // li r3,1; blr, then require translated execution to observe 2 and 1 again.
    struct VIMSubjectFunction probe={"VIMRosettaCodeUpdateProbe",lookup("VIMRosettaCodeUpdateProbe")};
    const unsigned char original[]={0x38,0x60,0,1,0x4e,0x80,0,0x20};
    const unsigned char changed[]={0x38,0x60,0,2};unsigned char code[8];
    if(!probe.address||!VIMSubjectRead(probe.address,code,sizeof(code))||memcmp(code,original,sizeof(code))||call(&probe,0,NULL)!=1)return 0;
    if(!VIMSubjectPatch(probe.address,changed,sizeof(changed),call,flush,protection))return 0;
    int changed_result=call(&probe,0,NULL)==2;
    int restored=VIMSubjectPatch(probe.address,original,4,call,flush,protection);
    int good=changed_result&&restored&&call(&probe,0,NULL)==1;
    syslog(LOG_NOTICE,"VIM PassthroughOpenGL: Rosetta subject code-cache fixture %s",good?"PASS":"FAIL");
    return good;
}
#endif
static int VIMFixRosettaRecovery(const char **stage,kern_return_t *protection)
{
    *stage="Rosetta subject boundary";
    VIMSubjectLookup lookup=(VIMSubjectLookup)dlsym(RTLD_DEFAULT,"bdl_lookup_subject_symbol");
    VIMSubjectCall call=(VIMSubjectCall)dlsym(RTLD_DEFAULT,"bdl_call_subject_function_int");
    if(!lookup||!call)return 0;
    struct VIMSubjectFunction describe={"dladdr",lookup("dladdr")};
    struct VIMSubjectFunction flush={"sys_icache_invalidate",lookup("sys_icache_invalidate")};
    uint32_t function=lookup("CGLCreateContext");
    if(!describe.address||!flush.address||!function||(function&3))return 0;
    // PPC Dl_info is four big-endian pointers, not native Intel Dl_info.
    unsigned char identity[16]={0};uint32_t args[]={function,(uint32_t)(uintptr_t)identity};
    *stage="Rosetta subject image identity";
    if(!call(&describe,2,args))return 0;
    uint32_t base=VIMSubjectWord(identity+4);unsigned char header[28];
    if(!VIMSubjectRead(base,header,sizeof(header))||VIMSubjectWord(header)!=MH_MAGIC||
       VIMSubjectWord(header+4)!=CPU_TYPE_POWERPC||VIMSubjectWord(header+12)!=MH_DYLIB)return 0;
    uint32_t count=VIMSubjectWord(header+16),bytes=VIMSubjectWord(header+20);
    if(!count||count>256||bytes<8||bytes>65536||bytes>UINT32_MAX-base-sizeof(header))return 0;
    unsigned char *commands=malloc(bytes);if(!commands)return 0;
    int valid=VIMSubjectRead(base+sizeof(header),commands,bytes),identified=0;uint32_t text=0,end=0;
    size_t cursor=0;*stage="Rosetta subject text bounds";
    for(uint32_t i=0;valid&&i<count;i++){
        if(cursor>bytes-8){valid=0;break;}
        const unsigned char *command=commands+cursor;uint32_t kind=VIMSubjectWord(command),length=VIMSubjectWord(command+4);
        if(length<8||length>bytes-cursor){valid=0;break;}
        if(kind==LC_ID_DYLIB){
            static const char expected[]="/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL";
            if(length<24){valid=0;break;}
            uint32_t name=VIMSubjectWord(command+8);
            if(name>=24&&name<=length&&sizeof(expected)<=length-name&&
               !memcmp(command+name,expected,sizeof(expected)))identified++;
        }
        if(kind==LC_SEGMENT){
            if(length<56){valid=0;break;}
            uint32_t sections=VIMSubjectWord(command+48);
            if(sections>(length-56)/68){valid=0;break;}
            if(!memcmp(command+8,SEG_TEXT,sizeof(SEG_TEXT))){
                uint32_t original=VIMSubjectWord(command+24);
                for(uint32_t j=0;j<sections;j++){
                    const unsigned char *section=command+56+j*68;
                    if(!memcmp(section,SECT_TEXT,sizeof(SECT_TEXT))){
                        uint32_t address=VIMSubjectWord(section+32),size=VIMSubjectWord(section+36);
                        if(address<original||address-original>UINT32_MAX-base||size>UINT32_MAX-(base+address-original)){valid=0;break;}
                        text=base+address-original;end=text+size;
                    }
                }
            }
        }
        cursor+=length;
    }
    free(commands);
    if(!valid||identified!=1||cursor!=bytes||!text||function<text||function>=end)return 0;
    size_t length=end-function;if(length>4096)length=4096;
    unsigned char code[4096];if(!VIMSubjectRead(function,code,length))return 0;
    // Actual 9J3050 PPC CGLCreateContext: the inner screen index r8 already
    // advanced. The outer addi r13,r2,1 creates the same extra record as Intel.
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
    /* Actual8S2167 Intel combo's PPC public OpenGL has the earlier686-slot
     * ABI. Ghidra resolves its completed inner r8 loop and outer r14 base:
     * +0x33c increments engine r19, adds r14+r8, advances0xac4, compares
     * against r27, then incorrectly adds another1. Keep this separate from
     * the restore DVD's759-slot r13/0xbe8 implementation below. */
    static const unsigned char pattern686[]={0x3a,0x73,0,1,0x7c,0x4e,0x42,0x14,
        0x3a,0x31,0x0a,0xc4,0x3b,0x9c,0,1,0x7f,0x9b,0x98,0,0x39,0xc2,0,1};
    static const unsigned char replacement686[]={0x39,0xc2,0,0};
    /* Actual8R4088 PPC loop: inner r8 has advanced, yet the next engine's
     * base r13 adds another record. Its engine stride is0xbe8 (not9J3050's
     * 0xbe0), and the outer comparison uses r27, not r28. */
    static const unsigned char pattern759[]={0x3a,0x94,0,1,0x7c,0x4d,0x42,0x14,
        0x39,0xef,0x0b,0xe8,0x3a,0x10,0,1,0x7f,0x9b,0xa0,0,0x39,0xa2,0,1};
    static const unsigned char replacement759[]={0x39,0xa2,0,0};
#else
    static const unsigned char pattern[]={0x3a,0x94,0,1,0x7c,0x4d,0x42,0x14,
        0x39,0xef,0x0b,0xe0,0x3a,0x10,0,1,0x7f,0x9c,0xa0,0,0x39,0xa2,0,1};
    const unsigned char replacement[]={0x39,0xa2,0,0};
#endif
    struct {const unsigned char *bytes,*replacement;size_t size;unsigned slots;} variants[]={
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
        {pattern686,replacement686,sizeof(pattern686),686},
        {pattern759,replacement759,sizeof(pattern759),759}
#else
        {pattern,replacement,sizeof(pattern),VIM_GLI_DISPATCH_COUNT}
#endif
    };
    unsigned matches=0,slots=0;uint32_t address=0;const unsigned char *patch=NULL;
    for(size_t v=0;v<sizeof(variants)/sizeof(variants[0]);++v){
        if(variants[v].slots>VIM_GLI_DISPATCH_COUNT)continue;
        for(size_t i=0;i+variants[v].size<=length;i+=4)if(!memcmp(code+i,variants[v].bytes,variants[v].size)){
            address=function+(uint32_t)i+20;patch=variants[v].replacement;slots=variants[v].slots;matches++;
        }
    }
    *stage="Rosetta subject instruction match";
    if(matches!=1)return 0;
#ifdef VIM_ROSETTA_RECOVERY_DIAGNOSTICS
    *stage="Rosetta subject code-cache fixture";
    if(!VIMSubjectCodeFixture(lookup,call,&flush,protection))return 0;
#endif
    *stage="Rosetta subject private code and cache";
    int ready=VIMSubjectPatch(address,patch,4,call,&flush,protection);
    if(ready)vim_native_dispatch_count=slots;
    return ready;
}
#endif
