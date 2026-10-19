// Local component experiment. Compare exact selective writes, not whole copies.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        NSError *error = nil;
        NSString *source = @"#include <metal_stdlib>\nusing namespace metal;\n"
          "kernel void bytes(device const uchar *a[[buffer(0)]],device const uchar *b[[buffer(1)]],"
          "device uchar *g[[buffer(2)]],uint i[[thread_position_in_grid]]){if(a[i]!=b[i])g[i]=a[i];}\n"
          "kernel void wide(device const uint4 *a[[buffer(0)]],device const uint4 *b[[buffer(1)]],"
          "device uchar *g[[buffer(2)]],uint i[[thread_position_in_grid]]){"
          "uint4 x=a[i],y=b[i];if(all(x==y))return;"
          "for(uint w=0;w<4;w++){if(x[w]==y[w])continue;"
          "for(uint k=0;k<4;k++){uchar v=uchar(x[w]>>(k*8));"
          "if(v!=uchar(y[w]>>(k*8)))g[i*16+w*4+k]=v;}}}\n";
        id<MTLLibrary> lib = [device newLibraryWithSource:source options:nil error:&error];
        if (!lib) { NSLog(@"%@",error); return 1; }
        const NSUInteger length=16*1024*1024;
        id<MTLBuffer> a=[device newBufferWithLength:length options:MTLResourceStorageModeShared];
        id<MTLBuffer> b=[device newBufferWithLength:length options:MTLResourceStorageModeShared];
        id<MTLBuffer> g=[device newBufferWithLength:length options:MTLResourceStorageModeShared];
        id<MTLCommandQueue> queue=[device newCommandQueue];
        if (!a || !b || !g || !queue) return 2;
        uint8_t *after=a.contents,*before=b.contents,*guest=g.contents;
        for (unsigned mode=0;mode<3;mode++) {
            for (NSUInteger i=0;i<length;i++) {
                before[i]=(i*37+i/4096)&255;
                after[i]=before[i];
                if (mode==2 || (mode==1 && i%257==0)) after[i]^=0xa5;
            }
            for (unsigned variant=0;variant<2;variant++) {
                id<MTLFunction> fn=[lib newFunctionWithName:variant?@"wide":@"bytes"];
                id<MTLComputePipelineState> pipe=[device newComputePipelineStateWithFunction:fn error:&error];
                if (!pipe) { NSLog(@"%@",error); return 3; }
                double gpu=0;
                for (unsigned trial=0;trial<22;trial++) {
                    // CPU changed every destination byte after the baseline.
                    // Unchanged GPU bytes must NOT overwrite these CPU writes.
                    memset(guest,0x5c,length);
                    id<MTLCommandBuffer> cmd=[queue commandBuffer];
                    id<MTLComputeCommandEncoder> enc=[cmd computeCommandEncoder];
                    [enc setComputePipelineState:pipe];
                    [enc setBuffer:a offset:0 atIndex:0];
                    [enc setBuffer:b offset:0 atIndex:1];
                    [enc setBuffer:g offset:0 atIndex:2];
                    [enc dispatchThreads:MTLSizeMake(length/(variant?16:1),1,1)
                        threadsPerThreadgroup:MTLSizeMake(256,1,1)];
                    [enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
                    if (cmd.error) { NSLog(@"%@",cmd.error); return 4; }
                    if (trial>=2) gpu+=cmd.GPUEndTime-cmd.GPUStartTime;
                    if (trial==0 || trial==21) for(NSUInteger i=0;i<length;i++) {
                        uint8_t expected=after[i]!=before[i]?after[i]:0x5c;
                        if(guest[i]!=expected) { fprintf(stderr,"mismatch at %lu\n",(unsigned long)i); return 5; }
                    }
                }
                printf("mode=%u kernel=%s GPU20=%.6f exact-selective-writes=PASS\n",
                    mode,variant?"wide":"byte",gpu); fflush(stdout);
                [pipe release]; [fn release];
            }
        }
        [queue release]; [a release]; [b release]; [g release]; [lib release]; [device release];
    }
    return 0;
}
