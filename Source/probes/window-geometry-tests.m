#import "../VirtualIntelMac/VIMWindowGeometry.h"
#include <stdio.h>

int main(void) {
    @autoreleasepool {
        const NSSize screens[]={{800,600},{1024,768},{1280,800},{1440,900},
            {1512,982},{1728,1117},{1920,1080},{2560,1410},{3008,1692},{3840,2160}};
        for(NSUInteger i=0;i<sizeof(screens)/sizeof(screens[0]);i++) {
            NSSize a=VIMDefaultGuestContentSize(screens[i],48);
            for(NSValue *value in @[[NSValue valueWithSize:a]]) {
                NSSize size=value.sizeValue;
                NSCAssert(size.width>0&&size.height>0,@"Positive content size");
                NSCAssert(fabs(size.width/size.height-1.6)<.001,@"16:10 desktop");
                NSCAssert(size.width+48<=screens[i].width&&size.height+96<=screens[i].height,@"Frame, toolbar and margins fit");
            }
            NSCAssert(a.width<=1600&&a.height<=1000,@"Logical size caps");
            printf("%.0fx%.0f points: desktop %.0fx%.0f\n",
                   screens[i].width,screens[i].height,a.width,a.height);
        }
        puts("PASS: standard 16:10 window sizes fit every tested work area");
    }
}
