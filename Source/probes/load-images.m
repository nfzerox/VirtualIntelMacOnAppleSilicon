#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <dlfcn.h>
#include <stdio.h>

// Run in a disposable guest process before replacing any system files.
int main(int argc, char **argv) {
    @autoreleasepool {
        (void)MTLCopyAllDevices();
        int failed = 0;
        for (int i = 1; i < argc; ++i) {
            void *handle = dlopen(argv[i], RTLD_NOW | RTLD_LOCAL);
            if (!handle) {
                fprintf(stderr, "FAIL %s: %s\n", argv[i], dlerror());
                failed = 1;
            } else {
                printf("PASS %s\n", argv[i]);
            }
        }
        return failed;
    }
}
