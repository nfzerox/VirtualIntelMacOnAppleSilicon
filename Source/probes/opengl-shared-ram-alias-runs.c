/* Local research oracle for exact anonymous RAM aliasing, not GPU completion.
 * Count actual remap calls, verify both-direction sharing/order/duplicates,
 * region boundaries and failure cleanup. No QEMU or live VM is modified. */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/mach_time.h>
#include <assert.h>
#include <stdint.h>
static unsigned remaps;
static unsigned fail_remap;
static mach_vm_address_t first_destination;
static kern_return_t counted_remap(vm_map_t task,mach_vm_address_t *address,
    mach_vm_size_t bytes,mach_vm_offset_t mask,int flags,vm_map_t source,
    mach_vm_address_t source_address,boolean_t copy,vm_prot_t *current,
    vm_prot_t *maximum,vm_inherit_t inherit)
{
    if(!remaps)first_destination=*address;
    ++remaps;
    if(fail_remap && remaps==fail_remap)return KERN_RESOURCE_SHORTAGE;
    return mach_vm_remap(task,address,bytes,mask,flags,source,source_address,
        copy,current,maximum,inherit);
}
#define mach_vm_remap counted_remap
#include "../vendor/qemu/hw/display/vim-vram-memory.h"
#undef mach_vm_remap
#ifndef VIM_EXPECT_COALESCED
#define VIM_EXPECT_COALESCED 0
#endif
enum {N=1840,RegionPages=2048,TotalPages=6144};
static struct VIMVRAMMemoryMapping ram;
static uint32_t *source_word(uint64_t page)
{
    for(unsigned j=0;j<ram.region_count;++j){
        struct VIMSharedRAMRange *r=ram.regions+j;
        if(page>=r->guest && page-r->guest<r->bytes)
            return (void *)(uintptr_t)(ram.address+r->offset+page-r->guest);
    }
    assert(0);return NULL;
}
static void qualify(const char *name,const uint64_t *pages,unsigned count,unsigned runs)
{
    struct VIMVRAMMemoryMapping alias={0};remaps=0;
    for(unsigned i=0;i<count;++i)*source_word(pages[i])=(uint32_t)(pages[i]>>12)^0xa17e0000u;
    assert(!VIMSharedRAMMapPages(&ram,pages,count,&alias));
    assert(alias.bytes==(uint64_t)count*4096 && remaps==(VIM_EXPECT_COALESCED?runs:count));
    for(unsigned i=0;i<count;++i){
        volatile uint32_t *p=(void *)(uintptr_t)(alias.address+(uint64_t)i*4096);
        assert(*p==*source_word(pages[i]));
        *p^=0x51;assert(*p==*source_word(pages[i]));
        *source_word(pages[i])^=0x97;assert(*p==*source_word(pages[i]));
    }
    for(unsigned i=0;i<count;++i)
        assert(*(volatile uint32_t *)(uintptr_t)(alias.address+(uint64_t)i*4096)==*source_word(pages[i]));
    printf("PASS %s pages=%u actual_remaps=%u coherent aliases in both directions\n",name,count,remaps);
    VIMVRAMMemoryUnmap(&alias);assert(!alias.address && !alias.bytes);
}
static void reject(const uint64_t *pages,unsigned count)
{
    struct VIMVRAMMemoryMapping out={0};
    assert(VIMSharedRAMMapPages(&ram,pages,count,&out)!=KERN_SUCCESS);
    assert(!out.address && !out.bytes);
}
int main(void)
{
    if(vm_page_size!=4096){puts("SKIP Intel 4-KB mapper: larger host page granule remains unsupported");return 77;}
    ram.bytes=(uint64_t)TotalPages*4096;
    assert(!mach_vm_allocate(mach_task_self(),&ram.address,ram.bytes,VM_FLAGS_ANYWHERE));
    ram.region_count=3;
    ram.regions[0]=(struct VIMSharedRAMRange){0x10000000,0,RegionPages*4096};
    // Adjacent GPAs, deliberately NON-adjacent source offsets. Never merge
    // across a range just because guest physical addresses are consecutive.
    ram.regions[1]=(struct VIMSharedRAMRange){0x10800000,2*RegionPages*4096,RegionPages*4096};
    ram.regions[2]=(struct VIMSharedRAMRange){0x30000000,RegionPages*4096,RegionPages*4096};
    assert(VIMSharedRAMRangesValid(ram.regions,ram.region_count,ram.bytes));
    uint64_t pages[N];
    for(unsigned i=0;i<N;++i)pages[i]=ram.regions[0].guest+(uint64_t)i*4096;
    qualify("contiguous",pages,N,1);
    for(unsigned i=0;i<32;++i)pages[i]=ram.regions[0].guest+(uint64_t)(RegionPages-16+i)*4096;
    qualify("adjacent guest regions with reordered source storage",pages,32,2);
    for(unsigned i=0;i<32;++i)pages[i]=ram.regions[0].guest+(uint64_t)(31-i)*4096;
    qualify("reverse permutation",pages,32,32);
    for(unsigned i=0;i<16;++i)pages[i]=ram.regions[i%2?2:0].guest+(uint64_t)(i/2)*4096;
    qualify("alternating RAM ranges",pages,16,16);
    for(unsigned i=0;i<4;++i)pages[i]=ram.regions[0].guest;
    qualify("duplicate page aliases",pages,4,4);
    for(unsigned i=0;i<8;++i)pages[i]=ram.regions[0].guest+i*4096;
    pages[7]=0x20000000;reject(pages,8);pages[7]=UINT64_MAX-4095;reject(pages,8);
    pages[7]=ram.regions[0].guest+1;reject(pages,8);
    reject(pages,0);reject(pages,16385);
    for(unsigned i=0;i<4;++i)pages[i]=ram.regions[0].guest+(3-i)*4096;
    struct VIMVRAMMemoryMapping failed={0};remaps=0;fail_remap=2;
    assert(VIMSharedRAMMapPages(&ram,pages,4,&failed)==KERN_RESOURCE_SHORTAGE);
    assert(remaps==2 && !failed.address && !failed.bytes);fail_remap=0;
    mach_vm_address_t released=first_destination;mach_vm_size_t released_size=0;
    vm_region_basic_info_data_64_t info;mach_msg_type_number_t info_count=VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object=MACH_PORT_NULL;
    kern_return_t kr=mach_vm_region(mach_task_self(),&released,&released_size,
        VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&info_count,&object);
    assert(kr==KERN_INVALID_ADDRESS || (!kr && released>first_destination));
    if(object!=MACH_PORT_NULL)mach_port_deallocate(mach_task_self(),object);
    puts("PASS injected second-remap resource failure destroys the partial allocation");
    struct VIMVRAMMemoryMapping occupied={.address=123,.bytes=456};
    assert(VIMSharedRAMMapPages(&ram,pages,1,&occupied)==KERN_INVALID_ARGUMENT);
    assert(occupied.address==123 && occupied.bytes==456);
    puts("PASS holes/unaligned/overflow/count/occupied-output rejected; partial aliases not published");
    for(unsigned i=0;i<N;++i)pages[i]=ram.regions[0].guest+(uint64_t)i*4096;
    mach_timebase_info_data_t clock;assert(!mach_timebase_info(&clock));
    double total=0;unsigned calls=0;
    for(unsigned n=0;n<24;++n){
        struct VIMVRAMMemoryMapping alias={0};remaps=0;uint64_t start=mach_absolute_time();
        assert(!VIMSharedRAMMapPages(&ram,pages,N,&alias));
        uint64_t end=mach_absolute_time();
        if(n>=4){total+=(double)(end-start)*clock.numer/clock.denom/1000.0;calls+=remaps;}
        VIMVRAMMemoryUnmap(&alias);
    }
    printf("BENCH exact contiguous1840 pages samples=20 mean_us=%.2f remaps_per_sample=%u; not desktop FPS\n",total/20,calls/20);
    // The actual desktop has nearly one physical run per page. Compare
    // reconstructing that mapping with BOTH directions of a retained CPU
    // staging allocation. This is a feasibility cost bound, NOT a complete
    // GL upload/readback/ownership benchmark or permission to skip fences.
    unsigned char *staging=NULL;
    assert(!posix_memalign((void **)&staging,4096,N*4096));
    for(unsigned i=0;i<N;++i)pages[i]=ram.regions[0].guest+((uint64_t)i*1093%RegionPages)*4096;
    qualify("large fragmented permutation",pages,N,N);
    double alias_total=0,copy_total=0;
    for(unsigned n=0;n<16;++n){
        struct VIMVRAMMemoryMapping alias={0};uint64_t start=mach_absolute_time();
        assert(!VIMSharedRAMMapPages(&ram,pages,N,&alias));
        VIMVRAMMemoryUnmap(&alias);uint64_t end=mach_absolute_time();
        if(n>=4)alias_total+=(double)(end-start)*clock.numer/clock.denom/1000.0;
        start=mach_absolute_time();
        for(unsigned i=0;i<N;++i)memcpy(staging+(size_t)i*4096,source_word(pages[i]),4096);
        for(unsigned i=0;i<N;++i)memcpy(source_word(pages[i]),staging+(size_t)i*4096,4096);
        end=mach_absolute_time();
        if(n>=4)copy_total+=(double)(end-start)*clock.numer/clock.denom/1000.0;
        for(unsigned i=0;i<N;++i)assert(!memcmp(staging+(size_t)i*4096,source_word(pages[i]),4096));
    }
    printf("BENCH fragmented1840 samples=12 fresh_alias_map_unmap_mean_us=%.2f retained_gather_AND_scatter_mean_us=%.2f; exact CPU bytes; excludes GPU/pins; not desktop FPS\n",
        alias_total/12,copy_total/12);
    free(staging);
    VIMVRAMMemoryUnmap(&ram);return 0;
}
