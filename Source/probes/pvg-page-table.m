// Isolated donor page-table geometry probe; no VM or physical guest memory.
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <objc/message.h>
int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc < 2 || !dlopen(argv[1], RTLD_NOW | RTLD_LOCAL)) return 1;
        if (argc == 3 && !dlopen(argv[2], RTLD_NOW | RTLD_LOCAL)) return 4;
        Class cls = NSClassFromString(@"PGPageTableIterator");
        if (!cls) return 2;
        __block unsigned reads = 0, visits = 0;
        BOOL (^read)(uint64_t, uint64_t, void *) =
        ^BOOL(uint64_t address, uint64_t size, void *out) {
            printf("read address=0x%llx size=%llu\n", address, size);
            if (size % 4) return NO;
            for (uint64_t i = 0; i < size / 4; i++)
                ((uint32_t *)out)[i] = 0x80012345;
            reads++;
            return YES;
        };
        id iterator = ((id (*)(id, SEL, uint32_t, uint32_t, BOOL, id))objc_msgSend)(
            [cls alloc], sel_registerName("initWithRoot:depth:allowHoles:read:"),
            7, getenv("PVG_TEST_CROSS_LEVEL") ? 2 : 1, NO, read);
        void (^visitor)(uint64_t, uint32_t, BOOL) =
        ^(uint64_t offset, uint32_t page, BOOL flag) {
            printf("visit offset=0x%llx page=0x%x flag=%u\n", offset, page, flag);
            visits++;
        };
        ((void (*)(id, SEL, uint64_t, uint64_t, id))objc_msgSend)(iterator,
            sel_registerName("visitPagesInRange:length:visitor:"),
            getenv("PVG_TEST_CROSS_LEVEL") ? 0x3ff000 : 0,
            getenv("PVG_TEST_CROSS_LEVEL") ? 0x3000 : 32768, visitor);
        [iterator release];
        return reads && visits ? 0 : 3;
    }
}
