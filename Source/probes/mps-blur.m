#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>
#include <unistd.h>
// Bounded standalone reproducer for the MPS shader/library representation.
int main(void) {
    alarm(20);
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return 2;
        MTLTextureDescriptor *d = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
            width:32 height:32 mipmapped:NO];
        d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        id<MTLTexture> input = [device newTextureWithDescriptor:d];
        id<MTLTexture> output = [device newTextureWithDescriptor:d];
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> command = [queue commandBuffer];
        MPSImageGaussianBlur *blur = [[MPSImageGaussianBlur alloc]
            initWithDevice:device sigma:2.0];
        [blur encodeToCommandBuffer:command sourceTexture:input destinationTexture:output];
        [command commit];
        [command waitUntilCompleted];
        NSLog(@"MPS status=%lu error=%@", (unsigned long)command.status, command.error);
        return command.status == MTLCommandBufferStatusCompleted ? 0 : 1;
    }
}
