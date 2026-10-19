// Host capability probe for a lossless packed depth/stencil paging fallback.
// Restores nonuniform per-sample values and checks them with a compute shader.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <unistd.h>
#ifdef PVG_USE_QEMU_TRANSFER
#import "apple-gfx-paging-encoder.h"
#import "apple-gfx-msaa-transfer.h"
#endif
int main(int argc, const char **argv) {
    NSUInteger width=17,height=13;
    if(argc!=1 && argc!=3) { fprintf(stderr,"usage: %s [width height]\n",argv[0]);return 1; }
    if(argc==3) {
        char *end=NULL;
        unsigned long w=strtoul(argv[1],&end,10);
        if(!*argv[1]||*end||w<1||w>2048)return 1;
        unsigned long h=strtoul(argv[2],&end,10);
        if(!*argv[2]||*end||h<1||h>2048)return 1;
        width=w;height=h;
    }
    alarm(30);
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();
        if(!device)return 2;
        NSString *source=@"#include <metal_stdlib>\nusing namespace metal;\n"
            "vertex float4 fullScreen(uint i [[vertex_id]]) {"
            "const float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};return float4(p[i],0,1);}\n"
            "struct DS { float depth [[depth(any)]]; uint stencil [[stencil]]; };\n"
            "fragment DS restore(float4 p [[position]], uint sample [[sample_id]],"
            "const device uchar *bytes [[buffer(0)]], constant uint2 &layout [[buffer(1)]]) {"
            "const device uint2 *v=(const device uint2 *)(bytes+uint(p.y)*layout.x+(uint(p.x)*layout.y+sample)*8);"
            "DS out;out.depth=as_type<float>(v->x);out.stencil=v->y&255;return out;}\n"
            "kernel void verify(depth2d_ms<float,access::read> d [[texture(0)]],"
            "texture2d_ms<uint,access::read> t [[texture(1)]], const device uchar *bytes [[buffer(0)]],"
            "constant uint2 &layout [[buffer(1)]], device uint *bad [[buffer(2)]], uint2 p [[thread_position_in_grid]]) {"
            "if(p.x>=d.get_width()||p.y>=d.get_height())return; uint n=0;"
            "for(uint s=0;s<layout.y;s++){const device uint2 *v=(const device uint2 *)(bytes+p.y*layout.x+(p.x*layout.y+s)*8);"
            "if(abs(d.read(p,s)-as_type<float>(v->x))>0.000001f||t.read(p,s).x!=(v->y&255))n++;}"
            "bad[p.y*d.get_width()+p.x]=n;}\n"
            "kernel void download(depth2d_ms<float,access::read> d [[texture(0)]],"
            "texture2d_ms<uint,access::read> t [[texture(1)]], device uchar *bytes [[buffer(0)]],"
            "constant uint2 &layout [[buffer(1)]], uint2 p [[thread_position_in_grid]]) {"
            "if(p.x>=d.get_width()||p.y>=d.get_height())return;"
            "for(uint s=0;s<layout.y;s++){device uint2 *v=(device uint2 *)(bytes+p.y*layout.x+(p.x*layout.y+s)*8);"
            "*v=uint2(as_type<uint>(d.read(p,s)),t.read(p,s).x);}}\n"
            "kernel void verifySingle(depth2d<float,access::read> d [[texture(0)]],texture2d<uint,access::read> t [[texture(1)]],const device uchar *bytes [[buffer(0)]],constant uint2 &layout [[buffer(1)]],device uint *bad [[buffer(2)]],uint2 p [[thread_position_in_grid]]){"
            "if(p.x>=d.get_width()||p.y>=d.get_height())return;const device uint2 *v=(const device uint2 *)(bytes+p.y*layout.x+p.x*8);bad[p.y*d.get_width()+p.x]=(abs(d.read(p)-as_type<float>(v->x))>0.000001f||t.read(p).x!=(v->y&255));}\n"
            "kernel void downloadSingle(depth2d<float,access::read> d [[texture(0)]],texture2d<uint,access::read> t [[texture(1)]],device uchar *bytes [[buffer(0)]],constant uint2 &layout [[buffer(1)]],uint2 p [[thread_position_in_grid]]){"
            "if(p.x>=d.get_width()||p.y>=d.get_height())return;device uint2 *v=(device uint2 *)(bytes+p.y*layout.x+p.x*8);*v=uint2(as_type<uint>(d.read(p)),t.read(p).x);}";
        NSError *error=nil;
        id<MTLLibrary> library=[device newLibraryWithSource:source options:nil error:&error];
        if(!library){fprintf(stderr,"library: %s\n",error.description.UTF8String);return 3;}
        for(NSUInteger samples=1;samples<=8;samples*=2) {
            if(![device supportsTextureSampleCount:samples])continue;
            MTLRenderPipelineDescriptor *descriptor=[MTLRenderPipelineDescriptor new];
            descriptor.vertexFunction=[library newFunctionWithName:@"fullScreen"];
            descriptor.fragmentFunction=[library newFunctionWithName:@"restore"];
            descriptor.sampleCount=samples;
            descriptor.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8;
            descriptor.stencilAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8;
            id<MTLRenderPipelineState> pipeline=[device newRenderPipelineStateWithDescriptor:descriptor error:&error];
            fprintf(stderr,"device=%s samples=%lu pipeline=%s error=%s\n",device.name.UTF8String,
                    (unsigned long)samples,pipeline?"PASS":"FAIL",error.description.UTF8String?:"none");
            if(!pipeline)return 4;
            const NSUInteger row=width*samples*8+64,offset=256;
            fprintf(stderr,"size=%lux%lu paddedRow=%lu\n",(unsigned long)width,(unsigned long)height,(unsigned long)row);
            id<MTLBuffer> sourceBuffer=[device newBufferWithLength:offset+row*height+256 options:MTLResourceStorageModeShared];
            memset(sourceBuffer.contents,0xa5,sourceBuffer.length);
            for(NSUInteger y=0;y<height;y++)for(NSUInteger x=0;x<width;x++)for(NSUInteger s=0;s<samples;s++) {
                uint32_t *p=(uint32_t *)((uint8_t *)sourceBuffer.contents+offset+y*row+(x*samples+s)*8);
                float depth=(float)(1+(y*width+x)*samples+s)/(float)(width*height*samples+1);
                memcpy(p,&depth,4);p[1]=(uint32_t)((x+3*y+17*s)&255);
            }
            MTLTextureDescriptor *textureDescriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:width height:height mipmapped:NO];
            textureDescriptor.textureType=samples==1?MTLTextureType2D:MTLTextureType2DMultisample;
            textureDescriptor.sampleCount=samples;textureDescriptor.storageMode=MTLStorageModePrivate;
            textureDescriptor.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead|MTLTextureUsagePixelFormatView;
            id<MTLTexture> texture=[device newTextureWithDescriptor:textureDescriptor];
            id<MTLTexture> stencil=[texture newTextureViewWithPixelFormat:MTLPixelFormatX32_Stencil8];
            if(!texture||!stencil)return 5;
            MTLDepthStencilDescriptor *depthState=[MTLDepthStencilDescriptor new];
            depthState.depthCompareFunction=MTLCompareFunctionAlways;depthState.depthWriteEnabled=YES;
            MTLStencilDescriptor *stencilState=[MTLStencilDescriptor new];
            stencilState.stencilCompareFunction=MTLCompareFunctionAlways;
            stencilState.depthStencilPassOperation=MTLStencilOperationReplace;
            depthState.frontFaceStencil=stencilState;depthState.backFaceStencil=stencilState;
            id<MTLDepthStencilState> state=[device newDepthStencilStateWithDescriptor:depthState];
            id<MTLCommandQueue> queue=[device newCommandQueue];
            id<MTLCommandBuffer> commands=[queue commandBuffer];
            MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];
            pass.depthAttachment.texture=texture;pass.depthAttachment.loadAction=MTLLoadActionDontCare;pass.depthAttachment.storeAction=MTLStoreActionStore;
            pass.stencilAttachment.texture=texture;pass.stencilAttachment.loadAction=MTLLoadActionDontCare;pass.stencilAttachment.storeAction=MTLStoreActionStore;
            uint32_t layout[2]={(uint32_t)row,(uint32_t)samples};
