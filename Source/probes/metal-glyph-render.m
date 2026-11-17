// Finite glyph-style fragment masks, premultiplied blending and changing
// constants. Run unchanged on native Metal and a PVG guest. No system hooks.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/message.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#if PVG_TEST_SWIZZLE_TRANSPORT
#include "../compat/PVGTextureViewSwizzle.h"
#endif

enum { Width = 64, Height = 4, Iterations = 8 };

static int exercise(id<MTLDevice> device, id<MTLCommandQueue> queue,
                    BOOL alphaMask, BOOL inlineConstants, BOOL dualSource, BOOL swizzled) {
    NSString *fragment=dualSource?
        @"struct Output { float4 color [[color(0), index(0)]]; float4 coverage [[color(0), index(1)]]; };\n"
         "fragment Output fragment_main(float4 p [[position]],"
         "texture2d<float, access::read> mask [[texture(3)]],"
         "constant float4 &tint [[buffer(4)]]) {"
         "float a=mask.read(uint2(p.xy)).%@; Output o; o.color=float4(tint.rgb,1);"
         "o.coverage=float4(a,1-a,1,a); return o; }":
        @"fragment float4 fragment_main(float4 p [[position]],"
         "texture2d<float, access::read> mask [[texture(3)]],"
         "constant float4 &tint [[buffer(4)]]) {"
         "float a=mask.read(uint2(p.xy)).%@; return float4(tint.rgb*a,a); }";
    NSString *source = [NSString stringWithFormat:
        @"#include <metal_stdlib>\nusing namespace metal;\n"
         "vertex float4 vertex_main(constant float4 *p [[buffer(1)]],"
         "uint i [[vertex_id]]) { return p[i]; }\n%@",
         [NSString stringWithFormat:fragment,(alphaMask!=swizzled) ? @"a" : @"r"]];
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    MTLRenderPipelineDescriptor *pipelineDescription = [MTLRenderPipelineDescriptor new];
    pipelineDescription.vertexFunction = [library newFunctionWithName:@"vertex_main"];
    pipelineDescription.fragmentFunction = [library newFunctionWithName:@"fragment_main"];
    MTLRenderPipelineColorAttachmentDescriptor *color = pipelineDescription.colorAttachments[0];
    color.pixelFormat = MTLPixelFormatBGRA8Unorm;
    color.blendingEnabled = YES;
    color.sourceRGBBlendFactor = dualSource?MTLBlendFactorSource1Color:MTLBlendFactorOne;
    color.destinationRGBBlendFactor = dualSource?MTLBlendFactorOneMinusSource1Color:MTLBlendFactorOneMinusSourceAlpha;
    color.sourceAlphaBlendFactor = dualSource?MTLBlendFactorSource1Alpha:MTLBlendFactorOne;
    color.destinationAlphaBlendFactor = dualSource?MTLBlendFactorOneMinusSource1Alpha:MTLBlendFactorOneMinusSourceAlpha;
    id<MTLRenderPipelineState> pipeline = library ?
        [device newRenderPipelineStateWithDescriptor:pipelineDescription error:&error] : nil;
    if (!pipeline) {
        fprintf(stderr, "pipeline: %s\n", error.description.UTF8String ?: "unavailable");
        return 1;
    }
    MTLTextureDescriptor *maskDescription = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:alphaMask ? MTLPixelFormatA8Unorm : MTLPixelFormatR8Unorm
        width:Width height:Height mipmapped:NO];
    maskDescription.storageMode = MTLStorageModeShared;
    maskDescription.usage = MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
    id<MTLTexture> parentMask = [device newTextureWithDescriptor:maskDescription],mask=parentMask;
    if(swizzled) {
        MTLTextureSwizzleChannels channels=alphaMask?
            MTLTextureSwizzleChannelsMake(MTLTextureSwizzleAlpha,MTLTextureSwizzleZero,MTLTextureSwizzleZero,MTLTextureSwizzleOne):
            MTLTextureSwizzleChannelsMake(MTLTextureSwizzleZero,MTLTextureSwizzleZero,MTLTextureSwizzleZero,MTLTextureSwizzleRed);
        mask=[parentMask newTextureViewWithPixelFormat:maskDescription.pixelFormat textureType:MTLTextureType2D
            levels:NSMakeRange(0,1) slices:NSMakeRange(0,1) swizzle:channels];
        MTLTextureSwizzleChannels observed=mask.swizzle;
        printf("view class=%s parent=%s requested=%u,%u,%u,%u reported=%u,%u,%u,%u\n",
            NSStringFromClass([mask class]).UTF8String,NSStringFromClass([parentMask class]).UTF8String,
            channels.red,channels.green,channels.blue,channels.alpha,
            observed.red,observed.green,observed.blue,observed.alpha);
    }
    MTLTextureDescriptor *targetDescription = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
        width:Width height:Height mipmapped:NO];
    targetDescription.storageMode = MTLStorageModePrivate;
    targetDescription.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> target = [device newTextureWithDescriptor:targetDescription];
    id<MTLBuffer> readback = [device newBufferWithLength:Width * Height * 4
        options:MTLResourceStorageModeShared];
    id<MTLBuffer> uniforms = [device newBufferWithLength:4 * sizeof(float)
        options:MTLResourceStorageModeShared];
    const float positions[12] = {-1,-1,0,1, 3,-1,0,1, -1,3,0,1};
    id<MTLBuffer> vertices = [device newBufferWithBytes:positions length:sizeof(positions)
        options:MTLResourceStorageModeShared];
    if (!mask || !target || !readback || !uniforms || !vertices) {
        fputs("Required render resources unavailable\n", stderr); return 2;
    }
    uint8_t masks[Width * Height];
    for (unsigned i=0;i<sizeof(masks);++i) masks[i]=(i%4)*85;
    [parentMask replaceRegion:MTLRegionMake2D(0,0,Width,Height) mipmapLevel:0
        withBytes:masks bytesPerRow:Width];
    unsigned errors = 0;
    for (unsigned iteration=0;iteration<Iterations;++iteration) {
        float tint[4] = {0,0,0,1}; tint[iteration%3]=1;
        memcpy(uniforms.contents,tint,sizeof(tint));
        memset(readback.contents,0xa5,readback.length);
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture=target;
        pass.colorAttachments[0].loadAction=MTLLoadActionClear;
        pass.colorAttachments[0].storeAction=MTLStoreActionStore;
        pass.colorAttachments[0].clearColor=MTLClearColorMake(.25,.25,.25,1);
        id<MTLCommandBuffer> command = [queue commandBuffer];
        id<MTLRenderCommandEncoder> render = [command renderCommandEncoderWithDescriptor:pass];
        [render setRenderPipelineState:pipeline];
        if (inlineConstants) {
            [render setVertexBytes:positions length:sizeof(positions) atIndex:1];
            [render setFragmentBytes:tint length:sizeof(tint) atIndex:4];
        } else {
            [render setVertexBuffer:vertices offset:0 atIndex:1];
            [render setFragmentBuffer:uniforms offset:0 atIndex:4];
        }
        [render setFragmentTexture:mask atIndex:3];
        [render drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [render endEncoding];
        id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
        [blit copyFromTexture:target sourceSlice:0 sourceLevel:0
            sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(Width,Height,1)
            toBuffer:readback destinationOffset:0 destinationBytesPerRow:Width*4
            destinationBytesPerImage:Width*Height*4];
        [blit endEncoding]; [command commit]; [command waitUntilCompleted];
        if (command.status!=MTLCommandBufferStatusCompleted) {
            fprintf(stderr,"render: %s\n",command.error.description.UTF8String ?: "failed"); return 3;
        }
        const uint8_t *bytes=readback.contents;
        unsigned iterationErrors=0;
        for (unsigned pixel=0;pixel<Width*Height;++pixel) {
            for (unsigned channel=0;channel<4;++channel) {
                unsigned rgb=channel==0?2:channel==2?0:channel;
                float coverage=(pixel%4)/3.f;
                if(dualSource&&rgb==1)coverage=1-coverage;
                if(dualSource&&rgb==2)coverage=1;
                float expected=channel==3?255:255*(tint[rgb]*coverage+.25f*(1-coverage));
                if (fabsf(bytes[pixel*4+channel]-expected)>2) iterationErrors++;
            }
        }
        errors+=iterationErrors;
        printf("mask=%s constants=%s iteration=%u errors=%u first=%u,%u,%u,%u\n",
            alphaMask?"A8":"R8",inlineConstants?"inline":"buffer",iteration,
            iterationErrors,bytes[0],bytes[1],bytes[2],bytes[3]);
    }
    return errors?4:0;
}

