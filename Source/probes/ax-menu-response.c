#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int main(int argc, char **argv) {
    if (argc != 2) return 64;
    if (!AXIsProcessTrusted()) {
        fprintf(stderr, "Accessibility access unavailable; no prompt requested\n");
        return 77;
    }
    AXUIElementRef app = AXUIElementCreateApplication(atoi(argv[1]));
    CFTypeRef bar = NULL, children = NULL;
    AXError e = AXUIElementCopyAttributeValue(app, kAXMenuBarAttribute, &bar);
    if (!e) e = AXUIElementCopyAttributeValue(bar, kAXChildrenAttribute, &children);
    if (e || !children || CFGetTypeID(children) != CFArrayGetTypeID()) {
        fprintf(stderr, "Menu lookup failed: %d\n", e); return 1;
    }
    setbuf(stdout, NULL);
    for (CFIndex i = 0; i < CFArrayGetCount(children); ++i) {
        AXUIElementRef item = (AXUIElementRef)CFArrayGetValueAtIndex(children, i);
        CFTypeRef title = NULL;
        if (AXUIElementCopyAttributeValue(item, kAXTitleAttribute, &title)) continue;
        if (CFGetTypeID(title) == CFStringGetTypeID() && CFEqual(title, CFSTR("File"))) {
            for (int trial = 0; trial < 3; ++trial) {
                CFTypeRef value = NULL;
                double t = now();
                e = AXUIElementCopyAttributeValue(item, kAXTitleAttribute, &value);
                double query = now() - t;
                if (value) CFRelease(value);
                t = now();
                AXError press = AXUIElementPerformAction(item, kAXPressAction);
                double action = now() - t;
                printf("trial=%d query=%.6f query_error=%d press=%.6f press_error=%d\n",
                       trial, query, e, action, press);
                // Dismiss only; do not invoke any menu command.
                CGEventRef down = CGEventCreateKeyboardEvent(NULL, 53, true);
                CGEventRef up = CGEventCreateKeyboardEvent(NULL, 53, false);
                CGEventPost(kCGHIDEventTap, down); CGEventPost(kCGHIDEventTap, up);
                CFRelease(down); CFRelease(up);
                struct timespec delay = {0, 200000000}; nanosleep(&delay, NULL);
            }
        }
        CFRelease(title);
    }
    CFRelease(children); CFRelease(bar); CFRelease(app);
    return 0;
}
