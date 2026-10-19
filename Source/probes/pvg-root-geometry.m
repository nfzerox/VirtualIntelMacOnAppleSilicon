// Isolated method probe. No GPU, guest, or actual memory mappings are created.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include <dlfcn.h>
typedef struct { uint64_t address, length; } Range;
static uint64_t observed;
@interface RootMapProbe : NSObject
@end
@implementation RootMapProbe
- (BOOL)read:(uint64_t)address length:(uint64_t)length dst:(void *)dst {
    memset(dst, 0x5a, length);
    return YES;
}
- (id)newRangeMapping:(Range *)ranges rangeCount:(uint32_t)count
          totalLength:(uint64_t)length readOnly:(BOOL)readOnly {
    if (count != 1 || length != 4096 || ranges[0].length != length) abort();
    observed = ranges[0].address;
    return [NSObject new];
}
@end
int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc != 2 || !dlopen(argv[1], RTLD_NOW | RTLD_LOCAL)) return 1;
        Class cls = NSClassFromString(@"_PGDevice");
        Ivar map = class_getInstanceVariable(cls, "_memoryMap");
        if (!map) return 2;
        // Intentionally uninitialized zeroed device: the tested method's map
        // branch needs only _runState=0 and _memoryMap. Do not run its teardown.
        id device = class_createInstance(cls, 0);
        object_setIvar(device, map, [RootMapProbe new]);
        id result = ((id (*)(id, SEL, uint32_t, uint64_t))objc_msgSend)(device,
            sel_registerName("newContiguousMapping:length:"), 7, 4096);
        printf("PFN 7 -> physical address 0x%llx\n", observed);
        BOOL mappingOK = result && observed == 0x7000;
        SEL read = sel_registerName("readFromPhysicalAddress:length:dst:");
        unsigned char bytes[4097] = {0};
        BOOL valid = ((BOOL (*)(id, SEL, uint64_t, uint32_t, void *))objc_msgSend)(
            device, read, 0x4ffc, 4, bytes);
        unsigned rejected = 0;
        @try {
            ((BOOL (*)(id, SEL, uint64_t, uint32_t, void *))objc_msgSend)(
                device, read, 0x4ffc, 8, bytes);
        } @catch (NSException *e) { rejected++; }
        @try {
            ((BOOL (*)(id, SEL, uint64_t, uint32_t, void *))objc_msgSend)(
                device, read, 0x4000, 4097, bytes);
        } @catch (NSException *e) { rejected++; }
        printf("physical read valid=%d byte=0x%x invalid-rejections=%u/2\n",
               valid, bytes[0], rejected);
        return mappingOK && valid && bytes[0] == 0x5a && rejected == 2 ? 0 : 3;
    }
}
