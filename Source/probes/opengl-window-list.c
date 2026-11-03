/* Read-only compositor diagnostic: distinguish absent windows from lost pixels. */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
int main(void)
{
    CGRect display=CGDisplayBounds(CGMainDisplayID());
    printf("display_bounds=%g,%g %gx%g\n",display.origin.x,display.origin.y,display.size.width,display.size.height);
    CGEventRef event=CGEventCreate(NULL);
    if(event) { CGPoint p=CGEventGetLocation(event);printf("cursor=%g,%g\n",p.x,p.y);CFRelease(event); }
    CFArrayRef windows=CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly,kCGNullWindowID);
    if(!windows) return 1;
    for(CFIndex i=0;i<CFArrayGetCount(windows);++i) {
        CFDictionaryRef d=CFArrayGetValueAtIndex(windows,i);
        char owner[256]="",name[256]="";int wid=0,pid=0,layer=0;CGRect bounds=CGRectZero;
        CFStringRef o=CFDictionaryGetValue(d,kCGWindowOwnerName),n=CFDictionaryGetValue(d,kCGWindowName);
        if(o) CFStringGetCString(o,owner,sizeof(owner),kCFStringEncodingUTF8);
        if(n) CFStringGetCString(n,name,sizeof(name),kCFStringEncodingUTF8);
        CFNumberGetValue(CFDictionaryGetValue(d,kCGWindowNumber),kCFNumberIntType,&wid);
        CFNumberGetValue(CFDictionaryGetValue(d,kCGWindowOwnerPID),kCFNumberIntType,&pid);
        CFNumberGetValue(CFDictionaryGetValue(d,kCGWindowLayer),kCFNumberIntType,&layer);
        CGRectMakeWithDictionaryRepresentation(CFDictionaryGetValue(d,kCGWindowBounds),&bounds);
        printf("id=%d pid=%d layer=%d owner=%s name=%s bounds=%g,%g %gx%g\n",wid,pid,layer,owner,name,
            bounds.origin.x,bounds.origin.y,bounds.size.width,bounds.size.height);
    }
    CFRelease(windows);return 0;
}
