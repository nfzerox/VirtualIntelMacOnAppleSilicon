/* Real registered CGL window and actual driver endpoint, never a guessed CGS
 * ID or a replacement renderer. CPU/pin protocol only, NOT shared GPU render
 * or a desktop-performance qualification. Run x64 and i386 independently. */
#import <Cocoa/Cocoa.h>
#import <OpenGL/CGLContext.h>
#include <OpenGL/gl.h>
#include <IOKit/IOKitLib.h>
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
#include "../vendor/source/VMsvga2/vminclude/vim_passthrough_opengl.h"
#include "../vendor/source/VMsvga2/vminclude/vim_framebuffer.h"
#include "opengl-backing-loan-gpu.inc"
extern CGLError CGLGetSurface(CGLContextObj,uint32_t*,uint32_t*,uint32_t*);
static int outcome=1;
#define REQUIRE(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);goto done;}}while(0)
static IOReturn lockBacking(io_connect_t client,struct VIMSurfaceVRAM* backing)
{
    struct VIMSurfaceMapping mapping={0};size_t size=sizeof(mapping);
    IOReturn error=IOConnectCallStructMethod(client,kIOVM2DLockSurfaceBacking,NULL,0,&mapping,&size);
    if(error)return error;
    struct VIMSurfaceRAM ram={0};uint64_t args[]={0,UINT32_MAX};size=sizeof(ram);
    error=IOConnectCallMethod(client,kIOVM2DGetRAMSurface,args,2,NULL,0,NULL,NULL,&ram,&size);
    if(!error && (size!=sizeof(ram) || ram.backing.version!=VIM_SURFACE_RAM_VERSION))return kIOReturnBadArgument;
    if(!error)*backing=ram.backing;return error;
}
static IOReturn beginLoan(io_connect_t client,struct VIMSurfaceVRAM const* backing,struct VIMSurfaceBackingLoan* loan)
{size_t size=sizeof(*loan);return IOConnectCallStructMethod(client,kIOVM2DBeginRAMBackingLoan,backing,sizeof(*backing),loan,&size);}
static IOReturn endLoan(io_connect_t client,uint64_t serial,unsigned mode,struct VIMSurfaceState* state)
{uint64_t args[]={serial,mode};size_t size=sizeof(*state);return IOConnectCallMethod(client,kIOVM2DEndRAMBackingLoan,args,2,NULL,0,NULL,NULL,state,&size);}
static IOReturn pages(io_connect_t client,uint64_t serial,uint32_t first,struct VIMSurfaceRAM* ram)
{uint64_t args[]={serial,first};size_t size=sizeof(*ram);return IOConnectCallMethod(client,kIOVM2DGetRAMBackingLoan,args,2,NULL,0,NULL,NULL,ram,&size);}
@interface VIMLoanView:NSOpenGLView {BOOL _ran;}
@end
@implementation VIMLoanView
- (void)finish:(NSTimer*)timer {(void)timer;exit(outcome);}
- (void)drawRect:(NSRect)rect
{
    (void)rect;if(_ran)return;_ran=YES;
    [self.openGLContext makeCurrentContext];glClearColor(.2,.4,.8,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();
    io_service_t service=0;io_connect_t endpoint=0,observer=0;
    uint32_t ids[3]={0};uint32_t record[11]={0};uint64_t target[2]={0};size_t size=0;
    struct VIMGLTransfer open={1,0,0};struct VIMGLResult opened={0};
    struct VIMSurfaceVRAM backing={0};struct VIMSurfaceBackingLoan loan={0},duplicate={0};
    struct VIMSurfaceRAM snapshot={0},again={0};struct VIMSurfaceState state={0};
    struct VIMSurfaceMapping blocked={0};uint64_t read_only=1;
    REQUIRE(!CGLGetSurface(self.openGLContext.CGLContextObj,&ids[0],&ids[1],&ids[2]) && ids[2]);
    // Optional ID comes from the separate registered WS fixture above, NOT
    // from guessing an internal desktop surface. The CGL window is unchanged.
    if(getenv("VIM_LOAN_SURFACE_ID")){
        char* end=0;unsigned long chosen=strtoul(getenv("VIM_LOAN_SURFACE_ID"),&end,0);
        REQUIRE(end && !*end && chosen>2 && chosen<=UINT32_MAX);ids[2]=(uint32_t)chosen;
    }
    fprintf(stderr,"renderer=%s actual surface=%x\n",glGetString(GL_RENDERER),ids[2]);
    service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2Accel"));
    REQUIRE(service && !IOServiceOpen(service,mach_task_self(),2,&endpoint));
    REQUIRE(!IOServiceOpen(service,mach_task_self(),2,&observer));
    target[0]=ids[2];target[1]=0x800;size=sizeof(record);
    REQUIRE(!IOConnectCallMethod(endpoint,kIOVM2DSetSurface,target,2,NULL,0,NULL,NULL,record,&size));
    size=sizeof(record);REQUIRE(!IOConnectCallMethod(observer,kIOVM2DSetSurface,target,2,NULL,0,NULL,NULL,record,&size));
    size=sizeof(opened);REQUIRE(!IOConnectCallStructMethod(endpoint,kIOVM2DDirectGLTransfer,&open,sizeof(open),&opened,&size));
    fprintf(stderr,"endpoint capabilities=%x\n",opened.reserved);
    IOReturn queried=lockBacking(endpoint,&backing);
    fprintf(stderr,"actual RAM backing status=%x version=%u geometry=%ux%u pitch=%u bytes=%llu\n",
        queried,backing.version,backing.width,backing.height,backing.row_bytes,(unsigned long long)backing.length);
    REQUIRE(!queried);
    if(getenv("VIM_LOAN_QUERY_ONLY")){outcome=0;puts("OBSERVED actual RAM backing only; no loan qualification");goto done;}
    REQUIRE(opened.reserved&VIM_GL_RESULT_CAP_DRAINED_CLOSE);
    REQUIRE(!beginLoan(endpoint,&backing,&loan) && loan.serial && !memcmp(&loan.backing,&backing,sizeof(backing)));
    REQUIRE(beginLoan(endpoint,&backing,&duplicate)==kIOReturnBusy);
    size=sizeof(state);REQUIRE(IOConnectCallMethod(endpoint,kIOVM2DUnlockSurfaceBacking,&read_only,1,NULL,0,NULL,NULL,&state,&size)==kIOReturnBusy);
    size=sizeof(blocked);REQUIRE(IOConnectCallStructMethod(observer,kIOVM2DLockSurfaceBacking,NULL,0,&blocked,&size)==kIOReturnCannotLock);
    REQUIRE(pages(endpoint,loan.serial+1,0,&snapshot)==kIOReturnNotOpen);
    REQUIRE(!pages(endpoint,loan.serial,0,&snapshot) && snapshot.total && snapshot.count && !snapshot.first);
    for(unsigned i=0;i<snapshot.count;++i)REQUIRE(snapshot.pages[i] && !(snapshot.pages[i]&4095));
    REQUIRE(!pages(endpoint,loan.serial,0,&again) && !memcmp(&snapshot,&again,sizeof(snapshot)));
    REQUIRE(pages(endpoint,loan.serial,snapshot.total,&again)==kIOReturnBadArgument);
    REQUIRE(endLoan(endpoint,loan.serial+1,VIM_RAM_LOAN_READ_ONLY,&state)==kIOReturnNotOpen);
    REQUIRE(!endLoan(endpoint,loan.serial,VIM_RAM_LOAN_READ_ONLY,&state));
    REQUIRE(state.object==backing.state.object && state.incarnation==backing.state.incarnation &&
        state.content_epoch==backing.state.content_epoch && !(state.flags&VIM_SURFACE_STATE_CONTEXT_LOCKED));
    REQUIRE(pages(endpoint,loan.serial,0,&again)==kIOReturnNotOpen);
    puts("PASS scoped pin identity/pages, stale serials, exclusion and read-only release");
    REQUIRE(!lockBacking(endpoint,&backing));uint64_t old=loan.serial;
    REQUIRE(!beginLoan(endpoint,&backing,&loan) && loan.serial>old);
    REQUIRE(!endLoan(endpoint,loan.serial,VIM_RAM_LOAN_WRITTEN,&state) && state.content_epoch==backing.state.content_epoch+1);
    puts("PASS reused prepared backing and conservative written content epoch");
    if(getenv("VIM_LOAN_GPU")){
        REQUIRE(VIMQualifyBackingLoanGPU(&endpoint,observer));
        outcome=0;goto done;
    }
    REQUIRE(!lockBacking(endpoint,&backing) && !beginLoan(endpoint,&backing,&loan));
    if(getenv("VIM_LOAN_HOLD")){
        printf("READY held loan serial=%llu source=%x; close the registered source then send c\n",
            (unsigned long long)loan.serial,ids[2]);fflush(stdout);
        REQUIRE(getchar()=='c');
        REQUIRE(pages(endpoint,loan.serial,0,&again)==kIOReturnNotOpen);
        puts("PASS source close cancelled borrower before returning CPU backing");outcome=0;goto done;
    }
    REQUIRE(!IOServiceClose(endpoint));endpoint=0;
    REQUIRE(!lockBacking(observer,&backing));
    puts("PASS closing loan owner retires stream and returns CPU ownership");outcome=0;
done:
    if(endpoint)IOServiceClose(endpoint);if(observer)IOServiceClose(observer);if(service)IOObjectRelease(service);
    [NSTimer scheduledTimerWithTimeInterval:.2 target:self selector:@selector(finish:) userInfo:nil repeats:NO];
}
@end
int main(void)
{
    @autoreleasepool {
        NSApplication* app=[NSApplication sharedApplication];[app setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0};
        NSOpenGLPixelFormat* format=[[NSOpenGLPixelFormat alloc]initWithAttributes:attrs];if(!format)return 2;
        NSWindow* window=[[NSWindow alloc]initWithContentRect:NSMakeRect(180,180,320,180)
            styleMask:NSTitledWindowMask|NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
        [window setTitle:@"Prepared backing loan qualification"];
        VIMLoanView* view=[[VIMLoanView alloc]initWithFrame:NSMakeRect(0,0,320,180) pixelFormat:format];
        [window setContentView:view];[window makeKeyAndOrderFront:nil];[app activateIgnoringOtherApps:YES];[app run];
    }
    return outcome;
}
