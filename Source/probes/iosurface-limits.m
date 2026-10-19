// Inspect advertised dimensions and a new private surface; no screen/user data.
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#import <IOKit/IOKitLib.h>
#include <sys/sysctl.h>
#include <unistd.h>
int main(int argc,const char **argv){@autoreleasepool {
    alarm(10);size_t width=5120,height=64;
    if(argc==3){width=strtoul(argv[1],NULL,10);height=strtoul(argv[2],NULL,10);}
    else if(argc!=1)return 64;
    if(width<1||height<1||width>8192||height>8192||width*height>1024*1024)return 64;
    printf("maximum width=%zu height=%zu rowBytes=%zu\n",IOSurfaceGetPropertyMaximum(kIOSurfaceWidth),
        IOSurfaceGetPropertyMaximum(kIOSurfaceHeight),IOSurfaceGetPropertyMaximum(kIOSurfaceBytesPerRow));
    // Independently inspect the exact read-only selector/24-byte record used by
    // the actual Mojave/Catalina loose framework. Distinguish kernel limits from
    // process-cached or translated userland limits; never call an unknown host ABI.
    char release[64]={0};size_t releaseSize=sizeof(release);
    if(sysctlbyname("kern.osrelease",release,&releaseSize,NULL,0)==0&&
       (strtol(release,NULL,10)==18||strtol(release,NULL,10)==19)) {
        io_service_t root=IOServiceGetMatchingService(MACH_PORT_NULL,IOServiceMatching("IOSurfaceRoot"));
        io_connect_t connection=0;IOReturn opened=root?IOServiceOpen(root,mach_task_self(),0,&connection):kIOReturnNotFound;
        uint32_t record[6]={0};size_t bytes=sizeof(record);
        IOReturn result=opened==kIOReturnSuccess?IOConnectCallStructMethod(connection,13,NULL,0,record,&bytes):opened;
        printf("root limits status=0x%x bytes=%zu words=%u,%u,%u,%u,%u,%u\n",result,bytes,
            record[0],record[1],record[2],record[3],record[4],record[5]);
        if(connection)IOServiceClose(connection);if(root)IOObjectRelease(root);
    }
    IOSurfaceRef surface=IOSurfaceCreate((CFDictionaryRef)@{(id)kIOSurfaceWidth:@(width),(id)kIOSurfaceHeight:@(height),
        (id)kIOSurfaceBytesPerElement:@4,(id)kIOSurfacePixelFormat:@(0x42475241)});
    if(!surface){puts("create failed");return 1;}
    IOReturn result=IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL);
    printf("created=%zux%zu rowBytes=%zu allocation=%zu lock=0x%x\n",IOSurfaceGetWidth(surface),
        IOSurfaceGetHeight(surface),IOSurfaceGetBytesPerRow(surface),IOSurfaceGetAllocSize(surface),result);
    if(result==kIOReturnSuccess)IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
    CFRelease(surface);return result==kIOReturnSuccess?0:2;
}}
