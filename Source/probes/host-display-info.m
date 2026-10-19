#import <Cocoa/Cocoa.h>
int main(void) {
    @autoreleasepool {
        for(NSScreen *screen in NSScreen.screens) {
            CGDirectDisplayID display=[screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
            CGSize mm=CGDisplayScreenSize(display);
            printf("display=%u frame=%.0fx%.0f usable=%.0fx%.0f backingScale=%.1f pixels=%zux%zu mm=%.0fx%.0f\n",
                display,screen.frame.size.width,screen.frame.size.height,
                screen.visibleFrame.size.width,screen.visibleFrame.size.height,
                screen.backingScaleFactor,CGDisplayPixelsWide(display),CGDisplayPixelsHigh(display),mm.width,mm.height);
        }
    }
}
