// Minimal debugger target: identical lengths, contiguous versus two VM entries.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <mach/mach_vm.h>
#include <stdio.h>
static NSUInteger import_length = 8192;
__attribute__((noinline)) id import_fragmented(id<MTLDevice> device, void *bytes) {
    return [device newBufferWithBytesNoCopy:bytes length:import_length
        options:MTLResourceStorageModeShared deallocator:nil];
}
int main(void) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        printf("device-class=%s page=%lu\n", object_getClassName(device), vm_page_size);
        mach_vm_address_t base = 0, alias = 0;
        NSUInteger chunk = getenv("PVG_IMPORT_16K") ? 16384 : 4096;
        BOOL adjacent = getenv("PVG_IMPORT_ADJACENT") != NULL;
        BOOL reverse = getenv("PVG_IMPORT_REVERSE") != NULL;
        BOOL padded = getenv("PVG_IMPORT_PADDED") != NULL;
        if (padded && (chunk != 4096 || adjacent || reverse || getenv("PVG_IMPORT_LAZY"))) return 64;
        NSUInteger count = (adjacent || reverse || padded) ? 4 : 2;
        NSUInteger offsets[4];
        if ((adjacent || reverse) && chunk != 4096) return 64;
        for (NSUInteger i = 0; i < count; i++)
            offsets[i] = padded ? i * 16384 : (reverse ? (3 - i) * chunk : (adjacent ? i * chunk : i * 32768));
        import_length = chunk * count;
        // Align the allocation itself, not merely a pointer inside it: the
        // backing-object offset must also align to the native kernel page.
        if (mach_vm_map(mach_task_self(), &base, 131072, 16383, VM_FLAGS_ANYWHERE,
                MACH_PORT_NULL, 0, FALSE, VM_PROT_READ | VM_PROT_WRITE,
                VM_PROT_ALL, VM_INHERIT_COPY) ||
            mach_vm_map(mach_task_self(), &alias, 65536, 16383, VM_FLAGS_ANYWHERE,
                MACH_PORT_NULL, 0, FALSE, VM_PROT_READ | VM_PROT_WRITE,
                VM_PROT_ALL, VM_INHERIT_COPY)) return 1;
        if (!getenv("PVG_IMPORT_LAZY")) memset((void *)base, 0x19, 65536);
        for (unsigned i = 0; i < count; i++) {
            mach_vm_address_t target = alias + i * chunk;
            vm_prot_t current, maximum;
            if (mach_vm_remap(mach_task_self(), &target, chunk, 0,
                VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, mach_task_self(),
                base + offsets[i], FALSE, &current, &maximum, VM_INHERIT_COPY)) return 2;
        }
        mach_vm_address_t regionAddress = alias;
        mach_vm_size_t regionSize = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t infoCount = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = MACH_PORT_NULL;
        kern_return_t kr = mach_vm_region(mach_task_self(), &regionAddress,
            &regionSize, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info,
            &infoCount, &object);
        printf("alias=%llx region=%llx size=%llx object-offset=%llx kr=%d\n",
            alias, regionAddress, regionSize, info.offset, kr);
        if (object) mach_port_deallocate(mach_task_self(), object);
        id contiguous = [device newBufferWithBytesNoCopy:(void *)base length:import_length
            options:MTLResourceStorageModeShared deallocator:nil];
        printf("contiguous=%p\n", contiguous); fflush(stdout);
        if (padded) {
            // Observed on Apple Silicon: this strided import passes under
            // Rosetta and native ARM 4K, but the packed alias returns nil.
            // Four logical 4K pages, each owning an entire native 16K slot.
            // Importing the padded layout is the control; the packed alias
            // below asks Metal to omit the unused 12K between logical pages.
            id<MTLBuffer> slots = [device newBufferWithBytesNoCopy:(void *)base
                length:65536 options:MTLResourceStorageModeShared deallocator:nil];
            printf("padded-16K-slots=%p useful-bytes=16384 allocated-bytes=65536\n", slots);
            if (!slots) return 6;
            id<MTLCommandQueue> q = [device newCommandQueue];
            id<MTLCommandBuffer> c = [q commandBuffer];
            id<MTLBlitCommandEncoder> b = [c blitCommandEncoder];
            for (NSUInteger i = 0; i < count; i++)
                [b fillBuffer:slots range:NSMakeRange(offsets[i], 4096) value:0x51];
            [b endEncoding]; [c commit]; [c waitUntilCompleted];
            if (c.status != MTLCommandBufferStatusCompleted) return 7;
            for (NSUInteger i = 0; i < 65536; i++)
                if (((uint8_t *)base)[i] != (i % 16384 < 4096 ? 0x51 : 0x19)) return 8;
            puts("padded strided GPU writes and untouched padding: PASS");
            memset((void *)base, 0x19, 65536);
            [q release]; [slots release];
        }
        id fragmented = import_fragmented(device, (void *)alias);
        printf("fragmented chunk=%lu length=%lu buffer=%p\n", chunk, import_length, fragmented);
        if (fragmented) {
            id<MTLCommandQueue> queue = [device newCommandQueue];
            id<MTLCommandBuffer> command = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
            [blit fillBuffer:fragmented range:NSMakeRange(0, import_length) value:0x51];
            [blit endEncoding]; [command commit]; [command waitUntilCompleted];
            if (command.status != MTLCommandBufferStatusCompleted) return 4;
            for (NSUInteger i = 0; i < 65536; i++) {
                BOOL mapped = NO;
                for (NSUInteger j = 0; j < count; j++)
                    mapped |= i >= offsets[j] && i < offsets[j] + chunk;
                uint8_t expected = mapped ? 0x51 : (getenv("PVG_IMPORT_LAZY") ? 0 : 0x19);
                if (((uint8_t *)base)[i] != expected) return 5;
            }
            puts("fragmented zero-copy GPU write and untouched-gap verification: PASS");
            [queue release];
        }
        [fragmented release]; [contiguous release]; [device release];
        return 0;
    }
}
