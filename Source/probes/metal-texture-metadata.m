#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#include <unistd.h>

static unsigned failures;
static void check(id<MTLTexture> texture, MTLTextureDescriptor *d, const char *kind) {
    if (!texture) { fprintf(stderr,"FAIL %s allocation\n",kind); failures++; return; }
#define CHECK(prop) do { if (texture.prop != d.prop) { \
    fprintf(stderr,"FAIL %s %s expected=%lu got=%lu\n",kind,#prop,(unsigned long)d.prop,(unsigned long)texture.prop); failures++; } } while(0)
    CHECK(width); CHECK(height); CHECK(depth); CHECK(textureType);
    CHECK(pixelFormat); CHECK(mipmapLevelCount); CHECK(arrayLength);
    CHECK(sampleCount); CHECK(usage); CHECK(storageMode); CHECK(cpuCacheMode);
    CHECK(allowGPUOptimizedContents);
#undef CHECK
}
int main(void) {
    alarm(30);
    @autoreleasepool {
        id<MTLDevice> device = MTLCopyAllDevices().firstObject;
        if (!device) return 1;
        for (NSUInteger storage=0;storage<=2;storage++) {
            for (NSUInteger usage=1;usage<=7;usage++) {
                MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:64 height:32 mipmapped:YES];
                d.storageMode=storage; d.usage=usage|MTLTextureUsagePixelFormatView;
                d.allowGPUOptimizedContents=(usage&1)!=0;
                id<MTLTexture> t=[device newTextureWithDescriptor:d];
                check(t,d,"2D");
                id<MTLTexture> view=[t newTextureViewWithPixelFormat:d.pixelFormat];
                check(view,d,"view");
                [view release]; [t release];
                d.textureType=MTLTextureType2DArray; d.arrayLength=3;
                t=[device newTextureWithDescriptor:d]; check(t,d,"array"); [t release];
            }
        }
        MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:64 height:32 mipmapped:NO];
        d.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;
        d.storageMode=MTLStorageModeManaged;
        IOSurfaceRef s=IOSurfaceCreate((CFDictionaryRef)@{(id)kIOSurfaceWidth:@64,(id)kIOSurfaceHeight:@32,(id)kIOSurfaceBytesPerElement:@4,(id)kIOSurfaceBytesPerRow:@256,(id)kIOSurfacePixelFormat:@(0x52474241)});
        id<MTLTexture> t=[device newTextureWithDescriptor:d iosurface:s plane:0];
        check(t,d,"IOSurface"); [t release]; CFRelease(s);
        d.storageMode=MTLStorageModeShared;
        id<MTLBuffer> b=[device newBufferWithLength:8192 options:MTLResourceStorageModeShared];
        t=[b newTextureWithDescriptor:d offset:0 bytesPerRow:256];
        check(t,d,"buffer"); [t release]; [b release];
        fprintf(stderr,"metadata failures=%u\n",failures);
        return failures ? 2 : 0;
    }
}
