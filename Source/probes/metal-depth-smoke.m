#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,const char **argv) {
    alarm(20);
    NSUInteger size=64,samples=1; BOOL packed=NO,unoptimized=NO,color=NO,linear=NO,noReadback=NO,noDepth=NO,splitSync=NO,preserve=NO;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--packed"))packed=YES;
        else if(!strcmp(argv[i],"--color"))color=YES;
        else if(!strcmp(argv[i],"--linear"))linear=YES;
        else if(!strcmp(argv[i],"--no-readback"))noReadback=YES;
        else if(!strcmp(argv[i],"--no-depth"))noDepth=YES;
        else if(!strcmp(argv[i],"--split-sync"))splitSync=YES;
        else if(!strcmp(argv[i],"--preserve"))preserve=YES;
        else if(!strcmp(argv[i],"--unoptimized"))unoptimized=YES;
        else if(!strncmp(argv[i],"--size=",7)) {
            long value=strtol(argv[i]+7,NULL,10);if(value<1||value>2048)return 64;
            size=value;
        } else if(!strncmp(argv[i],"--samples=",10)) {
            long value=strtol(argv[i]+10,NULL,10);
            if(value!=1&&value!=2&&value!=4&&value!=8)return 64;
            samples=value;
        } else return 64;
    }
    @autoreleasepool {
        id<MTLDevice> d = MTLCopyAllDevices().firstObject;
        if (!d) { fprintf(stderr, "no Metal device\n"); return 3; }
        if (![d supportsTextureSampleCount:samples]) {
            fprintf(stderr,"sample count unsupported: %lu\n",(unsigned long)samples);return 3;
        }
        MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:packed?MTLPixelFormatDepth32Float_Stencil8:MTLPixelFormatDepth32Float width:size height:size mipmapped:NO];
        desc.storageMode = MTLStorageModePrivate;
        desc.usage = MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead|MTLTextureUsagePixelFormatView;
        desc.allowGPUOptimizedContents=!unoptimized;
        if(samples>1){desc.textureType=MTLTextureType2DMultisample;desc.sampleCount=samples;}
        id<MTLTexture> t = [d newTextureWithDescriptor:desc];
        if (!t) { fprintf(stderr, "depth texture creation failed\n"); return 1; }
        id<MTLCommandQueue> q = [d newCommandQueue];
        id<MTLCommandBuffer> b = [q commandBuffer];
        MTLRenderPassDescriptor *p = [MTLRenderPassDescriptor renderPassDescriptor];
        id<MTLTexture> resolved=nil;
        if(color) {
            MTLTextureDescriptor *c=[desc copy];
            c.pixelFormat=linear?MTLPixelFormatBGRA8Unorm:MTLPixelFormatBGRA8Unorm_sRGB;
            if(samples==1)c.storageMode=MTLStorageModeManaged;
            id<MTLTexture> target=[d newTextureWithDescriptor:c];
            c.textureType=MTLTextureType2D;c.sampleCount=1;c.storageMode=MTLStorageModeManaged;
            resolved=samples>1?[d newTextureWithDescriptor:c]:target;
            if(!target||!resolved)return 1;
            p.colorAttachments[0].texture=target;
            p.colorAttachments[0].loadAction=MTLLoadActionClear;
            p.colorAttachments[0].clearColor=MTLClearColorMake(1,0,0,1);
            p.colorAttachments[0].storeAction=samples>1?(preserve?MTLStoreActionStoreAndMultisampleResolve:MTLStoreActionMultisampleResolve):MTLStoreActionStore;
            if(samples>1)p.colorAttachments[0].resolveTexture=resolved;
            [c release];
        }
        p.depthAttachment.texture = noDepth?nil:t;
        p.depthAttachment.loadAction = MTLLoadActionClear;
        p.depthAttachment.storeAction = MTLStoreActionStore;
        p.depthAttachment.clearDepth = 0.5;
        if(packed&&!noDepth) {
            p.stencilAttachment.texture=t;
            p.stencilAttachment.loadAction=MTLLoadActionClear;
            p.stencilAttachment.storeAction=MTLStoreActionStore;
            p.stencilAttachment.clearStencil=1;
        }
        [[b renderCommandEncoderWithDescriptor:p] endEncoding];
        if(preserve) {
            [b commit];[b waitUntilCompleted];
            if(b.status!=MTLCommandBufferStatusCompleted)return 2;
            for(unsigned pass=0;pass<8;pass++) {
                b=[q commandBuffer];
                p.colorAttachments[0].loadAction=MTLLoadActionLoad;
                p.depthAttachment.loadAction=MTLLoadActionLoad;
                p.stencilAttachment.loadAction=MTLLoadActionLoad;
                [[b renderCommandEncoderWithDescriptor:p] endEncoding];
                [b commit];[b waitUntilCompleted];
                if(b.status!=MTLCommandBufferStatusCompleted)return 2;
            }
            fprintf(stderr,"completed 8 attachment load/store passes; checking contents below\n");
            b=[q commandBuffer];
        }
        if(color&&!noReadback&&splitSync) {
            [b commit];[b waitUntilCompleted];
            fprintf(stderr,"render-only status=%lu error=%s\n",(unsigned long)b.status,b.error.description.UTF8String?:"none");
            if(b.status!=MTLCommandBufferStatusCompleted)return 2;
            b=[q commandBuffer];
        }
        if(color&&!noReadback) {
            id<MTLBlitCommandEncoder> encoder=[b blitCommandEncoder];
            [encoder synchronizeResource:resolved];[encoder endEncoding];
        }
        [b commit]; [b waitUntilCompleted];
        fprintf(stderr, "depth packed=%d optimized=%d size=%lu samples=%lu color=%d linear=%d readback=%d status=%lu error=%s\n",packed,!unoptimized,(unsigned long)size,(unsigned long)samples,color,linear,color&&!noReadback, (unsigned long)b.status, b.error.description.UTF8String ?: "none");
        if(b.status!=MTLCommandBufferStatusCompleted)return 2;
        if(preserve && !noDepth && samples>1) {
            NSError *error=nil;
            NSString *source=@"#include <metal_stdlib>\nusing namespace metal;\n"
                "kernel void checkDepth(depth2d_ms<float, access::read> t [[texture(0)]], device uint *bad [[buffer(0)]], uint2 p [[thread_position_in_grid]]) {"
                "if(p.x>=t.get_width()||p.y>=t.get_height())return; uint n=0;"
                "for(uint s=0;s<t.get_num_samples();s++)if(abs(t.read(p,s)-0.5f)>0.00001f)n++;"
                "bad[p.y*t.get_width()+p.x]=n;}\n"
                "kernel void checkStencil(texture2d_ms<uint, access::read> t [[texture(0)]], device uint *bad [[buffer(0)]], uint2 p [[thread_position_in_grid]]) {"
                "if(p.x>=t.get_width()||p.y>=t.get_height())return; uint n=0;"
                "for(uint s=0;s<t.get_num_samples();s++)if(t.read(p,s).x!=1)n++;"
                "bad[p.y*t.get_width()+p.x]=n;}";
            id<MTLLibrary> library=[d newLibraryWithSource:source options:nil error:&error];
            for(unsigned plane=0;plane<(packed?2:1);plane++) {
            id<MTLTexture> input=plane?[t newTextureViewWithPixelFormat:MTLPixelFormatX32_Stencil8]:t;
            if(!input){fprintf(stderr,"stencil view unavailable\n");return 8;}
            id<MTLFunction> function=[library newFunctionWithName:plane?@"checkStencil":@"checkDepth"];
            id<MTLComputePipelineState> pipeline=function?[d newComputePipelineStateWithFunction:function error:&error]:nil;
            if(!pipeline){fprintf(stderr,"%s validation compile failed: %s\n",plane?"stencil":"depth",error.description.UTF8String);return 5;}
            id<MTLBuffer> results=[d newBufferWithLength:size*size*sizeof(uint32_t) options:MTLResourceStorageModeShared];
            if(!results)return 5;
            memset(results.contents,0xff,results.length);
            id<MTLCommandBuffer> validation=[q commandBuffer];
            id<MTLComputeCommandEncoder> compute=[validation computeCommandEncoder];
            [compute setComputePipelineState:pipeline];[compute setTexture:input atIndex:0];
            [compute setBuffer:results offset:0 atIndex:0];
            [compute dispatchThreadgroups:MTLSizeMake((size+7)/8,(size+7)/8,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];
            [compute endEncoding];[validation commit];[validation waitUntilCompleted];
            if(validation.status!=MTLCommandBufferStatusCompleted)return 6;
            uint64_t bad=0;uint32_t *values=results.contents;
            for(NSUInteger i=0;i<size*size;i++)bad+=values[i];
            fprintf(stderr,"%s checked pixels=%lu samples=%lu bad=%llu\n",plane?"stencil":"depth",(unsigned long)(size*size),(unsigned long)samples,(unsigned long long)bad);
            if(bad)return 7;
            }
        }
        if(color&&!noReadback) {
            unsigned char pixel[4]={0};
            [resolved getBytes:pixel bytesPerRow:4 fromRegion:MTLRegionMake2D(size/2,size/2,1,1) mipmapLevel:0];
            fprintf(stderr,"resolved BGRA=%u,%u,%u,%u\n",pixel[0],pixel[1],pixel[2],pixel[3]);
            if(pixel[0]||pixel[1]||pixel[2]!=255||pixel[3]!=255)return 1;
        }
        return 0;
    }
}
