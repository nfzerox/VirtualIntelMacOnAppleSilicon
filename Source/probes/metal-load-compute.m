#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc != 3) return 64;
    alarm(20);
    @autoreleasepool {
        id<MTLDevice> d=MTLCreateSystemDefaultDevice();
        NSError *error=nil;
        id<MTLLibrary> lib=[d newLibraryWithFile:@(argv[1]) error:&error];
        if (!lib) { fprintf(stderr,"library: %s\n",error.description.UTF8String); return 1; }
        id<MTLFunction> f=[lib newFunctionWithName:@(argv[2])];
        id<MTLComputePipelineState> p=[d newComputePipelineStateWithFunction:f error:&error];
        fprintf(stderr,"pipeline=%s error=%s\n",p?"OK":"FAIL",error.description.UTF8String?:"none");
        return p?0:2;
    }
}
