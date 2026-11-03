/* Read-only, period-compatible public mode inventory. Never selects a mode. */
#include <ApplicationServices/ApplicationServices.h>
#include <dlfcn.h>
#include <stdio.h>
#include "../tools/guest-display-options.h"

static void mode(CGDisplayModeRef value, const char *label)
{
    if (!value) return;
    printf("%s id=%x logical=%zux%zu pixels=%zux%zu flags=%08x\n",
        label, CGDisplayModeGetIODisplayModeID(value),
        CGDisplayModeGetWidth(value), CGDisplayModeGetHeight(value),
        CGDisplayModeGetPixelWidth(value), CGDisplayModeGetPixelHeight(value),
        CGDisplayModeGetIOFlags(value));
}

int main(void)
{
    CGDirectDisplayID display=CGMainDisplayID();
    printf("display=%x accelerated=%d\n", display, CGDisplayUsesOpenGLAcceleration(display));
    CGDisplayModeRef current=CGDisplayCopyDisplayMode(display);
    mode(current,"current");
    if (current) CGDisplayModeRelease(current);
    CFDictionaryRef options=VIMCopyDisplayModeOptions();
    CFArrayRef modes=CGDisplayCopyAllDisplayModes(display,options);
    if (options) CFRelease(options);
    if (!modes) return 1;
    printf("count=%ld\n",CFArrayGetCount(modes));
    for (CFIndex index=0;index<CFArrayGetCount(modes);++index)
        mode((CGDisplayModeRef)CFArrayGetValueAtIndex(modes,index),"available");
    CFRelease(modes);
    return 0;
}
