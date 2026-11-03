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
    if(argc!=3&&!move)return 2;
    if(move)++argv;
    char *end;errno=0;double x=strtod(argv[1],&end);if(errno||*end||!isfinite(x))return 2;
    double y=strtod(argv[2],&end);if(errno||*end||!isfinite(y))return 2;
    CGPoint point=CGPointMake(x,y);CGRect screen=CGDisplayBounds(CGMainDisplayID());
    if(!CGRectContainsPoint(screen,point))return 2;
    CGEventSourceRef source=CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
    if(!source)return 1;
    CGEventType types[]={kCGEventMouseMoved,kCGEventLeftMouseDown,kCGEventLeftMouseUp};
    for(unsigned i=0;i<(move?1:3);++i){
        CGEventRef event=CGEventCreateMouseEvent(source,types[i],point,kCGMouseButtonLeft);
        if(!event){CFRelease(source);return 1;}
        CGEventSetIntegerValueField(event,kCGMouseEventClickState,1);
        CGEventPost(kCGHIDEventTap,event);CFRelease(event);usleep(80000);
    }
    CFRelease(source);printf("Queued real %s at %.0f,%.0f\n",move?"pointer move":"click",x,y);return 0;
}
