/* 13F1911 hardware metadata, not a Float-derived surface ABI.
 * Signatures and the 24-byte output are from matching IOAccelerator
 * disassembly. Query our own native GL window and existing primary ID 1;
 * Display writes are disabled by default. The explicit primary-alpha-write
 * mode masks RGB, scissors one pixel under our own window, and restores its
 * alpha immediately. Never change a driver/system setting or save an image.
 */
#import <Cocoa/Cocoa.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#import <OpenGL/CGLContext.h>
#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <stdint.h>
#include <sys/sysctl.h>
extern CGLError CGLGetSurface(CGLContextObj,uint32_t *,uint32_t *,uint32_t *);
static int outcome=2;
static unsigned failures;
static int samplePointAlpha(CGLContextObj context,uint32_t surface,int width,int height,int x,int y,unsigned char *alpha)
{
    void *engine=dlopen("/System/Library/Frameworks/OpenGL.framework/Resources/GLEngine.bundle/GLEngine",RTLD_NOW|RTLD_LOCAL);
    CGLError (*set)(GLIContext,unsigned,const GLint *)=engine?(void *)dlsym(engine,"gliSetInteger"):NULL;
    if(!set) { if(engine)dlclose(engine);return 0; }
    GLint read=0,draw=0,mode=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);glGetIntegerv(GL_MATRIX_MODE,&mode);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    GLuint texture=0,output=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    GLint record[]={surface,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA,width,height,0,0x404,0};
    CGLError ce=set(context->rend,0x3e5,record);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glViewport(0,0,1,1);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_ALPHA_TEST);glDisable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glBegin(GL_QUADS);glTexCoord2f(x+.5f,y+.5f);glVertex2f(-1,-1);
    glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();
    glReadPixels(0,0,1,1,GL_ALPHA,GL_UNSIGNED_BYTE,alpha);
    GLenum error=glGetError();
    glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(mode);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,read);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,draw);glPopAttrib();
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);glDeleteTextures(1,&output);dlclose(engine);
    fprintf(stderr,"native primary point private-import CGL=%d error=%x\n",ce,error);
    return !ce && !error;
}
static void attachedAlphaBits(CGLContextObj context,uint32_t surface,int width,int height,NSWindow *window)
{
    uint32_t original[6]={0},attachment[6]={0};
    CGLError e=CGLGetSurface(context,&original[0],&original[1],&original[2]);
    void *engine=dlopen("/System/Library/Frameworks/OpenGL.framework/Resources/GLEngine.bundle/GLEngine",RTLD_NOW|RTLD_LOCAL);
    CGLError (*attach)(GLIContext,unsigned,const void *,unsigned,unsigned)=engine?(void *)dlsym(engine,"gliAttachDrawableWithOptions"):NULL;
    if(e || !attach) { if(engine)dlclose(engine);return; }
    // Read/query only while the existing primary is attached. No clear,
    // draw, resize, swap or publication may occur in this interval.
    glFinish();attachment[2]=surface;e=attach(context->rend,0x5a,attachment,0,0);
    GLint bits=-1;if(!e)glGetIntegerv(GL_ALPHA_BITS,&bits);
    fprintf(stderr,"native read-only attached surface=%x alpha-bits=%d CGL=%d error=%x\n",surface,bits,e,glGetError());
    if(!e && getenv("VIM_GL_PRIMARY_ALPHA_WRITE")) {
        NSScreen *screen=window.screen;NSRect frame=window.frame,display=screen.frame;
        CGFloat scale=screen.backingScaleFactor;
        int x=(int)((NSMidX(frame)-NSMinX(display))*scale);
        int y=(int)((NSMaxY(display)-NSMidY(frame))*scale);
        // This is not a whole-screen clear. One pixel under our own window,
        // no RGB writes, exact alpha restoration before detaching. Refuse a
        // different-size display or any incomplete state setup.
        if((int)(display.size.width*scale)==width && (int)(display.size.height*scale)==height &&
           x>=0 && y>=0 && x<width && y<height) {
            unsigned char before=0,after=0,restored=0;
            glReadPixels(x,y,1,1,GL_ALPHA,GL_UNSIGNED_BYTE,&before);
            glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_TRUE);glEnable(GL_SCISSOR_TEST);glScissor(x,y,1,1);
            GLboolean mask[4]={1,1,1,0};GLint box[4]={0};
            glGetBooleanv(GL_COLOR_WRITEMASK,mask);glGetIntegerv(GL_SCISSOR_BOX,box);
            if(!glGetError() && !mask[0] && !mask[1] && !mask[2] && mask[3] && glIsEnabled(GL_SCISSOR_TEST) &&
               box[0]==x && box[1]==y && box[2]==1 && box[3]==1) {
                glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
                glReadPixels(x,y,1,1,GL_ALPHA,GL_UNSIGNED_BYTE,&after);
                unsigned char sampled=0;
                int sampled_ok=samplePointAlpha(context,surface,width,height,x,y,&sampled);
                fprintf(stderr,"native primary alpha-zero private sampler=%u valid=%d\n",sampled,sampled_ok);
                glClearColor(0,0,0,before/255.0f);glClear(GL_COLOR_BUFFER_BIT);
                glReadPixels(x,y,1,1,GL_ALPHA,GL_UNSIGNED_BYTE,&restored);
                GLenum error=glGetError();
                fprintf(stderr,"native one-pixel alpha-only primary before=%u after-zero=%u restored=%u error=%x\n",
                    before,after,restored,error);
                if(error || !sampled_ok || after || sampled || restored!=before)++failures;
            } else { fprintf(stderr,"REFUSED primary alpha test: state guard failed\n");++failures; }
            glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDisable(GL_SCISSOR_TEST);glFlush();
        } else { fprintf(stderr,"REFUSED primary alpha test: own-window/display geometry mismatch\n");++failures; }
    }
    CGLError restored=attach(context->rend,0x50,original,0,0);
    fprintf(stderr,"native restore own drawable=%d\n",restored);if(restored)++failures;
    dlclose(engine);
}
static void sampleAlpha(CGLContextObj context,uint32_t surface,int width,int height)
{
    void *engine=dlopen("/System/Library/Frameworks/OpenGL.framework/Resources/GLEngine.bundle/GLEngine",RTLD_NOW|RTLD_LOCAL);
    CGLError (*set)(GLIContext,unsigned,const GLint *)=engine?(void *)dlsym(engine,"gliSetInteger"):NULL;
    if(!set)return;
    for(unsigned rgb=0;rgb<2;++rgb) {
        GLuint texture=0,output=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
        GLint record[]={surface,GL_TEXTURE_RECTANGLE_ARB,rgb?GL_RGB:GL_RGBA,width,height,0,0x404,0};
        CGLError ce=set(context->rend,0x3e5,record);
        if(ce) { fprintf(stderr,"native alpha-only import surface=%x CGL=%d\n",surface,ce);glDeleteTextures(1,&texture);continue; }
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        glViewport(0,0,16,16);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
        glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);glEnable(GL_TEXTURE_RECTANGLE_ARB);
        glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
        glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(width,0);glVertex2f(1,-1);
        glTexCoord2f(width,height);glVertex2f(1,1);glTexCoord2f(0,height);glVertex2f(-1,1);glEnd();
        // Only alpha reaches the CPU; no screen-color image is captured.
        unsigned char alpha[256]={0};glReadPixels(0,0,16,16,GL_ALPHA,GL_UNSIGNED_BYTE,alpha);
        unsigned minimum=255,maximum=0,zero=0;
        for(unsigned i=0;i<sizeof(alpha);++i) { if(alpha[i]<minimum)minimum=alpha[i];if(alpha[i]>maximum)maximum=alpha[i];zero+=alpha[i]==0; }
        fprintf(stderr,"native alpha-only surface=%x internal=%s min=%u max=%u zero=%u/256 error=%x\n",
            surface,rgb?"RGB":"RGBA",minimum,maximum,zero,glGetError());
        glDisable(GL_TEXTURE_RECTANGLE_ARB);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
        glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);glDeleteTextures(1,&output);
    }
    dlclose(engine);
}
@interface VIMSurfaceInfoView : NSOpenGLView
@property BOOL measured;
@end
@implementation VIMSurfaceInfoView
- (void)drawRect:(NSRect)rect
{
    (void)rect;if(self.measured)return;self.measured=YES;
    [self.openGLContext makeCurrentContext];
    glClearColor(.2,.4,.8,0);glClear(GL_COLOR_BUFFER_BIT);glFinish();
    uint32_t ids[3]={0};CGLError e=CGLGetSurface(self.openGLContext.CGLContextObj,&ids[0],&ids[1],&ids[2]);
    fprintf(stderr,"native renderer=%s own-surface=%x CGL=%d\n",glGetString(GL_RENDERER),ids[2],e);
    void *library=dlopen("/System/Library/PrivateFrameworks/IOAccelerator.framework/Versions/A/IOAccelerator",RTLD_NOW|RTLD_LOCAL);
    void *(*create)(io_service_t)=library?(void *)dlsym(library,"IOAccelDeviceCreate"):NULL;
    void (*release)(void *)=library?(void *)dlsym(library,"IOAccelDeviceRelease"):NULL;
    IOReturn (*info)(void *,uint32_t,uint32_t *)=library?(void *)dlsym(library,"IOAccelDeviceGetSurfaceInfo"):NULL;
    if(e || !ids[2] || !create || !release || !info)exit(2);
    io_iterator_t iterator=0;
    IOReturn found=IOServiceGetMatchingServices(kIOMasterPortDefault,IOServiceMatching("IOAccelerator"),&iterator);
    unsigned queried=0;
    if(!found)for(io_service_t service;(service=IOIteratorNext(iterator));IOObjectRelease(service)) {
        io_name_t name={0};IORegistryEntryGetName(service,name);
        if(IOObjectConformsTo(service,"VMsvga2Accel")) {
            fprintf(stderr,"Refusing research provider: native device ABI only\n");continue;
        }
        void *device=create(service);
        fprintf(stderr,"accelerator=%s native-device=%s\n",name,device?"created":"unavailable");
        if(!device)continue;
        uint32_t sources[]={ids[2],1};
        if(getenv("VIM_GL_QUERY_SURFACE"))sources[1]=(uint32_t)strtoul(getenv("VIM_GL_QUERY_SURFACE"),NULL,16);
        for(unsigned i=0;i<2;++i) {
            uint32_t record[6]={0};IOReturn result=info(device,sources[i],record);
            fprintf(stderr,"surface=%x info=%x words=%08x,%08x,%08x,%08x,%08x,%08x\n",sources[i],result,
                record[0],record[1],record[2],record[3],record[4],record[5]);
            if(!result) {
                ++queried;
                if(getenv("VIM_GL_SAMPLE_ALPHA"))sampleAlpha(self.openGLContext.CGLContextObj,sources[i],record[2],record[3]);
                if(i && getenv("VIM_GL_ATTACH_READ_ONLY"))attachedAlphaBits(self.openGLContext.CGLContextObj,sources[i],record[2],record[3],self.window);
            }
        }
        release(device);
    }
    if(iterator)IOObjectRelease(iterator);
    outcome=queried ? (failures?1:0) : 2;
    [NSApp terminate:nil];
}
@end
int main(void)
{
    char build[64]={0};size_t length=sizeof(build);
    if(sysctlbyname("kern.osversion",build,&length,NULL,0) || strcmp(build,"13F1911")) {
        fprintf(stderr,"Only matching 13F1911 native device ABI is qualified\n");return 2;
    }
    @autoreleasepool {
        NSApplication *app=[NSApplication sharedApplication];[app setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSOpenGLPixelFormatAttribute attributes[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0};
        NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];if(!format)return 2;
        NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(180,180,96,64)
            styleMask:NSTitledWindowMask|NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
        [window setTitle:@"Native surface metadata"];
        VIMSurfaceInfoView *view=[[VIMSurfaceInfoView alloc] initWithFrame:NSMakeRect(0,0,96,64) pixelFormat:format];
        [window setContentView:view];[window makeKeyAndOrderFront:nil];[app run];
    }
    return outcome;
}
