/* Finite native integration/latency check on this process's own real GL
 * window. Never changes WindowServer's primary surface or another app. */
#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach_time.h>
#undef check
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
#include "../vendor/source/VMsvga2/vminclude/vim_surface_query.h"
#include "../PassthroughOpenGL/cgs-metadata-query.h"
extern CGLError CGLGetSurface(CGLContextObj,uint32_t *,uint32_t *,uint32_t *);
static int outcome=2;
static int check(const char *name,int ok)
{fprintf(stderr,"%s %s\n",ok?"PASS":"FAIL",name);return ok;}
@interface VIMMetadataView : NSOpenGLView { BOOL tested; }
@end
@implementation VIMMetadataView
- (void)finish:(NSTimer *)timer {(void)timer;exit(outcome);}
- (void)drawRect:(NSRect)rect
{
    (void)rect;if(tested)return;tested=YES;
    [self.openGLContext makeCurrentContext];glClearColor(.2,.4,.8,1);
    glClear(GL_COLOR_BUFFER_BIT);glFinish();
    uint32_t ids[3]={0};CGLError ce=CGLGetSurface(self.openGLContext.CGLContextObj,&ids[0],&ids[1],&ids[2]);
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2Accel"));
    io_connect_t connection=0;IOReturn e=service?IOServiceOpen(service,mach_task_self(),2,&connection):kIOReturnNotFound;
    if(service)IOObjectRelease(service);
    int good=check("native hardware GL window and selected accelerator",!ce && ids[2] && !e);
    fprintf(stderr,"renderer=%s surface=%x open=%x\n",glGetString(GL_RENDERER),ids[2],e);
    if(!e){
        // Same default getTargetAndTrapForIndex(NULL) path as an older
        // provider which has no metadata entry at index1. Check the actual
        // guest kernel's error, not only a mock's chosen IOReturn value.
        IOReturn absent=IOConnectTrap4(connection,99,0,0,0,0);
        fprintf(stderr,"unknown trap error=%x\n",absent);
        good=check("unknown trap takes the adapter's old-provider fallback",
            absent==kIOReturnUnsupported || absent==kIOReturnBadArgument)&&good;
    }
    if(!e)e=IOConnectTrap4(connection,VIM_SURFACE_QUERY_TRAP,0,0,0,0);
    good=check("actual loaded provider supports read-only metadata trap",!e)&&good;
    uint64_t target[]={ids[2],0x800};uint32_t record[11];size_t n=sizeof(record);
    if(!e)e=IOConnectCallMethod(connection,kIOVM2DSetSurface,target,2,NULL,0,NULL,NULL,record,&n);
    struct VIMSurfaceMapping mapping={0};n=sizeof(mapping);int locked=0;
    if(!e){e=IOConnectCallStructMethod(connection,kIOVM2DLockGPUSurface,NULL,0,&mapping,&n);locked=!e;}
    good=check("own window bound and GPU backing held",!e)&&good;
    const unsigned selectors[]={kIOVM2DGetSurfaceGeometry,kIOVM2DGetSurfaceState,
        kIOVM2DGetSurfaceDrawingSize,kIOVM2DGetSurfacePixelFormat};
    unsigned char original[32],fast[32];
    for(unsigned i=0;!e && i<4;++i){
        n=VIMSurfaceQueryBytes(i+1);memset(original,0xa5,sizeof(original));memset(fast,0xa5,sizeof(fast));
        IOReturn old=IOConnectCallStructMethod(connection,selectors[i],NULL,0,original,&n);
        size_t fast_bytes=VIMSurfaceQueryBytes(i+1);
        IOReturn query=VIMSurfaceMetadataQuery(connection,selectors[i],fast,&fast_bytes);
        fprintf(stderr,"kind=%u MIG=%x trap=%x bytes=%lu\n",i+1,old,query,(unsigned long)n);
        good=check("MIG/trap results and successful metadata match",old==query && (old || !memcmp(original,fast,sizeof(fast))))&&good;
    }
    if(!e){
        good=check("reject wrong length, kind, reserved word and user pointer",
            IOConnectTrap4(connection,1,1,(uintptr_t)fast,31,0)==kIOReturnBadArgument &&
            IOConnectTrap4(connection,1,5,(uintptr_t)fast,32,0)==kIOReturnBadArgument &&
            IOConnectTrap4(connection,1,1,(uintptr_t)fast,32,1)==kIOReturnBadArgument &&
            IOConnectTrap4(connection,1,1,1,32,0)==kIOReturnVMError)&&good;
        mach_timebase_info_data_t timebase;mach_timebase_info(&timebase);
        for(unsigned pass=0;pass<4;++pass){
            uint64_t start=mach_absolute_time();
            for(unsigned i=0;i<2000 && !e;++i){
                n=32;e=(pass&1)?VIMSurfaceMetadataQuery(connection,kIOVM2DGetSurfaceState,fast,&n):
                    IOConnectCallStructMethod(connection,kIOVM2DGetSurfaceState,NULL,0,fast,&n);
            }
            double ns=(mach_absolute_time()-start)*(double)timebase.numer/timebase.denom/2000.;
            fprintf(stderr,"metadata %s pass=%u ns/query=%.1f error=%x\n",(pass&1)?"adapter":"MIG",pass,ns,e);
        }
    }
    if(locked){uint64_t flag=VIM_SURFACE_UNLOCK_READ_ONLY;struct VIMSurfaceState state={0};n=sizeof(state);
        IOReturn released=IOConnectCallMethod(connection,kIOVM2DUnlockSurfaceBacking,&flag,1,NULL,0,NULL,NULL,&state,&n);
        good=check("read-only backing released",!released)&&good;
        n=32;IOReturn old=IOConnectCallStructMethod(connection,kIOVM2DGetSurfaceState,NULL,0,original,&n);
        n=32;IOReturn query=VIMSurfaceMetadataQuery(connection,kIOVM2DGetSurfaceState,fast,&n);
        good=check("unlocked getter errors remain identical",old==query && old!=0)&&good;
    }
    if(connection)IOServiceClose(connection);
    unsigned char pixel[4]={0};glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    good=check("native window pixels preserved",!glGetError() && pixel[0]==51 && pixel[1]==102 && pixel[2]==204 && pixel[3]==255)&&good;
    outcome=good && !e?0:1;
    [NSTimer scheduledTimerWithTimeInterval:1 target:self selector:@selector(finish:) userInfo:nil repeats:NO];
}
@end
int main(void)
{
    NSAutoreleasePool *pool=[NSAutoreleasePool new];NSApplication *app=[NSApplication sharedApplication];
    NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
        NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0};
    NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];if(!format)return 2;
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(180,180,128,96)
        styleMask:NSTitledWindowMask|NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
    [window setTitle:@"Read-only surface metadata"];
    VIMMetadataView *view=[[VIMMetadataView alloc] initWithFrame:NSMakeRect(0,0,128,96) pixelFormat:format];
    [window setContentView:view];[window makeKeyAndOrderFront:nil];[app activateIgnoringOtherApps:YES];
    [app run];[pool drain];return outcome;
}
