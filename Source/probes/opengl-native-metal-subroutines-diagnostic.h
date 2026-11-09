/* Historical owned-process diagnostic only; never include in the executor.
 * Apple Metal GL's software-stage path can leave its subroutine-table source
 * null while the shared GL state still has a positive location count. The
 * subsequent typed table copy is unnecessary: that stage already ran in
 * GLEngine. The unchanged native all-stage CTS pixel case verifies this.
 *
 * Rebind ONLY this loose renderer image's memcpy import, never the process's
 * copy routine or executable text. On the exceptional null source, recognize
 * the typed table-copy call site in setRenderProgramUniforms: source reloaded
 * from the stack, then a direct call to this image's memcpy stub. Ordinary
 * copies and other null-source faults retain their original behavior. No
 * shader rewriting, private structure offsets, build addresses or per-draw
 * GL queries. Intel hosts do not install any correction.
 */
#if defined(__arm64__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

struct DGMetalCopyLocation {
    const struct mach_header *image;
    uintptr_t method_begin,method_end,copy_stub;
    uintptr_t *copy_slot;
    int authenticated;
};
static struct DGMetalCopyLocation dg_metal_copy;
static pthread_mutex_t dg_metal_copy_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned dg_metal_copy_ready;
#ifndef DG_METAL_SOFTWARE_STAGE_OBSERVED
#define DG_METAL_SOFTWARE_STAGE_OBSERVED(bytes) ((void)0)
#endif
/* An assembly alias prevents an optimizing compiler from replacing the
 * forwarding call with a builtin or applying it to another image's import. */
extern void *DGOriginalMetalCopy(void *,const void *,size_t) __asm__("_memcpy");

static void *DGMetalSoftwareStageCopy(void *destination,const void *source,size_t bytes)
{
    if(!source&&bytes&&!(bytes%sizeof(uint32_t))&&
       __atomic_load_n(&dg_metal_copy_ready,__ATOMIC_ACQUIRE)){
        uintptr_t pc=(uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0));
        if(pc>=dg_metal_copy.method_begin+8&&pc<dg_metal_copy.method_end){
            const uint32_t *instructions=(const uint32_t *)pc;
            uint32_t load=instructions[-2],call=instructions[-1];
            /* LDR X1,[SP,#imm]; BL imported memcpy. Normal vector-data
             * copies are tail-called from a different outlined helper. */
            intptr_t displacement=(int32_t)(call<<6)>>4;
            if((load&UINT32_C(0xffc003ff))==UINT32_C(0xf94003e1)&&
               (call&UINT32_C(0xfc000000))==UINT32_C(0x94000000)&&
               pc-4+displacement==dg_metal_copy.copy_stub){
                DG_METAL_SOFTWARE_STAGE_OBSERVED(bytes);
                return destination;
            }
        }
    }
    return DGOriginalMetalCopy(destination,source,bytes);
}

static int DGMetalFileSpan(uint64_t offset,uint64_t bytes,const struct segment_command_64 *link)
{return offset>=link->fileoff&&offset-link->fileoff<=link->filesize&&bytes<=link->filesize-(offset-link->fileoff);}

static int DGMetalCopyLocation(const struct mach_header_64 *header,intptr_t slide,struct DGMetalCopyLocation *out)
{
    if(header->magic!=MH_MAGIC_64||header->cputype!=CPU_TYPE_ARM64)return 0;
    const struct segment_command_64 *text=NULL,*link=NULL;
    const struct symtab_command *symbols=NULL;
    const struct dysymtab_command *dynamic=NULL;
    const struct linkedit_data_command *starts=NULL;
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
        else if(command->cmd==LC_DYSYMTAB&&command->cmdsize>=sizeof(*dynamic))dynamic=(const void *)command;
        else if(command->cmd==LC_FUNCTION_STARTS&&command->cmdsize>=sizeof(*starts))starts=(const void *)command;
        command=(const void *)((const char *)command+command->cmdsize);
    }
    if(!text||!link||!symbols||!dynamic||!starts||
       !DGMetalFileSpan(symbols->symoff,(uint64_t)symbols->nsyms*sizeof(struct nlist_64),link)||
       !DGMetalFileSpan(symbols->stroff,symbols->strsize,link)||
       !DGMetalFileSpan(dynamic->indirectsymoff,(uint64_t)dynamic->nindirectsyms*4,link)||
       !DGMetalFileSpan(starts->dataoff,starts->datasize,link))return 0;
    uintptr_t base=slide+link->vmaddr-link->fileoff;
    const struct nlist_64 *table=(const void *)(base+symbols->symoff);
    const char *strings=(const void *)(base+symbols->stroff);
    const uint32_t *indirect=(const void *)(base+dynamic->indirectsymoff);
    uintptr_t method=0;
    for(uint32_t i=0;i<symbols->nsyms;++i){
        uint32_t index=table[i].n_un.n_strx;
        if(index>=symbols->strsize||!memchr(strings+index,0,symbols->strsize-index))return 0;
        if((table[i].n_type&N_TYPE)==N_SECT&&
           !strcmp(strings+index,"__ZN13GLDContextRec24setRenderProgramUniformsEv"))method=table[i].n_value;
    }
    if(!method||method<text->vmaddr||method-text->vmaddr>=text->vmsize)return 0;
    const unsigned char *cursor=(const void *)(base+starts->dataoff),*limit=cursor+starts->datasize;
    uintptr_t address=text->vmaddr,method_end=0;int found=0;
    while(cursor<limit){
        uint64_t delta=0;unsigned shift=0;unsigned char byte;
        do {if(cursor==limit||shift>=64)return 0;byte=*cursor++;delta|=(uint64_t)(byte&127)<<shift;shift+=7;}while(byte&128);
        if(!delta)break;if(delta>UINTPTR_MAX-address)return 0;address+=delta;
        if(address==method)found=1;
        else if(address>method){method_end=address;break;}
    }
    if(!found||method_end<=method||method_end-text->vmaddr>text->vmsize)return 0;
    memset(out,0,sizeof(*out));out->image=(const void *)header;
    out->method_begin=slide+method;out->method_end=slide+method_end;
    command=(const void *)(header+1);
    for(uint32_t i=0;i<header->ncmds;++i){
        if(command->cmd==LC_SEGMENT_64){
            const struct segment_command_64 *segment=(const void *)command;
            const struct section_64 *section=(const void *)(segment+1);
            for(uint32_t j=0;j<segment->nsects;++j){
                unsigned type=section[j].flags&SECTION_TYPE;
                if(type!=S_SYMBOL_STUBS&&type!=S_NON_LAZY_SYMBOL_POINTERS)continue;
                uint64_t stride=type==S_SYMBOL_STUBS?section[j].reserved2:sizeof(uintptr_t);
                if(!stride||section[j].size%stride||section[j].reserved1>dynamic->nindirectsyms||
                   section[j].size/stride>dynamic->nindirectsyms-section[j].reserved1)return 0;
                for(uint64_t slot=0;slot<section[j].size/stride;++slot){
                    uint32_t index=indirect[section[j].reserved1+slot];
                    if(index&(INDIRECT_SYMBOL_LOCAL|INDIRECT_SYMBOL_ABS))continue;
                    if(index>=symbols->nsyms)return 0;
                    if(strcmp(strings+table[index].n_un.n_strx,"_memcpy"))continue;
                    uintptr_t location=slide+section[j].addr+slot*stride;
                    if(type==S_SYMBOL_STUBS){if(out->copy_stub)return 0;out->copy_stub=location;}
                    else {
                        if(out->copy_slot||!(segment->maxprot&VM_PROT_WRITE)||(segment->initprot&VM_PROT_EXECUTE))return 0;
                        out->copy_slot=(uintptr_t *)location;
                        out->authenticated=!strcmp(section[j].sectname,"__auth_got");
                    }
                }
            }
        }
        command=(const void *)((const char *)command+command->cmdsize);
    }
    return out->copy_stub&&out->copy_slot;
}

