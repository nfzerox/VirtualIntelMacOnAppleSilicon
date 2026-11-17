// Load only into the owned PVG helper for a bounded failure reproduction.
// Observe failed Metal imports without changing the descriptor or result.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#import <objc/runtime.h>
#include <stdio.h>

static IMP originalTexture;
static id traceTexture(id self,SEL selector,MTLTextureDescriptor *descriptor,
                       IOSurfaceRef surface,NSUInteger plane) {
    id result=((id(*)(id,SEL,id,IOSurfaceRef,NSUInteger))originalTexture)
        (self,selector,descriptor,surface,plane);
    static unsigned failures;
    if(!result&&__atomic_fetch_add(&failures,1,__ATOMIC_RELAXED)<8) {
        fprintf(stderr,"PVG import failed: format=%lu size=%lux%lu usage=0x%lx storage=%lu type=%lu plane=%lu surface=%p",
            (unsigned long)descriptor.pixelFormat,(unsigned long)descriptor.width,(unsigned long)descriptor.height,
            (unsigned long)descriptor.usage,(unsigned long)descriptor.storageMode,
            (unsigned long)descriptor.textureType,(unsigned long)plane,surface);
        if(surface)fprintf(stderr," surface-format=0x%x size=%zux%zu planes=%zu pitch=%zu plane-size=%zux%zu plane-pitch=%zu",
            IOSurfaceGetPixelFormat(surface),IOSurfaceGetWidth(surface),IOSurfaceGetHeight(surface),
            IOSurfaceGetPlaneCount(surface),IOSurfaceGetBytesPerRow(surface),
            IOSurfaceGetWidthOfPlane(surface,plane),IOSurfaceGetHeightOfPlane(surface,plane),
            IOSurfaceGetBytesPerRowOfPlane(surface,plane));
        fprintf(stderr,"\n");
    }
    return result;
}

__attribute__((constructor)) static void install(void) {
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    Class cls=object_getClass(device);
    SEL selector=@selector(newTextureWithDescriptor:iosurface:plane:);
    Method method=class_getInstanceMethod(cls,selector);
    if(method) {
        originalTexture=method_getImplementation(method);
        class_replaceMethod(cls,selector,(IMP)traceTexture,method_getTypeEncoding(method));
        fprintf(stderr,"PVG failed-texture observer installed\n");
    }
    [device release];
}
