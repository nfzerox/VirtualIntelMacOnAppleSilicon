#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <objc/message.h>
#include <stdio.h>
int main(void) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        NSError *error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:
            @"#include <metal_stdlib>\nusing namespace metal; kernel void probe(device uint *v [[buffer(0)]]) {v[0]=42;}"
            options:nil error:&error];
        if (!library) { fprintf(stderr, "%s\n", error.description.UTF8String); return 1; }
        id function = [library newFunctionWithName:@"probe"];
        void *(*send)(id,SEL) = (void *)objc_msgSend;
        for (NSString *selectorName in @[@"functionData", @"libraryData"]) {
            SEL selector = NSSelectorFromString(selectorName);
            if (![function respondsToSelector:selector]) continue;
            const unsigned char *bytes = send(function, selector);
            printf("%s=%p\n", selectorName.UTF8String, bytes);
            if (!bytes) continue;
            // Diagnostic only: inspect the same field window used by the bridge.
            for (unsigned i=0; i<0x60; ++i) {
                if (!(i%16)) printf("%02x: ", i);
                printf("%02x%c", bytes[i], i%16==15 ? '\n' : ' ');
            }
        }
    }
    return 0;
}
