/* Finite ordinary wheel input in an OWNED real Finder comparison window.
 * No GL calls, synthetic window, preferences, AX or delivery/FPS assertion.
 * Inspect the paired recording; CGEventPost has no delivery acknowledgment.
 */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc,char **argv)
{
    if(argc!=3){fputs("usage: opengl-coverflow-scroll FINDER_PID WINDOW_ID\n",stderr);return 2;}
    char *p=NULL,*q=NULL;long pid=strtol(argv[1],&p,10);unsigned long wid=strtoul(argv[2],&q,10);
    if(!p||*p||!q||*q||pid<=0||pid>INT32_MAX||!wid||wid>UINT32_MAX)return 2;
    CFArrayRef windows=CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow,(CGWindowID)wid);
    CGRect bounds=CGRectZero;int found=0;
    for(CFIndex i=0;windows && i<CFArrayGetCount(windows);++i){
        CFDictionaryRef entry=CFArrayGetValueAtIndex(windows,i);int owner=0,number=0;
        CFNumberGetValue(CFDictionaryGetValue(entry,kCGWindowOwnerPID),kCFNumberIntType,&owner);
        CFNumberGetValue(CFDictionaryGetValue(entry,kCGWindowNumber),kCFNumberIntType,&number);
        CFStringRef name=CFDictionaryGetValue(entry,kCGWindowName);
        CFStringRef app=CFDictionaryGetValue(entry,kCGWindowOwnerName);
        if(owner==pid && (unsigned)number==wid && app && CFEqual(app,CFSTR("Finder")) && name &&
           CFStringHasPrefix(name,CFSTR("vim-coverflow-match.")) &&
           CGRectMakeWithDictionaryRepresentation(CFDictionaryGetValue(entry,kCGWindowBounds),&bounds))found=1;
    }
    if(windows)CFRelease(windows);
    if(!found || bounds.size.width<300 || bounds.size.height<300){
        fputs("Refused: require the owned Finder comparison window. No input sent.\n",stderr);return 2;
    }
    CGEventRef before=CGEventCreate(NULL);if(!before)return 1;
    CGPoint original=CGEventGetLocation(before);CFRelease(before);
    CGPoint point=CGPointMake(CGRectGetMidX(bounds),bounds.origin.y+bounds.size.height*.4);
    // A floating/modal utility can cover Finder even after Finder activates.
    // Do not post into it, or call an unchanged movie a successful workload.
    windows=CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly,kCGNullWindowID);
    int exposed=0;
    for(CFIndex i=0;windows && i<CFArrayGetCount(windows);++i){
        CFDictionaryRef entry=CFArrayGetValueAtIndex(windows,i);CGRect r=CGRectZero;
        int owner=0,number=0,layer=0;double alpha=1;
        CFNumberGetValue(CFDictionaryGetValue(entry,kCGWindowLayer),kCFNumberIntType,&layer);
        CFNumberRef opacity=CFDictionaryGetValue(entry,kCGWindowAlpha);
        if(opacity)CFNumberGetValue(opacity,kCFNumberDoubleType,&alpha);
        if(layer<0 || layer>1000 || alpha<.01 ||
           !CGRectMakeWithDictionaryRepresentation(CFDictionaryGetValue(entry,kCGWindowBounds),&r) ||
           !CGRectContainsPoint(r,point))continue;
        CFNumberGetValue(CFDictionaryGetValue(entry,kCGWindowOwnerPID),kCFNumberIntType,&owner);
        CFNumberGetValue(CFDictionaryGetValue(entry,kCGWindowNumber),kCFNumberIntType,&number);
        exposed=owner==pid && (unsigned)number==wid;break;
    }
    if(windows)CFRelease(windows);
    if(!exposed){fputs("Refused: another window covers the input point. No input sent.\n",stderr);return 2;}
    CGEventSourceRef source=CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
    if(!source)return 1;
    CGEventRef move=CGEventCreateMouseEvent(source,kCGEventMouseMoved,point,kCGMouseButtonLeft);
    if(!move){CFRelease(source);return 1;}
    CGEventPost(kCGHIDEventTap,move);CFRelease(move);usleep(150000);
    printf("READY owned Finder=%ld window=%lu point=%.1f,%.1f; wheel only\n",pid,wid,point.x,point.y);fflush(stdout);
    int good=1;
    for(unsigned phase=0;phase<4 && good;++phase){
        int delta=phase&1?-1:1;
        printf("PHASE %u horizontal_lines=%d steps=16\n",phase,delta);fflush(stdout);
        for(unsigned step=0;step<16;++step){
            CGEventRef wheel=CGEventCreateScrollWheelEvent(source,kCGScrollEventUnitLine,2,0,delta);
            if(!wheel){good=0;break;}
            CGEventSetLocation(wheel,point);CGEventPost(kCGHIDEventTap,wheel);CFRelease(wheel);
            usleep(100000);
        }
        usleep(300000);
    }
    move=CGEventCreateMouseEvent(source,kCGEventMouseMoved,original,kCGMouseButtonLeft);
    if(move){CGEventPost(kCGHIDEventTap,move);CFRelease(move);}
    CFRelease(source);return good?0:1;
}
