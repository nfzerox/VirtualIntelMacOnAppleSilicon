// Finite component test of the actual older-framework gather/scatter helpers.
// Compile x86_64: the translated4KB VM API can build a truly fragmented alias.
#import <AppKit/AppKit.h>
#include "apple-gfx-legacy-shadow.m"
#include <mach/mach_vm.h>

static IMP release_alias_mapping;
static mach_vm_address_t retired_alias;
static void observe_alias_release(AppleGFXMetalAlias *owner, SEL cmd) {
    mach_vm_address_t address=owner->address;
    ((void (*)(id,SEL))release_alias_mapping)(owner,cmd);
    if(address&&!owner->address)__atomic_store_n(&retired_alias,address,__ATOMIC_RELEASE);
}

@interface ClassicProbeTask : NSObject {
@public void *bytes; id<MTLDevice> device;
}
- (void *)mappedAddressForPageNumber:(uint32_t)page length:(uint64_t)length;
- (id)mtlDevice;
@end
@implementation ClassicProbeTask
- (void *)mappedAddressForPageNumber:(uint32_t)page length:(uint64_t)length { return bytes; }
- (id)mtlDevice { return device; }
@end

// Exercise the actual selector/ABI installer and resource-list wrapper too,
// not only the GPU kernels. The native Metal framework does not load PVG.
@interface PGBufferResource : NSObject {
@public id _buffer, _transferBuffer; bool _isPrivate;
}
- (void)ensurePhysical;
- (BOOL)needsPrepare;
- (BOOL)needsSync;
- (BOOL)needsComputePaging;
- (void)prepareInEncoder:(id)encoder;
- (void)synchronizeInEncoder:(id)encoder;
- (void)prepareInComputeEncoder:(id)encoder;
- (void)synchronizeInComputeEncoder:(id)encoder;
@end
@implementation PGBufferResource
- (void)ensurePhysical {}
- (BOOL)needsPrepare {return NO;}
- (BOOL)needsSync {return NO;}
- (BOOL)needsComputePaging {return NO;}
- (void)prepareInEncoder:(id)encoder {}
- (void)synchronizeInEncoder:(id)encoder {}
- (void)prepareInComputeEncoder:(id)encoder {}
- (void)synchronizeInComputeEncoder:(id)encoder {}
@end
@interface PGBackingResource : NSObject {id _transferBuffer;}
- (void)prepareInEncoder:(id)encoder;
- (void)synchronizeInEncoder:(id)encoder;
@end
@implementation PGBackingResource
- (void)prepareInEncoder:(id)encoder {}
- (void)synchronizeInEncoder:(id)encoder {}
@end
@interface PGTask : ClassicProbeTask {
    id _pagingQueue;
@public NSArray *resources;
}
- (id)newBufferForVirtualPage:(uint32_t)page length:(uint64_t)length;
- (void)runBlock:(void (^)(void))block;
- (id)getHostResource:(uint32_t)identifier;
- (id)getExistingHostResource:(uint32_t)identifier;
- (id)device;
- (AppleGFXClassicPrepare)prepareResources:(const AppleGFXClassicResource *)list count:(uint32_t)count event:(id)event value:(uint64_t)value;
- (void)completeResources:(const AppleGFXClassicResource *)list count:(uint32_t)count sync:(AppleGFXClassicComplete)sync completionHandler:(id)completion;
@end
@implementation PGTask
- (id)newBufferForVirtualPage:(uint32_t)page length:(uint64_t)length {return nil;}
- (void)runBlock:(void (^)(void))block {block();}
- (id)getHostResource:(uint32_t)identifier {return resources[identifier];}
- (id)getExistingHostResource:(uint32_t)identifier {return resources[identifier];}
- (id)device {return nil;}
- (AppleGFXClassicPrepare)prepareResources:(const AppleGFXClassicResource *)list count:(uint32_t)count event:(id)event value:(uint64_t)value {
    AppleGFXClassicPrepare sync={0};
    for(uint32_t i=0;i<count;i++)sync.download|=list[i].readback!=0;
    return sync;
}
- (void)completeResources:(const AppleGFXClassicResource *)list count:(uint32_t)count sync:(AppleGFXClassicComplete)sync completionHandler:(id)completion {}
@end

