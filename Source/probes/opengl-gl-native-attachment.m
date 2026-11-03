/* Measure the two real Mavericks GLI attachment modes on our own NSOpenGLView.
 * CGLGetSurface's three uint32 outputs are verified in the matching CGL binary;
 * the six-word type-5a record is verified in CoreGraphics attach_surface and
 * traced in the physical WindowServer. No system process is modified.
 */
#import <Cocoa/Cocoa.h>
#import <OpenGL/CGLContext.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdint.h>
#include "opengl-gl-feedback.h"
extern CGLError CGLGetSurface(CGLContextObj, uint32_t *, uint32_t *, uint32_t *);
typedef CGLError (*Attach)(GLIContext, unsigned, const void *, unsigned, unsigned);
typedef CGLError (*SetInteger)(GLIContext, unsigned, const GLint *);
static int outcome=2;
static GLint nativeAlphaBits=-1;
static int solid(const char *name,int w,int h,unsigned r,unsigned g,unsigned b)
{
    unsigned char pixel[4]={0};glReadPixels(w/2,h/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    GLenum error=glGetError();int good=!error && pixel[0]==r && pixel[1]==g && pixel[2]==b && pixel[3]==255;
    fprintf(stderr,"%s %s RGBA=%u,%u,%u,%u error=%x\n",good?"PASS":"FAIL",name,
        pixel[0],pixel[1],pixel[2],pixel[3],error);return good;
}
static int rows(const char *name,int width,int height,int reversed)
{
    unsigned char bottom[4]={0},top[4]={0};
    glReadPixels(width/2,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,bottom);
    glReadPixels(width/2,height-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,top);
    GLenum error=glGetError();
    unsigned alpha=getenv("VIM_GL_ZERO_ALPHA") && nativeAlphaBits>0 ? 0 : 255;
    int good=!error && bottom[0]==(reversed?0:255) && !bottom[1] &&
        bottom[2]==(reversed?255:0) && bottom[3]==alpha &&
        top[0]==(reversed?255:0) && !top[1] && top[2]==(reversed?0:255) && top[3]==alpha;
    fprintf(stderr,"%s %s bottom=%u,%u,%u,%u top=%u,%u,%u,%u error=%x\n",good?"PASS":"FAIL",name,
        bottom[0],bottom[1],bottom[2],bottom[3],top[0],top[1],top[2],top[3],error);
    return good;
}
static void sampleSurface(GLuint texture,int width,int height)
{
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glEnable(GL_TEXTURE_RECTANGLE_ARB);glColor4f(1,1,1,1);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2f(-1,-1);
    glTexCoord2f(width,0);glVertex2f(1,-1);
    glTexCoord2f(width,height);glVertex2f(1,1);
    glTexCoord2f(0,height);glVertex2f(-1,1);glEnd();glFinish();
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
}
@interface VIMAttachmentView : NSOpenGLView {
    BOOL _measured;
}
@property BOOL measured;
@end
@implementation VIMAttachmentView
@synthesize measured=_measured;
- (void)finish:(NSTimer *)timer { (void)timer;exit(outcome); }
- (void)drawRect:(NSRect)rect
{
    (void)rect;if(self.measured)return;self.measured=YES;
    [[self openGLContext] makeCurrentContext];
    CGLContextObj c=[[self openGLContext] CGLContextObj];
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    if(getenv("VIM_REQUIRE_INTEL") && (!renderer || !strstr(renderer,"Intel"))) {
        fprintf(stderr,"REFUSED non-Intel reference renderer: %s\n",renderer?renderer:"none");exit(2);
    }
    if(getenv("VIM_GL_QUERY_OFFSCREEN")) {
        GLsizei w=-1,h=-1;GLint pitch=-1;void *pixels=NULL;
        CGLError offscreen=CGLGetOffScreen(c,&w,&h,&pitch,&pixels);
        fprintf(stderr,"native window CGLGetOffScreen status=%d size=%dx%d row=%d has-pixels=%d renderer=%s\n",
            offscreen,w,h,pitch,pixels!=NULL,renderer?renderer:"none");
    }
    NSRect backing=[self convertRectToBacking:self.bounds];
    int width=backing.size.width,height=backing.size.height;
    glGetIntegerv(GL_ALPHA_BITS,&nativeAlphaBits);
    fprintf(stderr,"native default framebuffer alpha bits=%d requested-minimum=%d\n",nativeAlphaBits,
        getenv("VIM_GL_ALPHA_MINIMUM_ZERO") ? 0 : 8);
    glViewport(0,0,width,height);glDisable(GL_SCISSOR_TEST);
    GLfloat alpha=getenv("VIM_GL_ZERO_ALPHA") ? 0 : 1;
    glClearColor(1,0,0,alpha);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(0,height/2,width,height-height/2);
    glClearColor(0,0,1,alpha);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    glFinish();int good=rows("ordinary-window",width,height,0);
    uint32_t original[6]={0},unflipped[6]={0};
    CGLError e=CGLGetSurface(c,&original[0],&original[1],&original[2]);
    fprintf(stderr,"renderer=%s surface=%x connection=%x window=%u geometry=%dx%d get=%d\n",
        glGetString(GL_RENDERER),original[2],original[0],original[1],width,height,e);
    const char *resources=getenv("GL_RESOURCES");
    char enginePath[4096];
    snprintf(enginePath,sizeof(enginePath),"%s/GLEngine.bundle/GLEngine",resources ? resources :
        "/System/Library/Frameworks/OpenGL.framework/Resources");
    void *engine=dlopen(enginePath,RTLD_NOW|RTLD_LOCAL);
    Attach attach=engine ? (Attach)dlsym(engine,"gliAttachDrawableWithOptions") : NULL;
    SetInteger setInteger=engine ? (SetInteger)dlsym(engine,"gliSetInteger") : NULL;
    if(!e && original[2] && attach) {
        unflipped[2]=original[2];
        e=attach(c->rend,0x5a,unflipped,0,0);
        fprintf(stderr,"attach unflipped=%d\n",e);
        if(!e) {
            good=rows("unflipped-same-existing-surface",width,height,1) && good;
            if(setInteger) for(unsigned mode=0;mode<2;++mode) {
                GLuint texture=0,fbo=0,output=0;glGenTextures(1,&texture);
                glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
                glPixelStorei(GL_UNPACK_SWAP_BYTES,1);glPixelStorei(GL_UNPACK_LSB_FIRST,1);
                GLint record[]={original[2],GL_TEXTURE_RECTANGLE_ARB,GL_RGBA,width,height,0,0x404,0};
                if(getenv("VIM_GL_IMPORT_FLAGS"))record[6]=(GLint)strtoul(getenv("VIM_GL_IMPORT_FLAGS"),NULL,16);
                e=setInteger(c->rend,mode ? 0x3e7 : 0x3e5,record);
                fprintf(stderr,"import same surface operation=%x flags=%x result=%d\n",mode?0x3e7:0x3e5,mode?0x1100:record[6],e);
                GLint swap=0,lsb=0;glGetIntegerv(GL_UNPACK_SWAP_BYTES,&swap);glGetIntegerv(GL_UNPACK_LSB_FIRST,&lsb);
                fprintf(stderr,"%s private import retains unpack swap=%d lsb=%d\n",swap==1&&lsb==1?"PASS":"FAIL",swap,lsb);
                if(swap!=1 || lsb!=1)good=0;
                if(!e) {
                    // The native private surface texture is sampleable but
                    // not necessarily FBO-renderable. Use an ordinary output.
                    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
                    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
                    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
                    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
                    GLenum status=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
                    fprintf(stderr,"sample output framebuffer=%x\n",status);
                    if(status==GL_FRAMEBUFFER_COMPLETE_EXT) {
                        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
                        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
                        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
                        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
                        glEnable(GL_TEXTURE_RECTANGLE_ARB);glColor4f(1,1,1,1);
                        glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
                        glBegin(GL_QUADS);
                        glTexCoord2f(0,0);glVertex2f(-1,-1);
                        glTexCoord2f(width,0);glVertex2f(1,-1);
                        glTexCoord2f(width,height);glVertex2f(1,1);
                        glTexCoord2f(0,height);glVertex2f(-1,1);glEnd();
                        glFinish();good=rows(mode?"surface-texture-3e7-1100":"surface-texture-3e5-404",width,height,1) && good;
                        glDisable(GL_TEXTURE_RECTANGLE_ARB);
                        if(getenv("VIM_GL_SAME_CURRENT_LIVE")) {
                            // The source remains attached to this very CGL
                            // context. A snapshot can pass the initial image
                            // check but not this later ordered GPU mutation.
                            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
                            glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
                            glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);glFlush();
                            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
                            glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
                            sampleSurface(texture,width,height);
                            unsigned char pixel[4]={0};glReadPixels(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                            GLenum error=glGetError();
                            int live=!error && !pixel[0] && pixel[1]==255 && !pixel[2] && pixel[3]==255;
                            fprintf(stderr,"%s same-current unflipped live alias op=%x RGBA=%u,%u,%u,%u error=%x\n",
                                live?"PASS":"FAIL",mode?0x3e7:0x3e5,pixel[0],pixel[1],pixel[2],pixel[3],error);
                            good=live && good;
                            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
                            glClearColor(0,1,1,1);glClear(GL_COLOR_BUFFER_BIT);
                            // Import orders earlier draws without requiring
                            // the client to publish its CPU shadow first.
                            glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
                            e=setInteger(c->rend,mode?0x3e7:0x3e5,record);
                            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
                            glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
                            sampleSurface(texture,width,height);
                            glReadPixels(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);error=glGetError();
                            int ordered=!e && !error && !pixel[0] && pixel[1]==255 && pixel[2]==255 && pixel[3]==255;
                            fprintf(stderr,"%s same-current import orders unflushed draws op=%x RGBA=%u,%u,%u,%u cgl=%d error=%x\n",
                                ordered?"PASS":"FAIL",mode?0x3e7:0x3e5,pixel[0],pixel[1],pixel[2],pixel[3],e,error);
                            good=ordered && good;
                            if(getenv("VIM_GL_SELF_SAMPLE")) {
                                glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
                                if(getenv("VIM_GL_TEXTURE_BARRIER"))glTextureBarrierNV();
                                sampleSurface(texture,width,height);
                                glReadPixels(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);error=glGetError();
                                int feedback=!error && !pixel[0] && pixel[1]==255 && pixel[2]==255 && pixel[3]==255;
                                fprintf(stderr,"%s native private surface self-sample op=%x barrier=%d RGBA=%u,%u,%u,%u error=%x\n",
                                    feedback?"PASS":"FAIL",mode?0x3e7:0x3e5,getenv("VIM_GL_TEXTURE_BARRIER")!=NULL,
                                    pixel[0],pixel[1],pixel[2],pixel[3],error);
                                good=feedback && good;
                            }
                            if(getenv("VIM_GL_LINEAR_FEEDBACK"))good=VIMLinearFeedback(texture,width,height) && good;
                            if(getenv("VIM_GL_QUEUED_FEEDBACK"))good=VIMQueuedAliasFeedback(texture,width,height) && good;
                            if(getenv("VIM_GL_NEIGHBORHOOD_FEEDBACK"))
                                good=VIMNeighborhoodFeedback(c->rend,setInteger,texture,width,height) && good;
                            // Restore the asymmetric fixture for the other
                            // import operation and final ordinary reattach.
                            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
                            glClearColor(0,0,1,alpha);glClear(GL_COLOR_BUFFER_BIT);
                            glEnable(GL_SCISSOR_TEST);glScissor(0,height/2,width,height-height/2);
                            glClearColor(1,0,0,alpha);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);glFlush();
                        }
                    } else good=0;
                    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glReadBuffer(GL_FRONT);
                    glDrawBuffer(GL_FRONT);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&output);
                } else good=0;
                glDeleteTextures(1,&texture);
            } else good=0;
            e=attach(c->rend,0x50,original,0,0);
            fprintf(stderr,"restore ordinary=%d\n",e);
            if(!e)good=rows("ordinary-restored",width,height,0) && good;
            if(!e && getenv("VIM_GL_ATTACHMENT_SWITCH")) {
                // Observe native attachment/publication ordering without a
                // retaining texture alias. Do not assume an unflushed batch
                // survives changing the underlying native drawing target.
                NSWindow *other=[[NSWindow alloc] initWithContentRect:NSMakeRect(350,180,width,height)
                    styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:NO];
                [other setReleasedWhenClosed:NO];[other setTitle:@"Native attachment lifetime control"];
                NSOpenGLView *view=[[NSOpenGLView alloc] initWithFrame:NSMakeRect(0,0,width,height)
                    pixelFormat:self.pixelFormat];[other setContentView:view];[other orderFront:nil];
                [view.openGLContext makeCurrentContext];[view.openGLContext update];
                uint32_t second[6]={0};
                e=CGLGetSurface(view.openGLContext.CGLContextObj,&second[0],&second[1],&second[2]);
                CGLSetCurrentContext(c);
                if(!e && second[2])e=attach(c->rend,0x5a,unflipped,0,0);else e=kCGLBadDrawable;
                if(!e) {
                    glDisable(GL_SCISSOR_TEST);glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
                    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
                    if(getenv("VIM_GL_ATTACHMENT_FLUSH"))glFlush();
                    e=attach(c->rend,0x5a,second,0,0);
                }
                if(!e) {
                    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
                    good=solid("second attachment writes independent blue backing",width,height,0,0,255) && good;
                    /* Explicit published-backing control, separate from the
                     * older unflushed-repeat observation below. glReadPixels
                     * alone is not a CGS surface-publication request. Keep
                     * the original test unchanged unless this is selected. */
                    if(getenv("VIM_GL_ATTACHMENT_REPEAT_PUBLISHED"))glFlush();
                    e=attach(c->rend,0x5a,unflipped,0,0);
                }
                if(!e && getenv("VIM_GL_ATTACHMENT_FLUSH"))
                    good=solid("flushed green survives different-window attachment",width,height,0,255,0) && good;
                else if(!e) {
                    unsigned char pixel[4]={0};glReadPixels(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                    GLenum error=glGetError();good=!error && good;
                    fprintf(stderr,"OBSERVATION unflushed attachment switch RGBA=%u,%u,%u,%u error=%x\n",
                        pixel[0],pixel[1],pixel[2],pixel[3],error);
                }
                fprintf(stderr,"attachment-switch result=%d\n",e);good=!e && good;
                if(!e && getenv("VIM_GL_ATTACHMENT_REPEAT")) {
                    fprintf(stderr,"repeat control: %s second-window contents\n",
                        getenv("VIM_GL_ATTACHMENT_REPEAT_PUBLISHED")?"explicitly published":"unflushed");
                    for(unsigned repeat=0;repeat<32 && good;++repeat) {
                        e=attach(c->rend,0x5a,second,0,0);
                        good=!e && solid("repeated independent blue attachment",width,height,0,0,255);
                        if(!good)break;
                        e=attach(c->rend,0x5a,unflipped,0,0);
                        good=!e && solid("repeated retained green attachment",width,height,0,255,0);
                    }
                }
                CGLError restored=attach(c->rend,0x50,original,0,0);good=!restored && good;
                [other close];[view release];[other release];
            }
            if(!e)outcome=good?0:1;
        }
    }
    [NSTimer scheduledTimerWithTimeInterval:1 target:self selector:@selector(finish:) userInfo:nil repeats:NO];
}
@end
int main(void)
{
    @autoreleasepool {
        NSApplication *app=[NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,getenv("VIM_GL_ALPHA_MINIMUM_ZERO") ? 0 : 8,0,0};
        if(getenv("VIM_REQUIRE_INTEL"))attrs[6]=(NSOpenGLPixelFormatAttribute)kCGLPFASupportsAutomaticGraphicsSwitching;
        NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
        if(!format)return 2;
        NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(180,180,128,96)
            styleMask:NSTitledWindowMask|NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
        [window setTitle:@"Native attachment contract"];
        VIMAttachmentView *view=[[VIMAttachmentView alloc] initWithFrame:NSMakeRect(0,0,128,96) pixelFormat:format];
        [window setContentView:view];[window makeKeyAndOrderFront:nil];
        [app activateIgnoringOtherApps:YES];[app run];
    }
    return outcome;
}
