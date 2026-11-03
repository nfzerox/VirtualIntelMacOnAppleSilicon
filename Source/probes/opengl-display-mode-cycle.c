/* Force display-surface recreation for bounded native driver tracing.
 * Session-only configuration; restore the exact original mode before exit.
 * Run with the DTrace page-flip probe, not as a performance workload.
 */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <unistd.h>

static CGError apply(CGDirectDisplayID display,CGDisplayModeRef mode)
{
    CGDisplayConfigRef config=NULL;
    CGError e=CGBeginDisplayConfiguration(&config);
    if(e)return e;
    e=CGConfigureDisplayWithDisplayMode(config,display,mode,NULL);
    if(e){CGCancelDisplayConfiguration(config);return e;}
    return CGCompleteDisplayConfiguration(config,kCGConfigureForSession);
}
int main(void)
{
    CGDirectDisplayID display=CGMainDisplayID();
    CGDisplayModeRef original=CGDisplayCopyDisplayMode(display),candidate=NULL;
    CFArrayRef modes=CGDisplayCopyAllDisplayModes(display,NULL);
    if(!original || !modes)return 2;
    for(CFIndex i=0;i<CFArrayGetCount(modes);++i){
        CGDisplayModeRef m=(CGDisplayModeRef)CFArrayGetValueAtIndex(modes,i);
        if(CGDisplayModeGetWidth(m)==1024 && CGDisplayModeGetHeight(m)==768 &&
           CGDisplayModeGetIODisplayModeID(m)!=CGDisplayModeGetIODisplayModeID(original)){
            candidate=m;break;
        }
    }
    if(!candidate){fprintf(stderr,"No safe 1024x768 alternate mode; unchanged\n");return 2;}
    printf("original=%zux%zu pixels=%zux%zu\n",CGDisplayModeGetWidth(original),
        CGDisplayModeGetHeight(original),CGDisplayModeGetPixelWidth(original),
        CGDisplayModeGetPixelHeight(original));fflush(stdout);
    CGError changed=apply(display,candidate);
    printf("alternate status=%d\n",changed);fflush(stdout);
    if(!changed)sleep(2);
    CGError restored=apply(display,original);
    CGDisplayModeRef current=CGDisplayCopyDisplayMode(display);
    int same=current && CGDisplayModeGetIODisplayModeID(current)==CGDisplayModeGetIODisplayModeID(original)
        && CGDisplayModeGetWidth(current)==CGDisplayModeGetWidth(original)
        && CGDisplayModeGetHeight(current)==CGDisplayModeGetHeight(original)
        && CGDisplayModeGetPixelWidth(current)==CGDisplayModeGetPixelWidth(original)
        && CGDisplayModeGetPixelHeight(current)==CGDisplayModeGetPixelHeight(original);
    printf("restore status=%d exact=%d\n",restored,same);
    if(current)CGDisplayModeRelease(current);
    CGDisplayModeRelease(original);CFRelease(modes);
    return changed || restored || !same ? 1 : 0;
}
