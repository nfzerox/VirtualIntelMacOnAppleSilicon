/* One explicit real mouse click on a physical reference/guest. Not an AX
 * preference change or service; verify the visible response independently. */
#include <ApplicationServices/ApplicationServices.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
int main(int argc,char **argv) {
    if(argc!=3)return 64;
    long xy[2];
    for(int i=0;i<2;i++){char *end=NULL;xy[i]=strtol(argv[i+1],&end,10);if(!*argv[i+1]||*end)return 64;}
    CGRect bounds=CGDisplayBounds(CGMainDisplayID());CGPoint point=CGPointMake(xy[0],xy[1]);
    if(!CGRectContainsPoint(bounds,point))return 64;
    CGEventSourceRef source=CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);if(!source)return 1;
    CGEventType types[]={kCGEventMouseMoved,kCGEventLeftMouseDown,kCGEventLeftMouseUp};
    for(unsigned i=0;i<3;i++){
        CGEventRef event=CGEventCreateMouseEvent(source,types[i],point,kCGMouseButtonLeft);
        if(!event){CFRelease(source);return 1;}
        CGEventSetIntegerValueField(event,kCGMouseEventClickState,1);
        CGEventPost(kCGHIDEventTap,event);CFRelease(event);usleep(100000);
    }
    CFRelease(source);puts("Posted one click; require visible response evidence.");return 0;
}
