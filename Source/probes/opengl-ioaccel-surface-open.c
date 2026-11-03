/* Public IOKit diagnostic: identify the actual accelerator service and its
 * application-created surface admission. Never opens a display framebuffer.
 * A successful surface is immediately destroyed; no existing surface changes.
 */
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOAccelSurfaceConnect.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sysctl.h>
#include <string.h>

static IOReturn set_mode(io_connect_t c,uint64_t id,uint64_t mode)
{
    uint64_t args[]={id,mode};
    return IOConnectCallScalarMethod(c,kIOAccelSurfaceSetIDMode,args,2,NULL,NULL);
}

int main(void)
{
    char release[64]={0};size_t releaseSize=sizeof(release);
    if(sysctlbyname("kern.osrelease",release,&releaseSize,NULL,0))return 2;
    int leopard=!strncmp(release,"9.",2);
    io_iterator_t it=0;io_service_t service=0;
    IOReturn error=IOServiceGetMatchingServices(kIOMasterPortDefault,
        IOServiceMatching("VMsvga2Accel"),&it);
    if(error)return 1;
    int good=0;
    while((service=IOIteratorNext(it))){
        io_string_t path={0};IORegistryEntryGetPath(service,kIOServicePlane,path);
        printf("service=%x path=%s\n",service,path);
        io_connect_t connection=0;
        error=IOServiceOpen(service,mach_task_self(),0,&connection);
        printf("surface_open=%x connection=%x\n",error,connection);
        if(!error){
            uint32_t id=arc4random()|0x40000000U;
            IOReturn primary=set_mode(connection,1,0x200024),companion=set_mode(connection,2,0x200024);
            IOReturn window=set_mode(connection,id,0x24),depth=set_mode(connection,id,0x200023);
            IOReturn cube=set_mode(connection,id,0x300024),mipmap=set_mode(connection,id,0x110024);
            IOReturn twoD=set_mode(connection,id,0x100024);
            IOReturn valid=set_mode(connection,id,0x200024);
            io_connect_t other=0;IOReturn opened=IOServiceOpen(service,mach_task_self(),0,&other);
            IOReturn duplicate=opened?opened:set_mode(other,id,0x200024);
            // This legacy scalar dispatcher transports the ID as 32 bits;
            // passing a wider test value does not test a kernel 64-bit claim.
            printf("primary=%x companion=%x window=%x depth=%x cube=%x mipmap=%x twoD=%x valid=%x duplicate=%x\n",
                primary,companion,window,depth,cube,mipmap,twoD,valid,duplicate);
            good=primary==kIOReturnNotPrivileged && companion==kIOReturnNotPrivileged &&
                window==(leopard?kIOReturnSuccess:kIOReturnNotPrivileged) && depth==kIOReturnNotPrivileged &&
                cube==kIOReturnNotPrivileged && mipmap==kIOReturnNotPrivileged &&
                !twoD && !valid && duplicate==kIOReturnExclusiveAccess;
            if(other)IOServiceClose(other);IOServiceClose(connection);
        }
        IOObjectRelease(service);
    }
    IOObjectRelease(it);puts(good?"PASS private pbuffer admission and rejected display/alias claims":"FAIL private pbuffer admission");
    return good?0:1;
}
