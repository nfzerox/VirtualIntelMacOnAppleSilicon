/* Mavericks display-density policy for host-driven custom modes.
 * Native panel metadata handles boot. Below its native-panel size threshold,
 * CoreGraphics advertises 2x but defaults to 1x. Select the public 2x mode
 * after a real display-change notification, never from a frame/poll loop.
 * No capture, input synthesis, injection, preferences or network access. */
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
// Leopard exposes only 1x dictionary modes. Host-driven resize is handled by
// guest-display's genuine CGDisplaySwitchToMode boundary; there is no density
// selection to run in the background. The common launch job stops after a
// successful exit. Do not load newer display APIs or a polling substitute.
int main(void){return 0;}
#else
#include <ApplicationServices/ApplicationServices.h>
#import <AppKit/AppKit.h>
#include <IOKit/IOKitLib.h>
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#include "../tools/guest-display-options.h"
#include "../vendor/source/VMsvga2/vminclude/vim_framebuffer.h"

static int queued, tracing;
static unsigned handled_generation;
static unsigned number(CFDictionaryRef properties,CFStringRef key)
{
    CFTypeRef value=CFDictionaryGetValue(properties,key);int64_t result=0;
    if(value && CFGetTypeID(value)==CFNumberGetTypeID())
        CFNumberGetValue(value,kCFNumberSInt64Type,&result);
    return result>0 && result<=UINT32_MAX?(unsigned)result:0;
}
static void reconcile(void)
{
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2"));
    if(!service)return;
    CFMutableDictionaryRef properties=NULL;
    IOReturn result=IORegistryEntryCreateCFProperties(service,&properties,kCFAllocatorDefault,0);
    IOObjectRelease(service);
    if(result || !properties)return;
    unsigned scale=number(properties,CFSTR("VIMNativePanelScale"));
    unsigned width=number(properties,CFSTR("VIMNativePanelRequestWidth"));
    unsigned height=number(properties,CFSTR("VIMNativePanelRequestHeight"));
    unsigned generation=number(properties,CFSTR("VIMNativePanelRequestGeneration"));
    struct VIMFramebufferLayout layout={0};
    CFTypeRef data=CFDictionaryGetValue(properties,CFSTR(VIM_FRAMEBUFFER_LAYOUT_KEY));
    if(data && CFGetTypeID(data)==CFDataGetTypeID() && CFDataGetLength(data)==sizeof(layout))
        memcpy(&layout,CFDataGetBytePtr(data),sizeof(layout));
    CFRelease(properties);
    if(tracing)fprintf(stderr,"display-density: generation=%u handled=%u request=%ux%u layout=%ux%u scale=%u\n",
        generation,handled_generation,width,height,layout.width,layout.height,scale);
    if(!generation || generation==handled_generation || scale!=2 || width<1440 || height<900 ||
       (width&1) || (height&1) || layout.version!=VIM_FRAMEBUFFER_LAYOUT_VERSION ||
       layout.width!=width || layout.height!=height)return;
    CGDirectDisplayID display=CGMainDisplayID();
    CGDisplayModeRef current=CGDisplayCopyDisplayMode(display);
    if(!current)return;
    int matching=CGDisplayModeGetPixelWidth(current)==width && CGDisplayModeGetPixelHeight(current)==height;
    int correct=matching && CGDisplayModeGetWidth(current)==width/2 && CGDisplayModeGetHeight(current)==height/2;
    CGDisplayModeRelease(current);
    // A pending host generation owns this request. At initial Aqua login,
    // CG can still report the boot mode while the advertised custom mode
    // and driver layout already match the host. Select it too; the consumed
    // generation/layout guards prevent replay over later manual changes.
    if(correct) { handled_generation=generation;return; }
    CFDictionaryRef options=VIMCopyDisplayModeOptions();
    CFArrayRef modes=CGDisplayCopyAllDisplayModes(display,options);
    if(options)CFRelease(options);
    CGDisplayModeRef selected=NULL;
    if(modes)for(CFIndex i=0;i<CFArrayGetCount(modes);++i) {
        CGDisplayModeRef mode=(CGDisplayModeRef)CFArrayGetValueAtIndex(modes,i);
        if(CGDisplayModeGetPixelWidth(mode)==width && CGDisplayModeGetPixelHeight(mode)==height &&
           CGDisplayModeGetWidth(mode)==width/2 && CGDisplayModeGetHeight(mode)==height/2) {
            selected=CGDisplayModeRetain(mode);break;
        }
    }
    if(modes)CFRelease(modes);
    if(!selected) { if(tracing)fprintf(stderr,"display-density: no 2x choice for %ux%u\n",width,height);return; }
    // Never repeat a consumed host request in response to our own callback,
    // or override a later manually chosen mode with cached CG information.
    handled_generation=generation;
    CGError error=VIMSetDisplayMode(display,selected);
    CGDisplayModeRelease(selected);
    if(tracing || error)fprintf(stderr,"display-density: %ux%u at 2x result=%d\n",width,height,error);
}
static void changed(CGDirectDisplayID display,CGDisplayChangeSummaryFlags flags,void *context)
{
    (void)display;(void)context;
    if(tracing)fprintf(stderr,"display-density: notification display=%u flags=%x\n",display,flags);
    if(flags&kCGDisplayBeginConfigurationFlag)return;
    // Apple's callback contract prohibits reconfiguring inside the callback.
    // Queue a single operation after the notification has returned instead.
    dispatch_async(dispatch_get_main_queue(),^{
        if(queued)return;queued=1;
        dispatch_async(dispatch_get_main_queue(),^{queued=0;reconcile();});
    });
}
int main(void)
{
    @autoreleasepool {
    // A bare CFRunLoop receives callbacks made by our own configuration
    // calls, but does not pump external CGS port notifications on 10.9.
    // Use the real AppKit connection/event pump, with no windows or Dock UI.
    NSApplication *application=[NSApplication sharedApplication];
    [application setActivationPolicy:NSApplicationActivationPolicyProhibited];
    tracing=getenv("VIM_DISPLAY_AGENT_TRACE")!=NULL;
    CGError error=CGDisplayRegisterReconfigurationCallback(changed,NULL);
    if(error) { fprintf(stderr,"display-density: could not subscribe (%d)\n",error);return 1; }
    reconcile();
    [application run];
    CGDisplayRemoveReconfigurationCallback(changed,NULL);
    return 0;
    }
}
#endif