#ifdef PVG_USE_QEMU_TRANSFER
            AppleGFXPagingEncoder *paging=[[AppleGFXPagingEncoder alloc] initWithCommandBuffer:commands];
            [paging performTransfer:^(id<MTLCommandBuffer> parent) {
                [[AppleGFXMSAATransfer forDevice:device] encode:parent texture:texture
                    buffer:sourceBuffer offset:offset row:row download:NO];
            }];
#else
            id<MTLRenderCommandEncoder> render=[commands renderCommandEncoderWithDescriptor:pass];
            [render setRenderPipelineState:pipeline];[render setDepthStencilState:state];
            [render setFragmentBuffer:sourceBuffer offset:offset atIndex:0];
            [render setFragmentBytes:layout length:sizeof(layout) atIndex:1];
            [render drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[render endEncoding];
#endif
            id<MTLComputePipelineState> verifier=[device newComputePipelineStateWithFunction:[library newFunctionWithName:samples==1?@"verifySingle":@"verify"] error:&error];
            if(!verifier)return 6;
            id<MTLBuffer> results=[device newBufferWithLength:width*height*4 options:MTLResourceStorageModeShared];
            memset(results.contents,0xff,results.length);
            id<MTLComputeCommandEncoder> compute=[commands computeCommandEncoder];
            [compute setComputePipelineState:verifier];[compute setTexture:texture atIndex:0];[compute setTexture:stencil atIndex:1];
            [compute setBuffer:sourceBuffer offset:offset atIndex:0];[compute setBytes:layout length:sizeof(layout) atIndex:1];[compute setBuffer:results offset:0 atIndex:2];
            [compute dispatchThreadgroups:MTLSizeMake((width+7)/8,(height+7)/8,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];[compute endEncoding];
            id<MTLComputePipelineState> downloader=[device newComputePipelineStateWithFunction:[library newFunctionWithName:samples==1?@"downloadSingle":@"download"] error:&error];
            if(!downloader)return 9;
            id<MTLBuffer> downloaded=[device newBufferWithLength:sourceBuffer.length options:MTLResourceStorageModeShared];
            memset(downloaded.contents,0x5a,downloaded.length);
#ifdef PVG_USE_QEMU_TRANSFER
            [paging performTransfer:^(id<MTLCommandBuffer> parent) {
                [[AppleGFXMSAATransfer forDevice:device] encode:parent texture:texture
                    buffer:downloaded offset:offset row:row download:YES];
            }];
            [paging endEncoding];[paging release];
#else
            compute=[commands computeCommandEncoder];
            [compute setComputePipelineState:downloader];[compute setTexture:texture atIndex:0];[compute setTexture:stencil atIndex:1];
            [compute setBuffer:downloaded offset:offset atIndex:0];[compute setBytes:layout length:sizeof(layout) atIndex:1];
            [compute dispatchThreadgroups:MTLSizeMake((width+7)/8,(height+7)/8,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];[compute endEncoding];
#endif
            [commands commit];[commands waitUntilCompleted];
            if(commands.status!=MTLCommandBufferStatusCompleted){fprintf(stderr,"GPU error: %s\n",commands.error.description.UTF8String);return 7;}
            uint64_t bad=0;for(NSUInteger i=0;i<width*height;i++)bad+=((uint32_t *)results.contents)[i];
            fprintf(stderr,"restore samples=%lu values=%lu bad=%llu\n",(unsigned long)samples,(unsigned long)(width*height*samples),(unsigned long long)bad);
            if(bad)return 8;
            NSUInteger mismatched=0,paddingChanged=0;
            for(NSUInteger y=0;y<height;y++) {
                const uint8_t *expected=(const uint8_t *)sourceBuffer.contents+offset+y*row;
                const uint8_t *actual=(const uint8_t *)downloaded.contents+offset+y*row;
                for(NSUInteger x=0;x<width*samples*8;x++)if(expected[x]!=actual[x])mismatched++;
                for(NSUInteger x=width*samples*8;x<row;x++)if(actual[x]!=0x5a)paddingChanged++;
            }
            for(NSUInteger i=0;i<downloaded.length;i++) {
                if(i>=offset && i<offset+row*height)continue;
                if(((uint8_t *)downloaded.contents)[i]!=0x5a)paddingChanged++;
            }
            fprintf(stderr,"roundtrip samples=%lu offset=%lu mismatchedBytes=%lu paddingChanged=%lu\n",(unsigned long)samples,(unsigned long)offset,(unsigned long)mismatched,(unsigned long)paddingChanged);
            if(mismatched||paddingChanged)return 10;
        }
    }
    return 0;
}
