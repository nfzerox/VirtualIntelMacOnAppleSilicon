#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <objc/message.h>
#include <dispatch/dispatch.h>
#include <stdio.h>
#ifdef PVG_COMPARE_WIRE
extern dispatch_data_t PVGMojaveSerializeRender(MTLRenderPipelineDescriptor *);
#endif
@interface PVGWireFunction : NSObject
@end
@implementation PVGWireFunction
- (unsigned)functionRef { return 42; }
- (MTLFunctionType)functionType { return MTLFunctionTypeKernel; }
@end
int main(int argc,char **argv) {
    @autoreleasepool {
        id device = MTLCreateSystemDefaultDevice();
        if(argc==2&&!strcmp(argv[1],"--gl-properties")) {
            if(!device)device=[MTLCopyAllDevices() firstObject];
            SEL serialize=sel_registerName("serializeRenderPipelineDescriptor:");
            if(![device respondsToSelector:serialize])return 2;
            NSArray *names=@[@"setPointCoordLowerLeft:",@"setPointSizeOutputVS:",
                @"setTwoSideEnabled:",@"setLogicOperationEnabled:",@"setSampleCoverageInvert:"];
            for(NSString *name in names) {
                MTLRenderPipelineDescriptor *d=[MTLRenderPipelineDescriptor new];
                SEL setter=sel_registerName(name.UTF8String);
                if(![d respondsToSelector:setter]){printf("%s unavailable\n",name.UTF8String);[d release];continue;}
                for(unsigned value=0;value<2;++value) {
                    ((void(*)(id,SEL,BOOL))objc_msgSend)(d,setter,value);
                    dispatch_data_t data=((dispatch_data_t(*)(id,SEL,id))objc_msgSend)(device,serialize,d);
                    if(!data)return 3;
                    const void *bytes=NULL;size_t length=0;
                    dispatch_data_t mapped=dispatch_data_create_map(data,&bytes,&length);
                    printf("%s=%u length=%zu ",name.UTF8String,value,length);
                    for(size_t i=0;i<length;++i)printf("%02x",((const unsigned char*)bytes)[i]);
                    puts("");dispatch_release(mapped);dispatch_release(data);
                }
                [d release];
            }
            return 0;
        }
        SEL serialize = sel_registerName("serializeComputePipelineDescriptor:");
        if (![device respondsToSelector:serialize]) return 2;
        MTLComputePipelineDescriptor *descriptor = [MTLComputePipelineDescriptor new];
        for (unsigned mode=0; mode<5; ++mode) {
            if (mode) descriptor.computeFunction = (id)[PVGWireFunction new];
            if (mode==2) descriptor.threadGroupSizeIsMultipleOfThreadExecutionWidth=YES;
            if (mode==3) descriptor.label=@"hello";
            if (mode==4) descriptor.maxTotalThreadsPerThreadgroup=128;
            id (*send)(id,SEL,id) = (void *)objc_msgSend;
            dispatch_data_t data = (dispatch_data_t)send(device, serialize, descriptor);
            const void *bytes = NULL; size_t length=0;
            dispatch_data_t mapped = dispatch_data_create_map(data,&bytes,&length);
            printf("mode=%u length=%zu\n", mode,length);
            for(size_t i=0;i<length;++i)printf("%02x%c",((const unsigned char *)bytes)[i],i%16==15?'\n':' ');
            puts("");
            dispatch_release(mapped); dispatch_release(data);
        }
        serialize=sel_registerName("serializeRenderPipelineDescriptor:");
        for (unsigned mode=0; mode<12; ++mode) {
            MTLRenderPipelineDescriptor *d=[MTLRenderPipelineDescriptor new];
            if(mode==1) d.vertexFunction=(id)[PVGWireFunction new];
            if(mode==2) d.fragmentFunction=(id)[PVGWireFunction new];
            if(mode==3) d.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;
            if(mode==4) {d.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;d.colorAttachments[0].blendingEnabled=YES;d.colorAttachments[0].sourceRGBBlendFactor=MTLBlendFactorSourceAlpha;d.colorAttachments[0].destinationRGBBlendFactor=MTLBlendFactorOneMinusSourceAlpha;}
            if(mode==5) d.colorAttachments[2].pixelFormat=MTLPixelFormatRGBA16Float;
            if(mode==6) {d.sampleCount=4;d.alphaToCoverageEnabled=YES;d.alphaToOneEnabled=YES;d.rasterizationEnabled=NO;d.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float;d.stencilAttachmentPixelFormat=MTLPixelFormatStencil8;}
            if(mode==7) {d.vertexBuffers[3].mutability=MTLMutabilityImmutable;d.fragmentBuffers[2].mutability=MTLMutabilityMutable;}
            if(mode==8) {d.vertexDescriptor=[MTLVertexDescriptor vertexDescriptor];d.vertexDescriptor.attributes[2].format=MTLVertexFormatFloat3;d.vertexDescriptor.attributes[2].offset=16;d.vertexDescriptor.attributes[2].bufferIndex=3;d.vertexDescriptor.layouts[3].stride=32;}
            if(mode==9) {d.label=@"hello";d.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;d.vertexFunction=(id)[PVGWireFunction new];d.fragmentFunction=(id)[PVGWireFunction new];}
            if(mode==10) {d.colorAttachments[0].rgbBlendOperation=MTLBlendOperationSubtract;d.colorAttachments[0].alphaBlendOperation=MTLBlendOperationMax;d.colorAttachments[0].sourceAlphaBlendFactor=MTLBlendFactorSourceAlpha;d.colorAttachments[0].destinationAlphaBlendFactor=MTLBlendFactorOneMinusSourceAlpha;d.colorAttachments[0].writeMask=MTLColorWriteMaskRed;}
            if(mode==11) {d.tessellationPartitionMode=MTLTessellationPartitionModeFractionalEven;d.maxTessellationFactor=16;d.tessellationFactorScaleEnabled=YES;d.tessellationFactorFormat=MTLTessellationFactorFormatHalf;d.tessellationControlPointIndexType=MTLTessellationControlPointIndexTypeUInt32;d.tessellationFactorStepFunction=MTLTessellationFactorStepFunctionPerInstance;d.tessellationOutputWindingOrder=MTLWindingCounterClockwise;}
            id (*send)(id,SEL,id)=(void *)objc_msgSend;
            dispatch_data_t data=(dispatch_data_t)send(device,serialize,d);
            const void *bytes=NULL;size_t length=0;
            dispatch_data_t mapped=dispatch_data_create_map(data,&bytes,&length);
            printf("render=%u length=%zu\n",mode,length);
#ifdef PVG_COMPARE_WIRE
            dispatch_data_t ours=PVGMojaveSerializeRender(d);
            const void *ourBytes=NULL;size_t ourLength=0;
            dispatch_data_t ourMap=dispatch_data_create_map(ours,&ourBytes,&ourLength);
            if(length!=ourLength||memcmp(bytes,ourBytes,length)) {
                fprintf(stderr,"MISMATCH render=%u native=%zu bridge=%zu\n",mode,length,ourLength);return 3;
            }
            dispatch_release(ourMap);dispatch_release(ours);
#endif
            for(size_t i=0;i<length;++i)printf("%02x%c",((const unsigned char *)bytes)[i],i%16==15?'\n':' ');
            puts("");dispatch_release(mapped);dispatch_release(data);[d release];
        }
    }
    return 0;
}
