/* Read-only geometry of WindowServer's currently shaped primary surface.
 * --geometry SURFACE queries metadata without locking/publishing CPU pixels. */
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include "../vendor/source/VMsvga2/vminclude/vim_framebuffer.h"
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
static void sample(const char *name,const struct VIMSurfaceMapping *mapping,int x,int y)
{
    const struct VIMFramebufferLayout *l=&mapping->layout;
    uint64_t offset=(uint64_t)l->offset+(uint64_t)y*l->row_bytes+(uint64_t)x*4;
    if(x<0 || y<0 || (unsigned)x>=l->width || (unsigned)y>=l->height ||
       offset>mapping->length || 4>mapping->length-offset) return;
    const unsigned char *p=(void *)(uintptr_t)(mapping->address+offset);
    printf("%s x=%d y=%d BGRA=%u,%u,%u,%u\n",name,x,y,p[0],p[1],p[2],p[3]);
}
int main(int argc,char **argv)
{
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2Accel"));
    io_connect_t connection=0;
    IOReturn e=service ? IOServiceOpen(service,mach_task_self(),2,&connection) : kIOReturnNotFound;
    if(service) IOObjectRelease(service);
    int geometry_only=argc==3 && !strcmp(argv[1],"--geometry");
    uint64_t args[]={geometry_only?strtoul(argv[2],NULL,0):1,0x800};uint32_t record[11]={0};size_t bytes=sizeof(record);
    if(!e) e=IOConnectCallMethod(connection,kIOVM2DSetSurface,args,2,NULL,0,NULL,NULL,record,&bytes);
    struct VIMSurfaceMapping mapping={0};bytes=sizeof(mapping);int locked=0;
    if(!e && !geometry_only) { e=IOConnectCallStructMethod(connection,kIOVM2DLockSurfaceBacking,NULL,0,&mapping,&bytes);locked=!e; }
    struct VIMSurfaceClip clip={0};bytes=sizeof(clip);
    if(!e) e=IOConnectCallStructMethod(connection,kIOVM2DGetSurfaceClip,NULL,0,&clip,&bytes);
    printf("clip error=%x bytes=%zu version=%u count=%u origin=%d,%d drawable=%ux%u pitch=%u\n",
        e,bytes,clip.version,clip.count,clip.origin_x,clip.origin_y,mapping.layout.width,mapping.layout.height,mapping.layout.row_bytes);
    if(!e && clip.count<=VIM_SURFACE_CLIP_RECTS)
        for(unsigned i=0;i<clip.count;++i) printf("rect[%u]=%d,%d %dx%d\n",i,clip.rects[i].x,clip.rects[i].y,clip.rects[i].w,clip.rects[i].h);
    if(locked || (!e && geometry_only)) {
        struct VIMSurfaceGeometry geometry={0};bytes=sizeof(geometry);
        IOReturn status=IOConnectCallStructMethod(connection,kIOVM2DGetSurfaceGeometry,NULL,0,&geometry,&bytes);
        printf("geometry error=%x version=%u source=%ux%u view=%d,%d %ux%u gpu-root=%u\n",status,
            geometry.version,geometry.source_width,geometry.source_height,
            geometry.x,geometry.y,geometry.width,geometry.height,geometry.reserved);
    }
    if(locked && argc==3 && !geometry_only) sample("primary-shadow",&mapping,atoi(argv[1]),atoi(argv[2]));
    if(locked) { uint64_t option=0,flags=0;uint32_t n=1;IOConnectCallScalarMethod(connection,kIOVM2DUnlockMemory,&option,1,&flags,&n); }
    if(connection) IOServiceClose(connection);
    if(argc==3 && !geometry_only) {
        service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2"));
        CFDataRef layout=service ? IORegistryEntryCreateCFProperty(service,CFSTR(VIM_FRAMEBUFFER_LAYOUT_KEY),kCFAllocatorDefault,0) : NULL;
        struct VIMSurfaceMapping scanout={0};io_connect_t fb=0;
        if(layout && CFGetTypeID(layout)==CFDataGetTypeID() && CFDataGetLength(layout)==sizeof(scanout.layout))
            memcpy(&scanout.layout,CFDataGetBytePtr(layout),sizeof(scanout.layout));
        IOReturn status=service ? IOServiceOpen(service,mach_task_self(),kIOFBSharedConnectType,&fb) : kIOReturnNotFound;
        if(!status) status=IOConnectMapMemory64(fb,110,mach_task_self(),&scanout.address,&scanout.length,kIOMapAnywhere);
        printf("scanout map=%x size=%ux%u pitch=%u\n",status,scanout.layout.width,scanout.layout.height,scanout.layout.row_bytes);
        if(!status) {
            sample("framebuffer",&scanout,atoi(argv[1]),atoi(argv[2]));
            IOConnectUnmapMemory64(fb,110,mach_task_self(),scanout.address);
        }
        if(fb) IOServiceClose(fb);if(layout) CFRelease(layout);if(service) IOObjectRelease(service);
    }
    return !!e;
}
