// Read-only validation of the shared mode boundary on native period systems.
#include "../tools/guest-display-modes.h"
#include <stdio.h>
int main(void){
    CGDirectDisplayID display=CGMainDisplayID();
    size_t width=CGDisplayPixelsWide(display),height=CGDisplayPixelsHigh(display);
    VIMGuestDisplayMode current=VIMGuestCurrentMode(display);
    int matches=VIMGuestModeMatches(current,width,height,1);
    CFArrayRef modes=VIMGuestDisplayModes(display);int enumerated=0;
    for(CFIndex i=0;modes&&i<CFArrayGetCount(modes);i++)
        if(VIMGuestModeMatches((VIMGuestDisplayMode)CFArrayGetValueAtIndex(modes,i),width,height,1))enumerated=1;
    if(current)CFRelease(current);if(modes)CFRelease(modes);
    printf("native-current=%d native-enumeration=%d width=%zu height=%zu\n",matches,enumerated,width,height);
    return matches&&enumerated?0:1;
}
