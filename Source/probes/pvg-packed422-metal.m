#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#include "../vendor/qemu/hw/display/apple-gfx-packed422.h"
#include <assert.h>
#include <string.h>

int main(void) {@autoreleasepool {
    VIMPackedBackingPlane plane={0,1280,720,0x08001400},original=plane;
    assert(VIMNormalizePacked422Plane(0x76323136,&plane)&&plane.width==640);
    assert(!VIMNormalizePacked422Plane(0x76323136,&plane));
    VIMPackedBackingPlane other=original;
    assert(!VIMNormalizePacked422Plane(0x42475241,&other)&&!memcmp(&other,&original,sizeof(other)));
    other.strideAndElement=0x08000100;
    assert(!VIMNormalizePacked422Plane(0x76323136,&other));
    NSDictionary *properties=@{(__bridge NSString *)kIOSurfaceWidth:@(plane.width),
        (__bridge NSString *)kIOSurfaceHeight:@(plane.height),(__bridge NSString *)kIOSurfacePixelFormat:@(0x76323136),
        (__bridge NSString *)kIOSurfacePlaneInfo:@[@{
            (__bridge NSString *)kIOSurfacePlaneWidth:@(plane.width),(__bridge NSString *)kIOSurfacePlaneHeight:@(plane.height),
            (__bridge NSString *)kIOSurfacePlaneBytesPerRow:@5120,(__bridge NSString *)kIOSurfacePlaneBytesPerElement:@8,
            (__bridge NSString *)kIOSurfacePlaneOffset:@0,(__bridge NSString *)kIOSurfacePlaneSize:@(5120*720)}]};
    IOSurfaceRef surface=IOSurfaceCreate((__bridge CFDictionaryRef)properties);assert(surface);
    assert(IOSurfaceLock(surface,0,NULL)==0);
    uint64_t *bytes=IOSurfaceGetBaseAddressOfPlane(surface,0);assert(bytes);
    for(unsigned i=0;i<640*720;i++)bytes[i]=UINT64_C(0x12345678abcd0000)+i;
    assert(IOSurfaceUnlock(surface,0,NULL)==0);
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();assert(device);
    MTLTextureDescriptor *description=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Uint width:640 height:720 mipmapped:NO];
    description.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite|MTLTextureUsageRenderTarget;
    id<MTLTexture> texture=[device newTextureWithDescriptor:description iosurface:surface plane:0];
    assert(texture);
    id<MTLBuffer> copy=[device newBufferWithLength:5120*720 options:MTLResourceStorageModeShared];assert(copy);
    id<MTLCommandQueue> queue=[device newCommandQueue];id<MTLCommandBuffer> command=queue.commandBuffer;
    id<MTLBlitCommandEncoder> blit=command.blitCommandEncoder;
    [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(640,720,1) toBuffer:copy destinationOffset:0 destinationBytesPerRow:5120 destinationBytesPerImage:5120*720];
    [blit endEncoding];[command commit];[command waitUntilCompleted];assert(!command.error);
    assert(!memcmp(bytes,copy.contents,5120*720));
    puts("PASS: packed v216 geometry, unchanged accepted/other layouts, original stride rejection and full GPU byte readback");
    [queue release];[copy release];[texture release];[device release];CFRelease(surface);
    return 0;
}}