/* Sign a normal arm64 function address for the renderer's arm64e import.
 * PACIA is an unprivileged instruction on every Apple-silicon host. Match
 * the existing signed import before changing it; never guess its PAC scheme. */
static uintptr_t DGMetalSignImport(uintptr_t value,uintptr_t discriminator)
{__asm__ volatile("pacia %0, %1":"+r"(value):"r"(discriminator));return value;}

static int DGPrepareNativeMetalSubroutines(void)
{
    int result=0;pthread_mutex_lock(&dg_metal_copy_lock);
    for(uint32_t i=0;i<_dyld_image_count();++i){
        const char *name=_dyld_get_image_name(i);
        if(!name||!strstr(name,"/AppleMetalOpenGLRenderer.bundle/"))continue;
        const struct mach_header_64 *header=(const void *)_dyld_get_image_header(i);
        struct DGMetalCopyLocation location;
        if(!DGMetalCopyLocation(header,_dyld_get_image_vmaddr_slide(i),&location)){result=-1;break;}
        uintptr_t original=(uintptr_t)&DGOriginalMetalCopy,replacement=(uintptr_t)&DGMetalSoftwareStageCopy;
        uintptr_t current=__atomic_load_n(location.copy_slot,__ATOMIC_ACQUIRE);
        if(location.authenticated){
            uintptr_t discriminator=(uintptr_t)location.copy_slot;
            if(current!=DGMetalSignImport(original,discriminator)&&current!=DGMetalSignImport(replacement,discriminator)){
                discriminator=0;
                if(current!=DGMetalSignImport(original,discriminator)&&current!=DGMetalSignImport(replacement,discriminator)){result=-2;break;}
            }
            original=DGMetalSignImport(original,discriminator);replacement=DGMetalSignImport(replacement,discriminator);
        }
        if(current==replacement){result=1;break;}
        if(current!=original){result=-2;break;}
        mach_vm_address_t address=(mach_vm_address_t)location.copy_slot;
        mach_vm_size_t length=0;vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t object=MACH_PORT_NULL;
        kern_return_t error=mach_vm_region(mach_task_self(),&address,&length,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object);
        if(object!=MACH_PORT_NULL)mach_port_deallocate(mach_task_self(),object);
        uintptr_t slot=(uintptr_t)location.copy_slot;
        if(error||length<sizeof(uintptr_t)||slot<address||slot-address>length-sizeof(uintptr_t)||
           (info.protection&VM_PROT_EXECUTE)){result=-3;break;}
        mach_vm_address_t page=slot&~((mach_vm_address_t)vm_page_size-1);
        error=mach_vm_protect(mach_task_self(),page,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY);
        if(error){result=-4;break;}
        dg_metal_copy=location;__atomic_store_n(&dg_metal_copy_ready,1,__ATOMIC_RELEASE);
        __atomic_store_n(location.copy_slot,replacement,__ATOMIC_RELEASE);
        error=mach_vm_protect(mach_task_self(),page,vm_page_size,FALSE,info.protection);
        result=error?-5:1;break;
    }
    pthread_mutex_unlock(&dg_metal_copy_lock);return result;
}
#else
static int DGPrepareNativeMetalSubroutines(void){return 0;}
#endif
