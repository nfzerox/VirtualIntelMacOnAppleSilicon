/* Finite scroll workload in Snow Leopard's real Dictionary results window.
 * No synthetic drawing, preferences, event tap, or renderer policy changes.
 * CGEventPost acknowledges posting, not delivery: inspect the captured result.
 */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc,char **argv)
{
    if(argc>2||(argc==2&&strcmp(argv[1],"up")&&strcmp(argv[1],"down")))return 64;
    int delta=argc==2&&!strcmp(argv[1],"up")?3:-3;
    CFArrayRef windows=CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly,kCGNullWindowID);
    CGRect bounds=CGRectZero;
    for(CFIndex i=0;windows&&i<CFArrayGetCount(windows);++i){
        CFDictionaryRef entry=CFArrayGetValueAtIndex(windows,i);
        CFStringRef owner=CFDictionaryGetValue(entry,kCGWindowOwnerName);
        int layer=0;CFNumberRef number=CFDictionaryGetValue(entry,kCGWindowLayer);
        if(number)CFNumberGetValue(number,kCFNumberIntType,&layer);
        if(owner&&CFEqual(owner,CFSTR("Dictionary"))&&!layer&&
           CGRectMakeWithDictionaryRepresentation(CFDictionaryGetValue(entry,kCGWindowBounds),&bounds)&&
           bounds.size.width>300&&bounds.size.height>200)break;
        bounds=CGRectZero;
    }
    if(windows)CFRelease(windows);
    if(CGRectIsEmpty(bounds)){fputs("Open Dictionary's results window first.\n",stderr);return 2;}
    CGPoint point=CGPointMake(bounds.origin.x+bounds.size.width*.35,bounds.origin.y+bounds.size.height*.65);
    printf("Dictionary bounds=%g,%g %gx%g scroll point=%g,%g\n",bounds.origin.x,bounds.origin.y,
        bounds.size.width,bounds.size.height,point.x,point.y);fflush(stdout);
    CGEventSourceRef source=CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
    if(!source)return 1;
    CGEventRef move=CGEventCreateMouseEvent(source,kCGEventMouseMoved,point,kCGMouseButtonLeft);
    if(!move){CFRelease(source);return 1;}
    CGEventPost(kCGHIDEventTap,move);CFRelease(move);
    for(unsigned step=0;step<32;++step){
        CGEventRef wheel=CGEventCreateScrollWheelEvent(source,kCGScrollEventUnitLine,1,delta);
        if(!wheel){CFRelease(source);return 1;}
        CGEventSetLocation(wheel,point);CGEventPost(kCGHIDEventTap,wheel);CFRelease(wheel);usleep(40000);
    }
    CFRelease(source);printf("Posted32 scroll events delta=%d; verify actual content changed.\n",delta);return 0;
}
