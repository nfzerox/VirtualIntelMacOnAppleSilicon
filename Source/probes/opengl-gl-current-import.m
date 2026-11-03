/* Private self-import semantics on physical Mavericks Intel and the guest.
 * Paint AFTER type-5a attachment: the older mixed-mode fixture's initial
 * attachment readback made an NVIDIA-specific assumption about old contents.
 * This strict fixture does not weaken or hide that separate observation.
 */
#import <Cocoa/Cocoa.h>
#import <OpenGL/CGLContext.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#import <OpenGL/CGLRenderers.h>
#include <dlfcn.h>
#include <IOKit/IOKitLib.h>
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
#include "../vendor/source/VMsvga2/vminclude/vim_framebuffer.h"
#include "opengl-gl-feedback.h"
extern CGLError CGLGetSurface(CGLContextObj,uint32_t *,uint32_t *,uint32_t *);
typedef CGLError (*VIMSet)(GLIContext,unsigned,const GLint *);
static int failures;
static void VIMCheck(int good,const char *name)
{fprintf(stderr,"%s %s\n",good?"PASS":"FAIL",name);failures+=!good;}
static void paint(int w,int h,float r,float g,float b)
{
    glUseProgram(0);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
    glViewport(0,0,w,h);glDisable(GL_SCISSOR_TEST);glClearColor(r,g,b,1);glClear(GL_COLOR_BUFFER_BIT);
}
/* Explicit guest-only negative control: a real CPU writer must invalidate
 * same-drawable GPU preservation. Never invoked on native Apple hardware. */
static int cpuWrite(uint32_t surface,int w,int h)
{
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2Accel"));
    io_connect_t connection=0;IOReturn e=service?IOServiceOpen(service,mach_task_self(),2,&connection):kIOReturnNotFound;
    if(service)IOObjectRelease(service);
    uint64_t target[]={surface,0x800};uint32_t record[11];size_t size=sizeof(record);
    if(!e)e=IOConnectCallMethod(connection,kIOVM2DSetSurface,target,2,NULL,0,NULL,NULL,record,&size);
    struct VIMSurfaceMapping m={0};size=sizeof(m);int locked=0;
    if(!e){e=IOConnectCallStructMethod(connection,kIOVM2DLockSurfaceBacking,NULL,0,&m,&size);locked=!e;}
    if(!e && (size!=sizeof(m) || m.layout.version!=VIM_FRAMEBUFFER_LAYOUT_VERSION ||
       m.layout.bits_per_pixel!=32 || m.layout.width!=(unsigned)w || m.layout.height!=(unsigned)h ||
       m.layout.row_bytes<(unsigned)w*4 || m.layout.offset>m.length ||
       (uint64_t)(h-1)*m.layout.row_bytes+(uint64_t)w*4>m.length-m.layout.offset))e=kIOReturnBadArgument;
    if(!e)for(int y=0;y<h;++y){
        unsigned char *row=(void *)(uintptr_t)(m.address+m.layout.offset+(uint64_t)y*m.layout.row_bytes);
        for(int x=0;x<w;++x){row[x*4]=255;row[x*4+1]=0;row[x*4+2]=255;row[x*4+3]=255;}
    }
    if(locked){uint64_t mode=0,flags=0;uint32_t n=1;
        IOReturn unlocked=IOConnectCallScalarMethod(connection,kIOVM2DUnlockMemory,&mode,1,&flags,&n);if(!e)e=unlocked;}
    if(connection)IOServiceClose(connection);return !e;
}
static void sample(GLuint texture,GLuint fbo,int w,int h,unsigned r,unsigned g,unsigned b)
{
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glViewport(0,0,32,32);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);glEnable(GL_TEXTURE_RECTANGLE_ARB);glColor4f(1,1,1,1);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(w,0);glVertex2f(1,-1);
    glTexCoord2f(w,h);glVertex2f(1,1);glTexCoord2f(0,h);glVertex2f(-1,1);glEnd();
    unsigned char p[4]={0};glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);GLenum e=glGetError();
    fprintf(stderr,"sample RGBA=%u,%u,%u,%u expected=%u,%u,%u,255 error=%x\n",p[0],p[1],p[2],p[3],r,g,b,e);
    VIMCheck(!e&&p[0]==r&&p[1]==g&&p[2]==b&&p[3]==255,"ordered source pixels in separate FBO");
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
}
static void checkPattern(GLuint texture,GLuint fbo,int w,int h,int iw,int ih,int flipped)
{
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glViewport(0,0,32,32);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);glEnable(GL_TEXTURE_RECTANGLE_ARB);glColor4f(1,1,1,1);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(iw,0);glVertex2f(1,-1);
    glTexCoord2f(iw,ih);glVertex2f(1,1);glTexCoord2f(0,ih);glVertex2f(-1,1);glEnd();
    unsigned char image[32*32*4];glReadPixels(0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,image);
    unsigned bad=0;
    for(int y=0;y<32;++y)for(int x=0;x<32;++x){
        int sx=(int)((x+.5)*iw/32),sy=(int)((y+.5)*ih/32);
        if(flipped)sy=h-1-sy;
        int green=sx>=w/2,blue=!green&&sy>=h/2;
        unsigned char *p=image+(y*32+x)*4;
        bad+=p[0]!=(green||blue?0:255)||p[1]!=(green?255:0)||p[2]!=(blue?255:0)||p[3]!=255;
    }
    GLenum error=glGetError();fprintf(stderr,"pattern mismatch pixels=%u/1024 error=%x\n",bad,error);
    VIMCheck(!bad&&!error,"all private-import rows/columns preserve native orientation and crop");
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
}
@interface VIMCurrentImportView:NSOpenGLView { BOOL _measured; }
@property BOOL measured;
@end
@implementation VIMCurrentImportView
@synthesize measured=_measured;
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;if(self.measured)return;self.measured=YES;[self.openGLContext makeCurrentContext];
    CGLContextObj c=self.openGLContext.CGLContextObj;const char *renderer=(const char *)glGetString(GL_RENDERER);
    fprintf(stderr,"renderer=%s\n",renderer?renderer:"none");if(!renderer||!strstr(renderer,"Intel"))exit(2);
    uint32_t ids[6]={0},attachRecord[6]={0};if(CGLGetSurface(c,ids,ids+1,ids+2)||!ids[2])exit(2);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
    NSRect bounds=self.bounds;