int main(int argc,const char **argv) {
    BOOL dualSource=argc==2&&(!strcmp(argv[1],"--dual-source")||!strcmp(argv[1],"--swizzle-dual"));
    BOOL swizzled=argc==2&&(!strcmp(argv[1],"--swizzle")||!strcmp(argv[1],"--swizzle-dual"));
    if(argc!=1&&!dualSource&&!swizzled)return 64;
    alarm(60); // Finite diagnostic only; no provisioning deadline.
    @autoreleasepool {
        // Select explicitly, like the existing PVG resource fixtures: a root
        // command-agent invocation need not have a per-session default device.
        id<MTLDevice> device=MTLCopyAllDevices().firstObject;
        if (!device) { fputs("No Metal device\n",stderr); return 1; }
        SEL serializer=sel_registerName("serializer");
        if ([device respondsToSelector:serializer]) {
            id value=((id(*)(id,SEL))objc_msgSend)(device,serializer);
            printf("device=%s serializer=%s\n",NSStringFromClass([device class]).UTF8String,
                NSStringFromClass([value class]).UTF8String);
#if PVG_TEST_SWIZZLE_TRANSPORT
            // Metal and GL both request this repair. A second installation
            // must be harmless, not replace the saved original with itself.
            if (!pvgInstallTextureSwizzleTransport([value class]) ||
                !pvgInstallTextureSwizzleTransport([value class])) return 3;
#endif
        }
        id<MTLCommandQueue> queue=[device newCommandQueue];
        int failed=0;
        for (unsigned mask=0;mask<2;++mask)
            for (unsigned constants=0;constants<2;++constants)
                failed|=exercise(device,queue,mask,constants,dualSource,swizzled);
        printf("%s glyph-style %smask/%sblend/constants rendering\n",failed?"FAIL":"PASS",swizzled?"swizzled ":"",dualSource?"dual-source ":"");
        return failed?1:0;
    }
}
