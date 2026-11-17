/* One real, finite login-session click for reference UI comparisons.
 * No AX frame setters, preferences, private APIs or persistent input service. */
#include <ApplicationServices/ApplicationServices.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char **argv)
{
    int move=argc==4&&!strcmp(argv[1],"--move");
    int right=argc==4&&!strcmp(argv[1],"--right");
    if(argc!=3&&!move&&!right)return 2;
    if(move||right)++argv;
    char *end;errno=0;double x=strtod(argv[1],&end);if(errno||*end||!isfinite(x))return 2;
    double y=strtod(argv[2],&end);if(errno||*end||!isfinite(y))return 2;
    CGPoint point=CGPointMake(x,y);CGRect screen=CGDisplayBounds(CGMainDisplayID());
    if(!CGRectContainsPoint(screen,point))return 2;
    CGEventSourceRef source=CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
    if(!source)return 1;
    CGEventType types[]={kCGEventMouseMoved,kCGEventLeftMouseDown,kCGEventLeftMouseUp};
    if(right){types[1]=kCGEventRightMouseDown;types[2]=kCGEventRightMouseUp;}
    for(unsigned i=0;i<(move?1:3);++i){
        CGEventRef event=CGEventCreateMouseEvent(source,types[i],point,
            right?kCGMouseButtonRight:kCGMouseButtonLeft);
        if(!event){CFRelease(source);return 1;}
        CGEventSetIntegerValueField(event,kCGMouseEventClickState,1);
        CGEventPost(kCGHIDEventTap,event);CFRelease(event);usleep(80000);
    }
    CFRelease(source);printf("Queued real %s at %.0f,%.0f\n",
        move?"pointer move":right?"right click":"click",x,y);return 0;
}
