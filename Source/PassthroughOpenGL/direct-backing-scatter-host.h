/* Local research: a retained description of already-pinned guest RAM, not
 * a second virtual mapping or GPU registration. The exported RAM capability
 * must outlive the plan. Caller retains the REAL guest CPU gate through the
 * last copy, and proves GPU completion before reading a source IOSurface.
 * No GPU fence/socket ACK alone grants CPU ownership of these bytes. */
#ifndef DG_BACKING_SCATTER_HOST_H
#define DG_BACKING_SCATTER_HOST_H
#include "../vendor/qemu/hw/display/vim-vram-memory.h"
#include <IOSurface/IOSurface.h>
struct DGScatterPlan {
    unsigned refs,count;
    uint32_t width,height,pitch;
    mach_vm_address_t ram;
    uint64_t length;
    uint64_t offsets[];
};
static int DGScatterOffsetCompare(const void *a,const void *b)
{uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b;return (x>y)-(x<y);}
static struct DGScatterPlan *DGScatterPlanCreate(const struct VIMVRAMMemoryMapping *ram,
    const uint64_t *pages,unsigned count,uint32_t width,uint32_t height,uint32_t pitch)
{
    if(!ram || !ram->address || !ram->region_count || !pages || !count || count>16384 ||
       !width || !height || width>8192 || height>8192 || pitch<width*4 || pitch>8192*4 ||
       (pitch&31) || (uint64_t)pitch*height>(uint64_t)count*4096 ||
       !VIMSharedRAMRangesValid(ram->regions,ram->region_count,ram->bytes))return NULL;
    struct DGScatterPlan *p=calloc(1,sizeof(*p)+(size_t)count*sizeof(uint64_t));
    uint64_t *sorted=malloc((size_t)count*sizeof(uint64_t));
    if(!p || !sorted){free(p);free(sorted);return NULL;}
    p->refs=1;p->count=count;p->width=width;p->height=height;p->pitch=pitch;
    p->ram=ram->address;p->length=(uint64_t)count*4096;
    for(unsigned i=0;i<count;++i){
        int found=0;
        if(!(pages[i]&4095))for(unsigned j=0;j<ram->region_count;++j){
            const struct VIMSharedRAMRange *r=ram->regions+j;
            if(pages[i]>=r->guest && pages[i]-r->guest<r->bytes &&
               4096<=r->bytes-(pages[i]-r->guest)){
                p->offsets[i]=r->offset+pages[i]-r->guest;found=1;break;
            }
        }
        if(!found){free(sorted);free(p);return NULL;}
        sorted[i]=p->offsets[i];
    }
    // Two logical output pages aliasing the same physical page have no
    // unambiguous whole-image CPU materialization. Keep the old path rather
    // than invent last-writer semantics. Sort a PRIVATE validation set only.
    qsort(sorted,count,sizeof(*sorted),DGScatterOffsetCompare);
    for(unsigned i=1;i<count;++i)if(sorted[i]==sorted[i-1]){free(sorted);free(p);return NULL;}
    free(sorted);return p;
}
static void DGScatterPlanRetain(struct DGScatterPlan *p)
{if(p)__sync_add_and_fetch(&p->refs,1);}
static void DGScatterPlanPut(struct DGScatterPlan *p)
{if(p && !__sync_sub_and_fetch(&p->refs,1))free(p);}
static int DGScatterSurfaceTransfer(const struct DGScatterPlan *p,IOSurfaceRef surface,int gathering)
{
    if(!p || !surface || IOSurfaceGetPlaneCount(surface) ||
       IOSurfaceGetBytesPerElement(surface)!=4 || IOSurfaceGetPixelFormat(surface)!=0x42475241 ||
       IOSurfaceGetWidth(surface)<p->width || IOSurfaceGetHeight(surface)<p->height)return 0;
    size_t row=IOSurfaceGetBytesPerRow(surface),bytes=IOSurfaceGetAllocSize(surface);
    if(row<(size_t)p->width*4 || (uint64_t)(p->height-1)*row+(uint64_t)p->width*4>bytes)return 0;
    uint32_t options=gathering?0:kIOSurfaceLockReadOnly;
    if(IOSurfaceLock(surface,options,NULL)!=kIOReturnSuccess)return 0;
    unsigned char *surface_bytes=IOSurfaceGetBaseAddress(surface);int good=surface_bytes!=NULL;
    for(unsigned y=0;good && y<p->height;++y){
        uint64_t offset=(uint64_t)y*p->pitch;size_t left=(size_t)p->width*4,at=0;
        while(left){
            unsigned page=(unsigned)(offset/4096);size_t within=(size_t)(offset&4095),n=4096-within;
            if(n>left)n=left;
            void *ram_bytes=(void *)(uintptr_t)(p->ram+p->offsets[page]+within);
            if(gathering)memcpy(surface_bytes+(size_t)y*row+at,ram_bytes,n);
            else memcpy(ram_bytes,surface_bytes+(size_t)y*row+at,n);
            offset+=n;at+=n;left-=n;
        }
    }
    kern_return_t unlocked=IOSurfaceUnlock(surface,options,NULL);
    // Failure after a prefix must quarantine the guest gate; never retry as
    // though no CPU writes happened or announce completion on unlock failure.
    return good && unlocked==kIOReturnSuccess;
}
static int DGScatterCopyCompletedSurface(const struct DGScatterPlan *p,IOSurfaceRef surface)
{return DGScatterSurfaceTransfer(p,surface,0);}
/* Caller owns genuine CPU-current source contents AND CPU exclusion, and
 * flushes prior GPU users before this writable lock. Unlock supplies native
 * IOSurface coherence, not a fabricated completed-GPU/CPU-ownership witness. */
static int DGScatterGatherSurface(const struct DGScatterPlan *p,IOSurfaceRef surface)
{return DGScatterSurfaceTransfer(p,surface,1);}
#endif
