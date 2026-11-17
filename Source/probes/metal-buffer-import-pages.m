// Finite public-Metal page-alignment control. No VM, GPU writes or donor patch.
#import <AppKit/AppKit.h>
#import "metal-buffer-import-pages.h"

int main(void) {@autoreleasepool {
    [NSApplication sharedApplication];
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    if(!device){fprintf(stderr,"SKIP: this process context has no Metal device\n");return 77;}
    // A translated process can report4KB while its native Metal implementation
    // uses16KB. Keep every range inside our own allocation, with no deallocator.
    BOOL control=VIMMetalBufferImportPages(device,^id<MTLBuffer>(void *bytes,NSUInteger length) {
        return [device newBufferWithBytesNoCopy:bytes length:length
            options:MTLResourceStorageModeShared deallocator:nil];
    });
    [device release];return control?0:1;
}}
