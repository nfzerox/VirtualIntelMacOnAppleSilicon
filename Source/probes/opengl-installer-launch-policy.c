/* Guest-only qualification of recovery dyld's launch policy. Does not attach
 * to another task or modify policy. Run an ad-hoc copy with the original
 * installer's entitlements to distinguish signing provenance from entitlement
 * contents before changing a generated recovery image. */
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern int csops(pid_t, unsigned int, void *, size_t);
int main(void) {
    unsigned int flags=0;int status=csops(getpid(),0,&flags,sizeof(flags));
    int loaded=0;
    for(unsigned int i=0;i<_dyld_image_count();i++) {
        const char *name=_dyld_get_image_name(i);
        if(name&&strstr(name,"opengl-installer-automation.dylib"))loaded=1;
    }
    printf("csops=%d flags=0x%x adapter-loaded=%d\n",status,flags,loaded);
    return status?1:0;
}
