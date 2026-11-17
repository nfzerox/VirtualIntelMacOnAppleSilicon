/* Inspect explicitly supplied global IOSurface IDs without mapping pixels,
 * mutating storage, creating a GL context or attaching to another process. */
#include <IOSurface/IOSurface.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

int main(int argc,char **argv)
{
    if(argc<2){fprintf(stderr,"usage: iosurface-metadata ID ...\n");return 64;}
    for(int i=1;i<argc;++i){
        char *end=NULL;errno=0;unsigned long long id=strtoull(argv[i],&end,0);
        if(errno||!argv[i][0]||*end||!id||id>UINT32_MAX)return 64;
        IOSurfaceRef surface=IOSurfaceLookup((IOSurfaceID)id);
        if(!surface){printf("id=%llu unavailable\n",id);continue;}
        size_t planes=IOSurfaceGetPlaneCount(surface);
        printf("id=%llu format=%08x size=%zux%zu element=%zu row=%zu allocation=%zu planes=%zu\n",
            id,(unsigned)IOSurfaceGetPixelFormat(surface),IOSurfaceGetWidth(surface),
            IOSurfaceGetHeight(surface),IOSurfaceGetBytesPerElement(surface),
            IOSurfaceGetBytesPerRow(surface),IOSurfaceGetAllocSize(surface),planes);
        for(size_t p=0;p<(planes?planes:1);++p)
            printf(" plane=%zu size=%zux%zu element=%zu row=%zu\n",p,
                IOSurfaceGetWidthOfPlane(surface,p),IOSurfaceGetHeightOfPlane(surface,p),
                IOSurfaceGetBytesPerElementOfPlane(surface,p),IOSurfaceGetBytesPerRowOfPlane(surface,p));
        CFRelease(surface);
    }
    return 0;
}