#else
    NSRect bounds=[self convertRectToBacking:self.bounds];
#endif
    int w=bounds.size.width,h=bounds.size.height;
    char path[4096];const char *resources=getenv("GL_RESOURCES");
    snprintf(path,sizeof(path),"%s/GLEngine.bundle/GLEngine",resources?resources:"/System/Library/Frameworks/OpenGL.framework/Resources");
    void *engine=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    CGLError (*attach)(GLIContext,unsigned,const void *,unsigned,unsigned)=engine?dlsym(engine,"gliAttachDrawableWithOptions"):NULL;
    VIMSet set=engine?dlsym(engine,"gliSetInteger"):NULL;if(!attach||!set)exit(2);
    attachRecord[2]=ids[2];
    int native_window=getenv("VIM_CURRENT_IMPORT_NATIVE_WINDOW")!=NULL;
    fprintf(stderr,"current import drawable=%s\n",native_window?"ordinary native window":"unflipped accelerator");
    if(!native_window && attach(c->rend,0x5a,attachRecord,0,0))exit(2);
    int cropped=getenv("VIM_CURRENT_IMPORT_CROP")!=NULL;
    int iw=cropped?w/2:w,ih=cropped?h/2:h;
    if(cropped && !strcmp(getenv("VIM_CURRENT_IMPORT_CROP"),"asymmetric")){iw=w-17;ih=h-13;}
    fprintf(stderr,"current import extent=%dx%d drawable=%dx%d\n",iw,ih,w,h);
    const unsigned operations[]={0x3e5,0x3e6,0x3e7};
    for(unsigned op=0;op<sizeof(operations)/sizeof(*operations);++op){
        GLuint source=0,destination=0,fbo=0;glGenTextures(1,&source);glGenTextures(1,&destination);
        glBindTexture(GL_TEXTURE_2D,destination);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,destination,0);
        VIMCheck(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"sample FBO complete");
        GLint front=getenv("VIM_CURRENT_IMPORT_FRONT_LEFT")?GL_FRONT_LEFT:GL_FRONT;
        if(getenv("VIM_CURRENT_IMPORT_MENU_FLAGS"))front=0x404;
        GLint record[]={ids[2],GL_TEXTURE_RECTANGLE_ARB,GL_RGBA,iw,ih,0,front,0};unsigned key=operations[op];
        fprintf(stderr,"self-import operation=%x\n",key);paint(w,h,0,0,1);glFlush();
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        VIMCheck(!set(c->rend,key,record),"initial current-view import");sample(source,fbo,iw,ih,0,0,255);
        paint(w,h,0,1,1);
        if(getenv("VIM_CURRENT_IMPORT_REATTACH"))
            VIMCheck(!attach(c->rend,native_window?0x50:0x5a,native_window?ids:attachRecord,0,0),
                "same owned drawable reattachment after unflushed drawing");
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
        VIMCheck(!set(c->rend,key,record),"reimport after unflushed drawing");sample(source,fbo,iw,ih,0,255,255);
        paint(w,h,1,1,0);GLint zero=0;VIMCheck(!set(c->rend,300,&zero),"private barrier after unflushed drawing");
        sample(source,fbo,iw,ih,255,255,0);
        if(!cropped)VIMCheck(VIMNeighborhoodFeedback(c->rend,set,source,w,h),"neighborhood alias matches independent GPU copy");
        else {
            /* Establish which half of the native framebuffer a smaller
             * private import denotes; uniform colors cannot prove this. */
            paint(w,h,1,0,0);glEnable(GL_SCISSOR_TEST);glScissor(0,h/2,w,h-h/2);
            glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
            glScissor(w/2,0,w-w/2,h);glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
            VIMCheck(!set(c->rend,key,record),"cropped top/bottom pattern import");
            GLint tw=0,th=0;glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_WIDTH,&tw);
            glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_HEIGHT,&th);
            fprintf(stderr,"cropped native texture dimensions=%dx%d\n",tw,th);
            VIMCheck(tw==iw&&th==ih,"cropped texture reports requested native extent");
            int centerX=(int)(16.5*iw/32),centerY=(int)(16.5*ih/32);
            if(native_window)centerY=h-1-centerY;
            int centerGreen=centerX>=w/2,centerBlue=!centerGreen&&centerY>=h/2;
            sample(source,fbo,iw,ih,centerGreen||centerBlue?0:255,centerGreen?255:0,centerBlue?255:0);
            checkPattern(source,fbo,w,h,iw,ih,native_window);
        }
        if(getenv("VIM_CURRENT_IMPORT_CPU_WRITE")){
            paint(w,h,1,1,0);glFlush();
            VIMCheck(cpuWrite(ids[2],w,h),"actual newer CPU write to owned surface");
            VIMCheck(!attach(c->rend,native_window?0x50:0x5a,native_window?ids:attachRecord,0,0),
                "same owned drawable reattachment after CPU write");
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
            VIMCheck(!set(c->rend,key,record),"reimport after newer CPU write");sample(source,fbo,iw,ih,255,0,255);
        }
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&source);glDeleteTextures(1,&destination);
    }
    fprintf(stderr,"current-import failures=%d\n",failures);exit(failures?1:0);
}
@end
int main(void){@autoreleasepool {
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
    // Snow predates automatic-switching formats. Select online Intel BEFORE
    // context creation on the reference; the owned VM keeps its real renderer.
    GLint selected=0;
    io_service_t vm=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2"));
    if(vm)IOObjectRelease(vm);
    else {
        CGLRendererInfoObj info=NULL;GLint count=0;
        if(CGLQueryRendererInfo(~0U,&info,&count))return 2;
        for(GLint i=0;i<count;i++) {
            GLint id=0,online=0,accelerated=0;
            CGLDescribeRenderer(info,i,kCGLRPRendererID,&id);CGLDescribeRenderer(info,i,kCGLRPOnline,&online);
            CGLDescribeRenderer(info,i,kCGLRPAccelerated,&accelerated);
            if(online&&accelerated&&(id&kCGLRendererIDMatchingMask)==0x24300)selected=id;
        }
        CGLDestroyRendererInfo(info);if(!selected)return 77;
    }
    NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
        NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,
        selected?NSOpenGLPFARendererID:0,(NSOpenGLPixelFormatAttribute)selected,0};
#else
    NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
        (NSOpenGLPixelFormatAttribute)kCGLPFASupportsAutomaticGraphicsSwitching,NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0};
#endif
    NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];if(!format)return 2;
    NSRect frame=getenv("VIM_CURRENT_IMPORT_SHORT")?NSMakeRect(180,180,1024,22):NSMakeRect(180,180,128,96);
    NSWindow *window=[[NSWindow alloc] initWithContentRect:frame
        styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:NO];
    window.title=@"Owned current-surface import qualification";
    VIMCurrentImportView *view=[[VIMCurrentImportView alloc] initWithFrame:NSMakeRect(0,0,frame.size.width,frame.size.height) pixelFormat:format];
    window.contentView=view;[window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];[NSApp run];return 2;
}}
