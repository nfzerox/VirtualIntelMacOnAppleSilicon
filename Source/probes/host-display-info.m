#import <Cocoa/Cocoa.h>
int main(void) {
    @autoreleasepool {
        for(NSScreen *screen in NSScreen.screens) {
            CGDirectDisplayID display=[[screen.deviceDescription objectForKey:@"NSScreenNumber"] unsignedIntValue];
            CGSize mm=CGDisplayScreenSize(display);
            CGDisplayModeRef mode=CGDisplayCopyDisplayMode(display);
            printf("display=%u frame=%.0fx%.0f usable=%.0fx%.0f backingScale=%.1f pixels=%zux%zu mm=%.0fx%.0f\n",
                display,screen.frame.size.width,screen.frame.size.height,
                screen.visibleFrame.size.width,screen.visibleFrame.size.height,
                screen.backingScaleFactor,mode?CGDisplayModeGetPixelWidth(mode):0,
                mode?CGDisplayModeGetPixelHeight(mode):0,mm.width,mm.height);
            if(mode)CGDisplayModeRelease(mode);
        }
    }
}
