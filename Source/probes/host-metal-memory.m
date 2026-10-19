#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <unistd.h>

int main(void) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        printf("device=%s vm_page_size=%lu sysconf=%ld\n", device.name.UTF8String,
               vm_page_size, sysconf(_SC_PAGESIZE));
        mach_vm_address_t base = 0;
        kern_return_t status = mach_vm_allocate(mach_task_self(), &base, 65536,
                                                VM_FLAGS_ANYWHERE);
        if (status != KERN_SUCCESS) return 1;
        for (size_t offset = 0; offset <= 16384; offset += 4096) {
            for (size_t size = 4096; size <= 16384; size *= 2) {
                id<MTLBuffer> buffer = [device newBufferWithBytesNoCopy:(void *)(base + offset)
                    length:size options:MTLResourceStorageModeShared deallocator:nil];
                printf("offset=%zu length=%zu buffer=%p contents=%p\n",
                       offset, size, buffer, buffer.contents);
                [buffer release];
            }
        }
        mach_vm_address_t alias = 0;
        if (mach_vm_allocate(mach_task_self(), &alias, 65536, VM_FLAGS_ANYWHERE)) return 2;
        for (unsigned i = 0; i < 16; ++i) {
            mach_vm_address_t target = alias + i * 4096;
            vm_prot_t current, maximum;
            status = mach_vm_remap(mach_task_self(), &target, 4096, vm_page_size - 1,
                VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, mach_task_self(),
                base + ((i * 7) % 16) * 4096, false, &current, &maximum, VM_INHERIT_COPY);
            printf("fragment remap index=%u kr=%d target=0x%llx\n", i, status, target);
            if (status != KERN_SUCCESS) return 3;
        }
        for (size_t size = 4096; size <= 65536; size *= 2) {
            id<MTLBuffer> buffer = [device newBufferWithBytesNoCopy:(void *)alias
                length:size options:MTLResourceStorageModeShared deallocator:nil];
            printf("fragmented length=%zu buffer=%p\n", size, buffer);
            [buffer release];
        }
        // Exercise the proposed fallback through a real GPU write and copyback.
        memset((void *)alias, 0x19, 65536);
        id<MTLBuffer> shadow = [device newBufferWithBytes:(void *)alias
            length:65536 options:MTLResourceStorageModeShared];
        if (!shadow || memcmp(shadow.contents, (void *)alias, 65536)) return 4;
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        [blit fillBuffer:shadow range:NSMakeRange(0, 65536) value:0x73];
        [blit endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        if (commands.status != MTLCommandBufferStatusCompleted) return 5;
        memcpy((void *)alias, shadow.contents, 65536);
        for (unsigned i = 0; i < 65536; ++i)
            if (((unsigned char *)base)[i] != 0x73) return 6;
        puts("shadow upload / GPU fill / fragmented copyback: PASS");
        // Test GPU scatter/gather without CPU copies or fragmented imports.
        id<MTLBuffer> pages[16];
        for (unsigned i = 0; i < 16; ++i) {
            pages[i] = [device newBufferWithBytesNoCopy:(void *)(alias + i * 4096)
                length:4096 options:MTLResourceStorageModeShared deallocator:nil];
            if (!pages[i]) return 7;
        }
        commands = [queue commandBuffer];
        blit = [commands blitCommandEncoder];
        [blit fillBuffer:shadow range:NSMakeRange(0, 65536) value:0x41];
        for (unsigned i = 0; i < 16; ++i)
            [blit copyFromBuffer:shadow sourceOffset:i * 4096 toBuffer:pages[i]
                destinationOffset:0 size:4096];
        [blit endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        if (commands.status != MTLCommandBufferStatusCompleted) return 8;
        for (unsigned i = 0; i < 65536; ++i)
            if (((unsigned char *)base)[i] != 0x41) return 9;
        puts("GPU scatter into 16 separate guest-page imports: PASS");
        // Uniform fills cannot detect page swaps or rounded import offsets.
        commands = [queue commandBuffer];
        blit = [commands blitCommandEncoder];
        for (unsigned i = 0; i < 16; ++i)
            [blit fillBuffer:pages[i] range:NSMakeRange(0, 4096) value:i + 1];
        [blit endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        if (commands.status != MTLCommandBufferStatusCompleted) return 10;
        for (unsigned i = 0; i < 16; ++i)
            for (unsigned j = 0; j < 4096; ++j)
                if (((unsigned char *)base)[((i * 7) % 16) * 4096 + j] != i + 1) {
                    fprintf(stderr, "page-address mismatch alias=%u byte=%u\n", i, j);
                    return 11;
                }
        commands = [queue commandBuffer];
        blit = [commands blitCommandEncoder];
        for (unsigned i = 0; i < 16; ++i)
            [blit copyFromBuffer:pages[i] sourceOffset:0 toBuffer:shadow
                destinationOffset:i * 4096 size:4096];
        [blit endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        if (commands.status != MTLCommandBufferStatusCompleted) return 12;
        for (unsigned i = 0; i < 65536; ++i)
            if (((unsigned char *)shadow.contents)[i] != i / 4096 + 1) return 13;
        puts("Distinct-page GPU writes / permutation / gather: PASS");
        // CPU and GPU may update disjoint regions of shared storage. Whole-
        // buffer scatter must not overwrite a later CPU write to an unused byte.
        id<MTLBuffer> baseline = [device newBufferWithBytes:shadow.contents
            length:65536 options:MTLResourceStorageModeShared];
        ((uint8_t *)alias)[7 * 4096 + 99] = 0xea;
        commands = [queue commandBuffer];
        blit = [commands blitCommandEncoder];
        [blit copyFromBuffer:shadow sourceOffset:7 * 4096 toBuffer:pages[7]
            destinationOffset:0 size:4096];
        [blit endEncoding]; [commands commit]; [commands waitUntilCompleted];
        if (commands.status != MTLCommandBufferStatusCompleted) return 14;
        if (((uint8_t *)alias)[7 * 4096 + 99] == 0xea) return 15;
        puts("Whole-page scatter overwrites unrelated newer CPU byte: REPRODUCED");
        ((uint8_t *)alias)[7 * 4096 + 99] = 0xea;
        NSError *error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:
            @"#include <metal_stdlib>\nusing namespace metal;\n"
             "kernel void scatter(device const uchar *after [[buffer(0)]],"
             "device const uchar *before [[buffer(1)]],"
             "device uchar *guest [[buffer(2)]], uint i [[thread_position_in_grid]])"
             "{ if (after[i] != before[i]) guest[i] = after[i]; }"
            options:nil error:&error];
        id<MTLFunction> function = [library newFunctionWithName:@"scatter"];
        id<MTLComputePipelineState> pipeline = function ?
            [device newComputePipelineStateWithFunction:function error:&error] : nil;
        if (!pipeline) { fprintf(stderr, "%s\n", error.description.UTF8String); return 16; }
        commands = [queue commandBuffer];
        blit = [commands blitCommandEncoder];
        [blit fillBuffer:shadow range:NSMakeRange(0, 16) value:0x99];
        [blit endEncoding];
        id<MTLComputeCommandEncoder> compute = [commands computeCommandEncoder];
        [compute setComputePipelineState:pipeline];
        for (unsigned i = 0; i < 16; i++) {
            [compute setBuffer:shadow offset:i * 4096 atIndex:0];
            [compute setBuffer:baseline offset:i * 4096 atIndex:1];
            [compute setBuffer:pages[i] offset:0 atIndex:2];
            [compute dispatchThreads:MTLSizeMake(4096, 1, 1)
                threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
        }
        [compute endEncoding]; [commands commit]; [commands waitUntilCompleted];
        if (commands.status != MTLCommandBufferStatusCompleted) return 17;
        if (((uint8_t *)alias)[7 * 4096 + 99] != 0xea) return 18;
        for (unsigned i = 0; i < 65536; i++) {
            uint8_t expected = i < 16 ? 0x99 : (i == 7 * 4096 + 99 ? 0xea : i / 4096 + 1);
            if (((uint8_t *)alias)[i] != expected) return 19;
        }
        puts("Changed-byte GPU scatter preserves unrelated CPU writes: PASS");
        [pipeline release]; [function release]; [library release]; [baseline release];
        for (unsigned i = 0; i < 16; ++i) [pages[i] release];
        [queue release];
        [shadow release];
        mach_vm_deallocate(mach_task_self(), alias, 65536);
        mach_vm_deallocate(mach_task_self(), base, 65536);
    }
}
