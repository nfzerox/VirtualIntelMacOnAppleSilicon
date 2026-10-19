#import <Foundation/Foundation.h>
#include <math.h>

// All dimensions are AppKit points, independent of the host's pixel density.
// Fit a 16:10 desktop in 85% of the usable work area after reserving 24 pt
// around the window and its actual toolbar/title-bar height. Prefer familiar
// modes; very small screens get a proportionally fitted, even-pixel fallback.
static inline NSSize VIMDefaultGuestContentSize(NSSize visible, CGFloat chrome) {
    CGFloat width=MAX(1,visible.width-48);
    CGFloat height=MAX(1,visible.height-48-MAX(0,chrome));
    CGFloat fit=MIN(width*.85/1600.,height*.85/1000.);
    fit=MIN(fit,1.);
    const NSSize modes[]={{1600,1000},{1440,900},{1280,800},{1152,720},
                          {1024,640},{960,600},{800,500},{640,400}};
    for(NSUInteger i=0;i<sizeof(modes)/sizeof(modes[0]);i++)
        if(modes[i].width<=1600*fit&&modes[i].height<=1000*fit)return modes[i];
    if(width>=640&&height>=400)return NSMakeSize(640,400);
    // On unusually small displays, use the available space before shrinking
    // below 640x400. Multiples of 16 preserve an exact 8:5 ratio and even pixels.
    CGFloat narrow=floor(MIN(width,height*1.6)/16)*16;
    return NSMakeSize(MAX(16,narrow),MAX(10,narrow*5/8));
}
