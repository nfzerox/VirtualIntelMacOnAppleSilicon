#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <time.h>
#include <dlfcn.h>
static double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static NSSet *windows(pid_t pid) {
    NSArray *list = CFBridgingRelease(CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly, kCGNullWindowID));
    NSMutableSet *result = [NSMutableSet set];
    for (NSDictionary *w in list)
        if ([w[(id)kCGWindowOwnerPID] intValue] == pid &&
            [w[(id)kCGWindowLayer] intValue] == 0)
            [result addObject:w[(id)kCGWindowNumber]];
    return result;
}
static AXUIElementRef find(AXUIElementRef element, int depth) {
    if (!depth) return NULL;
    CFTypeRef title = NULL;
    AXUIElementCopyAttributeValue(element, kAXTitleAttribute, &title);
    BOOL match = title && CFEqual(title, CFSTR("New Finder Window"));
    if (title) CFRelease(title);
    if (match) return (AXUIElementRef)CFRetain(element);
    CFTypeRef children = NULL;
    AXUIElementCopyAttributeValue(element, kAXChildrenAttribute, &children);
    AXUIElementRef result = NULL;
    if (children && CFGetTypeID(children) == CFArrayGetTypeID())
        for (CFIndex i = 0; i < CFArrayGetCount(children) && !result; i++)
            result = find((AXUIElementRef)CFArrayGetValueAtIndex(children, i), depth-1);
    if (children) CFRelease(children);
    return result;
}
int main(int argc, char **argv) {
    @autoreleasepool {
        NSRunningApplication *finder = [NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.apple.finder"].firstObject;
        if (!finder) return 1;
        if (argc == 2 && !strcmp(argv[1], "--inventory")) {
            printf("Finder pid=%d onscreen-normal-windows=%lu\n",
                finder.processIdentifier, (unsigned long)windows(finder.processIdentifier).count);
            return 0;
        }
        if (argc != 1) return 64;
        if (!AXIsProcessTrusted()) { fprintf(stderr, "Accessibility unavailable\n"); return 77; }
        [finder activateWithOptions:NSApplicationActivateIgnoringOtherApps];
        AXUIElementRef app = AXUIElementCreateApplication(finder.processIdentifier);
        CFTypeRef bar = NULL;
        AXUIElementCopyAttributeValue(app, kAXMenuBarAttribute, &bar);
        AXUIElementRef item = bar ? find(bar, 5) : NULL;
        if (!item) { fprintf(stderr, "New Finder Window menu item not found\n"); return 2; }
        NSSet *before = windows(finder.processIdentifier);
        double start = now();
        AXError error = AXUIElementPerformAction(item, kAXPressAction);
        printf("action-return=%.3f error=%d\n", now()-start, error); fflush(stdout);
        BOOL found = NO;
        NSNumber *created = nil;
        while (!error && now()-start < 20) {
            if (finder.terminated) {
                fprintf(stderr, "Finder exited during measurement; invalid timing\n");
                break;
            }
            NSMutableSet *added = [windows(finder.processIdentifier) mutableCopy];
            [added minusSet:before];
            if (added.count) {
                found = YES;
                if (added.count == 1) created = added.anyObject;
                break;
            }
            [NSThread sleepForTimeInterval:0.05];
        }
        printf("new-window-ordered=%d elapsed=%.3f (not pixels presented)\n", found, now()-start);
        // Only close the unique newly observed window, never the frontmost
        // window by assumption. Missing API or ambiguous result leaves it open.
        AXError (*getWindow)(AXUIElementRef, CGWindowID *) =
            dlsym(RTLD_DEFAULT, "_AXUIElementGetWindow");
        CFTypeRef axWindows = NULL;
        if (created && getWindow &&
            AXUIElementCopyAttributeValue(app, kAXWindowsAttribute, &axWindows) == kAXErrorSuccess &&
            CFGetTypeID(axWindows) == CFArrayGetTypeID()) {
            for (CFIndex i=0; i<CFArrayGetCount(axWindows); i++) {
                AXUIElementRef window = (AXUIElementRef)CFArrayGetValueAtIndex(axWindows,i);
                CGWindowID windowID = 0;
                if (getWindow(window, &windowID) || windowID != created.unsignedIntValue) continue;
                CFTypeRef close = NULL;
                AXError e = AXUIElementCopyAttributeValue(window,kAXCloseButtonAttribute,&close);
                if (!e && close) e = AXUIElementPerformAction(close,kAXPressAction);
                printf("close-created-window=%u error=%d\n",windowID,e);
                if (close) CFRelease(close);
                // AXPress acknowledges delivery, not closure. Do not let a
                // following run include this disappearing window in its
                // baseline (or mistake a reused window ID for an old one).
                double closeStart = now();
                while (!e && [windows(finder.processIdentifier) containsObject:created]
                       && now()-closeStart < 5 && !finder.terminated)
                    [NSThread sleepForTimeInterval:0.05];
                printf("created-window-closed=%d\n",
                    ![windows(finder.processIdentifier) containsObject:created]);
                break;
            }
        }
        if (axWindows) CFRelease(axWindows);
        CFRelease(item); if (bar) CFRelease(bar); CFRelease(app);
        return found ? 0 : 1;
    }
}
