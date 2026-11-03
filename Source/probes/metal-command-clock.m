/* Qualify Metal's reported host seconds against actual CPU submission and
 * completion before treating GPUStartTime as queue latency. No VM changes. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <assert.h>
#include <stdio.h>
int main(void)
{
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();assert(device);
        id<MTLCommandQueue> queue=[device newCommandQueue];assert(queue);
        id<MTLBuffer> buffer=[device newBufferWithLength:4*1024*1024 options:MTLResourceStorageModeShared];assert(buffer);
        printf("device=%s; raw host seconds; GPU end must not follow CPU completion\n",device.name.UTF8String);
        for(unsigned i=0;i<8;++i){
            id<MTLCommandBuffer> command=[queue commandBuffer];id<MTLBlitCommandEncoder> encoder=[command blitCommandEncoder];
            assert(command && encoder);[encoder fillBuffer:buffer range:NSMakeRange(0,buffer.length) value:(uint8_t)(i+1)];
            [encoder endEncoding];CFTimeInterval before=CACurrentMediaTime();[command commit];[command waitUntilCompleted];
            CFTimeInterval after=CACurrentMediaTime(),start=command.GPUStartTime,end=command.GPUEndTime;
            assert(command.status==MTLCommandBufferStatusCompleted);
            assert(((volatile uint8_t *)buffer.contents)[0]==i+1 &&
                   ((volatile uint8_t *)buffer.contents)[buffer.length-1]==i+1);
            printf("round=%u submitted=%.9f GPU_start=%.9f GPU_end=%.9f CPU_completed=%.9f queue_ms=%.3f work_ms=%.3f end_minus_CPU_ms=%.3f ordered=%d\n",
                i,before,start,end,after,(start-before)*1000,(end-start)*1000,(end-after)*1000,
                start>=before && end>=start && end<=after);
        }
    }
    return 0;
}
