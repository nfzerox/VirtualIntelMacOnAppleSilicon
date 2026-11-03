/* Owned research control: query counters or select same-boot CPU/native A/B.
 * Does not expose raw physical addresses or permit framebuffer writes. */
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <string.h>
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
#include "../vendor/source/VMsvga2/vminclude/vim_passthrough_opengl.h"
int main(int argc,char** argv)
{
    uint64_t mode=0;
    if(argc==2 && !strcmp(argv[1],"off"))mode=1;
    else if(argc==2 && !strcmp(argv[1],"on"))mode=2;
    else if(argc==2 && !strcmp(argv[1],"upload-off"))mode=3;
    else if(argc==2 && !strcmp(argv[1],"upload-on"))mode=4;
    else if(argc==2 && !strcmp(argv[1],"bulk-off"))mode=5;
    else if(argc==2 && !strcmp(argv[1],"bulk-on"))mode=6;
    else if(argc==2 && !strcmp(argv[1],"doorbell-off"))mode=7;
    else if(argc==2 && !strcmp(argv[1],"doorbell-on"))mode=8;
    else if(argc==2 && !strcmp(argv[1],"command-copy-off"))mode=9;
    else if(argc==2 && !strcmp(argv[1],"command-copy-4k"))mode=10;
    else if(argc==2 && !strcmp(argv[1],"command-copy-32k"))mode=11;
    else if(argc==2 && !strcmp(argv[1],"command-copy-64k"))mode=12;
    else if(argc==2 && !strcmp(argv[1],"parallel-off"))mode=13;
    else if(argc==2 && !strcmp(argv[1],"parallel-on"))mode=14;
    else if(argc!=1) { fprintf(stderr,"usage: %s [on|off|upload-on|upload-off|bulk-on|bulk-off|doorbell-on|doorbell-off|command-copy-off|command-copy-4k|command-copy-32k|command-copy-64k|parallel-on|parallel-off]\n",argv[0]);return 2; }
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2Accel"));
    io_connect_t connection=0;
    IOReturn error=service?IOServiceOpen(service,mach_task_self(),2,&connection):kIOReturnNotFound;
    if(service)IOObjectRelease(service);
    struct VIMGLFramebufferStats stats={0};size_t size=sizeof(stats);
    if(!error)error=IOConnectCallMethod(connection,kIOVM2DFramebufferBlitControl,&mode,1,NULL,0,NULL,NULL,&stats,&size);
    if(connection)IOServiceClose(connection);
    if(error || size!=sizeof(stats) || stats.version!=1) { fprintf(stderr,"query failed %x size=%lu\n",error,(unsigned long)size);return 1; }
    if(mode>=13 && !(stats.flags&256u)){fputs("Independent-doorbell control unavailable; no supported policy change.\n",stderr);return 1;}
    printf("enabled=%u supported=%u ram_upload=%u bulk_enabled=%u bulk_supported=%u doorbell_supported=%u doorbell_enabled=%u parallel_supported=%u parallel_enabled=%u rejection=%u command_copy_KiB=%u blits=%llu bytes=%llu fallbacks=%llu\n",stats.flags&1,(stats.flags>>1)&1,(stats.flags>>2)&1,(stats.flags>>3)&1,(stats.flags>>4)&1,(stats.flags>>5)&1,(stats.flags>>6)&1,(stats.flags>>8)&1,(stats.flags>>9)&1,(stats.flags>>10)&63u,(stats.flags>>16)&255u,
           stats.blits,stats.bytes,stats.fallbacks);
    return 0;
}
