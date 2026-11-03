// One-shot mode selection, not a background resize poller. No capture/TCC APIs.
#import <Foundation/Foundation.h>
#import <ApplicationServices/ApplicationServices.h>
#include <unistd.h>
#include <dlfcn.h>
#include "guest-display-modes.h"
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 101400
#include "guest-system-version.h"
#include "guest-hidpi-policy.h"
#endif

int main(int argc,const char *argv[]) {@autoreleasepool {
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 101400
    if(argc==2&&!strcmp(argv[1],"--prepare-policy")) {
        NSString *version=[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"][@"ProductVersion"];
        if(geteuid()||![@[@"mojave",@"catalina",@"bigsur"] containsObject:VIMGuestForSystemVersion(version)?:@""])return 64;
        BOOL changed=NO;
        if(!VIMPrepareNativeHiDPIPolicy(&changed)){fprintf(stderr,"The native Retina display policy could not be saved.\n");return 5;}
        printf("{\"Changed\":%s,\"RestartRequired\":%s}\n",changed?"true":"false",changed?"true":"false");return 0;
    }
#endif
    if(argc!=4)return 64;
    long numbers[3];
    for(int i=0;i<3;i++){char *end=NULL;numbers[i]=strtol(argv[i+1],&end,10);if(!*argv[i+1]||*end)return 64;}
    size_t w=numbers[0],h=numbers[1],scale=numbers[2];
    if(w<320||h<200||w>8192||h>8192||(scale!=1&&scale!=2)||w%scale||h%scale)return 64;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    if(scale!=1)return 64; // The native dictionary mode API has no Retina modes.
#endif
    // The host has just updated the panel mode list. Allow that single
    // hot-plug notification to arrive, without assuming a fixed delay works.
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:4];
    CGDirectDisplayID display=CGMainDisplayID();VIMGuestDisplayMode selected=NULL;
    do {
        CFArrayRef modes=VIMGuestDisplayModes(display);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
        // Tiger's NSArray has no fast-enumeration ABI.
        if(modes)for(CFIndex index=0;index<CFArrayGetCount(modes);index++) {
            VIMGuestDisplayMode mode=(VIMGuestDisplayMode)CFArrayGetValueAtIndex(modes,index);
#else
        for(id item in (__bridge NSArray *)modes) {
            VIMGuestDisplayMode mode=(__bridge VIMGuestDisplayMode)item;
#endif
            if(VIMGuestModeMatches(mode,w,h,scale)) {
                selected=(VIMGuestDisplayMode)CFRetain(mode);break;
            }
        }
        if(modes)CFRelease(modes);
        if(selected)break;
        usleep(100000);
    }while(deadline.timeIntervalSinceNow>0);
    if(!selected){fprintf(stderr,"The requested display mode is not available yet.\n");return 2;}
    CGError error=VIMGuestSetMode(display,selected);
    CFRelease(selected);
    if(error!=kCGErrorSuccess){fprintf(stderr,"macOS could not change the display mode (%d).\n",error);return 3;}
    // A successful API call alone is insufficient: require observed geometry.
    for(int i=0;i<20;i++) {
        VIMGuestDisplayMode current=VIMGuestCurrentMode(display);
        BOOL matches=VIMGuestModeMatches(current,w,h,scale);
        if(current)CFRelease(current);
        if(matches){printf("{\"PixelWidth\":%zu,\"PixelHeight\":%zu,\"Scale\":%zu}\n",w,h,scale);return 0;}
        usleep(50000);
    }
    fprintf(stderr,"macOS did not retain the requested display mode.\n");return 4;
}}
