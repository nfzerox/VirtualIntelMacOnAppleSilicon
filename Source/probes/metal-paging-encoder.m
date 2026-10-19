// Build with -I /path/to/qemu/hw/display. Exercises the actual QEMU adapter.
#import "apple-gfx-paging-encoder.h"
#include <unistd.h>
int main(void) {
    alarm(30);
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> queue=[device newCommandQueue];
        NSError *error=nil;
        id<MTLLibrary> library=[device newLibraryWithSource:
            @"#include <metal_stdlib>\nusing namespace metal; kernel void middle(device uchar *b [[buffer(0)]], uint i [[thread_position_in_grid]]){b[16+i]=0x22;}"
            options:nil error:&error];
        id<MTLComputePipelineState> pipeline=[device newComputePipelineStateWithFunction:[library newFunctionWithName:@"middle"] error:&error];
        if(!pipeline){fprintf(stderr,"pipeline: %s\n",error.description.UTF8String);return 2;}
        id<MTLBuffer> buffer=[device newBufferWithLength:64 options:MTLResourceStorageModeShared];
        id<MTLBuffer> result=[device newBufferWithLength:64 options:MTLResourceStorageModeShared];
        id<MTLEvent> event=[device newEvent];
        id<MTLCommandBuffer> before=[queue commandBuffer];
        [before encodeSignalEvent:event value:7];[before commit];
        id<MTLCommandBuffer> commands=[queue commandBuffer];
        [commands encodeWaitForEvent:event value:7];
        AppleGFXPagingEncoder *paging=[[AppleGFXPagingEncoder alloc] initWithCommandBuffer:commands];
        [(id<MTLBlitCommandEncoder>)paging fillBuffer:buffer range:NSMakeRange(0,64) value:0x11];
        [paging performTransfer:^(id<MTLCommandBuffer> parent) {
            if(parent!=commands)abort();
            id<MTLComputeCommandEncoder> compute=[parent computeCommandEncoder];
            [compute setComputePipelineState:pipeline];[compute setBuffer:buffer offset:0 atIndex:0];
            [compute dispatchThreadgroups:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(16,1,1)];
            [compute endEncoding];
        }];
        [(id<MTLBlitCommandEncoder>)paging fillBuffer:buffer range:NSMakeRange(32,16) value:0x33];
        [paging endEncoding];
        BOOL rejected=NO;
        @try {[(id<MTLBlitCommandEncoder>)paging fillBuffer:buffer range:NSMakeRange(0,1) value:0xff];}
        @catch(NSException *exception){rejected=YES;}
        [paging release];
        [commands encodeSignalEvent:event value:8];[commands commit];
        id<MTLCommandBuffer> after=[queue commandBuffer];
        [after encodeWaitForEvent:event value:8];
        id<MTLBlitCommandEncoder> copy=[after blitCommandEncoder];
        [copy copyFromBuffer:buffer sourceOffset:0 toBuffer:result destinationOffset:0 size:64];[copy endEncoding];
        [after commit];[after waitUntilCompleted];
        if(commands.status!=MTLCommandBufferStatusCompleted||after.status!=MTLCommandBufferStatusCompleted)return 3;
        unsigned bad=0;const unsigned char *bytes=result.contents;
        for(unsigned i=0;i<64;i++){unsigned expected=i>=16&&i<32?0x22:i>=32&&i<48?0x33:0x11;if(bytes[i]!=expected)bad++;}
        fprintf(stderr,"paging adapter blit-compute-blit bytes=64 bad=%u endedRejected=%d events=7,8\n",bad,rejected);
        return bad||!rejected?1:0;
    }
}
