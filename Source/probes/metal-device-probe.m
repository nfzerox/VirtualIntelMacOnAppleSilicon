#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <stdio.h>

int main(void) {
    @autoreleasepool {
        NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
        printf("device-count=%lu\n", (unsigned long)devices.count);
        for (id<MTLDevice> device in devices) {
            printf("device=%s registryID=0x%llx lowPower=%d headless=%d removable=%d\n",
                   device.name.UTF8String,
                   (unsigned long long)device.registryID,
                   device.lowPower, device.headless, device.removable);
            id<MTLCommandQueue> queue = [device newCommandQueue];
            id<MTLBuffer> buffer = [device newBufferWithLength:4096
                                                       options:MTLResourceStorageModeShared];
            printf("queue=%s buffer=%s\n",
                   queue != nil ? "yes" : "nil",
                   buffer != nil ? "yes" : "nil");
            if(!queue||!buffer)return 2;
            memset(buffer.contents,0,4096);
            id<MTLCommandBuffer> command=[queue commandBuffer];
            id<MTLBlitCommandEncoder> encoder=[command blitCommandEncoder];
            if(!command||!encoder)return 3;
            [encoder fillBuffer:buffer range:NSMakeRange(0,4096) value:0xA5];
            [encoder endEncoding];[command commit];
            double deadline=NSProcessInfo.processInfo.systemUptime+15;
            while(command.status<MTLCommandBufferStatusCompleted&&NSProcessInfo.processInfo.systemUptime<deadline)
                [NSThread sleepForTimeInterval:.001];
            if(command.status!=MTLCommandBufferStatusCompleted)return 4;
            for(NSUInteger i=0;i<4096;i++)if(((unsigned char *)buffer.contents)[i]!=0xA5)return 5;
            puts("GPU fill/readback=4096/4096");
        }

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) {
            fprintf(stderr, "MTLCreateSystemDefaultDevice returned nil\n");
            return 1;
        }
        printf("default=%s registryID=0x%llx\n",
               device.name.UTF8String,
               (unsigned long long)device.registryID);
        return 0;
    }
}
