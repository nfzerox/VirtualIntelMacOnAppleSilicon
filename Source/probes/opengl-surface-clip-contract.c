/* Own offscreen surface only: no primary ID, display flush, screen sampling,
 * or change to another process's shape. The public 10.9 surface ABI supplies
 * geometry on native Intel. No claim about pixel visibility follows merely
 * from accepting a shape. Research surface creation is WindowServer-only:
 * do not weaken that restriction to run this diagnostic. */
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <IOKit/graphics/IOAccelSurfaceConnect.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int run(io_service_t accelerator,unsigned count)
{
    io_connect_t surface=0;
    /* Non-primary, per-process owned surface, disposed before returning. */
    uint64_t id=0x70000000u|((unsigned)getpid()<<3)|count;
    IOReturn e=IOServiceOpen(accelerator,mach_task_self(),0,&surface);
    uint64_t mode[]={id,kIOAccelSurfaceModeWindowedBit|kIOAccelSurfaceModeColorDepth8888};
    if(!e)e=IOConnectCallScalarMethod(surface,kIOAccelSurfaceSetIDMode,mode,2,NULL,NULL);
    struct { uint32_t count;IOAccelBounds bounds,rects[2]; } region={0};
    region.count=count;region.bounds=(IOAccelBounds){31,47,64,48};
    region.rects[0]=(IOAccelBounds){33,51,20,30};
    region.rects[1]=(IOAccelBounds){61,51,12,30};
    uint64_t options[]={kIOAccelSurfaceShapeIdentityScaleBit,0};
    if(!e)e=IOConnectCallMethod(surface,kIOAccelSurfaceSetShape,options,2,
        &region,sizeof(IOAccelDeviceRegion)+count*sizeof(IOAccelBounds),NULL,NULL,NULL,NULL);
    IOAccelSurfaceInformation info={0};size_t bytes=sizeof(info);uint64_t lock=kIOAccelSurfaceLockInBacking;
    int locked=0;
    if(!e){e=IOConnectCallMethod(surface,kIOAccelSurfaceReadLockOptions,&lock,1,NULL,0,NULL,NULL,&info,&bytes);locked=!e;}
    /* Native Intel reports width/height but no CPU mapping/stride for this
     * driver-owned offscreen allocation. Do not infer mapping from success. */
    int good=!e && bytes==sizeof(info) && info.width==64 && info.height==48;
    printf("%s native shape count=%u error=%x geometry=%ux%u stride=%u\n",good?"PASS":"FAIL",
        count,e,info.width,info.height,info.rowBytes);
    if(locked) {
        IOReturn unlock=IOConnectCallScalarMethod(surface,kIOAccelSurfaceReadUnlockOptions,&lock,1,NULL,NULL);
        if(unlock){printf("FAIL unlock=%x\n",unlock);good=0;}
    }
    if(surface)IOServiceClose(surface);
    return good;
}
int main(int argc,char **argv)
{
    int require_intel=argc==2 && !strcmp(argv[1],"--require-intel");
    if(argc!=1 && !require_intel)return 2;
    io_service_t accelerator=0;UInt32 index=0;
    IOReturn e=IOAccelFindAccelerator(CGDisplayIOServicePort(CGMainDisplayID()),&accelerator,&index);
    if(e || !accelerator){printf("No display accelerator: %x\n",e);return 2;}
    io_name_t name={0};IORegistryEntryGetName(accelerator,name);
    int research=IOObjectConformsTo(accelerator,"VMsvga2Accel");
    printf("accelerator=%s research=%d framebuffer=%u\n",name,research,index);
    if(index || research || !strstr(name,"Intel")){IOObjectRelease(accelerator);return 2;}
    int good=1;for(unsigned count=0;count<3;++count)good=run(accelerator,count) && good;
    IOObjectRelease(accelerator);return good?0:1;
}
