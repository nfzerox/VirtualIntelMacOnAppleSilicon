#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <mach/mach.h>
#include <stdint.h>
#include <stdio.h>

static void add_u32(CFMutableDictionaryRef properties, CFStringRef key,
    uint32_t value)
{
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt32Type, &value);
    CFDictionarySetValue(properties, key, number);
    CFRelease(number);
}

static void run_case(const char *name, uint32_t width, uint32_t height,
    int complete, int global)
{
    CFMutableDictionaryRef properties = CFDictionaryCreateMutable(NULL, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    add_u32(properties, kIOSurfaceWidth, width);
    add_u32(properties, kIOSurfaceHeight, height);
    add_u32(properties, kIOSurfaceBytesPerElement, 4);
    if (complete) {
        add_u32(properties, kIOSurfaceBytesPerRow, width * 4);
        add_u32(properties, kIOSurfaceAllocSize, width * height * 4);
        add_u32(properties, kIOSurfacePixelFormat, 'BGRA');
    }
    if (global)
        CFDictionarySetValue(properties, kIOSurfaceIsGlobal,
            kCFBooleanTrue);

    IOSurfaceRef surface = IOSurfaceCreate(properties);
    CFRelease(properties);
    if (!surface) {
        printf("%s create=nil\n", name);
        return;
    }

    IOReturn lock_result = IOSurfaceLock(surface, 0, NULL);
    mach_port_t port = IOSurfaceCreateMachPort(surface);
    printf("%s create=%p id=%u alloc=%zu lock=0x%x base=%p port=0x%x\n",
        name, surface, IOSurfaceGetID(surface), IOSurfaceGetAllocSize(surface),
        lock_result, IOSurfaceGetBaseAddress(surface), port);
    if (lock_result == kIOReturnSuccess)
        IOSurfaceUnlock(surface, 0, NULL);
    if (MACH_PORT_VALID(port))
        mach_port_deallocate(mach_task_self(), port);
    CFRelease(surface);
}

int main(void)
{
    run_case("tiny-minimal", 64, 64, 0, 0);
    run_case("tiny-complete", 64, 64, 1, 0);
    run_case("display-complete", 1024, 768, 1, 0);
    run_case("display-global", 1024, 768, 1, 1);
    return 0;
}
