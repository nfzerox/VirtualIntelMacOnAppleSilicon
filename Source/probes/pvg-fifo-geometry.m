// Exercise the donor's child-FIFO mapper against a fake physical map.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include <dlfcn.h>
typedef struct { uint64_t address, length; } Range;
static uint32_t pfns[4096], node[16];
static unsigned expectedCount, observedCount;
static BOOL valid;
static uint64_t tableLength, totalLength;
@interface FIFOMappingProbe : NSObject { @public void *bytes; }
@end
@implementation FIFOMappingProbe
- (void *)virtualAddress { return bytes; }
@end
@interface FIFODeviceProbe : NSObject
@end
@implementation FIFODeviceProbe
- (id)newContiguousMapping:(uint32_t)page length:(uint64_t)length {
    tableLength = length;
    valid &= page == 7;
    FIFOMappingProbe *m = [FIFOMappingProbe new]; m->bytes = pfns; return m;
}
- (id)newRangeMapping:(Range *)ranges rangeCount:(uint32_t)count
          totalLength:(uint64_t)length readOnly:(BOOL)readOnly {
    observedCount = count; totalLength = length;
    valid &= count == expectedCount && length == (uint64_t)count * 4096;
    for (unsigned i = 0; i < count; i++)
        valid &= ranges[i].address == ((uint64_t)pfns[i] << 12) && ranges[i].length == 4096;
    FIFOMappingProbe *m = [FIFOMappingProbe new]; m->bytes = node; return m;
}
@end
int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc != 2 || !dlopen(argv[1], RTLD_NOW | RTLD_LOCAL)) return 1;
        Class cls = NSClassFromString(@"PGChildFIFO");
        Ivar deviceIvar = class_getInstanceVariable(cls, "_device");
        Ivar nodeIvar = class_getInstanceVariable(cls, "_baseNode");
        if (!deviceIvar || !nodeIvar) return 2;
        BOOL all = YES;
        unsigned cases[] = {2, 1024};
        for (unsigned c = 0; c < 2; c++) {
            expectedCount = cases[c]; valid = YES;
            memset(pfns, 0, sizeof(pfns));
            for (unsigned i = 0; i < expectedCount; i++) pfns[i] = 11 + i;
            // Nonzero beyond the Intel table catches an ARM-sized scan.
            if (expectedCount == 1024) pfns[1024] = 9999;
            node[4] = 7;
            id fifo = class_createInstance(cls, 0);
            object_setIvar(fifo, deviceIvar, [FIFODeviceProbe new]);
            void *ptr = node;
            memcpy((char *)fifo + ivar_getOffset(nodeIvar), &ptr, sizeof(ptr));
            BOOL ok = ((BOOL (*)(id, SEL))objc_msgSend)(fifo, sel_registerName("setupMapping"));
            valid &= ok && tableLength == 4096;
            printf("case=%u count=%u table=%llu total=%llu valid=%d\n",
                   expectedCount, observedCount, tableLength, totalLength, valid);
            all &= valid;
            // Intentionally leaked: no initialized GPU/FIFO thread to tear down.
        }
        return all ? 0 : 3;
    }
}
