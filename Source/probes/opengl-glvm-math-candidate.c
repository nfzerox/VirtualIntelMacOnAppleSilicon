/* Research candidate, NOT included in the executor. It corrects the native
 * interpreter but not inlined JIT copies; do not promote this partial control.
 * Apple's FP64 interpreter tests tie parity using trunc(input), not floor(input).
 * Intercept only its own math-table calls from glvmOperationROUNDEVEN. Ordinary
 * floor/ceil, other operations, GPU shaders, and Intel hosts are unchanged.
 * The loaded image supplies symbol/function bounds; no private structure
 * offsets, executable patches, shader rewriting, or global math interposer. */
#if defined(__arm64__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

struct DGGLVMMathImage {
    uintptr_t method_begin,method_end,constants_begin,constants_end;
};
static struct DGGLVMMathImage dg_glvm_math_image;
static pthread_mutex_t dg_glvm_math_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned dg_glvm_math_ready,dg_glvm_math_installed;
extern double DGGLVMOriginalFloor(double) __asm__("_floor");
extern double DGGLVMOriginalCeil(double) __asm__("_ceil");
#ifndef DG_GLVM_ROUND_OBSERVED
#define DG_GLVM_ROUND_OBSERVED(kind) ((void)0)
#endif
#ifndef DG_GLVM_FORWARD_OBSERVED
#define DG_GLVM_FORWARD_OBSERVED(kind,caller) ((void)0)
#endif

static double DGGLVMEvenValue(double value)
{
    if(!isfinite(value)||value==0.0)return value;
    double lower=floor(value),fraction=value-lower;
    if(fraction>0.5||(fraction==0.5&&fmod(lower,2.0)!=0.0))lower+=1.0;
    return lower==0.0?copysign(0.0,value):lower;
}
static int DGGLVMRoundingCaller(uintptr_t caller)
{
    return __atomic_load_n(&dg_glvm_math_ready,__ATOMIC_ACQUIRE)&&
        caller>=dg_glvm_math_image.method_begin&&caller<dg_glvm_math_image.method_end;
}
static double DGGLVMFloor(double value)
{
    uintptr_t caller=(uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0));
    if(DGGLVMRoundingCaller(caller)){DG_GLVM_ROUND_OBSERVED(0);return DGGLVMEvenValue(value);}
    DG_GLVM_FORWARD_OBSERVED(0,caller);
    return DGGLVMOriginalFloor(value);
}
static double DGGLVMCeil(double value)
{
    uintptr_t caller=(uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0));
    if(DGGLVMRoundingCaller(caller)){DG_GLVM_ROUND_OBSERVED(1);return DGGLVMEvenValue(value);}
    DG_GLVM_FORWARD_OBSERVED(1,caller);
    return DGGLVMOriginalCeil(value);
}
static int DGGLVMSymbolName(const char *strings,uint32_t length,uint32_t offset,const char *name)
{
    size_t bytes=strlen(name)+1;
    return offset<length&&bytes<=length-offset&&!memcmp(strings+offset,name,bytes);
}
static int DGGLVMLinkSpan(uint64_t offset,uint64_t bytes,const struct segment_command_64 *link)
{return offset>=link->fileoff&&offset-link->fileoff<=link->filesize&&bytes<=link->filesize-(offset-link->fileoff);}

