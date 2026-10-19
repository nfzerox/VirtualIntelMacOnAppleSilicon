#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#import <objc/message.h>
#include <mach/mach_time.h>
#include <unistd.h>
static double now(void) {
    static mach_timebase_info_data_t t;
    if (!t.denom) mach_timebase_info(&t);
    return (double)mach_absolute_time()*t.numer/t.denom/1e9;
}
int main(int argc, char **argv) {
    alarm(60);
    @autoreleasepool {
        id<MTLDevice> d=MTLCopyAllDevices().firstObject;
        if (!d) return 1;
        NSError *err=nil;
        double start=now();
        id<MTLLibrary> lib=argc>1 ? [d newLibraryWithFile:@(argv[1]) error:&err] :
            [d newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal; kernel void test(device uint *p [[buffer(0)]], uint i [[thread_position_in_grid]]) {p[i]=i*3+7;}" options:nil error:&err];
        id<MTLFunction> f=[lib newFunctionWithName:@"test"];
        // Diagnostic branch coverage only: source stays valid Catalina AIR.
        // Advertise a newer private wrapper version to exercise its converter.
        if (getenv("PVG_BENCH_FORCE_CONVERSION") && f && argc==1) {
            uint8_t *data=((void *(*)(id,SEL))objc_msgSend)(f,sel_registerName("functionData"));
            if(!data)return 9;
            *(uint16_t *)(data+0x28)=2; *(uint16_t *)(data+0x2a)=3;
        }
        id<MTLComputePipelineState> pipeline=[d newComputePipelineStateWithFunction:f error:&err];
        if (!pipeline) {fprintf(stderr,"FAIL pipeline %s\n",err.description.UTF8String);return 2;}
        printf("first_library_pipeline_ms=%.3f\n",(now()-start)*1000);
        start=now();
        for (int i=0;i<20;i++) @autoreleasepool {
            id p=[d newComputePipelineStateWithFunction:f error:&err];
            if (!p) return 3; [p release];
        }
        printf("repeated_pipeline_us=%.3f\n",(now()-start)*1e6/20);
        MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:64 height:64 mipmapped:NO];
        td.storageMode=MTLStorageModePrivate; td.usage=MTLTextureUsageShaderRead;
        start=now();
        for (int i=0;i<500;i++) @autoreleasepool {id t=[d newTextureWithDescriptor:td]; if(!t)return 4;[t release];}
        printf("texture_create_us=%.3f\n",(now()-start)*1e6/500);
        IOSurfaceRef s=IOSurfaceCreate((CFDictionaryRef)@{(id)kIOSurfaceWidth:@64,(id)kIOSurfaceHeight:@64,(id)kIOSurfaceBytesPerElement:@4});
        if(!s)return 5;
        start=now();
        for(int i=0;i<2000;i++){if(IOSurfaceLock(s,0,NULL)||IOSurfaceUnlock(s,0,NULL))return 6;}
        printf("surface_lock_unlock_us=%.3f\n",(now()-start)*1e6/2000); CFRelease(s);
        id<MTLCommandQueue> q=[d newCommandQueue];
        id<MTLBuffer> out=[d newBufferWithLength:4096 options:MTLResourceStorageModeShared];
        start=now();
        for(int i=0;i<100;i++) @autoreleasepool {
            id<MTLCommandBuffer> b=[q commandBuffer]; id<MTLComputeCommandEncoder> e=[b computeCommandEncoder];
            [e setComputePipelineState:pipeline];[e setBuffer:out offset:0 atIndex:0];
            [e dispatchThreadgroups:MTLSizeMake(16,1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)];[e endEncoding];[b commit];[b waitUntilCompleted];
            if(b.status!=MTLCommandBufferStatusCompleted)return 7;
        }
        printf("submit_wait_us=%.3f\n",(now()-start)*1e6/100);
        const uint32_t *p=out.contents;
        for(int i=0;i<1024;i++)if(p[i]!=(uint32_t)i*3+7)return 8;
        printf("PASS verified=1024\n");
        [out release];[q release];[pipeline release];[f release];[lib release];
    }
}
