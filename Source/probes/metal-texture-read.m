#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#include <unistd.h>
int main(int argc, char **argv) {
    alarm(30);
    @autoreleasepool {
        id<MTLDevice> d = MTLCopyAllDevices().firstObject;
        NSError *error = nil;
        NSString *src = @"#include <metal_stdlib>\nusing namespace metal; kernel void test(texture2d<float, access::read> t [[texture(0)]], device float4 *o [[buffer(0)]], uint2 p [[thread_position_in_grid]]) {o[p.y*64+p.x]=t.read(p);}";
        id<MTLLibrary> l = [d newLibraryWithSource:src options:nil error:&error];
        id<MTLComputePipelineState> pipeline = [d newComputePipelineStateWithFunction:[l newFunctionWithName:@"test"] error:&error];
        if (!pipeline) {fprintf(stderr,"pipeline: %s\n",error.description.UTF8String); return 1;}
        NSUInteger side = argc > 2 ? strtoul(argv[2],NULL,10) : 64;
        MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:side height:side mipmapped:NO];
        desc.storageMode = argc > 1 && !strcmp(argv[1],"managed") ? MTLStorageModeManaged : MTLStorageModeShared;
        desc.usage = MTLTextureUsageShaderRead;
        IOSurfaceRef surface = NULL;
        BOOL upload = argc > 1 && !strcmp(argv[1],"upload");
        BOOL copy = argc > 1 && !strcmp(argv[1],"copy");
        BOOL alias = copy || (argc > 1 && !strcmp(argv[1],"alias"));
        BOOL useSurface = upload || alias || (argc > 1 && !strcmp(argv[1],"iosurface"));
        if (useSurface) {
            surface = IOSurfaceCreate((CFDictionaryRef)@{(id)kIOSurfaceWidth:@(side),(id)kIOSurfaceHeight:@(side),(id)kIOSurfaceBytesPerElement:@4,(id)kIOSurfaceBytesPerRow:@(side*4),(id)kIOSurfacePixelFormat:@(0x52474241)});
            IOSurfaceLock(surface,0,NULL);
            memset(IOSurfaceGetBaseAddress(surface),(alias || upload) ? 0 : 255,IOSurfaceGetAllocSize(surface));
            IOSurfaceUnlock(surface,0,NULL);
            desc.storageMode = MTLStorageModeManaged;
            desc.usage |= MTLTextureUsageRenderTarget;
        }
        id<MTLTexture> t = useSurface ? [d newTextureWithDescriptor:desc iosurface:surface plane:0] : [d newTextureWithDescriptor:desc];
        fprintf(stderr,"descriptor usage=%lu options=%lu texture usage=%lu storage=%lu type=%lu\n",desc.usage,desc.resourceOptions,t.usage,t.storageMode,t.textureType);
        unsigned char data[64*64*4]; memset(data,255,sizeof(data));
        if (!useSurface || upload) [t replaceRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0 withBytes:data bytesPerRow:256];
        id<MTLBuffer> out = [d newBufferWithLength:64*64*16 options:MTLResourceStorageModeShared];
        memset(out.contents,0,out.length);
        id<MTLCommandQueue> q = [d newCommandQueue];
        if (alias) {
            id<MTLCommandBuffer> clear = [q commandBuffer];
            MTLRenderPassDescriptor *p=[MTLRenderPassDescriptor renderPassDescriptor];
            p.colorAttachments[0].texture=t;
            p.colorAttachments[0].loadAction=MTLLoadActionClear;
            p.colorAttachments[0].storeAction=MTLStoreActionStore;
            p.colorAttachments[0].clearColor=MTLClearColorMake(1,1,1,1);
            if (copy) {
                id<MTLTexture> source=[d newTextureWithDescriptor:desc];
                [source replaceRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0 withBytes:data bytesPerRow:256];
                id<MTLBlitCommandEncoder> blit=[clear blitCommandEncoder];
                [blit copyFromTexture:source sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(64,64,1) toTexture:t destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
                [blit endEncoding];
            } else [[clear renderCommandEncoderWithDescriptor:p] endEncoding];
            [clear commit]; [clear waitUntilCompleted];
            t=[d newTextureWithDescriptor:desc iosurface:surface plane:0];
        }
        id<MTLCommandBuffer> b = [q commandBuffer];
        id<MTLComputeCommandEncoder> e = [b computeCommandEncoder];
        [e setComputePipelineState:pipeline]; [e setTexture:t atIndex:0]; [e setBuffer:out offset:0 atIndex:0];
        [e dispatchThreadgroups:MTLSizeMake(8,8,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)]; [e endEncoding];
        [b commit]; [b waitUntilCompleted];
        float *f = out.contents; unsigned errors = 0;
        for (unsigned i=0;i<64*64*4;i++) if (fabsf(f[i]-1)>0.001f) errors++;
        fprintf(stderr,"status=%lu first=%g errors=%u error=%s\n",(unsigned long)b.status,f[0],errors,b.error.description.UTF8String ?: "none");
        return errors || b.status != MTLCommandBufferStatusCompleted;
    }
}
