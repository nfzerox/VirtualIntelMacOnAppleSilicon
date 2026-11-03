/* Transfer an owned offscreen GPU drawable to/from an owned native CGS window
 * allocation. Numeric samples/counters, no primary framebuffer access. The
 * explicitly loaded backend is local and does not replace system GL symbols. */
#import <Cocoa/Cocoa.h>
#import <OpenGL/CGLContext.h>
#include <OpenGL/gl.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach_time.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
#include "../vendor/source/VMsvga2/vminclude/vim_framebuffer.h"
extern CGLError CGLGetSurface(CGLContextObj,uint32_t *,uint32_t *,uint32_t *);
static const char *backend_path;
static int outcome=2;
static int uploading;
static int scattering;
static double seconds(void)
{
    mach_timebase_info_data_t t;mach_timebase_info(&t);
    return mach_absolute_time()*(double)t.numer/t.denom/1e9;
}
@interface VIMDirectReadView : NSOpenGLView {
    BOOL _measured;
}
@property BOOL measured;
@end
@implementation VIMDirectReadView
@synthesize measured=_measured;
- (void)drawRect:(NSRect)rect
{
    (void)rect;if(self.measured)return;self.measured=YES;
    [self.openGLContext makeCurrentContext];glClearColor(.2,.4,.8,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();
    uint32_t ids[3]={0};CGLError ce=CGLGetSurface(self.openGLContext.CGLContextObj,&ids[0],&ids[1],&ids[2]);
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2Accel"));
    io_connect_t connection=0;IOReturn error=service ? IOServiceOpen(service,mach_task_self(),2,&connection) : kIOReturnNotFound;
    if(service)IOObjectRelease(service);
    uint64_t target[]={ids[2],0x800};uint32_t record[11];size_t n=sizeof(record);
    if(!error && !ce)error=IOConnectCallMethod(connection,kIOVM2DSetSurface,target,2,NULL,0,NULL,NULL,record,&n);
    struct VIMSurfaceMapping mapping={0};n=sizeof(mapping);
    if(!error && !ce)error=IOConnectCallStructMethod(connection,kIOVM2DLockSurfaceBacking,NULL,0,&mapping,&n);
    int locked=!error && !ce;
    uint64_t bytes=(uint64_t)mapping.layout.width*mapping.layout.height*4;
    int layout=locked && n==sizeof(mapping) && mapping.layout.version==VIM_FRAMEBUFFER_LAYOUT_VERSION &&
        mapping.address && mapping.layout.width && mapping.layout.height &&
        mapping.layout.width<=8192 && mapping.layout.height<=8192 && mapping.layout.bits_per_pixel==32 &&
        mapping.layout.row_bytes==(uint64_t)mapping.layout.width*4 &&
        mapping.layout.offset<=mapping.length && bytes<=mapping.length-mapping.layout.offset;
    fprintf(stderr,"owned surface=%x get=%d lock=%x layout=%d size=%ux%u bytes=%llu\n",ids[2],ce,error,
        layout,mapping.layout.width,mapping.layout.height,bytes);
    void *library=dlopen(backend_path,RTLD_LOCAL|RTLD_NOW);
    void *(*create)(const int *,void *)=library?(void *)dlsym(library,"DGCreateContext"):NULL;
    GLboolean (*current)(void *,void *,GLenum,GLsizei,GLsizei)=library?(void *)dlsym(library,"DGMakeCurrent"):NULL;
    void (*destroy)(void *)=library?(void *)dlsym(library,"DGDestroyContext"):NULL;
    int (*read)(void *,int,int)=library?(void *)dlsym(library,"DGReadDrawable"):NULL;
    int (*stats)(uint64_t *,uint64_t *)=library?(void *)dlsym(library,"DGReadTransferStats"):NULL;
    int (*writeStats)(uint64_t *,uint64_t *)=library?(void *)dlsym(library,"DGWriteTransferStats"):NULL;
    int (*import)(const void *,int,int,size_t,int)=library?(void *)dlsym(library,"DGImportDrawable"):NULL;
    int (*rows)(void *,size_t,int,int,int,int,int)=library?(void *)dlsym(library,"DGReadDrawableRectRows"):NULL;
    int (*supportsRows)(void)=library?(void *)dlsym(library,"DGSupportsScatterReads"):NULL;
    int (*supportsRAM)(void)=library?(void *)dlsym(library,"DGSupportsRAMScatterReads"):NULL;
    void (*scissor)(GLint,GLint,GLsizei,GLsizei)=library?(void *)dlsym(library,"glScissor"):NULL;
    void (*enable)(GLenum)=library?(void *)dlsym(library,"glEnable"):NULL;
    void (*disable)(GLenum)=library?(void *)dlsym(library,"glDisable"):NULL;
    void (*clearColor)(GLclampf,GLclampf,GLclampf,GLclampf)=library?(void *)dlsym(library,"glClearColor"):NULL;
    void (*clear)(GLbitfield)=library?(void *)dlsym(library,"glClear"):NULL;
    GLenum (*getError)(void)=library?(void *)dlsym(library,"glGetError"):NULL;
    int good=layout && create && current && destroy && read && stats && clearColor && clear && getError;
    if(uploading)good=good && writeStats && import;
    if(scattering)good=good && rows && supportsRows && scissor && enable && disable;
    unsigned char *heap=good?malloc((size_t)bytes):NULL;
    void *context=heap?create(NULL,NULL):NULL;
    good=good && heap && context && current(context,heap,GL_UNSIGNED_BYTE,mapping.layout.width,mapping.layout.height);
    if(good) {
        unsigned char *vram=(void *)(uintptr_t)(mapping.address+mapping.layout.offset);
        clearColor(.2,.4,.8,1);clear(GL_COLOR_BUFFER_BIT);
        // Warm GPU and allocations before comparing the actual routes.
        good=read(heap,mapping.layout.width,mapping.layout.height) && good;
        if(scattering) {
            int w=mapping.layout.width,h=mapping.layout.height,rw=w-13,rh=h-9;
            if(getenv("VIM_SCATTER_ONE_ROW"))rh=1;
            enable(GL_SCISSOR_TEST);scissor(0,h/2,w,h-h/2);clearColor(.8,.2,.4,1);clear(GL_COLOR_BUFFER_BIT);disable(GL_SCISSOR_TEST);
            good=read(heap,w,h) && supportsRows() && good;
            unsigned char *expected=malloc((size_t)bytes);
            if(!expected)good=0;
            for(int reverse=0;expected && reverse<2;++reverse) {
                memset(vram,0xa5,(size_t)bytes);memset(expected,0xa5,(size_t)bytes);
                for(int row=0;row<rh;++row)memcpy(expected+((size_t)(4+(reverse?rh-1-row:row))*w+6)*4,
                    heap+((size_t)(3+row)*w+5)*4,(size_t)rw*4);
                uint64_t before=0,staged=0,after=0,unused=0;int device=stats(&before,&staged);double start=seconds();
                int result=rows(vram+((size_t)4*w+6)*4,mapping.layout.row_bytes,reverse,5,3,rw,rh);
                double elapsed=seconds()-start;stats(&after,&unused);
                int correct=result==1 && device && after-before==(uint64_t)rw*rh*4 && !memcmp(vram,expected,(size_t)bytes);
                fprintf(stderr,"%s scatter reverse=%d source=5,3 %dx%d destination=6,4 pitch=%u %.3f ms direct_bytes=%llu guards=%s\n",
                    correct?"PASS":"FAIL",reverse,rw,rh,mapping.layout.row_bytes,elapsed*1000,after-before,correct?"PASS":"FAIL");
                good=correct && good;
            }
            if(expected && supportsRAM && supportsRAM()) {
                unsigned char *ram=malloc((size_t)bytes);
                int ram_good=ram && read(heap,w,h);
                for(int reverse=0;ram_good && reverse<2;++reverse) {
                    memset(ram,0x5a,(size_t)bytes);memset(expected,0x5a,(size_t)bytes);
                    for(int y=0;y<rh;++y)memcpy(expected+((size_t)(4+(reverse?rh-1-y:y))*w+6)*4,
                        heap+((size_t)(3+y)*w+5)*4,(size_t)rw*4);
                    uint64_t before=0,after=0,unused=0;int device=stats(&before,&unused);double start=seconds();
                    int result=rows(ram+((size_t)4*w+6)*4,(size_t)w*4,reverse,5,3,rw,rh);
                    double elapsed=seconds()-start;stats(&after,&unused);
                    int correct=result==1 && device && after-before==(uint64_t)rw*rh*4 && !memcmp(ram,expected,(size_t)bytes);
                    fprintf(stderr,"%s RAM scatter reverse=%d %.3f ms direct_bytes=%llu guards=%s\n",
                        correct?"PASS":"FAIL",reverse,elapsed*1000,after-before,correct?"PASS":"FAIL");
                    ram_good=correct && ram_good;
                }
                free(ram);good=ram_good && good;
                // QEMU must invalidate translated code, not only dirty the
                // RAM bitmap. Execute this program's own six-byte x86 stub
                // before and after a four-byte DMA overwrite of its page.
                size_t page=(size_t)getpagesize();
                unsigned char *code=mmap(NULL,page,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0);
                if(code==MAP_FAILED)good=0;
                else {
                    const unsigned char initial[]={0xb8,1,0,0,0,0xc3};memcpy(code,initial,sizeof(initial));
                    int (*function)(void)=(void *)code;int before=function();
                    clearColor(0,2.0f/255,184.0f/255,0);clear(GL_COLOR_BUFFER_BIT);
                    int transfer=rows(code,4,0,0,0,1,1);int after=transfer==1?function():-1;
                    int correct=before==1 && after==2 && code[0]==0xb8 && code[1]==2 && code[4]==0 && code[5]==0xc3;
                    fprintf(stderr,"%s RAM DMA invalidates translated code before=%d after=%d transfer=%d\n",
                        correct?"PASS":"FAIL",before,after,transfer);good=correct && good;
                    munmap(code,page);
                }
            }
            free(expected);
            int rejected=(supportsRAM && supportsRAM()) || rows(heap,(size_t)w*4,0,5,3,rw,rh)==-1;
            rejected=rows(vram,1,0,5,3,rw,rh)==-1 && rejected;
            good=rejected && read(heap,w,h) && good;
            fprintf(stderr,"%s negotiated RAM policy and bad pitch leave the stream usable\n",rejected?"PASS":"FAIL");
        }
        for(unsigned phase=0;!scattering && phase<3;++phase) {
            uint64_t direct_before=0,staged_before=0,direct_after=0,staged_after=0;
            if(uploading && phase==1)memcpy(vram,heap,(size_t)bytes);
            int device=(uploading?writeStats:stats)(&direct_before,&staged_before);double start=seconds();
            unsigned char *out=phase==1?vram:heap;
            for(unsigned i=0;i<20;++i) {
                if(uploading)good=import(out,mapping.layout.width,mapping.layout.height,mapping.layout.row_bytes,0) && good;
                else { clear(GL_COLOR_BUFFER_BIT);good=read(out,mapping.layout.width,mapping.layout.height) && good; }
            }
            // Include actual GPU consumption, not only queued send latency.
            if(uploading)good=read(heap,mapping.layout.width,mapping.layout.height) && good;
            double elapsed=seconds()-start;(uploading?writeStats:stats)(&direct_after,&staged_after);
            if(uploading)out=heap;
            int pixels=abs(out[0]-204)<=1 && abs(out[1]-102)<=1 && abs(out[2]-51)<=1 && out[3]==255 &&
                abs(out[bytes-4]-204)<=1 && abs(out[bytes-3]-102)<=1 && abs(out[bytes-2]-51)<=1 && out[bytes-1]==255;
            int ram_upload=uploading && getenv("VIM_RAM_UPLOAD")!=NULL;
            int route=device && (ram_upload ? direct_after-direct_before>=bytes*20 && staged_after==staged_before :
                                 phase==1 ? direct_after-direct_before==bytes*20 : direct_after==direct_before);
            fprintf(stderr,"%s %s %s phase=%u %.3f ms/frame direct_bytes=%llu staged_bytes=%llu pixels=%s\n",
                route && pixels?"PASS":"FAIL",uploading?"upload":"read",phase==1?"mapped VRAM":"ordinary heap",phase,elapsed*50,
                direct_after-direct_before,staged_after-staged_before,pixels?"PASS":"FAIL");
            good=route && pixels && good;
        }
        GLenum e=getError();fprintf(stderr,"%s backend GL error=%x\n",e?"FAIL":"PASS",e);good=!e && good;
    } else fprintf(stderr,"FAIL could not establish owned direct-transfer fixture\n");
    if(context && destroy)destroy(context);free(heap);
    if(locked) {
        uint64_t flags=0;struct VIMSurfaceState state;n=sizeof(state);
        IOReturn e=IOConnectCallMethod(connection,kIOVM2DUnlockSurfaceBacking,&flags,1,NULL,0,NULL,NULL,&state,&n);
        good=!e && good;
    }
    if(connection)IOServiceClose(connection);
    outcome=good?0:1;fprintf(stderr,"owned direct-%s outcome=%d\n",uploading?"upload":"read",outcome);
    [self performSelector:@selector(finish) withObject:nil afterDelay:1];
}
- (void)finish { exit(outcome); }
@end
int main(int argc,const char **argv)
{
    if(argc!=2)return 64;backend_path=argv[1];uploading=getenv("VIM_VRAM_UPLOAD")!=NULL;scattering=getenv("VIM_VRAM_SCATTER")!=NULL;
    @autoreleasepool {
        NSApplication *app=[NSApplication sharedApplication];[app setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0};
        NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];if(!format)return 2;
        NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(120,120,640,440)
            styleMask:NSTitledWindowMask|NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
        VIMDirectReadView *view=[[VIMDirectReadView alloc] initWithFrame:NSMakeRect(0,0,640,440) pixelFormat:format];
        view.wantsBestResolutionOpenGLSurface=YES;window.contentView=view;
        window.title=uploading?@"Owned VRAM upload qualification":@"Owned VRAM readback qualification";
        [window makeKeyAndOrderFront:nil];[window orderFrontRegardless];[app activateIgnoringOtherApps:YES];[app run];
    }
    return outcome;
}
