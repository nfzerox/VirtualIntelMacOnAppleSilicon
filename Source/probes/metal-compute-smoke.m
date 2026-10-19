#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    // Optional element count exercises multi-page host imports without changing
    // the shader. A bounded run cannot leave a stuck diagnostic in the guest.
    NSUInteger count = 1024;
    if (argc > 2) return 64;
    if (argc == 2) {
        char *end = NULL;
        unsigned long n = strtoul(argv[1], &end, 10);
        if (!*argv[1] || *end || n < 1 || n > 1048576) return 64;
        count = n;
    }
    alarm(60);
    @autoreleasepool {
        id<MTLDevice> device = MTLCopyAllDevices().firstObject;
        if (!device) {
            fprintf(stderr, "no Metal device\n");
            return 1;
        }

        uint32_t nonce = arc4random() & 0xffff;
        NSString *source = [NSString stringWithFormat:
            @"#include <metal_stdlib>\n"
             "using namespace metal;\n"
             "kernel void multiply(device const uint *a [[buffer(0)]],\n"
             "                     device const uint *b [[buffer(1)]],\n"
             "                     device uint *out [[buffer(2)]],\n"
             "                     uint i [[thread_position_in_grid]]) {\n"
             "  out[i] = a[i] * b[i] + 7 + %u;\n"
             "}\n", nonce];
        NSError *error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source
                                                     options:nil
                                                       error:&error];
        if (!library) {
            fprintf(stderr, "library creation failed: %s\n",
                    error.description.UTF8String);
            return 2;
        }
        id<MTLFunction> function = [library newFunctionWithName:@"multiply"];
        id<MTLComputePipelineState> pipeline =
            [device newComputePipelineStateWithFunction:function error:&error];
        if (!pipeline) {
            fprintf(stderr, "pipeline creation failed: %s\n",
                    error.description.UTF8String);
            return 3;
        }

        const NSUInteger bytes = count * sizeof(uint32_t);
        BOOL managed = getenv("PVG_COMPUTE_MANAGED") != NULL;
        MTLResourceOptions storage = managed ? MTLResourceStorageModeManaged : MTLResourceStorageModeShared;
        id<MTLBuffer> inputA = [device newBufferWithLength:bytes
                                                   options:storage];
        id<MTLBuffer> inputB = [device newBufferWithLength:bytes
                                                   options:storage];
        id<MTLBuffer> output = [device newBufferWithLength:bytes
                                                   options:storage];
        if (!inputA || !inputB || !output) {
            fprintf(stderr, "buffer allocation failed\n");
            return 4;
        }

        uint32_t *a = inputA.contents;
        uint32_t *b = inputB.contents;
        uint32_t *result = output.contents;
        unsigned rounds = getenv("PVG_COMPUTE_REPEAT") ? 32 : 1;
        id<MTLCommandQueue> queue = [device newCommandQueue];
        for (unsigned round = 0; round < rounds; round++) {
        for (NSUInteger index = 0; index < count; ++index) {
            a[index] = (uint32_t)(index * 3 + 1 + round * 17);
            b[index] = (uint32_t)(index ^ 0x155);
            result[index] = 0xdeadbeef;
        }
        if (managed) {
            [inputA didModifyRange:NSMakeRange(0, bytes)];
            [inputB didModifyRange:NSMakeRange(0, bytes)];
            [output didModifyRange:NSMakeRange(0, bytes)];
        }

        id<MTLCommandBuffer> command = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:pipeline];
        [encoder setBuffer:inputA offset:0 atIndex:0];
        [encoder setBuffer:inputB offset:0 atIndex:1];
        [encoder setBuffer:output offset:0 atIndex:2];
        [encoder dispatchThreads:MTLSizeMake(count, 1, 1)
           threadsPerThreadgroup:MTLSizeMake(
               MIN(count, pipeline.maxTotalThreadsPerThreadgroup), 1, 1)];
        [encoder endEncoding];
        if (managed) {
            id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
            [blit synchronizeResource:output];
            [blit endEncoding];
        }
        [command commit];
        [command waitUntilCompleted];
        if (command.status != MTLCommandBufferStatusCompleted) {
            fprintf(stderr, "command failed: %s\n",
                    command.error.description.UTF8String);
            return 5;
        }
        // Diagnostic only: distinguish an early completion notification from
        // a missing host-to-guest copy. Normal smoke tests have no delay.
        if (getenv("PVG_COMPUTE_DELAY_READBACK")) sleep(2);

        for (NSUInteger index = 0; index < count; ++index) {
            uint32_t expected = a[index] * b[index] + 7 + nonce;
            if (result[index] != expected) {
                fprintf(stderr, "round %u mismatch at %lu: got %u expected %u\n",
                        round, (unsigned long)index, result[index], expected);
                return 6;
            }
        }
        }
        printf("PASS device=%s elements=%lu rounds=%u storage=%s\n", device.name.UTF8String,
               (unsigned long)count, rounds, managed ? "managed" : "shared");
    }
    return 0;
}
