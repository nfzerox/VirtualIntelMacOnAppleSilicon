#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#include <unistd.h>

// Alternating CPU upload and partial GPU overwrite, retaining the texture and
// staging buffers across submissions. No shader compiler or UI is involved.
int main(void) {
    alarm(60);
    @autoreleasepool {
        id<MTLDevice> device = MTLCopyAllDevices().firstObject;
        id<MTLCommandQueue> queue = [device newCommandQueue];
        const NSUInteger w = 1024, h = 768, pitch = w * 4, length = pitch * h;
        MTLTextureDescriptor *td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
            width:w height:h mipmapped:NO];
        td.storageMode = getenv("PVG_TEXTURE_MANAGED") ? MTLStorageModeManaged : MTLStorageModeShared;
        td.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        IOSurfaceRef surface = NULL;
        if (getenv("PVG_TEXTURE_IOSURFACE")) {
            surface = IOSurfaceCreate((__bridge CFDictionaryRef)@{
                (id)kIOSurfaceWidth:@(w), (id)kIOSurfaceHeight:@(h),
                (id)kIOSurfaceBytesPerElement:@4, (id)kIOSurfaceBytesPerRow:@(pitch),
                (id)kIOSurfaceAllocSize:@(length), (id)kIOSurfacePixelFormat:@((uint32_t)'RGBA')});
            if (!surface) return 2;
        }
        id<MTLTexture> texture = surface ? [device newTextureWithDescriptor:td
            iosurface:surface plane:0] : [device newTextureWithDescriptor:td];
        id<MTLBuffer> patch = [device newBufferWithLength:length options:MTLResourceStorageModeShared];
        id<MTLBuffer> readback = [device newBufferWithLength:length options:MTLResourceStorageModeShared];
        NSMutableData *expected = [NSMutableData dataWithLength:length];
        if (!queue || !texture || !patch || !readback) {
            fprintf(stderr, "allocation failure device=%s queue=%p texture=%p patch=%p readback=%p storage=%lu\n",
                device.name.UTF8String, queue, texture, patch, readback, (unsigned long)td.storageMode);
            return 2;
        }
        for (unsigned round = 0; round < 16; ++round) {
            uint8_t *bytes = expected.mutableBytes;
            for (NSUInteger i = 0; i < length; ++i) bytes[i] = (i * 13 + round * 17) & 255;
            if (surface && getenv("PVG_TEXTURE_SURFACE_CPU")) {
                if (IOSurfaceLock(surface, 0, NULL) != kIOReturnSuccess) return 5;
                memcpy(IOSurfaceGetBaseAddress(surface), bytes, length);
                if (IOSurfaceUnlock(surface, 0, NULL) != kIOReturnSuccess) return 5;
            } else {
                [texture replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0
                    withBytes:bytes bytesPerRow:pitch];
            }
            memset(patch.contents, 0x40 + round, length);
            NSUInteger x = round * 37, y = round * 23;
            const NSUInteger pw = 257, ph = 129;
            id<MTLCommandBuffer> command = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
            [blit copyFromBuffer:patch sourceOffset:0 sourceBytesPerRow:pitch
                sourceBytesPerImage:length sourceSize:MTLSizeMake(pw, ph, 1)
                toTexture:texture destinationSlice:0 destinationLevel:0
                destinationOrigin:MTLOriginMake(x, y, 0)];
            [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
                toBuffer:readback destinationOffset:0 destinationBytesPerRow:pitch
                destinationBytesPerImage:length];
            [blit endEncoding]; [command commit]; [command waitUntilCompleted];
            if (command.status != MTLCommandBufferStatusCompleted) {
                fprintf(stderr, "GPU failure round=%u %s\n", round, command.error.description.UTF8String);
                return 3;
            }
            for (NSUInteger row = y; row < y + ph; ++row)
                memset(bytes + row * pitch + x * 4, 0x40 + round, pw * 4);
            const uint8_t *actual = readback.contents;
            for (NSUInteger i = 0; i < length; ++i) {
                if (actual[i] != bytes[i]) {
                    fprintf(stderr, "FAIL round=%u byte=%lu xy=%lu,%lu actual=%u expected=%u\n",
                        round, (unsigned long)i, (unsigned long)((i % pitch) / 4),
                        (unsigned long)(i / pitch), actual[i], bytes[i]);
                    return 4;
                }
            }
            printf("PASS reuse round=%u bytes=%lu\n", round, (unsigned long)length);
            fflush(stdout);
        }
        if (surface) CFRelease(surface);
    }
    return 0;
}