int main(void) {@autoreleasepool {
    [NSApplication sharedApplication];
    if(vm_page_size!=4096){fprintf(stderr,"SKIP requires4KB process VM pages\n");return 77;}
    const NSUInteger length=65536;
    mach_vm_address_t base=0,alias=0;
    if(mach_vm_allocate(mach_task_self(),&base,length,VM_FLAGS_ANYWHERE)||
       mach_vm_allocate(mach_task_self(),&alias,length,VM_FLAGS_ANYWHERE))return 1;
    for(unsigned i=0;i<length/4096;i++) {
        unsigned page=(i/4)*4+3-i%4;
        mach_vm_address_t destination=alias+i*4096;vm_prot_t current,maximum;
        if(mach_vm_remap(mach_task_self(),&destination,4096,0,VM_FLAGS_FIXED|VM_FLAGS_OVERWRITE,
            mach_task_self(),base+page*4096,FALSE,&current,&maximum,VM_INHERIT_COPY))return 2;
    }
    ClassicProbeTask *task=[ClassicProbeTask new];task->device=MTLCreateSystemDefaultDevice();
    if(!task->device){fprintf(stderr,"SKIP no Metal device\n");return 77;}
    task->bytes=(void *)base;
    id<MTLBuffer> direct=legacy_buffer(task,NULL,0,length);
    if(!direct||objc_getAssociatedObject(direct,&shadow_key))return 3;
    [direct release];
    task->bytes=(void *)alias;
    id<MTLBuffer> buffer=legacy_buffer(task,NULL,0,length);
    AppleGFXLegacyShadow *shadow=objc_getAssociatedObject(buffer,&shadow_key);
    if(!shadow){fprintf(stderr,"Expected actual fragmented import fallback\n");return 4;}
    id<MTLCommandQueue> queue=[task->device newCommandQueue];
    for(unsigned mode=0;mode<3;mode++) {
        uint8_t *guest=(void *)alias;
        for(NSUInteger i=0;i<length;i++)guest[i]=(i*37+i/4096)&255;
        id<MTLCommandBuffer> command=[queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];
        classic_compute_transfer(buffer,encoder,false);
        [encoder endEncoding];[command commit];[command waitUntilCompleted];
        if(command.status!=MTLCommandBufferStatusCompleted||
           memcmp(buffer.contents,guest,length)||memcmp(shadow->baseline.contents,guest,length))return 5;
        // Simulate changed GPU result bytes, then CPU writes after the snapshot.
        uint8_t *after=buffer.contents;
        for(NSUInteger i=0;i<length;i++)if(mode==2||(mode==1&&i%257==0))after[i]^=0xa5;
        memset(guest,0x5c,length);
        command=[queue commandBuffer];encoder=[command computeCommandEncoder];
        classic_compute_transfer(buffer,encoder,true);
        [encoder endEncoding];[command commit];[command waitUntilCompleted];
        if(command.status!=MTLCommandBufferStatusCompleted)return 6;
        for(NSUInteger i=0;i<length;i++) {
            uint8_t expected=(mode==2||(mode==1&&i%257==0))?(((i*37+i/4096)&255)^0xa5):0x5c;
            if(guest[i]!=expected){fprintf(stderr,"mode%u mismatch byte%lu\n",mode,(unsigned long)i);return 7;}
        }
        printf("PASS mode=%u fragmented GPU gather/baseline/scatter preserves CPU writes\n",mode);
    }
    // The CPU view must stay read-only, while a distinct Metal alias is truly
    // coherent in both directions and cannot outlive its owning buffer.
    uint8_t *source=(void *)base;source[0]=0x31;
    if(mach_vm_protect(mach_task_self(),base,4096,FALSE,VM_PROT_READ))return 8;
    release_alias_mapping=replace([AppleGFXMetalAlias class],"releaseMapping",(IMP)observe_alias_release);
    mach_vm_address_t released=0;
    mach_vm_address_t region=base;mach_vm_size_t size=0;mach_port_t object=MACH_PORT_NULL;
    vm_region_basic_info_data_64_t info={0};mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;
    @autoreleasepool {
    id<MTLBuffer> readonly=readonly_page_buffer(task->device,source,4096);
    if(!readonly||readonly.contents==source||((uint8_t *)readonly.contents)[0]!=0x31)return 9;
    ((uint8_t *)readonly.contents)[0]=0x5a;
    if(source[0]!=0x5a)return 10;
    if(mach_vm_region(mach_task_self(),&region,&size,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object)||
        (info.protection&VM_PROT_WRITE))return 11;
    if(object)mach_port_deallocate(mach_task_self(),object);
    released=(mach_vm_address_t)readonly.contents;
    [readonly release];
    } // Drain the driver's autoreleased owners before testing alias retirement.
    // VM addresses may immediately be reused by the driver's destructor.
    // Observe the actual owning lease's retirement, not an unrelated next map.
    if(__atomic_load_n(&retired_alias,__ATOMIC_ACQUIRE)!=released)return 12;
    replace([AppleGFXMetalAlias class],"releaseMapping",release_alias_mapping);
    id<MTLBuffer> gpuAlias=readonly_page_buffer(task->device,source,4096);
    uint32_t value=0x6d6d6d6d;
    id<MTLBuffer> upload=[task->device newBufferWithBytes:&value length:sizeof(value) options:MTLResourceStorageModeShared];
    if(!gpuAlias||!upload)return 14;
    @autoreleasepool {
        id<MTLCommandBuffer> command=[queue commandBuffer];
        id<MTLBlitCommandEncoder> encoder=[command blitCommandEncoder];
        [encoder copyFromBuffer:upload sourceOffset:0 toBuffer:gpuAlias destinationOffset:0 size:sizeof(value)];
        [encoder endEncoding];[command commit];[command waitUntilCompleted];
        if(command.status!=MTLCommandBufferStatusCompleted||memcmp(source,&value,sizeof(value)))return 15;
    }
    [upload release];[gpuAlias release];
    if(mach_vm_protect(mach_task_self(),base,4096,FALSE,VM_PROT_READ|VM_PROT_WRITE))return 13;
    printf("PASS private Metal alias is CPU/GPU coherent, preserves source protection, and retires with buffer\n");
    // A translated4KB task can request a read-only slice at any subpage of
    // the ARM host's16KB allocation. The first-page-only case above cannot
    // detect a remap that silently rounds a nonzero source offset down.
    uint8_t expected[65536];
    for(unsigned page=0;page<length/4096;page++) {
        for(NSUInteger i=0;i<length;i++)source[i]=expected[i]=(i/4096)*13+i%251;
        NSUInteger offset=page*4096;
        if(mach_vm_protect(mach_task_self(),base+offset,4096,FALSE,VM_PROT_READ))return 19;
        id<MTLBuffer> slice=readonly_page_buffer(task->device,source+offset,4096);
        if(!slice||memcmp(slice.contents,expected+offset,4096)) {
            fprintf(stderr,"read-only alias source offset mismatch page=%u\n",page);return 20;
        }
        @autoreleasepool {
            id<MTLCommandBuffer> command=[queue commandBuffer];
            id<MTLBlitCommandEncoder> encoder=[command blitCommandEncoder];
            [encoder fillBuffer:slice range:NSMakeRange(0,4096) value:page^0xa5];
            [encoder endEncoding];[command commit];[command waitUntilCompleted];
            if(command.status!=MTLCommandBufferStatusCompleted)return 21;
        }
        memset(expected+offset,page^0xa5,4096);
        if(memcmp(source,expected,length)) {
            fprintf(stderr,"read-only alias GPU write changed wrong source bytes page=%u\n",page);return 22;
        }
        [slice release];
        if(mach_vm_protect(mach_task_self(),base+offset,4096,FALSE,VM_PROT_READ|VM_PROT_WRITE))return 23;
    }
    printf("PASS all16 read-only4KB source offsets preserve exact GPU-write boundaries\n");
    printf("PASS native contiguous import stays zero-copy; fragmented runs=%lu\n",(unsigned long)shadow->pages.count);
    PGTask *protocol=[PGTask new];
    PGBufferResource *fragmented=[PGBufferResource new],*native=[PGBufferResource new],*private=[PGBufferResource new];
    fragmented->_buffer=buffer;
    native->_buffer=nil; // A not-yet-materialized native resource stays native.
    private->_buffer=buffer;private->_isPrivate=true;
    protocol->resources=@[fragmented,native,private];
    if(!classic_shadow_install()||classic_buffer_class!=[PGBufferResource class])return 16;
    AppleGFXClassicResource list[3]={{.identifier=0},{.identifier=1},{.identifier=2}};
    NSMutableData *copy=classic_readback_resources(protocol,list,3,true);
    if(!copy||!((AppleGFXClassicResource *)copy.mutableBytes)[0].readback||
        ((AppleGFXClassicResource *)copy.mutableBytes)[1].readback||
        ((AppleGFXClassicResource *)copy.mutableBytes)[2].readback||list[0].readback)return 17;
    AppleGFXClassicPrepare sync=[protocol prepareResources:list count:3 event:nil value:0];
    if(!sync.download||list[0].readback)return 18;
    printf("PASS actual classic ABI installer retains resource class and requests shadow readback without altering native/private resources or caller list\n");
    [private release];[native release];[fragmented release];[protocol release];
    [queue release];[buffer release];[task->device release];[task release];
    mach_vm_deallocate(mach_task_self(),alias,length);mach_vm_deallocate(mach_task_self(),base,length);
    return 0;
}}