static int DGGLVMMathImage(const struct mach_header_64 *header,intptr_t slide,struct DGGLVMMathImage *out)
{
    if(header->magic!=MH_MAGIC_64||header->cputype!=CPU_TYPE_ARM64)return 0;
    const struct segment_command_64 *text=NULL,*link=NULL;
    const struct symtab_command *symbols=NULL;const struct linkedit_data_command *starts=NULL;
    const char *end=(const char *)(header+1)+header->sizeofcmds;
    const struct load_command *command=(const void *)(header+1);
    for(uint32_t i=0;i<header->ncmds;++i){
        if((const char *)command>end-sizeof(*command)||command->cmdsize<sizeof(*command)||
           command->cmdsize>(size_t)(end-(const char *)command))return 0;
        if(command->cmd==LC_SEGMENT_64&&command->cmdsize>=sizeof(struct segment_command_64)){
            const struct segment_command_64 *segment=(const void *)command;
            if(segment->nsects>(command->cmdsize-sizeof(*segment))/sizeof(struct section_64))return 0;
            if(!strcmp(segment->segname,SEG_TEXT))text=segment;
            if(!strcmp(segment->segname,SEG_LINKEDIT))link=segment;
        }else if(command->cmd==LC_SYMTAB&&command->cmdsize>=sizeof(*symbols))symbols=(const void *)command;
        else if(command->cmd==LC_FUNCTION_STARTS&&command->cmdsize>=sizeof(*starts))starts=(const void *)command;
        command=(const void *)((const char *)command+command->cmdsize);
    }
    if(!text||!link||!symbols||!starts||
       !DGGLVMLinkSpan(symbols->symoff,(uint64_t)symbols->nsyms*sizeof(struct nlist_64),link)||
       !DGGLVMLinkSpan(symbols->stroff,symbols->strsize,link)||
       !DGGLVMLinkSpan(starts->dataoff,starts->datasize,link))return 0;
    uintptr_t base=slide+link->vmaddr-link->fileoff;
    const struct nlist_64 *table=(const void *)(base+symbols->symoff);
    const char *strings=(const void *)(base+symbols->stroff);
    uintptr_t method=0,constants=0,constants_end=0,method_end=0;unsigned constant_section=0;
    for(uint32_t i=0;i<symbols->nsyms;++i){
        if((table[i].n_type&N_STAB)||(table[i].n_type&N_TYPE)!=N_SECT)continue;
        uint32_t index=table[i].n_un.n_strx;
        if(DGGLVMSymbolName(strings,symbols->strsize,index,"_glvmOperationROUNDEVEN"))method=table[i].n_value;
        if(DGGLVMSymbolName(strings,symbols->strsize,index,"_glvm_constants")){
            constants=table[i].n_value;constant_section=table[i].n_sect;
        }
    }
    if(!method||!constants||!constant_section||method<text->vmaddr||method-text->vmaddr>=text->vmsize)return 0;
    command=(const void *)(header+1);unsigned section_index=0;
    for(uint32_t i=0;i<header->ncmds;++i){
        if(command->cmd==LC_SEGMENT_64){
            const struct segment_command_64 *segment=(const void *)command;
            const struct section_64 *sections=(const void *)(segment+1);
            for(uint32_t j=0;j<segment->nsects;++j)if(++section_index==constant_section){
                if((segment->initprot&VM_PROT_EXECUTE)||constants<sections[j].addr||
                   constants-sections[j].addr>=sections[j].size)return 0;
                constants_end=sections[j].addr+sections[j].size;
            }
        }
        command=(const void *)((const char *)command+command->cmdsize);
    }
    for(uint32_t i=0;i<symbols->nsyms;++i)if(!(table[i].n_type&N_STAB)&&
        (table[i].n_type&N_TYPE)==N_SECT&&table[i].n_sect==constant_section&&
        table[i].n_value>constants&&table[i].n_value<constants_end)constants_end=table[i].n_value;
    if(constants_end<=constants||constants%sizeof(uintptr_t)||
       (constants_end-constants)%sizeof(uintptr_t))return 0;
    const unsigned char *cursor=(const void *)(base+starts->dataoff),*limit=cursor+starts->datasize;
    uintptr_t address=text->vmaddr;int found=0;
    while(cursor<limit){
        uint64_t delta=0;unsigned shift=0;unsigned char byte;
        do {if(cursor==limit||shift>=64)return 0;byte=*cursor++;delta|=(uint64_t)(byte&127)<<shift;shift+=7;}while(byte&128);
        if(!delta)break;if(delta>UINTPTR_MAX-address)return 0;address+=delta;
        if(address==method)found=1;
        else if(address>method){method_end=address;break;}
    }
    if(!found||method_end<=method||method_end-text->vmaddr>text->vmsize)return 0;
    *out=(struct DGGLVMMathImage){slide+method,slide+method_end,slide+constants,slide+constants_end};return 1;
}
static uintptr_t DGGLVMSignMath(uintptr_t value,uintptr_t discriminator)
{__asm__ volatile("pacia %0, %1":"+r"(value):"r"(discriminator));return value;}
static int DGGLVMWritableSlot(uintptr_t slot)
{
    mach_vm_address_t address=slot;mach_vm_size_t length=0;vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t object=MACH_PORT_NULL;
    kern_return_t error=mach_vm_region(mach_task_self(),&address,&length,VM_REGION_BASIC_INFO_64,
        (vm_region_info_t)&info,&count,&object);
    if(object!=MACH_PORT_NULL)mach_port_deallocate(mach_task_self(),object);
    return !error&&length>=sizeof(uintptr_t)&&slot>=address&&slot-address<=length-sizeof(uintptr_t)&&
        (info.protection&(VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE))==(VM_PROT_READ|VM_PROT_WRITE);
}
static int DGPrepareNativeGLVMRoundEven(void)
{
    if(__atomic_load_n(&dg_glvm_math_installed,__ATOMIC_ACQUIRE))return 1;
    int result=0;pthread_mutex_lock(&dg_glvm_math_lock);
    if(dg_glvm_math_installed){result=1;goto finished;}
    for(uint32_t image=0;image<_dyld_image_count();++image){
        const char *name=_dyld_get_image_name(image);
        if(!name||!strstr(name,"/libGLProgrammability.dylib"))continue;
        struct DGGLVMMathImage location;
        if(!DGGLVMMathImage((const void *)_dyld_get_image_header(image),_dyld_get_image_vmaddr_slide(image),&location)){
            result=-1;break;
        }
        struct {uintptr_t *slot,value;} changes[8];unsigned count=0,functions[2]={0,0};
        const uintptr_t originals[]={(uintptr_t)&DGGLVMOriginalFloor,(uintptr_t)&DGGLVMOriginalCeil};
        const uintptr_t replacements[]={(uintptr_t)&DGGLVMFloor,(uintptr_t)&DGGLVMCeil};
        for(uintptr_t address=location.constants_begin;address<location.constants_end;address+=sizeof(uintptr_t)){
            uintptr_t *slot=(void *)address,current=__atomic_load_n(slot,__ATOMIC_ACQUIRE);
            for(unsigned function=0;function<2;++function){
                uintptr_t replacement;
                if(current==originals[function])replacement=replacements[function];
                else if(current==DGGLVMSignMath(originals[function],0))replacement=DGGLVMSignMath(replacements[function],0);
                else if(current==DGGLVMSignMath(originals[function],address))replacement=DGGLVMSignMath(replacements[function],address);
                else continue;
                if(count==sizeof(changes)/sizeof(*changes)||!DGGLVMWritableSlot(address)){result=-2;goto finished;}
                changes[count].slot=slot;changes[count++].value=replacement;++functions[function];break;
            }
        }
        if(!functions[0]||functions[0]!=functions[1]){result=-3;break;}
        dg_glvm_math_image=location;__atomic_store_n(&dg_glvm_math_ready,1,__ATOMIC_RELEASE);
        for(unsigned i=0;i<count;++i)__atomic_store_n(changes[i].slot,changes[i].value,__ATOMIC_RELEASE);
        __atomic_store_n(&dg_glvm_math_installed,1,__ATOMIC_RELEASE);result=1;break;
    }
finished:
    pthread_mutex_unlock(&dg_glvm_math_lock);return result;
}
#else
static int DGPrepareNativeGLVMRoundEven(void){return 0;}
#endif
