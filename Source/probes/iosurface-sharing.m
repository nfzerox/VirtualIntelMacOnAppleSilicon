#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#include <mach/mach.h>
int main(void) {
    @autoreleasepool {
        NSDictionary *p = @{(id)kIOSurfaceWidth:@64, (id)kIOSurfaceHeight:@64,
            (id)kIOSurfaceBytesPerElement:@4, (id)kIOSurfacePixelFormat:@(0x42475241),
            (id)kIOSurfaceIsGlobal:@YES};
        IOSurfaceRef s = IOSurfaceCreate((CFDictionaryRef)p);
        fprintf(stderr, "create=%p\n", s); if (!s) return 1;
        kern_return_t r = IOSurfaceLock(s, 0, NULL);
        fprintf(stderr, "lock=%x base=%p\n", r, IOSurfaceGetBaseAddress(s));
        if (r) return 2;
        memset(IOSurfaceGetBaseAddress(s), 0x5a, IOSurfaceGetAllocSize(s));
        IOSurfaceUnlock(s, 0, NULL);
        mach_port_t port = IOSurfaceCreateMachPort(s);
        IOSurfaceRef byPort = IOSurfaceLookupFromMachPort(port);
        IOSurfaceRef byID = IOSurfaceLookup(IOSurfaceGetID(s));
        fprintf(stderr, "port=%x byPort=%p byID=%p\n", port, byPort, byID);
        if (!byPort || !byID) return 3;
        r = IOSurfaceLock(byPort, kIOSurfaceLockReadOnly, NULL);
        if (r || !IOSurfaceGetBaseAddress(byPort)) return 4;
        unsigned char *bytes = IOSurfaceGetBaseAddress(byPort);
        size_t count = IOSurfaceGetAllocSize(byPort);
        for (size_t i = 0; i < count; ++i)
            if (bytes[i] != 0x5a) return 5;
        fprintf(stderr, "PASS shared lock=%x verified=%zu bytes\n", r, count);
        IOSurfaceUnlock(byPort, kIOSurfaceLockReadOnly, NULL);
        CFRelease(byPort); CFRelease(byID); CFRelease(s);
        mach_port_deallocate(mach_task_self(), port);
        return r != 0;
    }
}
