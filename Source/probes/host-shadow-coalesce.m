// Local experiment: compile with -I <qemu>/hw/display, x86_64, Metal/Foundation.
// Includes the actual implementation so this tests its run-length transfer loop.
#include "apple-gfx-legacy-shadow.m"
#include <mach/mach_vm.h>
#include <time.h>
static double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
@interface ProbeTask : NSObject {
@public
    void *bytes;
    id<MTLDevice> device;
}
- (void *)mappedAddressForPageNumber:(uint32_t)page length:(uint64_t)length;
- (id)mtlDevice;
@end
@implementation ProbeTask
- (void *)mappedAddressForPageNumber:(uint32_t)page length:(uint64_t)length { return bytes; }
- (id)mtlDevice { return device; }
@end
int main(int argc, char **argv) {
    @autoreleasepool {
        if (vm_page_size != 4096) return 64;
        mach_vm_address_t base = 0, alias = 0;
        const NSUInteger length = argc >= 2 ? strtoul(argv[1], NULL, 10) : 65536;
        BOOL fractured = argc == 3 && !strcmp(argv[2], "fractured");
        if (argc > 3 || (argc == 3 && !fractured)) return 64;
        if (!length || length > 16*1024*1024 || length % 16384) return 64;
        if (mach_vm_map(mach_task_self(), &base, length, 16383, VM_FLAGS_ANYWHERE,
            MACH_PORT_NULL, 0, FALSE, VM_PROT_READ | VM_PROT_WRITE,
            VM_PROT_ALL, VM_INHERIT_COPY)) return 1;
        if (mach_vm_map(mach_task_self(), &alias, length, 16383, VM_FLAGS_ANYWHERE,
            MACH_PORT_NULL, 0, FALSE, VM_PROT_READ | VM_PROT_WRITE,
            VM_PROT_ALL, VM_INHERIT_COPY)) return 2;
        for (NSUInteger i = 0; i < length; ++i) ((uint8_t *)base)[i] = (i * 37 + i / 4096) & 255;
        for (unsigned i = 0; i < length / 4096; ++i) {
            // One fractured 16K group forces fallback; other groups remain
            // importable runs. Reordering intact 16K groups alone is accepted.
            unsigned sourcePage = (length / 16384 - 1 - i / 4) * 4 +
                (fractured || i < 4 ? 3 - i % 4 : i % 4);
            mach_vm_address_t dest = alias + i * 4096;
            vm_prot_t current, maximum;
            if (mach_vm_remap(mach_task_self(), &dest, 4096, 0,
                VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, mach_task_self(),
                base + sourcePage * 4096, FALSE, &current, &maximum, VM_INHERIT_COPY)) return 3;
        }
        ProbeTask *task = [ProbeTask new];
        task->bytes = (void *)alias;
        task->device = MTLCreateSystemDefaultDevice();
        double begin = now();
        id<MTLBuffer> buffer = legacy_buffer(task, NULL, 0, length);
        double setup = now() - begin;
        AppleGFXLegacyShadow *shadow = objc_getAssociatedObject(buffer, &shadow_key);
        if (!shadow) { fprintf(stderr, "Expected fragmented import fallback\n"); return 4; }
        id<MTLCommandQueue> queue = [task->device newCommandQueue];
        id<MTLCommandBuffer> command = [queue commandBuffer];
        id<MTLBlitCommandEncoder> encoder = [command blitCommandEncoder];
        transfer(buffer, encoder, YES);
        [encoder endEncoding]; [command commit]; [command waitUntilCompleted];
        if (command.error || memcmp(buffer.contents, (void *)alias, length)) return 5;
        for (NSUInteger i = 0; i < length; ++i) ((uint8_t *)buffer.contents)[i] ^= 0xa5;
        command = [queue commandBuffer]; encoder = [command blitCommandEncoder];
        transfer(buffer, encoder, NO);
        [encoder endEncoding]; [command commit]; [command waitUntilCompleted];
        if (command.error || memcmp(buffer.contents, (void *)alias, length)) return 6;
        double encoding = 0, transfers = now();
        for (unsigned trial = 0; trial < 20; trial++) {
            @autoreleasepool {
                command = [queue commandBuffer];
                double start = now();
                encoder = [command blitCommandEncoder];
                transfer(buffer, encoder, YES); transfer(buffer, encoder, NO);
                [encoder endEncoding];
                encoding += now() - start;
                [command commit]; [command waitUntilCompleted];
                if (command.error) return 7;
            }
        }
        transfers = now() - transfers;
        if (memcmp(buffer.contents, (void *)alias, length)) return 8;
        printf("PASS runs=%lu bytes=%lu setup=%.6f encode20=%.6f transfer20=%.6f upload/download exact\n",
            (unsigned long)shadow->pages.count, (unsigned long)length, setup,
            encoding, transfers);
        return 0;
    }
}
