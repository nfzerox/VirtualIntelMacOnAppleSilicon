#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/message.h>
#include <mach/mach_time.h>
#include <unistd.h>
static double now(void){mach_timebase_info_data_t t;mach_timebase_info(&t);return (double)mach_absolute_time()*t.numer/t.denom/1e9;}
static void forceCold(id f){uint8_t *p=((void *(*)(id,SEL))objc_msgSend)(f,sel_registerName("functionData"));if(p){*(uint16_t *)(p+0x28)=2;*(uint16_t *)(p+0x2a)=3;}}
int main(int argc,char **argv){
    alarm(40);setvbuf(stdout,NULL,_IONBF,0);
    @autoreleasepool {
        int mode=argc>1?atoi(argv[1]):0;BOOL render=mode==3 || mode==4;
        id<MTLDevice> d=MTLCopyAllDevices().firstObject;NSError *error=nil;
        NSString *source=@"#include <metal_stdlib>\nusing namespace metal; kernel void k(device uint *o [[buffer(0)]]){o[0]=42;} vertex float4 v(uint n [[vertex_id]]){float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};return float4(p[n],0,1);} fragment float4 f(){return float4(1,0,0,1);}";
        id<MTLLibrary> l=[d newLibraryWithSource:source options:nil error:&error];
        id a=[l newFunctionWithName:render?@"v":@"k"],b=render?[l newFunctionWithName:@"f"]:nil;
        if(!a)return 1;forceCold(a);if(b)forceCold(b);
        dispatch_semaphore_t done=dispatch_semaphore_create(0);
        __block id pipeline=nil;__block int callbacks=0,callbackErrors=0;
        NSObject *callbackLock=[NSObject new];
        void (^complete)(id,NSError *)=^(id p,NSError *e){@synchronized(callbackLock){callbacks++;if(!p || e)callbackErrors++;if(p && !pipeline)pipeline=[p retain];if(e)fprintf(stderr,"callback error=%s\n",e.description.UTF8String);}dispatch_semaphore_signal(done);};
        MTLComputePipelineDescriptor *cd=[MTLComputePipelineDescriptor new];cd.computeFunction=a;
        MTLRenderPipelineDescriptor *rd=[MTLRenderPipelineDescriptor new];rd.vertexFunction=a;rd.fragmentFunction=b;rd.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
        double start=now();
        switch(mode){
        case 0:[d newComputePipelineStateWithFunction:a completionHandler:complete];break;
        case 1:[d newComputePipelineStateWithFunction:a options:0 completionHandler:^(id p,id r,NSError *e){complete(p,e);}];break;
        case 2:[d newComputePipelineStateWithDescriptor:cd options:0 completionHandler:^(id p,id r,NSError *e){complete(p,e);}];break;
        case 3:[d newRenderPipelineStateWithDescriptor:rd completionHandler:complete];break;
        case 4:[d newRenderPipelineStateWithDescriptor:rd options:0 completionHandler:^(id p,id r,NSError *e){complete(p,e);}];break;
        case 5:for(int i=0;i<8;i++)[d newComputePipelineStateWithFunction:a completionHandler:complete];break;
        default:return 64;
        }
        printf("mode=%d caller_return_ms=%.3f\n",mode,(now()-start)*1000);
        // A genuinely async bridge must retain inputs and snapshot descriptors.
        cd.label=@"mutated after request";
        rd.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;
        [cd release];[rd release];[a release];[b release];[l release];
        int expected=mode==5?8:1;
        for(int i=0;i<expected;i++)if(dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,30*NSEC_PER_SEC)))return 2;
        if(!pipeline || callbacks!=expected || callbackErrors)return 3;
        id<MTLCommandQueue> q=[d newCommandQueue];id<MTLCommandBuffer> cb=[q commandBuffer];
        id<MTLBuffer> out=[d newBufferWithLength:256 options:MTLResourceStorageModeShared];memset(out.contents,0,256);
        if(render){
            MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:4 height:4 mipmapped:NO];td.storageMode=MTLStorageModePrivate;td.usage=MTLTextureUsageRenderTarget;
            id<MTLTexture> t=[d newTextureWithDescriptor:td];MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=t;pass.colorAttachments[0].loadAction=MTLLoadActionClear;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
            id<MTLRenderCommandEncoder> e=[cb renderCommandEncoderWithDescriptor:pass];[e setRenderPipelineState:pipeline];[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[e endEncoding];
            id<MTLBlitCommandEncoder> blit=[cb blitCommandEncoder];[blit copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(4,4,1) toBuffer:out destinationOffset:0 destinationBytesPerRow:16 destinationBytesPerImage:64];[blit endEncoding];[t release];
        }else{
            id<MTLComputeCommandEncoder> e=[cb computeCommandEncoder];[e setComputePipelineState:pipeline];[e setBuffer:out offset:0 atIndex:0];[e dispatchThreadgroups:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];[e endEncoding];
        }
        [cb commit];[cb waitUntilCompleted];if(cb.status!=MTLCommandBufferStatusCompleted)return 4;
        if(render){uint8_t *p=out.contents;for(int i=0;i<16;i++)if(p[i*4]!=255 || p[i*4+1]!=0 || p[i*4+2]!=0 || p[i*4+3]!=255)return 5;}
        else if(*(uint32_t *)out.contents!=42)return 6;
        printf("PASS callback=%d GPU output verified\n",callbacks);[pipeline release];[out release];[q release];[callbackLock release];dispatch_release(done);
    }
}
