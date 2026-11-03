// Exact shader-output/CPU-reference controls and bounded CPU-clock timings.
// This is observer capacity, NOT guest FPS, presentation or input latency.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import "../VirtualIntelMac/VIMDisplayFingerprint.h"
#include <stdint.h>

static void check(BOOL good, NSString *message) {
    if(!good){fprintf(stderr,"FAIL %s\n",message.UTF8String);exit(1);}
}
static uint32_t mixword(uint32_t x) {
    x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);
}
static void encode(id<MTLCommandBuffer> command,id<MTLComputePipelineState> pipeline,
                   id<MTLTexture> texture,id<MTLBuffer> output,const uint32_t roi[4],NSUInteger mode) {
    id<MTLComputeCommandEncoder> compute=[command computeCommandEncoder];
    check(compute!=nil,@"compute encoder");[compute setComputePipelineState:pipeline];
    [compute setTexture:texture atIndex:0];[compute setBuffer:output offset:0 atIndex:0];
    [compute setBytes:roi length:16 atIndex:1];
    [compute dispatchThreadgroups:MTLSizeMake((texture.width+15)/16,(texture.height+15)/16,1)
           threadsPerThreadgroup:mode==2?MTLSizeMake(pipeline.threadExecutionWidth,1,1):MTLSizeMake(16,16,1)];[compute endEncoding];
}
static void finish(id<MTLCommandBuffer> command) {
    [command commit];[command waitUntilCompleted];
    check(command.status==MTLCommandBufferStatusCompleted,command.error.description?:@"command completion");
}
static void encodeCopy(id<MTLCommandBuffer> command,id<MTLComputePipelineState> pipeline,
                       id<MTLTexture> source,id<MTLTexture> destination,id<MTLBuffer> output,
                       const uint32_t roi[4]) {
    id<MTLComputeCommandEncoder> compute=[command computeCommandEncoder];
    check(compute!=nil,@"copy/reduction encoder");[compute setComputePipelineState:pipeline];
    [compute setTexture:source atIndex:0];[compute setTexture:destination atIndex:1];
    [compute setBuffer:output offset:0 atIndex:0];[compute setBytes:roi length:16 atIndex:1];
    uint32_t extent[]={(uint32_t)destination.width,(uint32_t)destination.height};
    [compute setBytes:extent length:sizeof(extent) atIndex:2];
    [compute dispatchThreadgroups:MTLSizeMake((destination.width+15)/16,(destination.height+15)/16,1)
           threadsPerThreadgroup:MTLSizeMake(pipeline.threadExecutionWidth,1,1)];[compute endEncoding];
}
static void encodeBlit(id<MTLCommandBuffer> command,id<MTLTexture> source,id<MTLTexture> destination) {
    id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];check(blit!=nil,@"copy encoder");
    [blit copyFromTexture:source sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(destination.width,destination.height,1) toTexture:destination
        destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];[blit endEncoding];
}
int main(void){@autoreleasepool{
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();check(device!=nil,@"Metal device");
    id<MTLCommandQueue> queue=[device newCommandQueue];
    id<MTLComputePipelineState> pipelines[3];
    for(NSUInteger i=0;i<3;i++){
        NSError *error=nil;
        id<MTLLibrary> library=[device newLibraryWithSource:VIMDisplayFingerprintSource(i) options:nil error:&error];
        check(library!=nil,error.description?:@"shader compilation");
        pipelines[i]=[device newComputePipelineStateWithFunction:[library newFunctionWithName:@"fingerprint"] error:&error];
        check(pipelines[i].maxTotalThreadsPerThreadgroup>=256,error.description?:@"pipeline capacity");
    }
    NSError *copyError=nil;
    id<MTLLibrary> copyLibrary=[device newLibraryWithSource:VIMDisplayFingerprintSourceWithCopy(2,YES) options:nil error:&copyError];
    id<MTLComputePipelineState> copyPipeline=[device newComputePipelineStateWithFunction:
        [copyLibrary newFunctionWithName:@"fingerprint"] error:&copyError];
    check(copyPipeline!=nil,copyError.description?:@"copy/reduction pipeline");
    printf("DEVICE %s simd_width=%lu Mac2=%d Apple7=%d\n",device.name.UTF8String,
        (unsigned long)pipelines[1].threadExecutionWidth,[device supportsFamily:MTLGPUFamilyMac2],
        [device supportsFamily:MTLGPUFamilyApple7]);
    const NSUInteger sizes[][2]={{1,1},{15,17},{16,16},{17,31},{513,257},{2560,1540},{4097,19}};
    for(NSUInteger format=0;format<2;format++)for(NSUInteger s=0;s<sizeof(sizes)/sizeof(sizes[0]);s++){
        NSUInteger w=sizes[s][0],h=sizes[s][1],gx=(w+15)/16,gy=(h+15)/16,bytes=gx*gy*16;
        NSMutableData *pixels=[NSMutableData dataWithLength:w*h*4];uint8_t *p=pixels.mutableBytes;
        uint32_t seed=0x31415926;
        for(NSUInteger n=0;n<pixels.length;n++){seed=seed*1664525u+1013904223u;p[n]=seed>>24;}
        MTLTextureDescriptor *desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
            format?MTLPixelFormatBGRA8Unorm:MTLPixelFormatRGBA8Unorm width:w height:h mipmapped:NO];
        desc.storageMode=MTLStorageModeShared;desc.usage=MTLTextureUsageShaderRead;
        id<MTLTexture> texture=[device newTextureWithDescriptor:desc];check(texture!=nil,@"test texture");
        [texture replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0 withBytes:p bytesPerRow:w*4];
        // Larger source/row pitch than the actual displayed crop: the hash's
        // pixel index and tile count must use displayed dimensions, not these
        // backing dimensions. The crop must copy alpha too, not just RGB.
        MTLTextureDescriptor *paddedDesc=[desc copy];paddedDesc.width=w+7;paddedDesc.height=h+3;
        id<MTLTexture> padded=[device newTextureWithDescriptor:paddedDesc];
        NSMutableData *paddedPixels=[NSMutableData dataWithLength:(w+7)*(h+3)*4];
        memset(paddedPixels.mutableBytes,0xa5,paddedPixels.length);
        for(NSUInteger y=0;y<h;y++)memcpy((uint8_t *)paddedPixels.mutableBytes+y*(w+7)*4,p+y*w*4,w*4);
        [padded replaceRegion:MTLRegionMake2D(0,0,w+7,h+3) mipmapLevel:0
            withBytes:paddedPixels.bytes bytesPerRow:(w+7)*4];
        MTLTextureDescriptor *copyDesc=[desc copy];copyDesc.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;
        id<MTLTexture> copied=[device newTextureWithDescriptor:copyDesc];check(copied!=nil,@"copied texture");
        id<MTLBuffer> copyOutput=[device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
        NSMutableData *copiedPixels=[NSMutableData dataWithLength:w*h*4];
        id<MTLBuffer> output[3]={[device newBufferWithLength:bytes options:MTLResourceStorageModeShared],
                              [device newBufferWithLength:bytes options:MTLResourceStorageModeShared],
                              [device newBufferWithLength:bytes options:MTLResourceStorageModeShared]};
        for(NSUInteger region=0;region<3;region++){
            uint32_t roi[4]={0,0,0,0};
            if(region==1){roi[2]=(uint32_t)w;roi[3]=(uint32_t)h;}
            if(region==2){roi[0]=w/3;roi[1]=h/4;roi[2]=w+13;roi[3]=h+17;}
            id<MTLCommandBuffer> command=[queue commandBuffer];
            for(NSUInteger i=0;i<3;i++)encode(command,pipelines[i],texture,output[i],roi,i);
            encodeCopy(command,copyPipeline,padded,copied,copyOutput,roi);
            finish(command);
            check(!memcmp(output[0].contents,output[1].contents,bytes),@"old/new exact tile outputs");
            check(!memcmp(output[0].contents,output[2].contents,bytes),@"old/single-SIMD exact tile outputs");
            NSMutableData *reference=[NSMutableData dataWithLength:bytes];uint32_t *r=reference.mutableBytes;
            for(NSUInteger tile=0;tile<gx*gy;tile++)r[tile*4+2]=UINT32_MAX;
            for(NSUInteger y=0;y<h;y++)for(NSUInteger x=0;x<w;x++){
                NSUInteger index=y*w+x,at=((y/16)*gx+x/16)*4;const uint8_t *c=p+index*4;
                uint32_t red=c[format?2:0],green=c[1],blue=c[format?0:2],v=red|(green<<8)|(blue<<16);
                uint32_t a=mixword(v^mixword((uint32_t)index));r[at]+=a;
                r[at+1]+=mixword(v+(uint32_t)index*0x9e3779b9u+0x85ebca6bu);
                if(x>=roi[0]&&x<roi[2]&&y>=roi[1]&&y<roi[3]){
                    r[at+3]+=a;
                    if(red>180&&green<140&&blue<140&&red>green+80&&red>blue+80&&green<=blue+24&&blue<=green+24)
                        r[at+2]=MIN(r[at+2],(uint32_t)((y<<16)|x));
                }
            }
            check(!memcmp(output[1].contents,r,bytes),@"all pixels/ROI/marker exact CPU reference");
            check(!memcmp(copyOutput.contents,r,bytes),@"fused cropped copy/reduction exact CPU tile reference");
            [copied getBytes:copiedPixels.mutableBytes bytesPerRow:w*4 fromRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0];
            check(!memcmp(p,copiedPixels.bytes,pixels.length),@"fused copy preserves EVERY RGBA/BGRA byte including alpha");
        }
        printf("PASS %lux%lu %s full/empty/clipped-ROI all tile words\n",(unsigned long)w,(unsigned long)h,format?"BGRA":"RGBA");
        if(w==2560&&format==1){
            uint32_t roi[4]={19,17,517,419};
            NSMutableArray<NSNumber *> *times[5]={[NSMutableArray array],[NSMutableArray array],[NSMutableArray array],
                [NSMutableArray array],[NSMutableArray array]};
            // Interleave after warm-up, avoiding false gains from frequency
            // ramping between sequential old/new/old blocks.
            for(NSUInteger trial=0;trial<140;trial++){
                for(NSUInteger step=0;step<5;step++){
                    NSUInteger candidate=(step+trial)%5;
                    id<MTLCommandBuffer> command=[queue commandBuffer];
                    if(candidate==3)encodeCopy(command,copyPipeline,padded,copied,copyOutput,roi);
                    else if(candidate==4){encodeBlit(command,padded,copied);encode(command,pipelines[2],copied,copyOutput,roi,2);}
                    else encode(command,pipelines[candidate],texture,output[candidate],roi,candidate);
                    CFTimeInterval begin=CACurrentMediaTime();finish(command);
                    if(trial>=20)[times[candidate] addObject:@((CACurrentMediaTime()-begin)*1000)];
                }
            }
            for(NSUInteger candidate=0;candidate<5;candidate++){
                [times[candidate] sortUsingSelector:@selector(compare:)];
                printf("TIMING %s interleaved CPU_commit_to_completion_ms median=%.4f p95=%.4f (observer only)\n",
                    candidate==4?"blit-then-single-SIMD":candidate==3?"fused-copy-single-SIMD":candidate==2?"single-SIMD":candidate?"SIMD-partials":"tree",
                    times[candidate][60].doubleValue,times[candidate][114].doubleValue);
            }
        }
    }
    puts("PASS exact dense fingerprint reduction; no presentation/FPS claim");return 0;
}}
