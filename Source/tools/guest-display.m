// One-shot mode selection, not a background resize poller. No capture/TCC APIs.
#import <Foundation/Foundation.h>
#import <ApplicationServices/ApplicationServices.h>
#include <unistd.h>

int main(int argc,const char *argv[]) {@autoreleasepool {
    if(argc!=4)return 64;
    long numbers[3];
    for(int i=0;i<3;i++){char *end=NULL;numbers[i]=strtol(argv[i+1],&end,10);if(!*argv[i+1]||*end)return 64;}
    size_t w=numbers[0],h=numbers[1],scale=numbers[2];
    if(w<320||h<200||w>8192||h>8192||(scale!=1&&scale!=2)||w%scale||h%scale)return 64;
    NSDictionary *options=@{(__bridge NSString *)kCGDisplayShowDuplicateLowResolutionModes:@YES};
    // The host has just updated the panel mode list. Allow that single
    // hot-plug notification to arrive, without assuming a fixed delay works.
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:4];
    CGDirectDisplayID display=CGMainDisplayID();CGDisplayModeRef selected=NULL;
    do {
        CFArrayRef modes=CGDisplayCopyAllDisplayModes(display,(__bridge CFDictionaryRef)options);
        for(id item in (__bridge NSArray *)modes) {
            CGDisplayModeRef mode=(__bridge CGDisplayModeRef)item;
            if(CGDisplayModeGetPixelWidth(mode)==w&&CGDisplayModeGetPixelHeight(mode)==h&&
               CGDisplayModeGetWidth(mode)==w/scale&&CGDisplayModeGetHeight(mode)==h/scale) {
                selected=CGDisplayModeRetain(mode);break;
            }
        }
        if(modes)CFRelease(modes);
        if(selected)break;
        usleep(100000);
    }while(deadline.timeIntervalSinceNow>0);
    if(!selected){fprintf(stderr,"The requested display mode is not available yet.\n");return 2;}
    CGDisplayConfigRef configuration=NULL;
    CGError error=CGBeginDisplayConfiguration(&configuration);
    if(error==kCGErrorSuccess) {
        error=CGConfigureDisplayWithDisplayMode(configuration,display,selected,NULL);
        if(error==kCGErrorSuccess)error=CGCompleteDisplayConfiguration(configuration,kCGConfigureForSession);
        else CGCancelDisplayConfiguration(configuration);
    }
    CGDisplayModeRelease(selected);
    if(error!=kCGErrorSuccess){fprintf(stderr,"macOS could not change the display mode (%d).\n",error);return 3;}
    // A successful API call alone is insufficient: require observed geometry.
    for(int i=0;i<20;i++) {
        CGDisplayModeRef current=CGDisplayCopyDisplayMode(display);
        BOOL matches=current&&CGDisplayModeGetPixelWidth(current)==w&&CGDisplayModeGetPixelHeight(current)==h&&
            CGDisplayModeGetWidth(current)==w/scale&&CGDisplayModeGetHeight(current)==h/scale;
        if(current)CGDisplayModeRelease(current);
        if(matches){printf("{\"PixelWidth\":%zu,\"PixelHeight\":%zu,\"Scale\":%zu}\n",w,h,scale);return 0;}
        usleep(50000);
    }
    fprintf(stderr,"macOS did not retain the requested display mode.\n");return 4;
}}
