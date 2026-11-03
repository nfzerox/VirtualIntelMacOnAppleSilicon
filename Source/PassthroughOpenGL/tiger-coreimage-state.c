/* Native Tiger CI has three compiler program kinds but two GL targets:
 * _program_enum = {VERTEX_PROGRAM_ARB, FRAGMENT_PROGRAM_ARB,
 *                  FRAGMENT_PROGRAM_ARB}. Its binding cache has two words;
 * indexing kind2 writes the first saved rectangle-texture word instead.
 * A private-heap watch caught _sync_program_binding doing this on BOTH
 * 8R4088 and physical8R3032/Intel GMA950. Texture/program name coincidence
 * hides the bug on the reference; separate object allocation exposes it.
 *
 * Alias the third compiler kind to the SAME fragment GL state at the four
 * state-entry boundaries, retaining all compiler variants/programs/effects.
 * This is not a feature mask, application workaround or GL name remapping.
 * Resolve symbols in the actual image and guard its measured instructions;
 * never use a fixed OS address. Only private process code pages change.
 */
#include <mach/mach.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/dyld.h>
#include <sys/mman.h>
#include <pthread.h>
#include <syslog.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static int VIMTigerCIHas(const unsigned char *p,size_t n,const unsigned char *needle,size_t length)
{
    unsigned found=0;
    for(size_t i=0;i+length<=n;++i)if(!memcmp(p+i,needle,length))++found;
    return found==1;
}
static int VIMTigerFixCIState(const struct mach_header *header,intptr_t slide,const char *path)
{
    if(!header||header->magic!=MH_MAGIC||header->cputype!=CPU_TYPE_I386||!path)return 0;
    const char *names[]={"_fe_gl_bind_program","_fe_gl_enable_program",
        "_fe_gl_disable_program","_fe_gl_program_is_enabled","_program_enum",
        "_sync_program_binding","_sync_texture_binding"};
    struct nlist symbols[8];memset(symbols,0,sizeof(symbols));
    for(unsigned i=0;i<7;++i)symbols[i].n_un.n_name=(char *)names[i];
    if(nlist(path,symbols))return 0;
    uintptr_t text=0,end=0,data=0,data_end=0;
    const struct load_command *command=(const void *)(header+1);
    const char *limit=(const char *)command+header->sizeofcmds;
    for(uint32_t i=0;i<header->ncmds;++i){
        if((const char *)command>limit-sizeof(*command)||command->cmdsize<sizeof(*command)||
           command->cmdsize>(size_t)(limit-(const char *)command))return 0;
        if(command->cmd==LC_SEGMENT){
            const struct segment_command *segment=(const void *)command;
            if(command->cmdsize<sizeof(*segment)||segment->nsects>
               (command->cmdsize-sizeof(*segment))/sizeof(struct section))return 0;
            const struct section *section=(const void *)(segment+1);
            for(uint32_t j=0;j<segment->nsects;++j){
                if(!strcmp(section[j].segname,"__TEXT")&&!strcmp(section[j].sectname,"__text")){
                    text=slide+section[j].addr;end=text+section[j].size;
                }
            }
            if(!strcmp(segment->segname,"__TEXT")){data=slide+segment->vmaddr;data_end=data+segment->vmsize;}
        }
        command=(const void *)((const char *)command+command->cmdsize);
    }
    unsigned char *entry[7];
    for(unsigned i=0;i<7;++i){
        if((symbols[i].n_type&N_TYPE)!=N_SECT)return 0;
        entry[i]=(void *)(slide+symbols[i].n_value);
        uintptr_t p=(uintptr_t)entry[i];
        if(i==4){if(p<data||p>data_end||data_end-p<12)return 0;}
        else if(p<text||p>end||end-p<128)return 0;
    }
    static const uint32_t targets[]={0x8620,0x8804,0x8804};
    static const unsigned char prologue[]={0x55,0x89,0xe5,0x57,0x56,0x53,0x83,0xec,0x1c};
    static const unsigned char query[]={0x55,0x89,0xe5,0x57,0x56,0x8b,0x7d,0x08};
    static const unsigned char program_store[]={0x89,0x44,0xba,0x14};
    static const unsigned char texture_store[]={0x89,0x44,0xba,0x1c};
    if(memcmp(entry[4],targets,sizeof(targets))||memcmp(entry[3],query,sizeof(query))||
       !VIMTigerCIHas(entry[5],112,program_store,sizeof(program_store))||
       !VIMTigerCIHas(entry[6],96,texture_store,sizeof(texture_store)))return 0;
    for(unsigned i=0;i<3;++i)if(memcmp(entry[i],prologue,sizeof(prologue)))return 0;
    int native_page_size=getpagesize();
    if(native_page_size<=0||(native_page_size&(native_page_size-1)))return 0;
    const vm_size_t page_size=(vm_size_t)native_page_size;
    uintptr_t page=(uintptr_t)entry[0]&~((uintptr_t)page_size-1);
    for(unsigned i=1;i<4;++i)if(((uintptr_t)entry[i]&~((uintptr_t)page_size-1))!=page)return 0;
    unsigned char *stubs=mmap(NULL,page_size,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(stubs==MAP_FAILED)return 0;
    static const unsigned char alias[]={0x83,0x7c,0x24,0x0c,0x02,0x75,0x08,
        0xc7,0x44,0x24,0x0c,0x01,0x00,0x00,0x00};
    for(unsigned i=0;i<4;++i){
        unsigned char *stub=stubs+i*32;memcpy(stub,alias,sizeof(alias));
        memcpy(stub+15,entry[i],5);stub[20]=0xe9;
        int32_t back=(int32_t)((uintptr_t)(entry[i]+5)-(uintptr_t)(stub+25));
        memcpy(stub+21,&back,4);
    }
    if(mprotect(stubs,page_size,PROT_READ|PROT_EXEC)){munmap(stubs,page_size);return 0;}
    vm_address_t region=page;vm_size_t size=0;vm_region_basic_info_data_t info={0};
    mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT;mach_port_t object=MACH_PORT_NULL;
    kern_return_t error=vm_region(mach_task_self(),&region,&size,VM_REGION_BASIC_INFO,
        (vm_region_info_t)&info,&count,&object);
    if(object!=MACH_PORT_NULL)mach_port_deallocate(mach_task_self(),object);
    if(error||region>page||size<page_size||page-region>size-page_size||
       !(info.protection&VM_PROT_READ))goto fail;
    /* Tiger split-library text is READ/max READ, not the modern RX mapping.
     * Follow its actual protection, as the existing CGL recovery fix does. */
    if(vm_protect(mach_task_self(),page,page_size,FALSE,
        VM_PROT_COPY|info.protection|VM_PROT_WRITE))goto fail;
    for(unsigned i=0;i<4;++i){
        int32_t jump=(int32_t)((uintptr_t)(stubs+i*32)-(uintptr_t)(entry[i]+5));
        entry[i][0]=0xe9;memcpy(entry[i]+1,&jump,4);
    }
    unsigned a,b,c,d;__asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"0"(0):"memory");
    if(vm_protect(mach_task_self(),page,page_size,FALSE,info.protection))return 0;
    return 1; // Stubs have process lifetime; original code now jumps there.
fail:
    munmap(stubs,page_size);return 0;
}
/* Driver admission only, before publishing a context/procedure table. CI may
 * be linked at startup or loaded after an ordinary OpenGL application starts.
 * No persistent dyld callback into an unloadable GL plug-in, process injection,
 * per-frame polling, disk replacement or host-security change is needed. */
static int VIMTigerPrepareCIState(void)
{
    /* This correction targets the native i386 QuartzCore image, not Rosetta's
     * translated PPC implementation or its restricted native wrapper. */
    if(VIMTigerIsRosetta())return 1;
    if(!nlist||!mprotect||!vm_region||!vm_protect||!_dyld_get_image_vmaddr_slide)return 0;
    static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
    static int status;static uint32_t inspected;
    pthread_mutex_lock(&lock);
    if(!status&&inspected!=_dyld_image_count()){
        inspected=_dyld_image_count();
        for(uint32_t i=0;i<inspected;++i){
            const char *path=_dyld_get_image_name(i);
            const char *leaf=path?strrchr(path,'/'):NULL;
            if(leaf&&!strcmp(leaf,"/QuartzCore")&&strstr(path,"/QuartzCore.framework/")){
                status=VIMTigerFixCIState(_dyld_get_image_header(i),_dyld_get_image_vmaddr_slide(i),path)?1:-1;
                if(status<0)syslog(LOG_ERR,"VIM PassthroughOpenGL: native Tiger CI program-state guard did not match; declining context admission");
                break;
            }
        }
    }
    int ready=status>=0;pthread_mutex_unlock(&lock);return ready;
}
