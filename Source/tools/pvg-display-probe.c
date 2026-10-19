#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>

static void print_display(CGDirectDisplayID display) {
    CGRect bounds = CGDisplayBounds(display);
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display);

    printf("display=0x%x main=%d active=%d online=%d builtin=%d "
           "bounds=%.0fx%.0f+%.0f+%.0f",
           display,
           CGDisplayIsMain(display),
           CGDisplayIsActive(display),
           CGDisplayIsOnline(display),
           CGDisplayIsBuiltin(display),
           bounds.size.width, bounds.size.height,
           bounds.origin.x, bounds.origin.y);

    if (mode != NULL) {
        printf(" mode=%zux%zu pixels=%zux%zu refresh=%.3f",
               CGDisplayModeGetWidth(mode), CGDisplayModeGetHeight(mode),
               CGDisplayModeGetPixelWidth(mode),
               CGDisplayModeGetPixelHeight(mode),
               CGDisplayModeGetRefreshRate(mode));
        CFRelease(mode);
    } else {
        printf(" mode=(null)");
    }
    putchar('\n');
}

int main(void) {
    CGDirectDisplayID displays[16];
    uint32_t count = 0;
    CGError result = CGGetOnlineDisplayList(16, displays, &count);
    printf("online-result=%d online-count=%u main=0x%x\n",
           result, count, CGMainDisplayID());
    for (uint32_t index = 0; index < count; ++index) {
        print_display(displays[index]);
    }

    count = 0;
    result = CGGetActiveDisplayList(16, displays, &count);
    printf("active-result=%d active-count=%u\n", result, count);
    for (uint32_t index = 0; index < count; ++index) {
        print_display(displays[index]);
    }
    return 0;
}
