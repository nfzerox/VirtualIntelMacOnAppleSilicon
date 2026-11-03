/* Owned diagnostic for VMsvga2's existing privileged custom-mode handshake.
 * No WindowServer connection stealing or preferences/cache modification. */
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/IOKitLib.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "common_fb.h" /* compile with -I Source/vendor/source/VMsvga2/FB */

static unsigned dimension(const char *text)
{
    char *end=NULL;errno=0;unsigned long value=strtoul(text,&end,10);
    if(errno || !end || *end || value<600 || value>2560) {
        fprintf(stderr,"invalid bounded research dimension: %s\n",text);exit(64);
    }
    return (unsigned)value;
}
int main(int argc,char **argv)
{
    if(argc!=1 && argc!=3) { fprintf(stderr,"usage: %s [pixel-width pixel-height]\n",argv[0]);return 64; }
    struct CustomModeData input={0,0,0},output={0,0,0};
    if(argc==3) { input.flags=1;input.width=dimension(argv[1]);input.height=dimension(argv[2]); }
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2"));
    io_connect_t connection=0;
    // Type 0 belongs exclusively to WindowServer; never open it here.
    IOReturn error=service?IOServiceOpen(service,mach_task_self(),2,&connection):kIOReturnNotFound;
    if(service)IOObjectRelease(service);
    fprintf(stderr,"research framebuffer open=%x\n",error);
    size_t size=sizeof(output);
    if(!error)error=IOConnectCallStructMethod(connection,0,&input,sizeof(input),&output,&size);
    if(connection)IOServiceClose(connection);
    if(error || size!=sizeof(output)) { fprintf(stderr,"custom-mode failed=%x bytes=%lu\n",error,(unsigned long)size);return 1; }
    printf("framebuffer pixels=%ux%u flags=%x\n",output.width,output.height,output.flags);
    CGDisplayModeRef mode=CGDisplayCopyDisplayMode(CGMainDisplayID());
    if(mode) {
        printf("current logical=%zux%zu pixels=%zux%zu flags=%x\n",CGDisplayModeGetWidth(mode),
            CGDisplayModeGetHeight(mode),CGDisplayModeGetPixelWidth(mode),CGDisplayModeGetPixelHeight(mode),
            CGDisplayModeGetIOFlags(mode));
        CGDisplayModeRelease(mode);
    }
    return argc==3 && (output.width!=input.width || output.height!=input.height)?2:0;
}
