/* Packed depth/stencil paging. No CPU pixel conversion or independent queue. */
#import <objc/runtime.h>

@interface AppleGFXMSAATransfer : NSObject {
    id<MTLLibrary> library;
    id<MTLComputePipelineState> downloadPipeline;
    id<MTLComputePipelineState> downloadSinglePipeline;
    id<MTLDepthStencilState> depthState;
    NSMutableDictionary *uploadPipelines;
}
+ (instancetype)forDevice:(id<MTLDevice>)device;
- (void)encode:(id<MTLCommandBuffer>)commands texture:(id<MTLTexture>)texture
        buffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset
           row:(NSUInteger)row download:(BOOL)download;
@end

@implementation AppleGFXMSAATransfer
+ (instancetype)forDevice:(id<MTLDevice>)device
{
    static char key;
    @synchronized (device) {
        AppleGFXMSAATransfer *helper = objc_getAssociatedObject(device, &key);
        if (!helper) {
            helper = [[[self alloc] init] autorelease];
            NSError *error = nil;
            NSString *source = @"#include <metal_stdlib>\nusing namespace metal;\n"
                "vertex float4 fullScreen(uint i [[vertex_id]]){const float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};return float4(p[i],0,1);}\n"
                "struct DS {float depth [[depth(any)]];uint stencil [[stencil]];};\n"
                "fragment DS restore(float4 p [[position]],uint s [[sample_id]],const device uchar *bytes [[buffer(0)]],constant uint2 &layout [[buffer(1)]]){"
                "const device uint2 *v=(const device uint2 *)(bytes+uint(p.y)*layout.x+(uint(p.x)*layout.y+s)*8);DS o;o.depth=as_type<float>(v->x);o.stencil=v->y&255;return o;}\n"
                "kernel void download(depth2d_ms<float,access::read> d [[texture(0)]],texture2d_ms<uint,access::read> t [[texture(1)]],device uchar *bytes [[buffer(0)]],constant uint2 &layout [[buffer(1)]],uint2 p [[thread_position_in_grid]]){"
                "if(p.x>=d.get_width()||p.y>=d.get_height())return;for(uint s=0;s<layout.y;s++){device uint2 *v=(device uint2 *)(bytes+p.y*layout.x+(p.x*layout.y+s)*8);*v=uint2(as_type<uint>(d.read(p,s)),t.read(p,s).x);}}\n"
                "kernel void downloadSingle(depth2d<float,access::read> d [[texture(0)]],texture2d<uint,access::read> t [[texture(1)]],device uchar *bytes [[buffer(0)]],constant uint2 &layout [[buffer(1)]],uint2 p [[thread_position_in_grid]]){"
                "if(p.x>=d.get_width()||p.y>=d.get_height())return;device uint2 *v=(device uint2 *)(bytes+p.y*layout.x+p.x*8);*v=uint2(as_type<uint>(d.read(p)),t.read(p).x);}";
            helper->library = [device newLibraryWithSource:source options:nil error:&error];
            id<MTLFunction> function = [helper->library newFunctionWithName:@"download"];
            if (function) {
                helper->downloadPipeline = [device newComputePipelineStateWithFunction:function error:&error];
                [function release];
            }
            function = [helper->library newFunctionWithName:@"downloadSingle"];
            if (function) {
                helper->downloadSinglePipeline = [device newComputePipelineStateWithFunction:function error:&error];
                [function release];
            }
            if (!helper->downloadPipeline || !helper->downloadSinglePipeline) {
                [NSException raise:NSInternalInconsistencyException format:@"MSAA paging shader: %@", error];
            }
            MTLDepthStencilDescriptor *state = [[[MTLDepthStencilDescriptor alloc] init] autorelease];
            state.depthCompareFunction = MTLCompareFunctionAlways;
            state.depthWriteEnabled = YES;
            MTLStencilDescriptor *stencil = [[[MTLStencilDescriptor alloc] init] autorelease];
            stencil.stencilCompareFunction = MTLCompareFunctionAlways;
            stencil.depthStencilPassOperation = MTLStencilOperationReplace;
            state.frontFaceStencil = stencil;
            state.backFaceStencil = stencil;
            helper->depthState = [device newDepthStencilStateWithDescriptor:state];
            helper->uploadPipelines = [[NSMutableDictionary alloc] init];
            objc_setAssociatedObject(device, &key, helper, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
        return helper;
    }
}
- (void)encode:(id<MTLCommandBuffer>)commands texture:(id<MTLTexture>)texture
        buffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset
           row:(NSUInteger)row download:(BOOL)download
{
    NSUInteger width = texture.width, height = texture.height, samples = texture.sampleCount;
    if (texture.pixelFormat != MTLPixelFormatDepth32Float_Stencil8 ||
        texture.textureType != (samples == 1 ? MTLTextureType2D : MTLTextureType2DMultisample) || !width || !height ||
        (samples != 1 && samples != 2 && samples != 4 && samples != 8) || row > UINT32_MAX ||
        width > UINT32_MAX / (samples * 8) || row < width * samples * 8 ||
        height > UINT32_MAX / row ||
        offset % 8 || row % 8 || offset > buffer.length ||
        height > (buffer.length - offset) / row) {
        [NSException raise:NSInvalidArgumentException format:@"Unsupported or out-of-bounds MSAA paging layout"];
    }
    uint32_t layout[2] = {(uint32_t)row, (uint32_t)samples};
    if (download) {
        id<MTLTexture> stencil = [texture newTextureViewWithPixelFormat:MTLPixelFormatX32_Stencil8];
        if (!stencil) {
            [NSException raise:NSInternalInconsistencyException format:@"MSAA stencil view unavailable"];
        }
        id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
        [encoder setComputePipelineState:samples == 1 ? downloadSinglePipeline : downloadPipeline];
        [encoder setTexture:texture atIndex:0];
        [encoder setTexture:stencil atIndex:1];
        [encoder setBuffer:buffer offset:offset atIndex:0];
        [encoder setBytes:layout length:sizeof(layout) atIndex:1];
        [encoder dispatchThreadgroups:MTLSizeMake((width + 7) / 8, (height + 7) / 8, 1)
              threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
        [encoder endEncoding];
        [stencil release];
    } else {
        id<MTLRenderPipelineState> pipeline;
        @synchronized (self) {
            pipeline = uploadPipelines[@(samples)];
            if (!pipeline) {
                MTLRenderPipelineDescriptor *descriptor = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
                id<MTLFunction> vertex = [library newFunctionWithName:@"fullScreen"];
                id<MTLFunction> fragment = [library newFunctionWithName:@"restore"];
                descriptor.vertexFunction = vertex;
                descriptor.fragmentFunction = fragment;
                [vertex release]; [fragment release];
                descriptor.rasterSampleCount = samples;
                descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
                descriptor.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
                NSError *error = nil;
                pipeline = [texture.device newRenderPipelineStateWithDescriptor:descriptor error:&error];
                if (!pipeline) {
                    [NSException raise:NSInternalInconsistencyException format:@"MSAA restore pipeline: %@", error];
                }
                uploadPipelines[@(samples)] = pipeline;
                [pipeline release];
            }
        }
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.depthAttachment.texture = texture;
        pass.depthAttachment.loadAction = MTLLoadActionDontCare;
        pass.depthAttachment.storeAction = MTLStoreActionStore;
        pass.stencilAttachment.texture = texture;
        pass.stencilAttachment.loadAction = MTLLoadActionDontCare;
        pass.stencilAttachment.storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
        [encoder setRenderPipelineState:pipeline];
        [encoder setDepthStencilState:depthState];
        [encoder setFragmentBuffer:buffer offset:offset atIndex:0];
        [encoder setFragmentBytes:layout length:sizeof(layout) atIndex:1];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
    }
}
- (void)dealloc
{
    [library release]; [downloadPipeline release]; [downloadSinglePipeline release]; [depthState release];
    [uploadPipelines release]; [super dealloc];
}
@end
